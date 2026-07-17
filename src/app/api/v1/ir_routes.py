"""IR-code endpoints: trigger LEARN mode + list captured codes with coverage
flags (design §4.2 LEARN flow, §5 risk)."""

import aiomqtt
from fastapi import APIRouter, Depends, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.deps import get_current_user, get_db, get_mqtt_publisher
from app.models.user import User
from app.schemas.common import MessageResponse
from app.schemas.ir import IrCoverageResponse, LearnRequest
from app.services import ir_service

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


@router.get("/codes", response_model=IrCoverageResponse)
async def list_codes(
    user: User = Depends(get_current_user),
    session: AsyncSession = Depends(get_db),
) -> IrCoverageResponse:
    """Captured codes + which required buttons (COOL 24-28/DRY/FAN/OFF) are
    still missing before auto-control can safely run.
    """
    codes = await ir_service.list_codes(session, user.org_id)
    missing = await ir_service.check_coverage(session, user.org_id)
    return IrCoverageResponse(codes=codes, missing=missing)
