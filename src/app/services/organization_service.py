"""Organization business logic."""

import uuid

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.exceptions import NotFoundError
from app.models.organization import Organization, OrgType

# The label a product carries between "sold" and "activated". The vendor no
# longer types a customer name at sale time — a product is just a code plus its
# nodes — so it is named after its code until a customer registers and their
# real name replaces it (auth_service.register). Detecting that placeholder is
# how register knows the org is still unclaimed and safe to rename.
PRODUCT_LABEL_PREFIX = "Sản phẩm "


def product_label(code: str) -> str:
    return f"{PRODUCT_LABEL_PREFIX}{code}"


def is_unclaimed(org: Organization) -> bool:
    """True while the org still wears its auto-generated product label, i.e.
    nobody has registered into it and no staff has manually renamed it."""
    return org.name.startswith(PRODUCT_LABEL_PREFIX)


async def create_org(
    session: AsyncSession,
    *,
    name: str,
    org_type: OrgType,
    contact: str | None = None,
    is_vendor: bool = False,
) -> Organization:
    org = Organization(name=name, type=org_type, contact=contact, is_vendor=is_vendor)
    session.add(org)
    await session.flush()  # assign id without committing (caller controls tx)
    return org


async def get_org(session: AsyncSession, org_id: uuid.UUID) -> Organization:
    org = await session.get(Organization, org_id)
    if org is None:
        raise NotFoundError("Organization not found")
    return org


async def list_orgs(session: AsyncSession) -> list[Organization]:
    """Every CUSTOMER organization, newest first — the vendor's customer list.

    Excludes the vendor's own back-office org: staff are not customers and must
    not show up among the households they serve. Intentionally not otherwise
    org-scoped — this is the one cross-tenant read in the app, and
    web/dependencies.require_sysadmin is what keeps it that way. Never call it
    from a customer-facing path.
    """
    stmt = (
        select(Organization)
        .where(Organization.is_vendor.is_(False))
        .order_by(Organization.created_at.desc())
    )
    return list((await session.execute(stmt)).scalars().all())


async def update_org(
    session: AsyncSession,
    org: Organization,
    *,
    name: str | None = None,
    contact: str | None = None,
) -> Organization:
    """Edit customer-facing org fields. Caller commits."""
    if name is not None:
        org.name = name
    if contact is not None:
        org.contact = contact or None
    await session.flush()
    return org
