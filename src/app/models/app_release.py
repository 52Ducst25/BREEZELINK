"""AppRelease model — a published build of the customer's Flutter app.

The vendor uploads an APK here and the app polls a public manifest to discover
it (services/release_service, api/ota_routes). Ported from SafeKitchen's
``app_releases``, with one deliberate difference: SafeKitchen requires the APK
to be scp'd onto the host before it can be registered, whereas this uploads
through the browser — the person publishing a release is a vendor admin with a
web login, not necessarily someone with shell access to the VPS.

``version_code`` is the comparison key, not ``version_name``. A monotonic
integer is the only thing two builds can be reliably ordered by; "2.10.0" vs
"2.9.0" compares wrong as a string, and Android itself upgrades on versionCode.
It is UNIQUE so the same build cannot be published twice under two names.

Exactly one row should have ``is_current``. That is enforced in the service
(release_service._clear_current) rather than by a partial unique index,
matching how SafeKitchen does it — and because "no current release" is a legal
state (a fresh install with nothing published yet).
"""

import uuid
from datetime import datetime

from sqlalchemy import BigInteger, Boolean, DateTime, ForeignKey, Integer, String, Text, Uuid
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base, utcnow


class AppRelease(Base):
    """One published APK."""

    __tablename__ = "app_releases"

    id: Mapped[uuid.UUID] = mapped_column(Uuid, primary_key=True, default=uuid.uuid4)

    # Android's own upgrade key. Unique: republishing a code would make
    # "is this newer than what I run" ambiguous.
    version_code: Mapped[int] = mapped_column(Integer, unique=True, index=True, nullable=False)
    version_name: Mapped[str] = mapped_column(String(50), nullable=False)

    # Bare filename inside the APK dir — never a path. api/ota_routes refuses
    # anything containing a separator, so a crafted name cannot walk out of the
    # directory when it is joined back on for the download.
    apk_filename: Mapped[str] = mapped_column(String(255), nullable=False)

    # Bytes, measured server-side after the upload lands. Never taken from the
    # client: it is shown to the customer as "how big is this download" and a
    # client-supplied number could simply be a lie.
    apk_size: Mapped[int] = mapped_column(BigInteger, default=0, nullable=False)

    changelog: Mapped[str | None] = mapped_column(Text, nullable=True)

    # Builds older than this must update before they may be used. Defaults to 1
    # (= nothing is forced) so publishing never accidentally locks users out.
    min_supported_code: Mapped[int] = mapped_column(Integer, default=1, nullable=False)

    is_current: Mapped[bool] = mapped_column(Boolean, default=False, index=True, nullable=False)

    # SET NULL, not CASCADE: deleting the admin who published a build must not
    # erase the build itself — the APK is still installed on customers' phones.
    published_by: Mapped[uuid.UUID | None] = mapped_column(
        Uuid, ForeignKey("users.id", ondelete="SET NULL"), nullable=True
    )

    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, nullable=False
    )
