"""Telemetry persistence + device lookup for the MQTT worker (Phase 4).

2-node architecture (design): exactly one outdoor + one indoor device per
org, so a device is uniquely identified by ``(org_id, node_type)`` — the
worker never receives a ``device_id`` directly, only the topic's org/node.
"""

import logging
import uuid
from datetime import datetime

from sqlalchemy import select, text
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.device import Device
from app.models.enums import NodeType
from app.models.telemetry import Telemetry

logger = logging.getLogger("breezelink.telemetry_service")

_DEFAULT_LIMIT = 200


async def get_device_by_org_and_node(
    session: AsyncSession, org_id: str, node_type: str
) -> Device | None:
    """Resolve ONE device of this node type for the org — the oldest if several.

    Answers "which indoor node carries the IR blaster for this household".

    The 2-node era could assume uniqueness and used ``scalar_one_or_none()``,
    which raises ``MultipleResultsFound`` the moment a household has a SECOND
    indoor node — something the admin form allows with a single click. That
    exception surfaced as a 500 on every override / IR endpoint, and inside the
    worker a broad ``except`` swallowed it, so automatic commands silently
    stopped being published while the system still looked healthy. Failing that
    way is worse than being approximate.

    Degrading to "oldest match" keeps the household behaving exactly as it did
    before the extra node appeared, and the warning marks the ambiguity. Real
    per-room decisions are Phase 2.
    """
    stmt = (
        select(Device)
        .where(
            Device.org_id == uuid.UUID(org_id),
            Device.node_type == NodeType(node_type),
        )
        .order_by(Device.created_at.asc(), Device.id.asc())
    )
    rows = list((await session.execute(stmt)).scalars().all())
    if len(rows) > 1:
        logger.warning(
            "Org %s has %d '%s' nodes; driving the oldest (%s). "
            "Per-room control is Phase 2 — the others are not controlled.",
            org_id, len(rows), node_type, rows[0].id,
        )
    return rows[0] if rows else None


async def get_device_for_topic(
    session: AsyncSession, org_id: str, device_uuid: str
) -> Device | None:
    """Resolve a node by uuid AND verify it belongs to the org named in the topic.

    The MQTT topic is attacker/typo-controlled input, not proof of ownership:
    ``get_device_by_uuid`` looks the node up GLOBALLY, and every caller then
    writes state keyed by ``topic.org_id``. A single mistyped ORG_ID in a hand
    edited ``config.h`` therefore files a real node's readings under a different
    household — poisoning that household's Redis state and driving its comfort
    engine.

    Returns None (caller skips the message) when the uuid is unknown OR when the
    topic's org does not match the device's real org.

    NOTE: this closes the misconfiguration hole, NOT a determined attacker — a
    device holding valid credentials can still publish to another household's
    topic, because both halves of the comparison come from that same topic.
    Only a broker-side topic ACL fixes that; see the audit's EMQX item.
    """
    device = await get_device_by_uuid(session, device_uuid)
    if device is None:
        return None
    if str(device.org_id) != str(org_id):
        logger.warning(
            "Topic org mismatch: uuid=%s belongs to org %s but published under org %s — ignored.",
            device_uuid, device.org_id, org_id,
        )
        return None
    return device


async def get_device_by_uuid(session: AsyncSession, device_uuid: str) -> Device | None:
    """Resolve a node by its ``device_uuid`` — the MQTT identity now carried in
    the topic (``bl/{org}/{device_uuid}/{kind}``). node_type/org come off the row.
    """
    stmt = select(Device).where(Device.device_uuid == device_uuid)
    return (await session.execute(stmt)).scalar_one_or_none()


async def persist_telemetry(
    session: AsyncSession,
    *,
    device_id: uuid.UUID,
    ts: datetime,
    temp: float,
    humidity: float,
    rssi: int,
    batt: float | None = None,
    watt: float | None = None,
) -> Telemetry:
    """Insert one telemetry row and commit."""
    row = Telemetry(
        device_id=device_id,
        ts=ts,
        temp=temp,
        humidity=humidity,
        rssi=rssi,
        watt=watt,
        batt=batt,
    )
    session.add(row)
    await session.commit()
    return row


async def list_telemetry(
    session: AsyncSession,
    *,
    device_id: uuid.UUID,
    start: datetime | None = None,
    end: datetime | None = None,
    limit: int = _DEFAULT_LIMIT,
) -> list[Telemetry]:
    """Chart series for one device — the MOST RECENT ``limit`` rows, returned
    oldest-first, optionally time-bounded (Phase 5 ``GET /telemetry``).

    H1 fix: the DB query takes the newest ``limit`` rows (``ts DESC LIMIT``)
    then reverses to ascending, so ``series[-1]`` is the latest reading and a
    ``limit=1`` "latest" query returns the current sample (not the oldest one).
    Charts still get a chronological window. Caller must have verified the
    device belongs to the caller's org (see ``device_service.get_device``).
    """
    stmt = select(Telemetry).where(Telemetry.device_id == device_id)
    if start is not None:
        stmt = stmt.where(Telemetry.ts >= start)
    if end is not None:
        stmt = stmt.where(Telemetry.ts <= end)
    stmt = stmt.order_by(Telemetry.ts.desc()).limit(limit)
    rows = list((await session.execute(stmt)).scalars().all())
    rows.reverse()  # newest-first from DB → oldest-first for the chart series
    return rows


# Splitting the window into a fixed number of slots (rather than a fixed slot
# WIDTH) keeps every range — a day or a month — at the same point count, so the
# chart stays readable and the payload stays small.
_DEFAULT_BUCKETS = 120


async def series_bucketed(
    session: AsyncSession,
    *,
    device_id: uuid.UUID,
    start: datetime,
    end: datetime,
    buckets: int = _DEFAULT_BUCKETS,
) -> list[dict]:
    """Down-sampled temp/humidity series: the window is cut into ``buckets``
    equal slots and each slot yields the AVERAGE of its samples.

    Why not reuse ``list_telemetry``: that returns the most recent ``limit``
    rows, so at a 15s cadence a 30-day request would plot only the last ~4
    hours while the UI labelled it "30 ngày" — a chart that quietly lies about
    its own range. Bucketing keeps the whole window represented regardless of
    sampling rate. Empty slots are simply absent (no fabricated zeros).

    Caller must have verified the device belongs to the caller's org.
    """
    span_sec = max((end - start).total_seconds(), 1.0)
    bucket_sec = max(span_sec / max(buckets, 1), 1.0)

    # Raw SQL: bucketing by epoch division has no clean ORM form, and doing it
    # in Python would mean pulling every row (the thing we are avoiding).
    # NOTE: bind NATIVE Python types (uuid.UUID / datetime), never ISO strings
    # with an explicit CAST. asyncpg infers each parameter's type from where it
    # sits in the statement (``ts >= $3`` → timestamptz) and then rejects a str
    # outright: "invalid input for query argument $3 ... got 'str'".
    stmt = text(
        """
        -- epoch is NUMERIC on PG14+; cast to float8 so :b is inferred float8
        -- (a numeric bind would make asyncpg demand a Decimal, not a float).
        SELECT to_timestamp(floor(extract(epoch FROM ts)::float8 / :b) * :b) AS bucket_ts,
               avg(temp)::float     AS temp,
               avg(humidity)::float AS humidity
        FROM telemetry
        WHERE device_id = :d
          AND ts >= :s
          AND ts <= :e
        GROUP BY 1
        ORDER BY 1
        """
    )
    result = await session.execute(
        stmt,
        {"b": float(bucket_sec), "d": device_id, "s": start, "e": end},
    )
    return [
        {"ts": row.bucket_ts, "temp": row.temp, "humidity": row.humidity}
        for row in result.all()
    ]
