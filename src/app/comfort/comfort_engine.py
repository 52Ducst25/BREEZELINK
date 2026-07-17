"""Pure orchestrator wiring the comfort pipeline together (design §1.2 A-E).

No I/O here: Phase 4's MQTT worker reads Redis/Postgres, builds
``ComfortInputs``, calls ``compute()``, then persists the returned
``ComfortResult`` to ``comfort_log`` and issues the IR command. Kept
synchronous on purpose — this is CPU-light math, not worth an async wrapper
(design §1.4: "compute_setpoint() thuan CPU nhe -> ham sync binh thuong").
"""

from dataclasses import dataclass
from datetime import datetime

from app.comfort.comfort_constants import ComfortConfig
from app.comfort.ir_snap import nearest_captured_temp
from app.comfort.mode_decision import decide
from app.comfort.running_mean import update_ema
from app.comfort.setpoint_calculator import compute_target
from app.models.enums import AcMode
from app.schemas.comfort import ComfortResult


@dataclass
class ComfortInputs:
    """Everything one ``compute()`` cycle needs, assembled by the caller from
    Redis state + Postgres config (Phase 2 services) — no I/O performed here.
    """

    cfg: ComfortConfig
    tin: float
    hin: float
    tout: float
    tout_ema_prev: float | None
    outdoor_stale: bool
    prev_mode: AcMode
    last_switch_ts: float
    now: datetime
    override_active: bool
    available_ir_temps: list[int]
    is_outdoor_tick: bool = True


def compute(inputs: ComfortInputs) -> ComfortResult | None:
    """Run the full pipeline.

    Returns ``None`` when a manual override is active (design §1.2 step E —
    the override gate runs FIRST, before any math, skipping auto-control
    entirely while the user holds manual control).
    """
    if inputs.override_active:
        return None

    stale = inputs.outdoor_stale
    if stale or not inputs.is_outdoor_tick:
        # H1 fix: the EMA may only advance/re-blend on a tick TRIGGERED BY
        # THE OUTDOOR NODE. Indoor telemetry fires ~10x more often (30s vs
        # outdoor's 5min, firmware §4.2); re-blending the same held raw Tout
        # on every indoor tick was pushing the effective per-outdoor-period
        # weight to ~0.89 instead of the intended alpha=0.2, destroying the
        # anti-oscillation damping (design §6). Indoor ticks (and a stale
        # outdoor reading, same rationale as before) just reuse the last
        # persisted running mean without touching it; only outdoor ticks
        # take the ``update_ema`` blend branch below.
        t_rm = inputs.tout_ema_prev if inputs.tout_ema_prev is not None else inputs.tout
        new_ema = t_rm
    else:
        t_rm = new_ema = update_ema(inputs.tout, inputs.tout_ema_prev, inputs.cfg.ema_alpha)

    t_neutral, penalty, t_target = compute_target(
        t_rm, inputs.hin, inputs.now.hour, inputs.cfg
    )
    t_set = nearest_captured_temp(t_target, inputs.available_ir_temps)
    mode: AcMode = decide(
        inputs.tin,
        inputs.hin,
        t_set,
        inputs.prev_mode,
        inputs.last_switch_ts,
        inputs.now.timestamp(),
        inputs.cfg,
    )

    return ComfortResult(
        t_rm=t_rm,
        t_neutral=t_neutral,
        humid_penalty=penalty,
        t_target=t_target,
        t_set=t_set,
        mode=mode,
        stale=stale,
        new_ema=new_ema,
    )
