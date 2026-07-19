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
from app.models.enums import AcMode
from app.models.user import User
from app.schemas.comfort import (
    ComfortLogRead,
    ComfortPreview,
    FanSpeedResponse,
    IrActionRequest,
    OverrideRequest,
    OverrideResponse,
)
from app.schemas.common import MessageResponse
from app.services import (
    comfort_log_service,
    comfort_preview_service,
    ir_action_service,
    ir_service,
    redis_config_cache,
    redis_override_service,
    redis_state_service,
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
    # Only COOL actually uses the setpoint — DRY/FAN/OFF snap to a fixed IR
    # button and discard it (ir_code_service._snap_temp). Clamping every mode
    # would 422 a power-OFF (or DRY/FAN) that merely carries a stale/out-of-range
    # setpoint, so the AC could never be turned off from a client whose dial is
    # not itself clamped to the real bounds (e.g. a member with no owner config).
    if data.mode == AcMode.COOL and not (
        float(cfg["clamp_min"]) <= data.setpoint <= float(cfg["clamp_max"])
    ):
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


def _current_indoor(state: dict | None) -> tuple[AcMode, int]:
    """Best-effort (mode, setpoint) from the Redis indoor state hash — carried
    as metadata on a fan-speed blast so the node's state echo stays consistent.
    Falls back to FAN/25 when unknown/unparsable (the fan step never changes
    mode/setpoint anyway; this is just non-authoritative metadata).
    """
    mode, setpoint = AcMode.FAN, 25
    if state:
        try:
            mode = AcMode(str(state.get("mode")))
        except ValueError:
            pass
        try:
            setpoint = int(float(state.get("setpoint")))
        except (TypeError, ValueError):
            pass
    return mode, setpoint


async def _send_ir_action(
    session: AsyncSession, mqtt_client: aiomqtt.Client, org_id: str, action: str
) -> FanSpeedResponse:
    """Blast one learned standalone action IR frame (FAN_SPEED, SLEEP, ECO, …).

    Open-loop: the cloud can't read the AC's resulting state — the button just
    presses the physical remote's button once. ``available=False`` (a normal
    200, not an error) when that button hasn't been learned yet, so the app can
    prompt the user to learn it instead of showing a failure.
    """
    raw = await ir_action_service.get_action_raw(session, org_id, action)
    if raw is None:
        return FanSpeedResponse(
            available=False,
            detail="Chưa học nút này. Vào tab Học lệnh để học từ điều khiển thật.",
        )
    device = await telemetry_service.get_device_by_org_and_node(session, org_id, "indoor")
    if device is None:
        raise NotFoundError("Indoor device not registered for this organization")

    mode, setpoint = _current_indoor(await redis_state_service.get_indoor_state(org_id))
    await command_publisher.publish_ir_action(
        mqtt_client,
        session,
        org_id=org_id,
        device_id=device.id,
        action=action,
        raw_timing=raw,
        mode=mode,
        setpoint=setpoint,
    )
    return FanSpeedResponse(available=True, detail="đã gửi lệnh")


@router.post("/ir-action", response_model=FanSpeedResponse)
async def send_ir_action(
    data: IrActionRequest,
    user: User = Depends(get_current_user),
    session: AsyncSession = Depends(get_db),
    mqtt_client: aiomqtt.Client = Depends(get_mqtt_publisher),
) -> FanSpeedResponse:
    """Press one learned remote button (any KNOWN_ACTIONS label). The remote UI
    uses this for every peripheral button; fan speed included."""
    if data.action not in ir_action_service.KNOWN_ACTIONS:
        raise AppException(f"unknown action '{data.action}'", status_code=422)
    return await _send_ir_action(session, mqtt_client, str(user.org_id), data.action)


@router.post("/fan-speed", response_model=FanSpeedResponse)
async def fan_speed_step(
    user: User = Depends(get_current_user),
    session: AsyncSession = Depends(get_db),
    mqtt_client: aiomqtt.Client = Depends(get_mqtt_publisher),
) -> FanSpeedResponse:
    """Back-compat shim for the already-shipped build 6, which posts here with
    no body. New builds use POST /ir-action {action: "FAN_SPEED"}."""
    return await _send_ir_action(session, mqtt_client, str(user.org_id), ir_action_service.FAN_SPEED)


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
