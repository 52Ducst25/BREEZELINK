"""Auth orchestration: register (org+owner tx), login, refresh."""

import uuid

from sqlalchemy.ext.asyncio import AsyncSession

from app.core.exceptions import ConflictError, UnauthorizedError
from app.core.security import (
    REFRESH_TOKEN_TYPE,
    create_access_token,
    create_refresh_token,
    decode_token,
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
        await session.commit()
    return _token_pair(user)


async def refresh(session: AsyncSession, refresh_token: str) -> str:
    payload = decode_token(refresh_token, expected_type=REFRESH_TOKEN_TYPE)
    user = await user_service.get_by_id(session, uuid.UUID(payload["sub"]))
    if user is None or not user.is_active:
        raise UnauthorizedError("Invalid refresh token")
    return create_access_token(str(user.id), str(user.org_id), user.role.value)
