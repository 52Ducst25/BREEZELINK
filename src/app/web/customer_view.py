"""Row assembly for the vendor's customer list.

Kept out of the route for the same reason dashboard_view exists: the handler
should read as "fetch, render", not as a pile of aggregate SQL.

Every count here is ONE grouped query over the whole table, joined to the orgs
in Python. The obvious alternative — asking per row inside the template loop —
is N+1: one customer costs 4 queries and looks fine, fifty customers cost 200
and the page crawls. The tables are small enough that grouping in the DB and
zipping in memory is both the simplest and the fastest option.
"""

import uuid
from dataclasses import dataclass

from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.device import Device
from app.models.invite_code import InviteCode
from app.models.organization import Organization
from app.models.user import User, UserRole
from app.services import device_presence, organization_service


@dataclass
class CustomerRow:
    """One line of the customers table."""

    org: Organization
    owner: User | None  # None until someone redeems a code
    node_count: int
    online_count: int
    code_total: int
    code_unused: int

    @property
    def registered(self) -> bool:
        """Has anyone actually activated an account for this customer yet?"""
        return self.owner is not None


async def _device_counts(session: AsyncSession) -> dict[uuid.UUID, tuple[int, int]]:
    rows = await session.execute(
        select(
            Device.org_id,
            func.count(Device.id),
            # Cùng luật với fleet_view (device_presence.is_online), viết bằng SQL.
            # Đếm theo cờ status thôi là sai: gateway chết thì cờ của các slave
            # đóng băng ở "online" mãi — xem services/device_presence.py.
            func.count(Device.id).filter(device_presence.online_filter()),
        ).group_by(Device.org_id)
    )
    return {org_id: (total, online) for org_id, total, online in rows.all()}


async def _code_counts(session: AsyncSession) -> dict[uuid.UUID, tuple[int, int]]:
    rows = await session.execute(
        select(
            InviteCode.org_id,
            func.count(InviteCode.id),
            func.count(InviteCode.id).filter(InviteCode.used_by_user_id.is_(None)),
        ).group_by(InviteCode.org_id)
    )
    return {org_id: (total, unused) for org_id, total, unused in rows.all()}


async def _owner_by_org(session: AsyncSession) -> dict[uuid.UUID, User]:
    """First owner per org, falling back to the earliest member.

    One query for every user rather than a correlated subquery per org: the
    "who is this customer" column needs a real row, not a count, and user
    tables here are tiny compared to telemetry.
    """
    users = (
        (await session.execute(select(User).order_by(User.created_at.asc())))
        .scalars()
        .all()
    )
    picked: dict[uuid.UUID, User] = {}
    for user in users:
        current = picked.get(user.org_id)
        if current is None or (
            current.role != UserRole.owner and user.role == UserRole.owner
        ):
            picked[user.org_id] = user
    return picked


def _matches(row: "CustomerRow", needle: str) -> bool:
    """Does this customer match the search box?

    Phone first — that is what staff have in hand when a customer calls — but
    a phone number lives in two places for the same person: ``org.contact``
    (typed at the sale, before any account exists) and ``owner.phone`` (typed
    by the customer when they redeemed the code). Searching only one of them
    silently loses whole customers, so both are matched.

    Digits are compared with separators stripped, because "0901 234 567",
    "0901-234-567" and "0901234567" are one number and nobody remembers which
    form was typed. Name is matched too, as a free bonus of the same pass.
    """
    fields = [row.org.name or "", row.org.contact or ""]
    if row.owner is not None:
        fields += [row.owner.phone or "", row.owner.email or "", row.owner.full_name or ""]

    lowered = needle.lower()
    if any(lowered in f.lower() for f in fields):
        return True

    digits = "".join(ch for ch in needle if ch.isdigit())
    if not digits:
        return False
    return any(digits in "".join(ch for ch in f if ch.isdigit()) for f in fields if f)


async def build_rows(session: AsyncSession, query: str | None = None) -> list[CustomerRow]:
    """Customer rows, optionally filtered by ``query`` (phone/name/email).

    Filtering happens after assembly rather than in SQL: the fields a phone
    search must cover live in two different tables (organizations.contact and
    users.phone), and the normalisation above — strip separators, compare
    digits — has no clean SQL equivalent without a functional index this
    project has no reason to carry yet. Customer counts are in the hundreds,
    not millions.
    """
    orgs = await organization_service.list_orgs(session)
    devices = await _device_counts(session)
    codes = await _code_counts(session)
    owners = await _owner_by_org(session)

    rows: list[CustomerRow] = []
    for org in orgs:
        node_count, online_count = devices.get(org.id, (0, 0))
        code_total, code_unused = codes.get(org.id, (0, 0))
        rows.append(
            CustomerRow(
                org=org,
                owner=owners.get(org.id),
                node_count=node_count,
                online_count=online_count,
                code_total=code_total,
                code_unused=code_unused,
            )
        )

    needle = (query or "").strip()
    if not needle:
        return rows
    return [r for r in rows if _matches(r, needle)]
