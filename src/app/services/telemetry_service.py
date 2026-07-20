"""Telemetry persistence + device lookup for the MQTT worker (Phase 4).

2-node architecture (design): exactly one outdoor + one indoor device per
org, so a device is uniquely identified by ``(org_id, node_type)`` — the
worker never receives a ``device_id`` directly, only the topic's org/node.
"""

import uuid
from datetime import datetime

from sqlalchemy import select
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
