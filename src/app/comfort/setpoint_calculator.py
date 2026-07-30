"""Adaptive setpoint pipeline: neutral temp, humidity correction, schedule
offset, and the final clamped target (design §1.2 steps B-D).

The regression constants below are FIXED science (de Dear & Brager RP-884
field study), not admin-tunable — kept as module constants here, distinct
from ``ComfortConfig`` (site-tunable knobs only, see comfort_constants.py).
"""

from app.comfort.comfort_constants import ComfortConfig

# de Dear & Brager, ASHRAE RP-884 (1997; 20k+ field entries, 160 buildings, 4
# continents): T_comf = 0.31*T_rm + 17.8, valid only for 10 <= T_rm <= 33.5.
# CAVEAT (researcher-01 §1): this model's original scope is naturally
# ventilated buildings, NOT air-conditioned rooms. BreezeLink reframes the
# output as an "energy-saving setpoint reference" (design §1.1 point 2), not
# a comfort-compliance guarantee — true comfort in a sealed AC room is
# restored by the humidity correction below (design §1.1 point 2 caveat).
ADAPTIVE_SLOPE = 0.31
ADAPTIVE_INTERCEPT = 17.8
T_RM_MIN = 10.0
T_RM_MAX = 33.5

# Piecewise humidity-penalty knee points (design §1.2 step C / researcher-01
# §3): below 60%RH sweat evaporates freely (no penalty); 60-75% band uses the
# tunable cfg.humid_slope (~0.05 degC/%RH default, approximating the NWS
# heat-index humidity contribution around 27-30 degC); above 75% steepens
# because evaporative cooling breaks down (Heat Index/apparent-temp regimes
# diverge faster there).
HUMID_LOW_KNEE = 60.0
HUMID_HIGH_KNEE = 75.0
HUMID_HIGH_SLOPE = 0.06


def neutral_temp(t_rm: float) -> float:
    """Adaptive neutral temperature from the running-mean outdoor temp.

    Clamps ``t_rm`` to [10, 33.5] before the regression — outside this band
    the de Dear & Brager field-study regression is not defined (design §1.2
    note); clamping keeps the formula well-behaved instead of extrapolating.
    """
    clamped = min(max(t_rm, T_RM_MIN), T_RM_MAX)
    return ADAPTIVE_SLOPE * clamped + ADAPTIVE_INTERCEPT


def humid_penalty(hin: float, slope: float) -> float:
    """Degrees to subtract from the neutral temp for high indoor humidity.

    3-branch piecewise (design §1.2 step C):
    - ``hin <= 60``: no penalty, evaporative cooling still effective.
    - ``60 < hin <= 75``: linear in the tunable ``slope`` (``cfg.humid_slope``).
    - ``hin > 75``: fixed 0.06 degC/%RH slope past the 75% knee, added on top
      of whatever penalty already accrued at the knee.

    NOTE: the brainstorm pseudocode hardcodes the knee-continuity value as a
    literal ``0.75`` (assuming the default ``slope=0.05``). Since
    ``humid_slope`` is admin-tunable (configs table), that literal would
    create a discontinuity if an admin changes the slope. This implementation
    instead recomputes the knee value as ``(75-60)*slope`` so the piecewise
    function stays continuous for any configured slope — equivalent to the
    design doc at the default slope, strictly more correct otherwise.
    """
    if hin <= HUMID_LOW_KNEE:
        return 0.0
    if hin <= HUMID_HIGH_KNEE:
        return (hin - HUMID_LOW_KNEE) * slope
    knee_penalty = (HUMID_HIGH_KNEE - HUMID_LOW_KNEE) * slope
    return knee_penalty + (hin - HUMID_HIGH_KNEE) * HUMID_HIGH_SLOPE


def schedule_offset(now_hour: int, cfg: ComfortConfig) -> float:
    """Night-time offset added to the target (design §1.2 step D).

    Handles both a non-wrapping window (``night_start < night_end``, e.g.
    1-5) and a midnight-wrapping window (``night_start > night_end``, e.g.
    23-6) via the half-open ``[night_start, night_end)`` convention.
    """
    if cfg.night_start <= cfg.night_end:
        in_window = cfg.night_start <= now_hour < cfg.night_end
    else:
        in_window = now_hour >= cfg.night_start or now_hour < cfg.night_end
    return cfg.night_offset if in_window else 0.0


def compute_target(
    t_rm: float, hin: float, now_hour: int, cfg: ComfortConfig
) -> tuple[float, float, float]:
    """Full continuous-target pipeline (design §1.2 steps B-D), pre-IR-snap.

    Returns ``(t_neutral, penalty, t_target_clamped)`` so the caller can
    persist every intermediate value to ``comfort_log`` (design §3.1).
    """
    t_neutral = neutral_temp(t_rm)
    penalty = humid_penalty(hin, cfg.humid_slope)
    offset = schedule_offset(now_hour, cfg)
    raw_target = t_neutral - penalty + offset
    clamped = min(max(raw_target, cfg.clamp_min), cfg.clamp_max)
    return t_neutral, penalty, clamped
