"""ComfortLog model — audit trail of every adaptive comfort-algorithm decision.

One row per computation cycle. Proves the algorithm's reasoning (running-mean
outdoor temp, adaptive neutral point, humidity penalty) for charts and demo
defense (design §1.2). All physical values are REAL — see telemetry.py for the
REAL-vs-Decimal rationale (same applies here: sensor-derived, not billing data).
"""

import uuid
from datetime import datetime

from sqlalchemy import REAL, DateTime, Enum, ForeignKey, SmallInteger, Uuid
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base
from app.models.enums import AcMode


class ComfortLog(Base):
    """One adaptive-setpoint decision: inputs, intermediate values, outcome."""

    __tablename__ = "comfort_log"

    id: Mapped[uuid.UUID] = mapped_column(Uuid, primary_key=True, default=uuid.uuid4)
    org_id: Mapped[uuid.UUID] = mapped_column(
        Uuid, ForeignKey("organizations.id", ondelete="CASCADE"), index=True, nullable=False
    )
    ts: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    t_out: Mapped[float] = mapped_column(REAL, nullable=False)
    # M1 fix: nullable — no defensible fallback exists when outdoor humidity
    # is missing (unlike t_out's EMA fallback); write None rather than
    # mislabeling indoor RH as an outdoor reading in the audit trail.
    h_out: Mapped[float | None] = mapped_column(REAL, nullable=True)
    t_in: Mapped[float] = mapped_column(REAL, nullable=False)
    h_in: Mapped[float] = mapped_column(REAL, nullable=False)
    t_rm: Mapped[float] = mapped_column(REAL, nullable=False)  # EMA running-mean Tout
    t_neutral: Mapped[float] = mapped_column(REAL, nullable=False)  # adaptive neutral point
    humid_penalty: Mapped[float] = mapped_column(REAL, nullable=False)
    t_target: Mapped[float] = mapped_column(REAL, nullable=False)  # pre-snap continuous target
    # Snapped to the nearest learned IR-code temp (integer degrees, matches ir_codes.temp).
    t_set: Mapped[int] = mapped_column(SmallInteger, nullable=False)
    mode: Mapped[AcMode] = mapped_column(
        Enum(AcMode, name="ac_mode", create_type=False), nullable=False
    )
