"""Snap a continuous target temperature to the nearest LEARNed IR-code
temperature (design §1.2 step D) — the AC can only be commanded at the
discrete temps captured via the app's "Hoc nut nay" (learn-this-button) flow.
"""


class NoIrCodesError(Exception):
    """Raised when there is no captured IR temp to snap to.

    Auto-control gating constraint #1 (phase-03 spec): without at least one
    learned IR code, the algorithm cannot emit any command. The caller
    (Phase 4) must catch this and keep auto-control gated off until the org
    finishes learning its IR-code table (design §5 risk: "thiếu mã nào ->
    setpoint đó không phát được").
    """


def nearest_captured_temp(target: float, available_temps: list[int]) -> int:
    """Return the LEARNed temp closest to ``target``.

    Ties are broken toward whichever candidate ``min()`` encounters first in
    ``available_temps`` (stable, deterministic given a fixed input order).
    Raises ``NoIrCodesError`` if the list is empty — there is nothing to
    snap to and no command can be issued.
    """
    if not available_temps:
        raise NoIrCodesError("No IR codes captured for this org/mode yet")
    return min(available_temps, key=lambda t: abs(target - t))
