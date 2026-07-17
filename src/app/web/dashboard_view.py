"""View-model shaping for the SSR dashboard/device pages.

Thermal-state classification lives here in Python, not in Jinja/CSS, for the
same reason SafeKitchen computed its gas/smoke/temp alarm "level" in
``web/routes.py`` rather than comparing numbers in a template: CSS cannot do
arithmetic, and burying a comparison in Jinja makes it untestable.

Colour rule (see static/app.css, "THERMAL COLOUR MAPPING"): every reading —
indoor, outdoor, AC mode — maps onto the same 4-step cold/neutral/warm/hot
vocabulary, aliased there onto the real design system's status tokens. Only
the classification INPUT differs per reading kind; the palette itself never
encodes "which sensor this is".
"""

from app.models.enums import AcMode

_MODE_LEVEL = {
    AcMode.COOL.value: "cold",
    AcMode.DRY.value: "warm",
    AcMode.FAN.value: "neutral",
    AcMode.OFF.value: "unknown",
}


def reading_from_state(state: dict | None) -> dict | None:
    """Reduce a Redis indoor/outdoor state hash to ``{t, h}``, or ``None``.

    Small deliberate duplicate of ``live_state_service._reading`` (a private,
    3-line helper) rather than importing a leading-underscore name across
    module boundaries for something this trivial.
    """
    if not state or state.get("t") is None or state.get("h") is None:
        return None
    return {"t": float(state["t"]), "h": float(state["h"])}


def classify_indoor(tin: float | None, t_target: float | None, deadband: float = 1.0) -> str:
    """Indoor thermal state relative to the computed target.

    Delta-based (not an absolute band) because indoor's job is "track
    t_target", not sit inside some fixed comfortable range regardless of
    what the algorithm is currently asking for.
    """
    if tin is None or t_target is None:
        return "unknown"
    delta = tin - t_target
    if delta <= -deadband:
        return "cold"
    if delta <= deadband:
        return "neutral"
    if delta <= deadband * 3:
        return "warm"
    return "hot"


def classify_outdoor(tout: float | None) -> str:
    """Outdoor thermal state on an absolute climate band.

    There is no "target" for the outside air — only how hot it objectively
    is — so this reads a fixed band instead of a delta.
    """
    if tout is None:
        return "unknown"
    if tout < 20:
        return "cold"
    if tout < 30:
        return "neutral"
    if tout < 34:
        return "warm"
    return "hot"


def mode_level(mode: str | None) -> str:
    """Map the decided AC mode onto the shared thermal vocabulary."""
    if mode is None:
        return "unknown"
    return _MODE_LEVEL.get(mode, "unknown")
