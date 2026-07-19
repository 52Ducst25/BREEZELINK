"""IrActionCode — a learned remote button that is NOT part of the (mode, temp)
comfort matrix, but a standalone action the app can replay on demand.

Today the only action is ``FAN_SPEED``: a single learned IR frame for the
remote's "fan speed" button. Each time the app sends it, the node blasts that
frame and the physical AC advances its own fan speed (Auto/Low/Med/High…). The
cloud cannot read the AC's resulting fan state — this is an OPEN-LOOP "press the
remote's fan button once" primitive.

Kept in its own table (not ``ir_codes``) on purpose: it must never collide with
the ``UNIQUE(org, mode, temp)`` snap key, must never satisfy the auto-control
coverage check, and its id must never land in ``commands.ir_code_id`` (whose FK
targets ``ir_codes``). Actuation still rides the existing generic IR-blast path
(``ir_raw`` in the cmd payload), so no firmware protocol change is needed to
*send* it — only the LEARN capture of a new labelled button.
"""

import uuid
from datetime import datetime

from sqlalchemy import (
    ARRAY,
    DateTime,
    ForeignKey,
    Integer,
    String,
    UniqueConstraint,
    Uuid,
)
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base, utcnow


class IrActionCode(Base):
    """One learned standalone action IR frame for an org (e.g. FAN_SPEED)."""

    __tablename__ = "ir_action_codes"
    __table_args__ = (
        UniqueConstraint("org_id", "action", name="uq_iraction_snap"),
    )

    id: Mapped[uuid.UUID] = mapped_column(Uuid, primary_key=True, default=uuid.uuid4)
    org_id: Mapped[uuid.UUID] = mapped_column(
        Uuid, ForeignKey("organizations.id", ondelete="CASCADE"), index=True, nullable=False
    )
    action: Mapped[str] = mapped_column(String(40), nullable=False)
    raw_timing: Mapped[list[int]] = mapped_column(ARRAY(Integer), nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, nullable=False
    )
