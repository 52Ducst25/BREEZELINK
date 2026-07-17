"""Shared domain enums for Aircon comfort-control tables.

Kept as plain ``str`` Enums (not native Python IntEnum) so Pydantic and
SQLAlchemy both serialize the readable string value directly.
"""

import enum


class NodeType(str, enum.Enum):
    """Physical ESP32 node role — outdoor sensor vs indoor sensor+IR blaster."""

    outdoor = "outdoor"
    indoor = "indoor"


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
