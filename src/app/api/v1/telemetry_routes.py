"""Telemetry chart-series endpoint (design §3.1: ``(device_id, ts DESC)`` index)."""

import uuid
from datetime import datetime

from fastapi import APIRouter, Depends, Query
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.deps import get_current_user, get_db
from app.models.user import User
from app.schemas.telemetry import TelemetryRead
from app.services import device_service, telemetry_service

router = APIRouter(prefix="/telemetry", tags=["telemetry"])


@router.get("", response_model=list[TelemetryRead])
async def get_telemetry(
    device_id: uuid.UUID,
    start: datetime | None = None,
    end: datetime | None = None,
    limit: int = Query(default=200, gt=0, le=1000),
    user: User = Depends(get_current_user),
    session: AsyncSession = Depends(get_db),
) -> list[TelemetryRead]:
    """Time-range series for one device, org-scoped (device ownership is
    verified before any rows are returned — no cross-tenant leak).
    """
    await device_service.get_device(session, user.org_id, device_id)
    return await telemetry_service.list_telemetry(
        session, device_id=device_id, start=start, end=end, limit=limit
    )


@router.get("/series")
async def get_telemetry_series(
    device_id: uuid.UUID,
    start: datetime,
    end: datetime,
    buckets: int = Query(default=120, gt=1, le=500),
    user: User = Depends(get_current_user),
    session: AsyncSession = Depends(get_db),
) -> list[dict]:
    """Down-sampled temp/humidity series for the app's per-device history
    charts (Hôm nay / 7 ngày / 30 ngày).

    Unlike ``GET /telemetry`` — which returns the newest N raw rows and would
    therefore cover only a few hours of a month-long window — this averages
    each of ``buckets`` equal time slots, so the series always spans the
    requested range. Org-scoped: device ownership is verified first.
    """
    await device_service.get_device(session, user.org_id, device_id)
    return await telemetry_service.series_bucketed(
        session, device_id=device_id, start=start, end=end, buckets=buckets
    )
