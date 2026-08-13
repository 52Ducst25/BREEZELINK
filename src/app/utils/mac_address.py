"""Parse the human-readable MAC a node reports into the BIGINT the DB stores.

Lives in its own module because two handlers need it for different reasons:
``telemetry_handler`` (a sensor node reports its MAC alongside a reading) and
``state_handler`` (the gateway has no sensor since the room sensors took over,
so its ``state`` message is the only thing it ever sends that can carry a MAC).

Identification only, NEVER authentication — the value is self-reported by the
firmware and trivially forged. ``devices.mac`` exists so the provisioning page
can show which physical board is which.
"""


def parse_mac(raw) -> int | None:
    """``"AA:BB:CC:DD:EE:FF"`` -> 48-bit int, or None if unusable.

    Accepts an int as-is (range-checked), or a string with ``:``/``-``
    separators. Returns None — never 0 — for anything unparseable, so callers
    can tell "no MAC reported" from a real address. An all-zero MAC means
    "unknown" on the wire and is rejected for the same reason.
    """
    if raw is None:
        return None
    if isinstance(raw, int):
        return raw if 0 < raw < (1 << 48) else None
    text = str(raw).replace(":", "").replace("-", "").strip()
    if len(text) != 12:
        return None
    try:
        value = int(text, 16)
    except ValueError:
        return None
    return value or None
