"""Shared domain enums for BreezeLink comfort-control tables.

Kept as plain ``str`` Enums (not native Python IntEnum) so Pydantic and
SQLAlchemy both serialize the readable string value directly.
"""

import enum


class NodeType(str, enum.Enum):
    """What a physical node is FOR. Three kinds since the 6-device layout:

    ``outdoor``  DHT22 outside, reaches the gateway over ESP-NOW. Its reading is
                 the only input to the running-mean (``tout_ema``).
    ``indoor``   the gateway/panel itself: WiFi+MQTT bridge, IR blaster, 2.8"
                 touch screen. It NO LONGER carries a temperature sensor — the
                 board reports no ``t``/``h`` of its own, only what it relays.
    ``room``     one of the corner sensors (ESP32-C3 + DHT22, BLE flood mesh).
                 Several per household; the worker medians the fresh ones into
                 the single ``t_in``/``h_in`` the comfort engine consumes.

    ``indoor`` is kept for boards still running the pre-split firmware (which
    did publish its own t/h) — dropping it would strand every deployed unit.
    """

    outdoor = "outdoor"
    indoor = "indoor"
    room = "room"


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
