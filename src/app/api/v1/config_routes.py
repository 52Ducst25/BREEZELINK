"""Admin-only comfort-algorithm tunables CRUD (design §3.1 ``configs`` table).

Both GET and PUT are owner-only (the ``admin`` role in design terms — this
codebase's role enum is owner/member, see ``api/deps.require_role``):
tunables affect every device in the org, not something an ordinary member
should read or change.
"""

from fastapi import APIRouter, Depends
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.deps import get_db, require_role
from app.models.user import User
from app.schemas.config import ConfigRead, ConfigUpdate
from app.services import config_service

router = APIRouter(prefix="/configs", tags=["configs"])


@router.get("", response_model=ConfigRead)
async def get_configs(
    user: User = Depends(require_role("owner")),
    session: AsyncSession = Depends(get_db),
) -> ConfigRead:
    return await config_service.get_config(session, user.org_id)


@router.put("", response_model=ConfigRead)
async def update_configs(
    data: ConfigUpdate,
    user: User = Depends(require_role("owner")),
    session: AsyncSession = Depends(get_db),
) -> ConfigRead:
    """Validated partial update; invalidates the Redis mirror so the comfort
    worker picks up the new values on its next tick.
    """
    return await config_service.update_config(session, user.org_id, data)
