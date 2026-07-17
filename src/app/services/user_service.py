"""User business logic: creation, lookup, authentication."""

import uuid

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.security import hash_password, verify_password
from app.models.user import User, UserRole


async def create_user(
    session: AsyncSession,
    *,
    org_id: uuid.UUID,
    email: str,
    password: str,
    role: UserRole,
    full_name: str | None = None,
    phone: str | None = None,
    location: str | None = None,
    is_sysadmin: bool = False,
) -> User:
    """Create a user. ``is_sysadmin`` defaults to False and must be passed
    explicitly — the registration path (auth_service.register) never does, so
    redeeming a code can never mint vendor staff.
    """
    user = User(
        org_id=org_id,
        email=email.lower(),
        full_name=full_name,
        phone=phone,
        location=location,
        password_hash=hash_password(password),
        role=role,
        is_sysadmin=is_sysadmin,
    )
    session.add(user)
    await session.flush()
    return user


async def list_sysadmins(session: AsyncSession) -> list[User]:
    """Vendor staff, across every org.

    Not org-scoped: staff accounts were seeded by hand and may sit in whatever
    org they were created in, so filtering by the caller's org would hide
    colleagues from each other. Only reachable behind require_sysadmin.
    """
    stmt = select(User).where(User.is_sysadmin.is_(True)).order_by(User.created_at.asc())
    return list((await session.execute(stmt)).scalars().all())


async def update_user(
    session: AsyncSession,
    user: User,
    *,
    email: str | None = None,
    full_name: str | None = None,
    phone: str | None = None,
    location: str | None = None,
    password: str | None = None,
    role: UserRole | None = None,
    is_active: bool | None = None,
) -> User:
    """Apply provided fields. Caller must have checked email uniqueness."""
    if email is not None:
        user.email = email.lower()
    if full_name is not None:
        user.full_name = full_name
    if phone is not None:
        user.phone = phone
    if location is not None:
        user.location = location
    if password is not None:
        user.password_hash = hash_password(password)
    if role is not None:
        user.role = role
    if is_active is not None:
        user.is_active = is_active
    await session.flush()
    return user


async def delete_user(session: AsyncSession, user: User) -> None:
    await session.delete(user)
    await session.flush()


async def get_by_email(session: AsyncSession, email: str) -> User | None:
    # Email is unique per-org, not globally. For stateless login (email only),
    # return the earliest-created match. Multi-org same-email login is a
    # documented scaffold limitation (add org selector on upgrade).
    stmt = (
        select(User)
        .where(User.email == email.lower())
        .order_by(User.created_at.asc())
        .limit(1)
    )
    result = await session.execute(stmt)
    return result.scalars().first()


async def get_by_id(session: AsyncSession, user_id: uuid.UUID) -> User | None:
    return await session.get(User, user_id)


async def get_by_org_email(
    session: AsyncSession, org_id: uuid.UUID, email: str
) -> User | None:
    stmt = select(User).where(User.org_id == org_id, User.email == email.lower())
    result = await session.execute(stmt)
    return result.scalar_one_or_none()


async def list_by_org(session: AsyncSession, org_id: uuid.UUID) -> list[User]:
    stmt = select(User).where(User.org_id == org_id).order_by(User.created_at.asc())
    result = await session.execute(stmt)
    return list(result.scalars().all())


async def authenticate(session: AsyncSession, email: str, password: str) -> User | None:
    """Return the user if credentials are valid and the account is active."""
    user = await get_by_email(session, email)
    if user is None or not user.is_active:
        return None
    if not verify_password(password, user.password_hash):
        return None
    return user
