"""API dependencies: auth + role enforcement.

TRAP #1: ``get_current_user`` is a PLAIN async function used directly as a
dependency. ``require_role(role)`` is a FACTORY — it MUST be called:
    Depends(require_role("owner"))        # correct
    Depends(require_role)                 # WRONG — leaves the route unprotected
"""

import uuid
from collections.abc import AsyncGenerator

import aiomqtt
from fastapi import Depends
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.database import get_db
from app.core.exceptions import ForbiddenError, UnauthorizedError
from app.core.mqtt import build_mqtt_client
from app.core.security import ACCESS_TOKEN_TYPE, decode_token
from app.core.tenant import set_current_org, set_current_user
from app.models.user import User, UserRole
from app.services import user_service

# auto_error=False so we raise our own consistent 401 (not FastAPI's default).
_bearer = HTTPBearer(auto_error=False)

# Re-export for route modules.
__all__ = ["get_db", "get_current_user", "require_role", "get_mqtt_publisher"]


async def get_current_user(
    credentials: HTTPAuthorizationCredentials | None = Depends(_bearer),
    session: AsyncSession = Depends(get_db),
) -> User:
    if credentials is None:
        raise UnauthorizedError("Not authenticated")
    payload = decode_token(credentials.credentials, expected_type=ACCESS_TOKEN_TYPE)
    try:
        user_id = uuid.UUID(payload["sub"])
    except (KeyError, ValueError) as exc:
        raise UnauthorizedError("Invalid token subject") from exc

    user = await user_service.get_by_id(session, user_id)
    if user is None or not user.is_active:
        raise UnauthorizedError("Not authenticated")

    # Ensure tenant context is populated for the service layer.
    set_current_org(str(user.org_id))
    set_current_user(str(user.id))
    return user


def restrict_for(user: User) -> uuid.UUID | None:
    """Device-visibility restriction: None for owners (see all org devices),
    the user's own id for members (only devices assigned to them)."""
    return None if user.role == UserRole.owner else user.id


def require_role(role: str):
    """Factory returning a dependency that enforces the given role."""

    async def _require(user: User = Depends(get_current_user)) -> User:
        if user.role.value != role:
            raise ForbiddenError("Insufficient permissions")
        return user

    return _require


async def get_mqtt_publisher() -> AsyncGenerator[aiomqtt.Client, None]:
    """One-off MQTT connection for API-triggered publishes (manual override,
    LEARN trigger). Opened per-request and closed right after — the worker
    (not the API) owns the long-lived subscriber connection (core/mqtt.py).
    """
    async with build_mqtt_client(identifier="aircon_api") as client:
        yield client
