"""Command model — record of every AC command dispatched to an indoor node.

Written by the comfort worker (source='auto') or the mobile app manual-override
path (source='manual'). ``req_id`` correlates with the MQTT command payload and
the node's ``state`` ack; ``ack_ts`` stays NULL until the node confirms receipt.
"""

import uuid
from datetime import datetime

from sqlalchemy import DateTime, Enum, ForeignKey, SmallInteger, String, Uuid
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base
from app.models.enums import AcMode, CommandSource


class Command(Base):
    """One dispatched (or attempted) AC command, auto or manual."""

    __tablename__ = "commands"

    id: Mapped[uuid.UUID] = mapped_column(Uuid, primary_key=True, default=uuid.uuid4)
    device_id: Mapped[uuid.UUID] = mapped_column(
        Uuid, ForeignKey("devices.id", ondelete="CASCADE"), index=True, nullable=False
    )
    ts: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    mode: Mapped[AcMode] = mapped_column(
        Enum(AcMode, name="ac_mode", create_type=False), nullable=False
    )
    setpoint: Mapped[int] = mapped_column(SmallInteger, nullable=False)
    # NULL when no learned IR code matches yet (e.g. gap in the captured table).
    ir_code_id: Mapped[uuid.UUID | None] = mapped_column(
        Uuid, ForeignKey("ir_codes.id", ondelete="SET NULL"), nullable=True
    )
    source: Mapped[CommandSource] = mapped_column(
        Enum(CommandSource, name="command_source"), nullable=False
    )
    req_id: Mapped[str] = mapped_column(String(64), nullable=False)
    ack_ts: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
