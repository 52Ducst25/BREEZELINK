"""Security primitives: bcrypt password hashing + JWT (HS256) helpers.

Pure functions — take payload + settings, no DB access. Used by services + deps.
"""

import hashlib
from datetime import datetime, timedelta, timezone

import bcrypt
from jose import JWTError, jwt

from app.config import get_settings
from app.core.exceptions import UnauthorizedError

# bcrypt rejects secrets longer than 72 bytes; truncate defensively.
_BCRYPT_MAX_BYTES = 72

ACCESS_TOKEN_TYPE = "access"
REFRESH_TOKEN_TYPE = "refresh"
RESET_TOKEN_TYPE = "reset"
# Distinct from "access" on purpose. The SSR cookie previously carried a real
# access token, so anything that could read the cookie could also drive the
# JSON API with it. A type the API never accepts keeps a stolen browser cookie
# confined to the pages a browser can already reach.
WEB_SESSION_TOKEN_TYPE = "web_session"

# Name of the httpOnly cookie carrying the access JWT for the SSR admin. Lives
# here (not in web/) because the WebSocket endpoint under api/ must read the same
# cookie: it is httpOnly, so a browser cannot hand the token to JS for a
# ?token= query param the way the Flutter app does.
SESSION_COOKIE_NAME = "access_token"
_RESET_TOKEN_TTL_MINUTES = 30


def _truncate(password: str) -> bytes:
    return password.encode("utf-8")[:_BCRYPT_MAX_BYTES]


def hash_password(password: str) -> str:
    """Return a bcrypt hash of ``password``."""
    return bcrypt.hashpw(_truncate(password), bcrypt.gensalt()).decode("utf-8")


def verify_password(password: str, password_hash: str) -> bool:
    """Verify ``password`` against a stored bcrypt hash."""
    try:
        return bcrypt.checkpw(_truncate(password), password_hash.encode("utf-8"))
    except ValueError:
        return False


def _create_token(
    *, sub: str, org_id: str, role: str, token_type: str, expires: timedelta
) -> str:
    settings = get_settings()
    now = datetime.now(timezone.utc)
    payload = {
        "sub": sub,
        "org_id": org_id,
        "role": role,
        "type": token_type,
        "iat": now,
        "exp": now + expires,
    }
    return jwt.encode(payload, settings.jwt_secret, algorithm=settings.jwt_alg)


def create_access_token(sub: str, org_id: str, role: str) -> str:
    settings = get_settings()
    return _create_token(
        sub=sub,
        org_id=org_id,
        role=role,
        token_type=ACCESS_TOKEN_TYPE,
        expires=timedelta(minutes=settings.access_token_ttl_minutes),
    )


def create_web_session_token(sub: str, org_id: str, role: str) -> str:
    """Token for the SSR admin cookie.

    Long-lived where the API's access token is short-lived (30 min), because
    the two renew differently: an API client holds a refresh token and rotates
    silently, while the SSR admin stores only this cookie — nothing to renew
    with. At 30 minutes the cookie simply died mid-session, the live-refresh
    fetch got redirected to /web/login, and app.js reloaded the whole page out
    from under whoever was reading it. A session-length cookie is the honest
    fix; the shorter type never bought security here, it only logged people
    out on a timer.
    """
    settings = get_settings()
    return _create_token(
        sub=sub,
        org_id=org_id,
        role=role,
        token_type=WEB_SESSION_TOKEN_TYPE,
        expires=timedelta(hours=settings.web_session_ttl_hours),
    )


def create_refresh_token(sub: str, org_id: str, role: str) -> str:
    settings = get_settings()
    return _create_token(
        sub=sub,
        org_id=org_id,
        role=role,
        token_type=REFRESH_TOKEN_TYPE,
        expires=timedelta(days=settings.refresh_token_ttl_days),
    )


def password_fingerprint(password_hash: str) -> str:
    """Short, non-reversible fingerprint of a stored bcrypt hash.

    Binds a reset token to the password that was current when it was issued —
    once the password changes, the fingerprint changes and any outstanding
    reset token stops validating (H3: makes the token effectively single-use).
    """
    return hashlib.sha256(password_hash.encode("utf-8")).hexdigest()[:16]


def create_reset_token(sub: str, pw_fingerprint: str) -> str:
    """Short-lived password-reset token (``purpose: reset``, ~30min TTL).

    KISS: no DB table for reset tokens — the JWT itself (signed, self-expiring)
    IS the proof. The ``pwf`` claim binds it to the user's current password
    hash so it cannot be replayed after the password is reset/changed (H3).
    """
    settings = get_settings()
    now = datetime.now(timezone.utc)
    payload = {
        "sub": sub,
        "type": RESET_TOKEN_TYPE,
        "pwf": pw_fingerprint,
        "iat": now,
        "exp": now + timedelta(minutes=_RESET_TOKEN_TTL_MINUTES),
    }
    return jwt.encode(payload, settings.jwt_secret, algorithm=settings.jwt_alg)


def decode_token(token: str, *, expected_type: str | None = None) -> dict:
    """Decode + validate a JWT. Raises ``UnauthorizedError`` on any failure.

    If ``expected_type`` is provided, reject tokens of a different type
    (e.g. a refresh token used where an access token is required).
    """
    settings = get_settings()
    try:
        payload = jwt.decode(token, settings.jwt_secret, algorithms=[settings.jwt_alg])
    except JWTError as exc:
        raise UnauthorizedError("Invalid or expired token") from exc

    if expected_type is not None and payload.get("type") != expected_type:
        raise UnauthorizedError("Invalid token type")
    return payload
