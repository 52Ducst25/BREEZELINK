"""Organization model — the multi-tenant root entity."""

import enum
import uuid

from sqlalchemy import Boolean, Enum, String, Uuid
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base, TimestampMixin


class OrgType(str, enum.Enum):
    household = "household"
    company = "company"


class Organization(Base, TimestampMixin):
    __tablename__ = "organizations"

    id: Mapped[uuid.UUID] = mapped_column(Uuid, primary_key=True, default=uuid.uuid4)
    name: Mapped[str] = mapped_column(String(200), nullable=False)
    type: Mapped[OrgType] = mapped_column(
        Enum(OrgType, name="org_type"), default=OrgType.household, nullable=False
    )
    contact: Mapped[str | None] = mapped_column(String(200), nullable=True)

    # The vendor's own back-office org, where staff accounts live — NOT a
    # customer household. It exists so admins are not "the owner of a
    # customer", and it is excluded from every customer listing
    # (organization_service.list_orgs). A boolean, not a third OrgType value,
    # for the same reason is_sysadmin is a column: extending a Postgres enum
    # needs ALTER TYPE, which cannot run inside an Alembic transaction.
    is_vendor: Mapped[bool] = mapped_column(Boolean, default=False, nullable=False)
