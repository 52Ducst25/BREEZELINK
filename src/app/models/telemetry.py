"""Telemetry model — periodic sensor readings pushed by outdoor/indoor nodes.

Numeric type decision (design §3.1 key insight): physical sensor values use
``REAL`` (Postgres float4), NOT ``Numeric``/``Decimal``. The DHT22 sensor has
±0.5°C / ±2-5%RH accuracy, so sub-0.1 precision is meaningless — Decimal's
exact-arithmetic guarantees are billing-grade over-engineering here (contrast
with PowerSpy's energy Numeric columns, where exact billing math matters).
"""

import uuid
from datetime import datetime

from sqlalchemy import REAL, BigInteger, DateTime, ForeignKey, Index, SmallInteger, Uuid, text
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base


class Telemetry(Base):
    """One row per periodic reading from a device (outdoor or indoor)."""

    __tablename__ = "telemetry"
    __table_args__ = (
        # Composite DESC index: chart/dashboard queries fetch latest-first per device.
        Index("ix_telemetry_device_ts", "device_id", text("ts DESC")),
    )

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    device_id: Mapped[uuid.UUID] = mapped_column(
        Uuid, ForeignKey("devices.id", ondelete="CASCADE"), nullable=False
    )
    ts: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    temp: Mapped[float] = mapped_column(REAL, nullable=False)
    humidity: Mapped[float] = mapped_column(REAL, nullable=False)
    rssi: Mapped[int] = mapped_column(SmallInteger, nullable=False)
    # Battery voltage — outdoor (battery-powered) node only; NULL for indoor.
    batt: Mapped[float | None] = mapped_column(REAL, nullable=True)
    # Instantaneous AC power draw in WATTS, from an in-line power sensor
    # (PZEM/ACS712-class) on the node. NULL until such hardware is fitted and
    # the firmware reports it — energy_service integrates these samples into kWh.
    watt: Mapped[float | None] = mapped_column(REAL, nullable=True)
