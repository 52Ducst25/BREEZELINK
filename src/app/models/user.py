"""User model. Email is unique per organization (not globally)."""

import enum
import uuid
from datetime import datetime

from sqlalchemy import (
    Boolean,
    DateTime,
    Enum,
    ForeignKey,
    LargeBinary,
    String,
    UniqueConstraint,
    Uuid,
)
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base, TimestampMixin


class UserRole(str, enum.Enum):
    owner = "owner"
    member = "member"


class User(Base, TimestampMixin):
    __tablename__ = "users"
    __table_args__ = (UniqueConstraint("org_id", "email", name="uq_user_org_email"),)

    id: Mapped[uuid.UUID] = mapped_column(Uuid, primary_key=True, default=uuid.uuid4)
    org_id: Mapped[uuid.UUID] = mapped_column(
        Uuid, ForeignKey("organizations.id", ondelete="CASCADE"), index=True, nullable=False
    )
    email: Mapped[str] = mapped_column(String(255), nullable=False)
    full_name: Mapped[str | None] = mapped_column(String(200), nullable=True)
    phone: Mapped[str | None] = mapped_column(String(20), nullable=True)
    # Household address — one location per household; its devices share it.
    location: Mapped[str | None] = mapped_column(String(200), nullable=True)
    password_hash: Mapped[str] = mapped_column(String(255), nullable=False)
    role: Mapped[UserRole] = mapped_column(
        Enum(UserRole, name="user_role"), default=UserRole.member, nullable=False
    )
    is_active: Mapped[bool] = mapped_column(Boolean, default=True, nullable=False)

    # Vendor staff, not a customer. `role` is authority WITHIN one org (owner vs
    # member of a household); this is the only thing that reaches ACROSS orgs, so
    # it gates the customer list and code issuing. It is a column rather than a
    # third UserRole value because Postgres needs ALTER TYPE ... ADD VALUE to
    # extend an enum, which cannot run inside Alembic's transaction — and because
    # "which household am I owner of" and "am I the vendor" are genuinely two
    # different questions that would otherwise share one field.
    #
    # Never settable from any registration path: it is granted by hand in the DB.
    # An activation code carries `role`, so a code that could also carry this
    # would let a customer mint themselves vendor access.
    is_sysadmin: Mapped[bool] = mapped_column(Boolean, default=False, nullable=False)

    # Which build of the Flutter app this person last signed in from, e.g.
    # "1.0.0+1". Reported by the app on every request via the X-App-Version
    # header and stamped here at login (api/v1/auth_routes.login).
    #
    # A plain String, not a JWT claim: a claim freezes at mint time and would
    # keep reporting the old build for the whole refresh-token lifetime after
    # the user updated. Nullable because the SSR admin and any pre-update build
    # send no header at all — "unknown" is a real answer, not zero.
    app_version: Mapped[str | None] = mapped_column(String(32), nullable=True)

    # When this account last authenticated. Stamped in auth_service.login_user
    # (the one path both the API and the SSR admin go through). Drives the staff
    # list's "đăng nhập lần cuối / cách đây bao lâu". Nullable = never signed in.
    #
    # This is NOT "online now": there is no session heartbeat for a person the
    # way there is for a device. It is a recency proxy — "last seen", not a live
    # presence light — which is the honest thing to show for an admin account.
    last_login_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )

    # Last authenticated request (any page), throttled to ~once a minute in
    # web/dependencies.get_current_user_or_none. This is what powers a real
    # "Online" status for staff: since the SSR admin re-fetches its page every
    # few seconds while open, an admin who has the panel open keeps this fresh,
    # and "online" = seen within the last couple of minutes. Distinct from
    # last_login_at (a one-shot sign-in stamp that never moves while browsing).
    last_seen_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )

    # Profile picture shown in the admin's top bar. Stored in-row rather than on
    # a volume: it is a handful of small images (vendor staff only), and a column
    # survives a redeploy that would wipe an unmounted directory — exactly the
    # trap the APK volume exists to avoid (models/app_release).
    #
    # DEFERRED on purpose: web/dependencies resolves the session user on EVERY
    # authenticated request, and without this the blob would be read from
    # Postgres on every page load just to render a 36px circle. Only the
    # avatar-serving route touches the bytes.
    avatar: Mapped[bytes | None] = mapped_column(LargeBinary, nullable=True, deferred=True)
    avatar_mime: Mapped[str | None] = mapped_column(String(64), nullable=True)

    # Non-NULL exactly when an avatar is set. Templates test THIS rather than
    # ``avatar`` (touching the deferred blob would trigger a lazy load per page),
    # and its timestamp doubles as the cache-buster in the <img> URL so a newly
    # uploaded picture appears immediately instead of showing the cached old one.
    avatar_updated_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
