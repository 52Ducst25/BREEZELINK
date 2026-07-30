"""Shared domain enums for BreezeLink comfort-control tables.

Kept as plain ``str`` Enums (not native Python IntEnum) so Pydantic and
SQLAlchemy both serialize the readable string value directly.
"""

import enum


class NodeType(str, enum.Enum):
    """Physical ESP32 node role — outdoor sensor vs indoor sensor+IR blaster."""

    outdoor = "outdoor"
    indoor = "indoor"


class NodeRole(str, enum.Enum):
    """Network role, INDEPENDENT of node_type. The ``master`` node is the one
    that connects to the gateway/cloud (WiFi + MQTT); ``slave`` nodes connect to
    the master locally over ESP-NOW and never talk to the cloud directly. Exactly
    one master per household (enforced in device_service.set_role). Nullable on a
    device until the vendor assigns it during provisioning.
    """

    master = "master"
    slave = "slave"


class CommandSource(str, enum.Enum):
    """Who triggered an AC command — the adaptive algorithm or a user tap."""

    auto = "auto"
    manual = "manual"


class AcMode(str, enum.Enum):
    """Air-conditioner operating mode decided by the comfort algorithm."""

    COOL = "COOL"
    DRY = "DRY"
    FAN = "FAN"
    OFF = "OFF"
