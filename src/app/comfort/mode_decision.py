"""Mode decision with hysteresis + dwell (design §1.3).

Of the 3 anti-oscillation layers in the full pipeline (EMA input, DEADBAND,
DWELL time), the latter two live here; EMA smoothing happens upstream in
``running_mean.py``.
"""

from app.comfort.comfort_constants import ComfortConfig
from app.models.enums import AcMode


def decide(
    tin: float,
    hin: float,
    t_set: int,
    prev_mode: AcMode,
    last_switch_ts: float,
    now: float,
    cfg: ComfortConfig,
) -> AcMode:
    """Decide the next AC mode.

    Priority order (design §1.3): DRY (dehumidify) takes priority over COOL
    when the room is already near setpoint but too humid; otherwise COOL if
    too warm, FAN if already cool enough (avoid re-cooling, save energy), or
    hold ``prev_mode`` inside the deadband (hysteresis).

    The resulting ``want`` is then vetoed by DWELL: if not enough time has
    passed since the last actual mode switch, the mode holds at
    ``prev_mode`` regardless of what the instantaneous reading wants — this
    protects the compressor from short-cycling even if temp/humidity keep
    fluctuating near a boundary.
    """
    if hin >= cfg.dry_rh and tin <= t_set + cfg.deadband:
        want = AcMode.DRY
    elif tin > t_set + cfg.deadband:
        want = AcMode.COOL
    elif tin < t_set - cfg.deadband:
        want = AcMode.FAN
    else:
        want = prev_mode

    if want != prev_mode and (now - last_switch_ts) < cfg.dwell_sec:
        return prev_mode
    return want
