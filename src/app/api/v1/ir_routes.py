"""IR-code endpoints: trigger LEARN mode + list captured codes with coverage
flags (design §4.2 LEARN flow, §5 risk)."""

import aiomqtt
from fastapi import APIRouter, Depends, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.deps import get_current_user, get_db, get_mqtt_publisher
from app.core.exceptions import AppException
from app.models.user import User
from app.schemas.common import MessageResponse
from app.schemas.ir import IrCoverageResponse, LearnActionRequest, LearnRequest
from app.services import ir_action_service, ir_service

router = APIRouter(prefix="/ir", tags=["ir"])


@router.post("/learn", response_model=MessageResponse, status_code=status.HTTP_202_ACCEPTED)
async def trigger_learn(
    data: LearnRequest,
    user: User = Depends(get_current_user),
    mqtt_client: aiomqtt.Client = Depends(get_mqtt_publisher),
) -> MessageResponse:
    """Put the indoor node into LEARN mode for one (mode, temp) button.

    Fire-and-forget: the captured code arrives later on the ``learn`` MQTT
    topic (Phase 4 ``learn_handler``), not in this response.
    """
    await ir_service.trigger_learn(mqtt_client, str(user.org_id), data.mode, data.temp)
    return MessageResponse(detail="learn triggered")


@router.post("/learn-action", response_model=MessageResponse, status_code=status.HTTP_202_ACCEPTED)
async def trigger_learn_action(
    data: LearnActionRequest,
    user: User = Depends(get_current_user),
    mqtt_client: aiomqtt.Client = Depends(get_mqtt_publisher),
) -> MessageResponse:
    """Put the node into LEARN mode for a standalone action button (FAN_SPEED).

    Same fire-and-forget contract as ``/learn`` — the captured frame arrives on
    the ``learn`` topic and is routed into ``ir_action_codes``.
    """
    if data.action not in ir_action_service.KNOWN_ACTIONS:
        raise AppException(f"unknown action '{data.action}'", status_code=422)
    await ir_service.trigger_learn_action(mqtt_client, str(user.org_id), data.action)
    return MessageResponse(detail="learn triggered")


@router.get("/codes", response_model=IrCoverageResponse)
async def list_codes(
    user: User = Depends(get_current_user),
    session: AsyncSession = Depends(get_db),
) -> IrCoverageResponse:
    """Captured codes + which required buttons (COOL 24-28/DRY/FAN/OFF) are
    still missing before auto-control can safely run, plus any learned
    standalone action buttons (e.g. FAN_SPEED).
    """
    codes = await ir_service.list_codes(session, user.org_id)
    missing = await ir_service.check_coverage(session, user.org_id)
    actions = await ir_action_service.list_actions(session, user.org_id)
    return IrCoverageResponse(codes=codes, missing=missing, actions=actions)
