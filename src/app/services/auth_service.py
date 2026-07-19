"""Auth orchestration: register (org+owner tx), login, refresh."""

import uuid
from datetime import datetime, timezone

from sqlalchemy.ext.asyncio import AsyncSession

from app.core.exceptions import ConflictError, UnauthorizedError
from app.core.mailer import send_reset_email
from app.core.security import (
    REFRESH_TOKEN_TYPE,
    RESET_TOKEN_TYPE,
    create_access_token,
    create_refresh_token,
    create_reset_token,
    decode_token,
    password_fingerprint,
)
from app.models.user import User
from app.schemas.auth import RegisterRequest
from app.services import invite_service, live_events, organization_service, user_service


def _token_pair(user: User) -> tuple[str, str]:
    sub = str(user.id)
    org = str(user.org_id)
    role = user.role.value
    return create_access_token(sub, org, role), create_refresh_token(sub, org, role)


async def register(
    session: AsyncSession, data: RegisterRequest, app_version: str | None = None
) -> tuple[str, str]:
    """Redeem an activation code into a user inside the code's organization.

    This no longer creates an organization. The vendor creates it when
    recording the sale (web/customer_routes) and hands the buyer a code; here
    we only spend that code. Anyone without a code cannot get in — which is
    the whole point of the change.

    Code lookup comes FIRST, before the email check: a stranger probing this
    endpoint should learn nothing about which emails exist until they have
    proven they hold a valid code.

    One transaction: ``get_unused`` locks the code row, the user is created,
    the code is marked spent, and all of it commits together. A crash anywhere
    in between rolls back the whole thing rather than burning a code without
    producing an account.
    """
    invite = await invite_service.get_unused(session, data.code)

    existing = await user_service.get_by_email(session, data.email)
    if existing is not None:
        raise ConflictError("Email already registered")

    user = await user_service.create_user(
        session,
        org_id=invite.org_id,
        email=data.email,
        password=data.password,
        role=invite.role,
        full_name=data.full_name,
        phone=data.phone,
    )
    if app_version:
        user.app_version = app_version[:32]
    await invite_service.mark_used(session, invite, user)

    # The vendor no longer types a customer name at sale time — the product
    # wears a "Sản phẩm <code>" placeholder until now. The buyer just told us
    # who they are, so carry that onto the org: this is the "web tự cập nhật"
    # the vendor asked for, the customer list naming itself from the app.
    #
    # Only while still unclaimed: a second person redeeming an extra code for
    # the same household (multi-phone) must not rename it out from under the
    # first, and a name a staff member set by hand must win.
    org = await organization_service.get_org(session, invite.org_id)
    if organization_service.is_unclaimed(org):
        org.name = (data.full_name or "").strip() or data.email
        if data.phone:
            org.contact = data.phone.strip()

    await session.commit()

    # AFTER the commit, never before: the vendor's page re-reads from Postgres
    # when nudged, so firing early would make it fetch a row that does not
    # exist yet — and would announce an account that a rollback then erased.
    await live_events.publish_vendor_change()
    return _token_pair(user)


async def login_user(session: AsyncSession, email: str, password: str) -> User:
    """Authenticate and return the user itself.

    The SSR admin needs the User, not the API's token pair: it mints its own
    web-session cookie (core/security.create_web_session_token) and has no use
    for a refresh token it cannot store anyway.
    """
    user = await user_service.authenticate(session, email, password)
    if user is None:
        raise UnauthorizedError("Invalid credentials")
    # Stamp last-seen for the staff list's recency column. Committed by the
    # caller: the API login() commits when it also writes app_version; the SSR
    # login route commits its own transaction. A flush here is enough to make
    # the value visible within the request's session either way.
    user.last_login_at = datetime.now(timezone.utc)
    await session.flush()
    return user


async def login(
    session: AsyncSession,
    email: str,
    password: str,
    app_version: str | None = None,
) -> tuple[str, str]:
    """Authenticate and mint tokens, recording which app build asked.

    ``app_version`` is stamped here rather than carried as a JWT claim: a claim
    freezes at mint time and would keep reporting the old build for the whole
    refresh-token lifetime after the customer updated. The header arrives on
    every request, so login is simply the cheapest honest place to persist it.

    Only written when it actually changed — a no-op UPDATE on every login would
    churn ``updated_at`` (TimestampMixin has onupdate=utcnow) and quietly turn
    "when was this account last modified" into "when did they last sign in".
    """
    user = await login_user(session, email, password)
    if app_version and user.app_version != app_version:
        user.app_version = app_version[:32]  # column width; never trust length
    # Always commit: login_user now stamps last_login_at on every sign-in, so
    # there is a pending write to persist even when app_version is unchanged.
    await session.commit()
    return _token_pair(user)


async def refresh(session: AsyncSession, refresh_token: str) -> str:
    payload = decode_token(refresh_token, expected_type=REFRESH_TOKEN_TYPE)
    user = await user_service.get_by_id(session, uuid.UUID(payload["sub"]))
    if user is None or not user.is_active:
        raise UnauthorizedError("Invalid refresh token")
    return create_access_token(str(user.id), str(user.org_id), user.role.value)


async def request_password_reset(session: AsyncSession, email: str) -> None:
    """Email a reset link to ``email`` if it belongs to an active account.

    Silent on a miss: the caller returns the same response whether or not the
    email exists, so nobody can probe which emails are registered. The signed
    JWT (purpose=reset) IS the token — no DB row to store or clean up.

    Fingerprinting the current password hash into the token is what makes it
    single-use: once the password changes, the fingerprint no longer matches
    and the old link stops working (see reset_password).
    """
    user = await user_service.get_by_email(session, email)
    if user is not None and user.is_active:
        token = create_reset_token(str(user.id), password_fingerprint(user.password_hash))
        await send_reset_email(user.email, token)  # never raises — see mailer


async def reset_password(session: AsyncSession, token: str, new_password: str) -> None:
    """Set a new password from a valid reset token, or raise UnauthorizedError.

    Shared by the JSON API and the SSR reset page so the exact same checks run
    on both: token type, account still active, and the password-fingerprint
    match that rejects a token minted for an older (already-changed) password.
    """
    payload = decode_token(token, expected_type=RESET_TOKEN_TYPE)
    try:
        user_id = uuid.UUID(payload["sub"])
    except (KeyError, ValueError) as exc:
        raise UnauthorizedError("Liên kết đặt lại không hợp lệ") from exc

    user = await user_service.get_by_id(session, user_id)
    if user is None or not user.is_active:
        raise UnauthorizedError("Liên kết đặt lại không hợp lệ")
    if payload.get("pwf") != password_fingerprint(user.password_hash):
        raise UnauthorizedError("Liên kết đặt lại đã hết hạn hoặc đã được dùng")

    await user_service.update_user(session, user, password=new_password)
    await session.commit()
