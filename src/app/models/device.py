"""Device model. A device IS an MQTT identity:
``device_uuid`` = MQTT username, ``mqtt_token`` = MQTT password.

mqtt_token is stored PLAINTEXT — EMQX built-in DB auth + the seed script need a
readable secret. Documented tradeoff: hash-on-upgrade (store only a hash and
have EMQX call an HTTP auth hook) when moving beyond the scaffold.

Phase 0: copied verbatim from the reference skeleton. ``node_type`` (outdoor/indoor)
is added in Phase 1 (database layer) below.
"""

import enum
import uuid
from datetime import datetime
from decimal import Decimal

from sqlalchemy import BigInteger, DateTime, Enum, ForeignKey, Numeric, String, Uuid
from sqlalchemy.orm import Mapped, mapped_column

from app.models.base import Base, TimestampMixin
from app.models.enums import NodeRole, NodeType


class DeviceStatus(str, enum.Enum):
    online = "online"
    offline = "offline"


class Device(Base, TimestampMixin):
    __tablename__ = "devices"

    id: Mapped[uuid.UUID] = mapped_column(Uuid, primary_key=True, default=uuid.uuid4)
    org_id: Mapped[uuid.UUID] = mapped_column(
        Uuid, ForeignKey("organizations.id", ondelete="CASCADE"), index=True, nullable=False
    )
    name: Mapped[str] = mapped_column(String(200), nullable=False)
    # Comfort-control role — which physical node this row represents (Phase 1).
    node_type: Mapped[NodeType] = mapped_column(Enum(NodeType, name="node_type"), nullable=False)
    # Network role — master (WiFi+MQTT to cloud) vs slave (ESP-NOW to the master).
    # Independent of node_type; assigned by the vendor during provisioning, so
    # nullable until then. device_service.set_role keeps at most one master/org.
    role: Mapped[NodeRole | None] = mapped_column(
        Enum(NodeRole, name="node_role"), nullable=True
    )
    location: Mapped[str | None] = mapped_column(String(200), nullable=True)
    # Optional geo coordinates for the map (WGS84 lat/lng).
    latitude: Mapped[Decimal | None] = mapped_column(Numeric(9, 6), nullable=True)
    longitude: Mapped[Decimal | None] = mapped_column(Numeric(9, 6), nullable=True)

    # MQTT credentials (device_uuid = username, mqtt_token = password).
    device_uuid: Mapped[str] = mapped_column(String(64), unique=True, index=True, nullable=False)
    mqtt_token: Mapped[str] = mapped_column(String(128), nullable=False)  # plaintext (see module doc)

    # Physical ESP32 identity: the 48-bit STA MAC as a natural number (BIGINT).
    # Self-reported by the firmware in each reading; identification only (NOT auth).
    # Display back as hex AA:BB:CC:DD:EE:FF when needed. Null until first reading.
    mac: Mapped[int | None] = mapped_column(BigInteger, nullable=True, index=True)

    status: Mapped[DeviceStatus] = mapped_column(
        Enum(DeviceStatus, name="device_status"),
        default=DeviceStatus.offline,
        nullable=False,
    )
    last_seen_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
