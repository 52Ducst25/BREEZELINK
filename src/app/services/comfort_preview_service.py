"""Assembles ``comfort_engine.compute()`` inputs from Redis/Postgres for the
read-only ``GET /comfort/preview`` endpoint (Phase 5) — no side effects, no
MQTT publish, no ``commands``/``comfort_log`` writes.

Always computes the auto decision with ``override_active=False`` even when
a manual override is currently active, since this is a preview of what auto
WOULD do, not a trigger to change control (design §1.2 step E gate only
applies to the real auto-control cycle, not this read path).

Missing-data policy (mirrors ``telemetry_handler``'s skip conditions so the
preview never claims a decision the worker would not actually make):
  * no indoor reading, or no outdoor reading AND no persisted EMA
    -> ``data_available=False`` (previously: readings defaulted to 0.0, which
       produced a real-looking setpoint from an empty database)
  * no LEARNed IR code -> target math only, ``t_set``/``mode`` stay None
    instead of bubbling ``NoIrCodesError`` up as a 500.
"""

from datetime import datetime, timezone

from sqlalchemy.ext.asyncio import AsyncSession

from app.comfort.comfort_constants import ComfortConfig
from app.comfort.comfort_engine import ComfortInputs, compute
from app.comfort.ir_snap import NoIrCodesError
from app.comfort.setpoint_calculator import compute_target
from app.models.enums import AcMode
from app.schemas.comfort import ComfortPreview
from app.services import ir_code_service, redis_config_cache, redis_override_service, redis_state_service

_NO_INDOOR = "no indoor telemetry yet — waiting for the indoor node's first reading"
_NO_OUTDOOR = "no outdoor telemetry yet — waiting for the outdoor node's first reading"
_NO_IR_CODES = (
    "auto-control gated off: no IR code learned yet — capture the table "
    "(COOL 24-28, DRY, FAN, OFF) via the app's LEARN flow first"
)


def _as_float(state: dict, key: str) -> float | None:
    value = state.get(key)
    return float(value) if value is not None else None


async def build_preview(session: AsyncSession, org_id: str) -> ComfortPreview:
    cfg = ComfortConfig.from_dict(await redis_config_cache.get_cfg(org_id, session))

    indoor = await redis_state_service.get_indoor_state(org_id) or {}
    outdoor = await redis_state_service.get_outdoor_state(org_id) or {}
    tout_ema_prev = await redis_state_service.get_tout_ema(org_id)

    tin, hin = _as_float(indoor, "t"), _as_float(indoor, "h")
    tout_raw = _as_float(outdoor, "t")

    # Honest empty states — do NOT fabricate a decision from absent readings.
    if tin is None or hin is None:
        return ComfortPreview(data_available=False, reason=_NO_INDOOR)
    if tout_raw is None and tout_ema_prev is None:
        return ComfortPreview(data_available=False, reason=_NO_OUTDOOR)

    tout = tout_raw if tout_raw is not None else tout_ema_prev
    outdoor_stale = await redis_state_service.is_outdoor_stale(org_id)
    prev_mode = AcMode(indoor["mode"]) if indoor.get("mode") else AcMode.OFF
    last_switch = await redis_state_service.get_last_switch(org_id)
    last_switch_ts = last_switch[1] if last_switch else 0.0
    now = datetime.now(timezone.utc)

    inputs = ComfortInputs(
        cfg=cfg,
        tin=tin,
        hin=hin,
        tout=tout,
        tout_ema_prev=tout_ema_prev,
        outdoor_stale=outdoor_stale,
        prev_mode=prev_mode,
        last_switch_ts=last_switch_ts,
        now=now,
        override_active=False,
        available_ir_temps=await ir_code_service.get_available_temps(session, org_id, AcMode.COOL),
        # Read path: never advance the persisted EMA (that is the worker's job
        # on a real outdoor tick) — preview must stay side-effect free.
        is_outdoor_tick=False,
    )

    override_on = await redis_override_service.override_active(org_id)
    try:
        result = compute(inputs)
    except NoIrCodesError:
        # Still show the target math so the UI can explain what it would aim
        # for once the IR table is learned; leave t_set/mode unresolved.
        t_rm = tout_ema_prev if tout_ema_prev is not None else tout
        t_neutral, penalty, t_target = compute_target(t_rm, hin, now.hour, cfg)
        return ComfortPreview(
            t_rm=t_rm,
            t_neutral=t_neutral,
            humid_penalty=penalty,
            t_target=t_target,
            stale=outdoor_stale,
            override_active=override_on,
            reason=_NO_IR_CODES,
        )

    assert result is not None  # override_active=False above => never gated out

    reason = (
        "manual override active — showing what auto would decide, for reference only"
        if override_on
        else f"auto: Tin={tin:.1f} vs Tset={result.t_set} -> {result.mode.value}"
    )
    return ComfortPreview(
        t_rm=result.t_rm,
        t_neutral=result.t_neutral,
        humid_penalty=result.humid_penalty,
        t_target=result.t_target,
        t_set=result.t_set,
        mode=result.mode,
        stale=result.stale,
        override_active=override_on,
        reason=reason,
    )
