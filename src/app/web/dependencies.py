"""SSR auth dependencies: cookie session (vs the JSON API's Bearer JWT).

The admin pages can't attach an Authorization header (they're plain browser
navigation, not fetch calls with custom headers), so they authenticate from
the same httpOnly cookie the WebSocket endpoint already reads for
``/api/v1/ws/live`` (see ``core.security.SESSION_COOKIE_NAME`` and
``api/v1/ws_routes.py``'s docstring on why that cookie exists at all).
"""

import uuid
from datetime import datetime, timezone

from fastapi import Depends, Request
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.database import get_db
from app.core.security import SESSION_COOKIE_NAME, WEB_SESSION_TOKEN_TYPE, decode_token
from app.core.tenant import set_current_org, set_current_user
from app.models.user import User
from app.services import user_service

__all__ = [
    "get_db",
    "RedirectToLogin",
    "get_current_user_or_none",
    "require_web_user",
    "require_sysadmin",
]


class RedirectToLogin(Exception):
    """No valid SSR session, or one that is not vendor staff. main.py
    registers exactly one app-level handler for this exception (303 ->
    ``/web/login``) — reused for the sysadmin gate too rather than inventing
    a second handler this phase has no way to wire up. Bouncing a customer
    back to login is safe (nothing is disclosed) even though the copy shown
    ("please sign in") is generic; the login POST itself explains the real
    reason, since by then they have proven their password.
    """


async def get_current_user_or_none(request: Request, session: AsyncSession) -> User | None:
    """Resolve the caller from the session cookie, or ``None`` if absent/invalid."""
    token = request.cookies.get(SESSION_COOKIE_NAME)
    if not token:
        return None
    try:
        payload = decode_token(token, expected_type=WEB_SESSION_TOKEN_TYPE)
    except Exception:
        return None
    try:
        user_id = uuid.UUID(payload["sub"])
    except (KeyError, ValueError):
        return None

    user = await user_service.get_by_id(session, user_id)
    if user is None or not user.is_active:
        return None

    # Heartbeat for the "Online" status. Updated on any authenticated page load,
    # THROTTLED to once a minute so the SSR admin's ~5s auto-refresh doesn't
    # write on every tick — one UPDATE/minute per active admin, no more. An
    # admin with the panel open keeps this fresh, so the staff list can show
    # "online" = seen in the last couple of minutes (users.html / the `online`
    # filter). GETs otherwise don't write, so this commit is the exception.
    now = datetime.now(timezone.utc)
    last = user.last_seen_at
    if last is None or (now - last).total_seconds() > 60:
        user.last_seen_at = now
        await session.commit()

    # Populate tenant contextvars for the service layer, same as the API's
    # get_current_user dependency (api/deps.py) does.
    set_current_org(str(user.org_id))
    set_current_user(str(user.id))
    return user


async def require_web_user(
    request: Request, session: AsyncSession = Depends(get_db)
) -> User:
    """Baseline gate for every SSR page: any authenticated org member."""
    user = await get_current_user_or_none(request, session)
    if user is None:
        raise RedirectToLogin()
    return user


async def require_sysadmin(user: User = Depends(require_web_user)) -> User:
    """Vendor-staff gate — guards EVERY page of this admin.

    This whole SSR admin is the vendor's back office; customers use the Flutter
    app. So the gate is not per-page any more, and there is deliberately no
    weaker alternative to reach for: an owner-of-their-own-household gate used
    to exist here and was what let customers into /web/config to retune their
    own comfort algorithm from a browser.

    ``role`` (owner/member) still means something — authority inside one
    household — but it says nothing about vendor staff, so it must never be
    what admits someone here.

    SafeKitchen repeated ``if not user.is_sysadmin: return RedirectResponse(...)``
    inline in six handlers; declaring ``Depends(require_sysadmin)`` once per
    route keeps the rule impossible to forget.
    """
    if not user.is_sysadmin:
        raise RedirectToLogin()
    return user
