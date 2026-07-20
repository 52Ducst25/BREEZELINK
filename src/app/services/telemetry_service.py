"""Telemetry persistence + device lookup for the MQTT worker (Phase 4).

2-node architecture (design): exactly one outdoor + one indoor device per
org, so a device is uniquely identified by ``(org_id, node_type)`` — the
worker never receives a ``device_id`` directly, only the topic's org/node.
"""

import uuid
from datetime import datetime

from sqlalchemy import select, text
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.device import Device
from app.models.enums import NodeType
from app.models.telemetry import Telemetry

_DEFAULT_LIMIT = 200


async def get_device_by_org_and_node(
    session: AsyncSession, org_id: str, node_type: str
) -> Device | None:
    """Look up the single device for this org/node (2-node design)."""
    stmt = select(Device).where(
        Device.org_id == uuid.UUID(org_id),
        Device.node_type == NodeType(node_type),
    )
    return (await session.execute(stmt)).scalar_one_or_none()


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
