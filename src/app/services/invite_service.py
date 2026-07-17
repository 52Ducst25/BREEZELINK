"""Activation-code issuing + redemption.

Codes are what let the vendor keep registration closed: the admin creates the
customer org, hands over a code, and only that code opens the door to that org
(see models/invite_code.py for the data shape).

Every function here ``flush``es but never ``commit``s — the caller owns the
transaction, matching organization_service/user_service. Redemption in
particular MUST stay in the same transaction as user creation, or a crash
between the two would spend a code without producing a user.
"""

import secrets
import uuid
from datetime import datetime, timezone

from sqlalchemy import func, select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.exceptions import ConflictError, NotFoundError
from app.models.invite_code import InviteCode
from app.models.user import User, UserRole

# 8 digits, no letters: the code is read aloud over the phone and typed on a
# phone keypad by a customer. Letters invite O/0 and I/1 mistakes, and a
# case-sensitive code would be worse. 10^8 with a uniqueness retry is ample —
# these are short-lived and scoped to one org, not a secret at rest.
_CODE_LENGTH = 8
_MAX_ATTEMPTS = 5


def _generate() -> str:
    # secrets, not random: a guessable code is a free account inside someone
    # else's household.
    return "".join(secrets.choice("0123456789") for _ in range(_CODE_LENGTH))


async def create_codes(
    session: AsyncSession,
    *,
    org_id: uuid.UUID,
    count: int = 1,
    role: UserRole = UserRole.owner,
    device_id: uuid.UUID | None = None,
) -> list[InviteCode]:
    """Issue ``count`` fresh codes for ``org_id``. Caller commits."""
    created: list[InviteCode] = []
    for _ in range(count):
        # Retry on collision rather than pre-checking: the unique index is the
        # only real arbiter under concurrency, so let it decide.
        for _attempt in range(_MAX_ATTEMPTS):
            code = InviteCode(
                code=_generate(), org_id=org_id, role=role, device_id=device_id
            )
            session.add(code)
            try:
                await session.flush()
            except IntegrityError:
                await session.rollback()
                continue
            created.append(code)
            break
        else:
            raise ConflictError("Could not generate a unique code, please retry")
    return created


async def list_for_org(session: AsyncSession, org_id: uuid.UUID) -> list[InviteCode]:
    rows = await session.execute(
        select(InviteCode)
        .where(InviteCode.org_id == org_id)
        .order_by(InviteCode.created_at.desc())
    )
    return list(rows.scalars().all())


async def delete_code(
    session: AsyncSession, org_id: uuid.UUID, code_id: uuid.UUID
) -> None:
    """Delete ONE unused code of a customer. Caller commits.

    Two guards, both intentional:
      * org match — the code must belong to the org in the URL, so a crafted
        id from another customer resolves to a 404, not a cross-tenant delete.
      * unused only — a spent code is the record that an account was activated
        with it. Deleting it would erase that history for no gain (a used code
        can never be redeemed again anyway), so it is refused. Only the extra
        unused codes the vendor over-generated are removable.
    """
    code = await session.get(InviteCode, code_id)
    if code is None or code.org_id != org_id:
        raise NotFoundError("Không tìm thấy mã")
    if code.used_by_user_id is not None:
        raise ConflictError("Mã đã được dùng, không thể xoá")
    await session.delete(code)
    await session.flush()


async def get_unused(session: AsyncSession, code: str) -> InviteCode:
    """Resolve a redeemable code and LOCK it, or raise.

    Deliberately one lookup for both "no such code" and "already used" so a
    caller probing the endpoint cannot tell a wrong code from a spent one.

    ``with_for_update`` is what actually enforces "one code = one account".
    Nothing in the schema does: ``used_by_user_id`` has no unique constraint,
    so two people redeeming the same code at once would both read it as unused
    and both get an account. The row lock makes the second one wait for the
    first to commit, after which its ``used_by_user_id IS NULL`` no longer
    matches and it correctly sees a spent code. The lock is released by the
    caller's commit — the same transaction that creates the user.
    """
    row = await session.execute(
        select(InviteCode)
        .where(InviteCode.code == code.strip(), InviteCode.used_by_user_id.is_(None))
        .with_for_update()
    )
    invite = row.scalar_one_or_none()
    if invite is None:
        raise NotFoundError("Mã kích hoạt không hợp lệ hoặc đã được sử dụng")
    return invite


async def mark_used(session: AsyncSession, invite: InviteCode, user: User) -> None:
    """Spend ``invite`` on ``user``. Caller commits — same tx as user creation."""
    invite.used_by_user_id = user.id
    invite.used_at = datetime.now(timezone.utc)
    await session.flush()


async def counts_by_org(session: AsyncSession) -> dict[uuid.UUID, tuple[int, int]]:
    """``{org_id: (total_codes, unused_codes)}`` for the customers table.

    One grouped query instead of per-row lookups — the table renders every
    customer at once, so a per-row count would be N+1 against the DB.
    """
    rows = await session.execute(
        select(
            InviteCode.org_id,
            func.count(InviteCode.id),
            func.count(InviteCode.id).filter(InviteCode.used_by_user_id.is_(None)),
        ).group_by(InviteCode.org_id)
    )
    return {org_id: (total, unused) for org_id, total, unused in rows.all()}
