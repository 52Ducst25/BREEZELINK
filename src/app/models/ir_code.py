"""IrCode model — learned/known AC remote codes, snap-keyed by (org, mode, temp).

Dual storage supports two capture styles without a future migration:
- ``protocol`` + ``state_bytes``: known-brand IR library path (e.g. IRremoteESP8266
  state protocols) once the AC brand is identified.
- ``raw_timing``: current raw-capture LEARN flow (app "Học nút này" — ESP32
  ``irrecv.decode()`` records a mark/space timing array, no brand knowledge needed).
Both columns are nullable; exactly one is populated per row.
"""

import uuid
from datetime import datetime

from sqlalchemy import (
    ARRAY,
    DateTime,
    Enum,
    ForeignKey,
    Integer,
    LargeBinary,
    SmallInteger,
    String,
    UniqueConstraint,
    Uuid,
)
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base, utcnow
from app.models.enums import AcMode


class IrCode(Base):
    """A single learned/known remote code for one (mode, temp) combination."""

    __tablename__ = "ir_codes"
    __table_args__ = (
        # Snap key: the comfort algorithm looks up the nearest captured temp
        # for a given (org, mode) via this uniqueness guarantee.
        UniqueConstraint("org_id", "mode", "temp", name="uq_ircode_snap"),
    )

    id: Mapped[uuid.UUID] = mapped_column(Uuid, primary_key=True, default=uuid.uuid4)
    org_id: Mapped[uuid.UUID] = mapped_column(
        Uuid, ForeignKey("organizations.id", ondelete="CASCADE"), index=True, nullable=False
    )
    brand: Mapped[str | None] = mapped_column(String(100), nullable=True)
    button_label: Mapped[str] = mapped_column(String(100), nullable=False)
    mode: Mapped[AcMode] = mapped_column(Enum(AcMode, name="ac_mode"), nullable=False)
    temp: Mapped[int] = mapped_column(SmallInteger, nullable=False)
    protocol: Mapped[str | None] = mapped_column(String(50), nullable=True)
    state_bytes: Mapped[bytes | None] = mapped_column(LargeBinary, nullable=True)
    raw_timing: Mapped[list[int] | None] = mapped_column(ARRAY(Integer), nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, nullable=False
    )
