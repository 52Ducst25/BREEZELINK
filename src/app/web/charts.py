"""Server-rendered SVG line charts — no Chart.js, no build step.

Ported from SafeKitchen's ``charts.py``. That dashboard drew a threshold
line (gas/smoke/temp alarm limit); this one draws a SETPOINT reference line
(``t_set`` from the comfort pipeline) so a temperature-history chart visibly
answers "was the room tracking what the algorithm asked for". The geometry
math is unchanged — only the domain concept (threshold -> setpoint) and the
optionality (a setpoint may legitimately not exist yet, see
comfort_preview_service) are new.
"""

VW = 600  # viewBox width, user units (SVG scales via preserveAspectRatio)
VH = 140
_PAD_TOP = 12
_PAD_BOTTOM = 18


def build_series(values: list[float], setpoint: float | None, unit: str = "") -> dict | None:
    """One numeric series -> SVG-ready geometry, or ``None`` if too few points.

    ``setpoint`` may be ``None`` (no learned IR code / auto-control not yet
    decided) — the dashed reference line is simply omitted, never faked as 0.
    """
    pts = [v for v in values if v is not None]
    if len(pts) < 2:
        return None

    bounds = pts if setpoint is None else pts + [setpoint]
    lo, hi = min(bounds), max(bounds)
    if hi == lo:
        hi = lo + 1  # avoid a divide-by-zero flat line when every point is equal

    span = hi - lo
    plot_h = VH - _PAD_TOP - _PAD_BOTTOM
    n = len(pts)

    def x(i: int) -> float:
        return round(i / (n - 1) * VW, 2)

    def y(v: float) -> float:
        return round(_PAD_TOP + (hi - v) / span * plot_h, 2)

    line = " ".join(f"{x(i)},{y(v)}" for i, v in enumerate(pts))
    area = f"0,{VH - _PAD_BOTTOM} {line} {VW},{VH - _PAD_BOTTOM}"

    return {
        "line": line,
        "area": area,
        "setpoint_y": y(setpoint) if setpoint is not None else None,
        "setpoint": round(setpoint, 1) if setpoint is not None else None,
        "last": round(pts[-1], 1),
        "min": round(min(pts), 1),
        "max": round(max(pts), 1),
        "unit": unit,
        "vw": VW,
        "vh": VH,
    }
