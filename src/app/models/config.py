"""Config model — one row per org: admin-tunable runtime constants for the
adaptive comfort algorithm (design §1.2/§1.3). Mirrored into Redis
(``bl:{org}:cfg``) and invalidated on update (Phase 2); consumed by the
comfort worker (Phase 3) instead of hardcoded constants.
"""

import uuid
from datetime import datetime

from sqlalchemy import REAL, DateTime, ForeignKey, Integer, SmallInteger, Uuid
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base, utcnow


class Config(Base):
    """Single-row-per-org table of tunable comfort-algorithm constants."""

    __tablename__ = "configs"

    org_id: Mapped[uuid.UUID] = mapped_column(
        Uuid, ForeignKey("organizations.id", ondelete="CASCADE"), primary_key=True
    )
    ema_alpha: Mapped[float] = mapped_column(REAL, nullable=False)  # T_rm EMA weight
    deadband: Mapped[float] = mapped_column(REAL, nullable=False)  # °C hysteresis band
    dwell_sec: Mapped[int] = mapped_column(Integer, nullable=False)  # min seconds between mode switches
    dry_rh: Mapped[float] = mapped_column(REAL, nullable=False)  # %RH threshold to trigger DRY
    humid_slope: Mapped[float] = mapped_column(REAL, nullable=False)  # °C penalty per %RH over 60
    clamp_min: Mapped[float] = mapped_column(REAL, nullable=False)  # safety clamp lower bound
    clamp_max: Mapped[float] = mapped_column(REAL, nullable=False)  # safety clamp upper bound
    override_hours: Mapped[int] = mapped_column(Integer, nullable=False)  # manual-override TTL
    night_start: Mapped[int] = mapped_column(SmallInteger, nullable=False)  # hour 0-23
    night_end: Mapped[int] = mapped_column(SmallInteger, nullable=False)  # hour 0-23
    night_offset: Mapped[float] = mapped_column(REAL, nullable=False)  # °C added during night window
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, onupdate=utcnow, nullable=False
    )
