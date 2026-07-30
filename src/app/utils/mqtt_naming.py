"""Single source of truth for MQTT topic + client-id conventions.

Referenced by device/comfort services, the worker consumer, and the firmware
sample so the scheme never drifts.

Topic scheme (bidirectional — this differs from a 1-way-telemetry-only design):
    bl/{org_id}/{device_uuid}/{kind}
        device_uuid: the node's devices.device_uuid (its MQTT identity), so any
                     number of nodes in one household (1 outdoor + N indoor)
                     each get their own topics — the node_type (indoor/outdoor)
                     is resolved from the devices row, not read off the topic.
        kind: telemetry | cmd | state | status | learn | override

    telemetry — device -> cloud sensor readings (QoS1, retain off)
    cmd       — cloud -> device downstream commands (QoS1, retain off)
    state     — device -> cloud current actuator state / ack (QoS1, retain on)
    status    — device -> cloud presence/LWT ("online"/"offline", retain on)
    override  — device -> cloud "the user took the wheel on my panel"
                (QoS1, retain OFF — see below)

``override`` MUST NOT be folded into ``state``, even though both are
device -> cloud and both carry mode/setpoint. ``state`` is published RETAINED
so a reconnecting worker learns the node's current actuator state without
waiting; a retained override would be replayed by the broker on every node
reconnect and silently re-arm the manual gate forever, permanently disabling
auto-control. Retain is exactly what makes ``state`` right and ``override``
wrong on the same topic, so they stay separate.

Client id: breezelink_{device_uuid}

The ``bl/`` prefix and the ``breezelink_`` client-id stem are the project's
former name. They are deliberately NOT renamed to ``aircon``: both are a wire
contract with firmware already flashed onto deployed nodes, which publish to
``bl/...`` and connect under that client id. Renaming either here silently
drops every existing device's traffic on the floor — the broker keeps
accepting the old topics, nothing errors, and the worker's wildcards just stop
matching. Change these only together with a firmware release + reflash.
"""

import enum
from dataclasses import dataclass


class NodeType(str, enum.Enum):
    outdoor = "outdoor"
    indoor = "indoor"


class TopicKind(str, enum.Enum):
    telemetry = "telemetry"
    cmd = "cmd"
    state = "state"
    status = "status"
    learn = "learn"
    override = "override"


TOPIC_TEMPLATE = "bl/{org_id}/{device_uuid}/{kind}"
CLIENT_ID_TEMPLATE = "breezelink_{device_uuid}"

# Worker subscription wildcards. org AND the device segment are wildcarded — a
# single worker serves every tenant and every node, and the device_uuid segment
# (previously the fixed node_type) is now per-device, so state/learn widened
# from "bl/+/indoor/..." to "bl/+/+/...".
TELEMETRY_WILDCARD = "bl/+/+/telemetry"
STATE_WILDCARD = "bl/+/+/state"
STATUS_WILDCARD = "bl/+/+/status"
LEARN_WILDCARD = "bl/+/+/learn"
OVERRIDE_WILDCARD = "bl/+/+/override"


def topic(org_id: str, device_uuid: str, kind: str) -> str:
    """Build a single topic: ``bl/{org_id}/{device_uuid}/{kind}``."""
    return TOPIC_TEMPLATE.format(org_id=org_id, device_uuid=device_uuid, kind=kind)


def client_id(device_uuid: str) -> str:
    return CLIENT_ID_TEMPLATE.format(device_uuid=device_uuid)


@dataclass(frozen=True)
class ParsedTopic:
    """A ``bl/{org_id}/{device_uuid}/{kind}`` topic, split into its 4 segments."""

    org_id: str
    device_uuid: str
    kind: str


def parse(topic_str: str) -> ParsedTopic:
    """Parse an incoming topic string into ``(org_id, device_uuid, kind)``.

    Raises ``ValueError`` on anything not matching the 4-segment
    ``bl/{org_id}/{device_uuid}/{kind}`` scheme (e.g. foreign topics on a shared
    broker) — callers (the consumer dispatch loop) must catch this and skip
    the message rather than crash.
    """
    parts = topic_str.split("/")
    if len(parts) != 4 or parts[0] != "bl":
        raise ValueError(f"Unrecognized topic: {topic_str!r}")
    _, org_id, device_uuid, kind = parts
    return ParsedTopic(org_id=org_id, device_uuid=device_uuid, kind=kind)
