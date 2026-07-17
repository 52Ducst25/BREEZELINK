"""``commands`` table writes — dispatched AC commands + app-level ACK match.

``insert_command`` is called by the auto path (Phase 4 command_publisher,
``source=auto``); the manual-override REST endpoint (Phase 5) reuses it with
``source=manual``. ``set_ack`` matches a device's ``state`` topic ack
(``req_id``) back to the row that requested it.
"""

import uuid
from datetime import datetime

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.command import Command
from app.models.enums import AcMode, CommandSource


async def insert_command(
    session: AsyncSession,
    *,
    device_id: uuid.UUID,
    ts: datetime,
    mode: AcMode,
    setpoint: int,
    ir_code_id: uuid.UUID | None,
    source: CommandSource,
    req_id: str,
) -> Command:
    """Insert one dispatched-command row and commit."""
    row = Command(
        device_id=device_id,
        ts=ts,
        mode=mode,
        setpoint=setpoint,
        ir_code_id=ir_code_id,
        source=source,
        req_id=req_id,
    )
    session.add(row)
    await session.commit()
    return row


async def set_ack(session: AsyncSession, req_id: str, ack_ts: datetime) -> bool:
    """Set ``ack_ts`` on the command matching ``req_id``.

    Returns ``False`` (no-op, logged by the caller) if no matching command is
    found — e.g. a stale/duplicate retained ``state`` message replaying an
    old ``ack`` after restart.
    """
    stmt = (
        select(Command)
        .where(Command.req_id == req_id)
        .order_by(Command.ts.desc())
        .limit(1)
    )
    command = (await session.execute(stmt)).scalar_one_or_none()
    if command is None:
        return False
    command.ack_ts = ack_ts
    await session.commit()
    return True
