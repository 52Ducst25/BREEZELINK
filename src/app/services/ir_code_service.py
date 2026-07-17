"""``ir_codes`` reads/writes shared by the command publisher (snap lookup)
and the LEARN handler (upsert from raw IR capture).

Kept as one small service (not folded into telemetry/command services) so
all direct ``IrCode`` table access lives in a single place — DRY for the two
Phase 4 callers.
"""

import uuid

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.enums import AcMode
from app.models.ir_code import IrCode

# H2/M3 fix: DRY/FAN/OFF are single fixed buttons with no setpoint, but
# ``ir_codes.temp`` is NOT NULL (migration 260714_2105) and part of the
# ``UNIQUE(org, mode, temp)`` snap key. Rather than relaxing the column to
# nullable (Postgres treats NULL as distinct in a unique index, which would
# let multiple DRY/FAN/OFF rows collide silently), we store a fixed sentinel
# temp for every non-COOL mode and always look them up mode-only via this
# sentinel — simplest option that keeps the existing schema/constraint as-is
# (KISS; decided during code-review fix-up, no migration change needed).
FIXED_MODE_TEMP = 0


def _snap_temp(mode: AcMode, temp: int) -> int:
    """Normalize the lookup/storage temp: COOL keeps its real setpoint,
    every fixed mode (DRY/FAN/OFF) always resolves to the shared sentinel.
    """
    return temp if mode == AcMode.COOL else FIXED_MODE_TEMP


async def get_available_temps(
    session: AsyncSession, org_id: str, mode: AcMode = AcMode.COOL
) -> list[int]:
    """Distinct captured temps for ``(org, mode)`` — snap candidates.

    Defaults to ``COOL`` since that is the only mode with a continuous
    learned temp range (design §5 risk); DRY/FAN are single fixed codes
    looked up directly by the decided mode afterwards.
    """
    stmt = (
        select(IrCode.temp)
        .where(IrCode.org_id == uuid.UUID(org_id), IrCode.mode == mode)
        .distinct()
    )
    return list((await session.execute(stmt)).scalars().all())


async def find_ir_code_id(
    session: AsyncSession, org_id: str, mode: AcMode, temp: int
) -> uuid.UUID | None:
    """Snap-key lookup: ``ir_codes`` id for ``(org, mode, temp)``.

    H2 fix: for a fixed mode (DRY/FAN/OFF) the caller's ``temp`` is whatever
    COOL-range value the setpoint snapper produced (there is no real
    setpoint for these modes) — it is ignored in favor of the shared
    ``FIXED_MODE_TEMP`` sentinel so the lookup is effectively mode-only.
    Returns ``None`` when that mode/temp combo hasn't been LEARNed yet —
    callers must tolerate a ``NULL ir_code_id`` on the dispatched command.
    """
    lookup_temp = _snap_temp(mode, temp)
    stmt = select(IrCode.id).where(
        IrCode.org_id == uuid.UUID(org_id), IrCode.mode == mode, IrCode.temp == lookup_temp
    )
    return (await session.execute(stmt)).scalar_one_or_none()


async def get_raw_timing(session: AsyncSession, ir_code_id: uuid.UUID) -> list[int] | None:
    """Raw mark/space timing array for one ``ir_codes`` row, or ``None``."""
    stmt = select(IrCode.raw_timing).where(IrCode.id == ir_code_id)
    return (await session.execute(stmt)).scalar_one_or_none()


async def upsert_learned_code(
    session: AsyncSession, org_id: str, mode: AcMode, temp: int | None, raw_timing: list[int]
) -> IrCode:
    """Insert or update the ``(org, mode, temp)`` row from a LEARN capture.

    M3 fix: ``temp`` is optional (``None``) for fixed modes — the node's
    LEARN echo for DRY/FAN/OFF carries no setpoint. COOL still requires a
    real temp (raises if missing, a genuine caller bug). Storage always
    normalizes through :func:`_snap_temp` so fixed-mode rows land on the
    shared sentinel and stay covered by ``UNIQUE(org, mode, temp)``.
    """
    if mode == AcMode.COOL and temp is None:
        raise ValueError("temp is required when learning a COOL code")
    stored_temp = _snap_temp(mode, temp or FIXED_MODE_TEMP)

    stmt = select(IrCode).where(
        IrCode.org_id == uuid.UUID(org_id), IrCode.mode == mode, IrCode.temp == stored_temp
    )
    existing = (await session.execute(stmt)).scalar_one_or_none()
    if existing is not None:
        existing.raw_timing = raw_timing
        await session.commit()
        return existing

    label = f"{mode.value}_{stored_temp}" if mode == AcMode.COOL else mode.value
    row = IrCode(
        org_id=uuid.UUID(org_id),
        mode=mode,
        temp=stored_temp,
        raw_timing=raw_timing,
        button_label=label,
    )
    session.add(row)
    await session.commit()
    return row
