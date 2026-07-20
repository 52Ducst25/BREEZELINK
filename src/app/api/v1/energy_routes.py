"""Energy endpoints: home 'Power' overview + the per-device watt/kWh series.

kWh is integrated on read from ``telemetry.watt`` (energy_service). Everything
is None/empty until a node reports watts — the app shows "chưa có dữ liệu".
"""

import uuid
from datetime import datetime, timedelta, timezone

from fastapi import APIRouter, Depends
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.deps import get_current_user, get_db
from app.models.user import User
from app.services import device_service, energy_service

router = APIRouter(prefix="/energy", tags=["energy"])


def _utc(dt: datetime | None, default: datetime) -> datetime:
    """Coerce a query datetime to UTC-aware so it compares against the tz-aware
    ``telemetry.ts`` (a naive value from the client is assumed to be UTC)."""
    if dt is None:
        return default
    return dt if dt.tzinfo is not None else dt.replace(tzinfo=timezone.utc)


@router.get("/overview")
async def energy_overview(
    user: User = Depends(get_current_user),
    session: AsyncSession = Depends(get_db),
) -> dict:
    """Today's kWh per device + % vs yesterday, for the home power card."""
    return await energy_service.overview(session, str(user.org_id))


@router.get("/series")
async def energy_series(
    device_id: uuid.UUID,
    start: datetime | None = None,
    end: datetime | None = None,
    user: User = Depends(get_current_user),
    session: AsyncSession = Depends(get_db),
) -> dict:
    """Watt samples + integrated kWh for one device over a time window.

    Defaults to the last 24h. ``get_device`` is org-scoped, so a device id from
    another household raises 404 rather than leaking its readings.
    """
    await device_service.get_device(session, user.org_id, device_id)
    now = datetime.now(timezone.utc)
    return await energy_service.series(
        session, device_id, _utc(start, now - timedelta(days=1)), _utc(end, now)
    )
