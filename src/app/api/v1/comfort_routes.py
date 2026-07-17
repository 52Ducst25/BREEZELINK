"""Comfort endpoints: read-only preview, manual override set/clear, and the
decision-history chart series (design §1.4 preview, §1.2E override, §3.1 log).

All routes are user-role (any authenticated org member) — no admin-only
comfort control per design (§1: manual override is a normal user action).
"""

import json
from datetime import datetime

import aiomqtt
from fastapi import APIRouter, Depends, Query
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.deps import get_current_user, get_db, get_mqtt_publisher
from app.core.exceptions import AppException, NotFoundError
from app.models.user import User
from app.schemas.comfort import (
    ComfortLogRead,
    ComfortPreview,
    OverrideRequest,
    OverrideResponse,
)
from app.schemas.common import MessageResponse
from app.services import (
    comfort_log_service,
    comfort_preview_service,
    ir_service,
    redis_config_cache,
    redis_override_service,
    telemetry_service,
)
from app.workers import command_publisher

router = APIRouter(prefix="/comfort", tags=["comfort"])


@router.get("/preview", response_model=ComfortPreview)
async def get_preview(
    user: User = Depends(get_current_user),
    session: AsyncSession = Depends(get_db),
) -> ComfortPreview:
    """Current auto-decision preview — no publish, no persistence."""
    return await comfort_preview_service.build_preview(session, str(user.org_id))


@router.post("/override", response_model=OverrideResponse)
async def set_override(
    data: OverrideRequest,
    user: User = Depends(get_current_user),
    session: AsyncSession = Depends(get_db),
    mqtt_client: aiomqtt.Client = Depends(get_mqtt_publisher),
) -> OverrideResponse:
    """Set a manual override: gate auto-control (Redis TTL) + publish the
    command immediately (source=manual). Still allowed even if the IR table
    is incomplete — the response surfaces a coverage warning instead of
    blocking the user's control action.
    """
    org_id = str(user.org_id)
    cfg = await redis_config_cache.get_cfg(org_id, session)
    if not (float(cfg["clamp_min"]) <= data.setpoint <= float(cfg["clamp_max"])):
        raise AppException(
            f"setpoint must be within [{cfg['clamp_min']}, {cfg['clamp_max']}]",
            status_code=422,
        )

    device = await telemetry_service.get_device_by_org_and_node(session, org_id, "indoor")
    if device is None:
        raise NotFoundError("Indoor device not registered for this organization")

    await redis_override_service.set_override(
        org_id,
        json.dumps({"mode": data.mode.value, "setpoint": data.setpoint}),
        hours=int(cfg["override_hours"]),
    )
    await command_publisher.publish_manual_command(
        mqtt_client,
        session,
        org_id=org_id,
        device_id=device.id,
        mode=data.mode,
        setpoint=data.setpoint,
    )

    missing = await ir_service.check_coverage(session, user.org_id)
    return OverrideResponse(detail="override set", missing_ir_codes=missing)


@router.delete("/override", response_model=MessageResponse)
async def clear_override(user: User = Depends(get_current_user)) -> MessageResponse:
    """Explicitly resume auto-control before the override TTL expires."""
    await redis_override_service.clear_override(str(user.org_id))
    return MessageResponse(detail="override cleared")


@router.get("/log", response_model=list[ComfortLogRead])
async def get_comfort_log(
    start: datetime | None = None,
    end: datetime | None = None,
    limit: int = Query(default=200, gt=0, le=1000),
    user: User = Depends(get_current_user),
    session: AsyncSession = Depends(get_db),
) -> list[ComfortLogRead]:
    """Decision-history series for the "why was this setpoint chosen" chart."""
    return await comfort_log_service.list_comfort_log(
        session, org_id=user.org_id, start=start, end=end, limit=limit
    )
