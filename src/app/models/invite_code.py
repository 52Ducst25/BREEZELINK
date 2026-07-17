"""InviteCode model — the activation code an admin hands to a customer.

Registration is closed: ``auth_service.register`` will not create an
organization on demand any more. The vendor sells a unit, creates the customer
org here, and issues one code per phone that should reach it. The customer
types the code in the mobile app and lands *inside the org that already
exists*, so the vendor keeps the customer list authoritative instead of
discovering orgs after the fact.

One code = one successful registration. ``used_by_user_id`` is the record of
that: it is NULL while the code is outstanding and set (together with
``used_at``) the moment it is redeemed, in the same transaction that creates
the user. A customer with two phones therefore needs two codes — hence the
"+" action on the customers page, mirroring how SafeKitchen issues extra codes
per sold device.

``role`` rides on the code rather than being chosen at redemption time: the
person typing it must not get to decide whether they become owner or member.
"""

import uuid
from datetime import datetime

from sqlalchemy import DateTime, Enum, ForeignKey, String, Uuid
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base, utcnow
from app.models.user import UserRole


class InviteCode(Base):
    """A single-use activation code bound to one organization."""

    __tablename__ = "invite_codes"

    id: Mapped[uuid.UUID] = mapped_column(Uuid, primary_key=True, default=uuid.uuid4)

    # Unique across the whole table, not per-org: redemption happens before we
    # know which org the caller belongs to (that is what the code is FOR), so
    # the code alone has to resolve to exactly one org.
    code: Mapped[str] = mapped_column(String(16), unique=True, index=True, nullable=False)

    org_id: Mapped[uuid.UUID] = mapped_column(
        Uuid, ForeignKey("organizations.id", ondelete="CASCADE"), index=True, nullable=False
    )

    # The unit this code was sold with. Nullable because a sale can be recorded
    # before its nodes are provisioned (the vendor creates the customer, the
    # installer commissions the hardware later) — the code still works, it just
    # has no unit attached yet. org_id, not this, decides what the redeemer can
    # see: a household's second node must not be invisible to them because the
    # code named the first one. This is provenance ("which unit did we hand
    # this to"), not an access rule.
    device_id: Mapped[uuid.UUID | None] = mapped_column(
        Uuid, ForeignKey("devices.id", ondelete="SET NULL"), index=True, nullable=True
    )

    role: Mapped[UserRole] = mapped_column(
        Enum(UserRole, name="user_role"), default=UserRole.owner, nullable=False
    )

    # SET NULL, not CASCADE: deleting a user must not silently delete the
    # evidence that a code was already spent, or the code would look fresh
    # again and let a second person register on it.
    used_by_user_id: Mapped[uuid.UUID | None] = mapped_column(
        Uuid, ForeignKey("users.id", ondelete="SET NULL"), nullable=True
    )
    used_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)

    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, nullable=False
    )
