"""Auth endpoints: register / login / refresh / logout / me / password reset.

``/register`` and ``/login`` are rate-limited (M4 — see core/rate_limit.py):
a public brute-force target with no throttle is the real exposure gate
before this API leaves the local network.
"""

import uuid

from fastapi import APIRouter, Depends, Request, status
from sqlalchemy.ext.asyncio import AsyncSession

from app.api.deps import get_current_user, get_db
from app.core.exceptions import UnauthorizedError
from app.core.mailer import send_reset_email
from app.core.rate_limit import limiter
from app.core.security import (
    RESET_TOKEN_TYPE,
    create_reset_token,
    decode_token,
    password_fingerprint,
)
from app.models.user import User
from app.schemas.auth import (
    AccessToken,
    ForgotPasswordRequest,
    LoginRequest,
    MeResponse,
    RefreshRequest,
    RegisterRequest,
    ResetPasswordRequest,
    TokenPair,
)
from app.services import auth_service, user_service

router = APIRouter(prefix="/auth", tags=["auth"])

# Generic response for forgot-password — identical on hit/miss so the API
# never reveals whether an email is registered (no user enumeration).
_FORGOT_PASSWORD_DETAIL = (
    "If an account exists for that email, a reset link has been sent."
)


@router.post("/register", response_model=TokenPair, status_code=status.HTTP_201_CREATED)
@limiter.limit("5/minute")
async def register(
    request: Request, data: RegisterRequest, session: AsyncSession = Depends(get_db)
) -> TokenPair:
    # Stamp the app build here too, not only at login: a customer who just
    # activated has never logged in separately, so without this their
    # "Phiên bản app" column stays "—" on the vendor page until their next
    # launch. Read off the header, same as login (X-App-Version).
    access, refresh = await auth_service.register(
        session, data, app_version=request.headers.get("X-App-Version")
    )
    return TokenPair(access_token=access, refresh_token=refresh)


@router.post("/login", response_model=TokenPair)
@limiter.limit("5/minute")
async def login(
    request: Request, data: LoginRequest, session: AsyncSession = Depends(get_db)
) -> TokenPair:
    # Read off the HEADER, not the body. LoginRequest is a plain pydantic model
    # with default config, so an "app_version" field in the JSON would be
    # silently DROPPED — no error, no value, just a column that stays null
    # forever. The header also needs no schema change and no app-side change to
    # this call, because ApiClient sets it once for every request it makes.
    # ``request`` is already here for the rate limiter; nothing to thread.
    access, refresh = await auth_service.login(
        session, data.email, data.password, app_version=request.headers.get("X-App-Version")
    )
    return TokenPair(access_token=access, refresh_token=refresh)


@router.post("/refresh", response_model=AccessToken)
async def refresh(data: RefreshRequest, session: AsyncSession = Depends(get_db)) -> AccessToken:
    access = await auth_service.refresh(session, data.refresh_token)
    return AccessToken(access_token=access)


@router.post("/logout")
async def logout() -> dict[str, str]:
    """Stateless logout — the client discards its tokens (HS256, no server denylist)."""
    return {"detail": "logged out"}


@router.get("/me", response_model=MeResponse)
async def me(user: User = Depends(get_current_user)) -> MeResponse:
    """Current caller's identity — backs the Flutter user_profile screen."""
    return MeResponse(id=user.id, email=user.email, org_id=user.org_id, role=user.role.value)


@router.post("/forgot-password", status_code=status.HTTP_202_ACCEPTED)
async def forgot_password(
    data: ForgotPasswordRequest, session: AsyncSession = Depends(get_db)
) -> dict[str, str]:
    """Always 202 regardless of whether the email exists (no enumeration).

    KISS: no DB table for reset tokens — a short-lived signed JWT
    (``purpose: reset``, see core/security.create_reset_token) IS the token.
    """
    user = await user_service.get_by_email(session, data.email)
    if user is not None and user.is_active:
        reset_token = create_reset_token(
            str(user.id), password_fingerprint(user.password_hash)
        )
        await send_reset_email(user.email, reset_token)
    return {"detail": _FORGOT_PASSWORD_DETAIL}


@router.post("/reset-password")
async def reset_password(
    data: ResetPasswordRequest, session: AsyncSession = Depends(get_db)
) -> dict[str, str]:
    payload = decode_token(data.token, expected_type=RESET_TOKEN_TYPE)
    try:
        user_id = uuid.UUID(payload["sub"])
    except (KeyError, ValueError) as exc:
        raise UnauthorizedError("Invalid reset token") from exc

    user = await user_service.get_by_id(session, user_id)
    if user is None or not user.is_active:
        raise UnauthorizedError("Invalid reset token")

    # H3: reject a token minted for an older password (already used / rotated).
    if payload.get("pwf") != password_fingerprint(user.password_hash):
        raise UnauthorizedError("Invalid reset token")

    await user_service.update_user(session, user, password=data.new_password)
    await session.commit()
    return {"detail": "Password has been reset"}
