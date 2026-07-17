"""Running-mean outdoor temperature proxy (design §1.1 caveat #1, §1.2 step A).

ASHRAE-55's adaptive model expects a multi-day *prevailing mean outdoor
temperature*. Aircon has no multi-day stable history at deploy time, so
an EMA of raw ``Tout`` telemetry is used as a proxy running mean instead — it
damps the cloud/sun noise that a raw instantaneous Tout would otherwise
inject straight into the setpoint.
"""


def update_ema(t_out: float, prev: float | None, alpha: float) -> float:
    """One EMA step: ``T_rm <- alpha*T_out + (1-alpha)*T_rm_prev``.

    ``alpha`` is the weight on the NEW sample (design's ``cfg.ema_alpha``,
    default 0.2). EN 15251's own running-mean formula names its decay factor
    the other way round: its alpha=0.8 is the weight on the OLD running mean
    (``Theta_rm = (1-alpha)*Theta_today + alpha*Theta_rm_prev``). The two are
    mathematically identical once you note ``design_alpha = 1 -
    EN15251_alpha`` — don't confuse the two conventions when tuning
    ``cfg.ema_alpha`` against EN 15251 literature.

    ``prev is None`` means cold start (no EMA persisted in Redis yet): seed
    the running mean with the first raw sample instead of blending against a
    nonexistent history.
    """
    if prev is None:
        return t_out
    return alpha * t_out + (1 - alpha) * prev
