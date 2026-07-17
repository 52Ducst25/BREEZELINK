"""``comfort_log`` writes — audit trail of every adaptive-algorithm decision
that resulted in a published command (design §3.1).

Only called when the comfort worker actually changes (mode, setpoint) — a
row here always pairs 1:1 with a dispatched ``commands`` row (Phase 4
command_publisher).
"""

import uuid
from datetime import datetime

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.comfort_log import ComfortLog
from app.schemas.comfort import ComfortResult

_DEFAULT_LIMIT = 200


async def insert_comfort_log(
    session: AsyncSession,
    *,
    org_id: uuid.UUID,
    ts: datetime,
    t_out: float,
    h_out: float | None,
    t_in: float,
    h_in: float,
    result: ComfortResult,
) -> ComfortLog:
    """Insert one comfort-decision audit row and commit."""
    row = ComfortLog(
        org_id=org_id,
        ts=ts,
        t_out=t_out,
        h_out=h_out,
        t_in=t_in,
        h_in=h_in,
        t_rm=result.t_rm,
        t_neutral=result.t_neutral,
        humid_penalty=result.humid_penalty,
        t_target=result.t_target,
        t_set=result.t_set,
        mode=result.mode,
    )
    session.add(row)
    await session.commit()
    return row


async def list_comfort_log(
    session: AsyncSession,
    *,
    org_id: uuid.UUID,
    start: datetime | None = None,
    end: datetime | None = None,
    limit: int = _DEFAULT_LIMIT,
) -> list[ComfortLog]:
    """Decision-history chart series for one org, oldest-first (Phase 5
    ``GET /comfort/log``), optionally bounded by a ``[start, end]`` range.
    """
    stmt = select(ComfortLog).where(ComfortLog.org_id == org_id)
    if start is not None:
        stmt = stmt.where(ComfortLog.ts >= start)
    if end is not None:
        stmt = stmt.where(ComfortLog.ts <= end)
    stmt = stmt.order_by(ComfortLog.ts.asc()).limit(limit)
    return list((await session.execute(stmt)).scalars().all())
