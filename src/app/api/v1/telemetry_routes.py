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
