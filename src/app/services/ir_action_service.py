"""``ir_action_codes`` reads/writes — standalone learned actions (e.g. the
remote's "fan speed" button) that live outside the (mode, temp) comfort matrix.

Deliberately separate from ``ir_code_service`` so an action code can never be
snapped/looked-up as a comfort code, and so the LEARN handler + the fan-speed
send endpoint have one clear place for this table's access.
"""

import uuid

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.ir_action_code import IrActionCode

# The only action today. Kept as a constant + set so the LEARN handler can
# recognise the node's echo label and reject anything else.
FAN_SPEED = "FAN_SPEED"
KNOWN_ACTIONS = frozenset({FAN_SPEED})


async def get_action_raw(session: AsyncSession, org_id: str, action: str) -> list[int] | None:
    """Raw mark/space timing for one org's action code, or ``None`` if unlearned."""
    stmt = select(IrActionCode.raw_timing).where(
        IrActionCode.org_id == uuid.UUID(org_id), IrActionCode.action == action
    )
    return (await session.execute(stmt)).scalar_one_or_none()


async def list_actions(session: AsyncSession, org_id: uuid.UUID) -> list[str]:
    """Action labels this org has learned, e.g. ``["FAN_SPEED"]``."""
    stmt = select(IrActionCode.action).where(IrActionCode.org_id == org_id)
    return list((await session.execute(stmt)).scalars().all())


async def upsert_action_code(
    session: AsyncSession, org_id: str, action: str, raw_timing: list[int]
) -> IrActionCode:
    """Insert or replace the ``(org, action)`` row from a LEARN capture."""
    stmt = select(IrActionCode).where(
        IrActionCode.org_id == uuid.UUID(org_id), IrActionCode.action == action
    )
    existing = (await session.execute(stmt)).scalar_one_or_none()
    if existing is not None:
        existing.raw_timing = raw_timing
        await session.commit()
        return existing

    row = IrActionCode(org_id=uuid.UUID(org_id), action=action, raw_timing=raw_timing)
    session.add(row)
    await session.commit()
    return row
