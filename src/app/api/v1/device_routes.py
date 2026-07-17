"""Device registration/listing endpoints. List is any org member (user);
registering a new physical node is owner-only (admin) — matches the phase
spec's ``GET/POST /devices | user/admin`` split.
"""

from fastapi import APIRouter, Depends, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.deps import get_current_user, get_db, require_role
from app.models.user import User
from app.schemas.device import DeviceCreate, DeviceRead
from app.services import device_service

router = APIRouter(prefix="/devices", tags=["devices"])


@router.get("", response_model=list[DeviceRead])
async def list_devices(
    user: User = Depends(get_current_user),
    session: AsyncSession = Depends(get_db),
) -> list[DeviceRead]:
    return await device_service.list_devices(session, user.org_id)


@router.post("", response_model=DeviceRead, status_code=status.HTTP_201_CREATED)
async def register_device(
    data: DeviceCreate,
    user: User = Depends(require_role("owner")),
    session: AsyncSession = Depends(get_db),
) -> DeviceRead:
    return await device_service.register_device(
        session,
        org_id=user.org_id,
        name=data.name,
        node_type=data.node_type,
        location=data.location,
    )
