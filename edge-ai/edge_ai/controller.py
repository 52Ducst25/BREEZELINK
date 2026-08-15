"""The edge-AI control loop: ingest gateway snapshots, forecast, decide, act.

Runs on the Linux half of the Arduino UNO Q (Debian on the Dragonwing QRB2210).
The STM32 half takes no part in this path — "edge AI" here means a Python
service on a small computer, not a sketch on a microcontroller.

WHAT IT ADDS OVER THE CLOUD, given the cloud already runs the same algorithm:

  1. It keeps working when the house loses internet. The cloud comfort loop is
     the only thing that adapts the setpoint to the weather, so an outage
     freezes the air conditioner at whatever it was last told. This node hears
     the gateway say "the server has been quiet for 300 seconds" and takes over
     — over Bluetooth, a link that does not care whether the router is up.
  2. It keeps per-corner history the cloud only stores as a median, which is
     what makes a forecast and per-sensor anomaly detection possible at all.

WHAT IT DELIBERATELY DOES NOT DO: override the cloud while the cloud is alive.
See cloud_watch.py — two commanders is worse than a slow one.
"""

import asyncio
import json
import logging
import os
import time
from datetime import datetime, timezone
from pathlib import Path

from edge_ai.cloud_watch import CloudWatch
from edge_ai.dashboard import Dashboard, build_state
from edge_ai.comfort_bridge import (
    ComfortConfig,
    ComfortInputs,
    NoIrCodesError,
    compute,
)
from edge_ai.history_store import HistoryStore
from edge_ai.predictor import find_anomalies, fit_trend
from edge_ai.prediction_score import PredictionScore
from edge_ai.protocol import KIND_ADVICE, KIND_COMMAND, Mode, Snapshot
from edge_ai.room_store import RoomStore
from edge_ai.thermal_model import ThermalModel
from edge_ai.weather import WeatherForecast

logger = logging.getLogger("edge.control")

# Mirrors src/app/services/config_service.DEFAULT_CONFIG.
#
# NOT IMPORTED FROM THERE ON PURPOSE, unlike the algorithm itself: that module
# pulls in SQLAlchemy and the ORM models, which is a heavy dependency to install
# on a field device for eleven numbers. The algorithm is imported (see
# comfort_bridge) because it is pure and because two implementations of the
# maths would be a real hazard; eleven constants that a human can diff are not.
#
# A HOUSEHOLD WHOSE ADMIN TUNED ITS CONFIG MUST PASTE ITS OWN via
# EDGE_COMFORT_CONFIG — otherwise this node computes with factory defaults and
# quietly disagrees with the cloud about the same room.
_FALLBACK_CFG = {
    "ema_alpha": 0.2,
    "deadband": 0.8,
    "dwell_sec": 600,
    "dry_rh": 72.0,
    "humid_slope": 0.05,
    "clamp_min": 24.0,
    "clamp_max": 28.0,
    "override_hours": 2,
    "night_start": 23,
    "night_end": 6,
    "night_offset": 0.5,
}

# How far ahead the forecast looks. 15 minutes is roughly how long this system
# takes to move a room a degree, so it is the horizon at which a prediction can
# still change what you would do now.
_FORECAST_MIN = 15.0

# If the edge's median and the gateway's differ by more than this, the two copies of
# the rule have drifted apart. 0.05 °C is below the protocol's own rounding (readings
# travel on the wire in units of 0.01 °C), so anything larger is a real divergence
# rather than noise.
_MEDIAN_DRIFT_C = 0.05


class Controller:
    def __init__(self, settings, link) -> None:
        self._s = settings
        self._link = link
        self._store = RoomStore(settings.history_sec)
        self._cloud = CloudWatch(settings.takeover_after_sec)
        self._cfg = self._load_cfg()

        # --- C: local history, --- A: the thermal model, --- B: the weather forecast.
        # The initialisation order is not arbitrary: the model batch-fits IMMEDIATELY
        # from the existing history, so after a restart it is usable at once rather
        # than relearning from scratch.
        self._history = HistoryStore(settings.history_db, settings.history_days)
        self._model = ThermalModel()
        self._model.load_history(self._history.recent(hours=settings.history_days * 24))
        self._score = PredictionScore(_FORECAST_MIN)

        self._weather: WeatherForecast | None = None
        self._weather_busy = False
        if settings.lat is not None and settings.lon is not None:
            self._weather = WeatherForecast(
                settings.lat, settings.lon,
                cache_path=Path(settings.history_db).with_name("weather-cache.json"),
            )
        else:
            logger.info("EDGE_LAT/EDGE_LON are not set - no forecast, so the model will "
                        "assume the outdoor temperature stands still at longer horizons")

        # The monitoring page on port 7000. Disables itself when not running in App Lab.
        self._dash = Dashboard(self._history)
        self._dash.start()

        # State that the earlier MQTT version had to reconstruct by eavesdropping on
        # all four topic types. The gateway now sends it directly in every snapshot --
        # it is the side that ACTUALLY knows what mode the air conditioner is in,
        # because it is the one transmitting the IR frames.
        self._prev_mode = Mode.OFF
        self._prev_setpoint: int | None = None
        self._last_switch_ts = 0.0
        self._tout_ema: float | None = None

        # Temperatures observed from the snapshots. ONLY used when EDGE_IR_TEMPS is
        # empty -- see _available_temps() for why the two sources are not mixed.
        self._seen_temps: set[int] = set()
        self._narrow_warned = False
        self._log_countdown = 1

    # -- config ---------------------------------------------------------------

    def _load_cfg(self) -> ComfortConfig:
        raw = os.getenv("EDGE_COMFORT_CONFIG", "").strip()
        if not raw:
            logger.warning(
                "EDGE_COMFORT_CONFIG is not set - using the default configuration. If this "
                "household has tuned the algorithm on the web UI, the edge will compute "
                "something DIFFERENT from the server."
            )
            return ComfortConfig.from_dict(_FALLBACK_CFG)
        return ComfortConfig.from_dict({**_FALLBACK_CFG, **json.loads(raw)})

    # -- ingest ---------------------------------------------------------------

    def on_snapshot(self, snapshot: Snapshot) -> None:
        """Called from the BLE task on every gateway notify. It only records; it
        decides nothing -- decisions live in tick(), which runs on its own cadence."""
        self._store.ingest(snapshot)
        self._cloud.update(snapshot.cloud_silence_sec)

        # Write to disk at the 60s cadence (rate-limited internally), then feed THAT
        # SAME sample into the model. One source, one cadence -- no second path to
        # diverge.
        row = self._history.ingest(snapshot)
        if row is not None:
            self._model.observe(row)

        # Air conditioner state: taken from the gateway, never inferred. Only a real
        # MODE change restarts the compressor dwell timer; a setpoint-only change does
        # not, otherwise mode switching would be frozen out.
        if snapshot.ac_mode is not Mode.UNKNOWN and snapshot.ac_mode != self._prev_mode:
            self._prev_mode = snapshot.ac_mode
            self._last_switch_ts = time.time()
        if snapshot.ac_setpoint is not None:
            self._prev_setpoint = snapshot.ac_setpoint
            # The server only commands combinations it has codes for, so every
            # setpoint the gateway reports as executed is evidence that "this
            # household has a code for that temperature".
            if snapshot.ac_mode is Mode.COOL:
                self._seen_temps.add(snapshot.ac_setpoint)

    # -- which temperatures can be commanded -----------------------------------

    def _available_temps(self) -> list[int]:
        """The COOL levels this household has IR codes for.

        THE HAND-DECLARED LIST WINS, and when it exists the observed levels are NOT
        mixed in. Mixing sounds generous but is wrong: observed levels come from
        `snapshot.ac_setpoint`, i.e. the state the gateway is currently showing -- and
        a user pressing the remote by hand changes that number too. Mixing turns one
        manual press into "this household has a code for that level", when it may not.

        With nothing declared it falls back to self-learning, and then it has to
        complain loudly: see the EDGE_IR_TEMPS note in config.py -- a half-filled list
        makes nearest_captured_temp() silently force every target to the one level it
        knows.
        """
        if self._s.ir_temps:
            return list(self._s.ir_temps)

        observed = sorted(self._seen_temps)
        if not observed:
            logger.info(
                "Do not yet know which levels this household has learned (EDGE_IR_TEMPS "
                "is empty and no COOL command from the server has been seen) - advising "
                "only, not commanding"
            )
        elif len(observed) < 3 and not self._narrow_warned:
            self._narrow_warned = True
            logger.warning(
                "ONLY %d levels known (%s) because we are self-learning by observation. "
                "Every target will be forced onto these levels - set EDGE_IR_TEMPS to stop "
                "the guessing:\n"
                "    SELECT DISTINCT temp FROM ir_codes WHERE mode='COOL' ORDER BY temp;",
                len(observed), ", ".join(f"{t}°C" for t in observed),
            )
        return observed

    # -- decide ---------------------------------------------------------------

    async def tick(self) -> None:
        """One control cycle. Never raises — a field service that dies on an
        edge case is worse than one that logs and tries again in 30 seconds."""
        try:
            await self._tick()
        except Exception:  # noqa: BLE001
            logger.exception("Control loop error - skipping this tick")

    async def _tick(self) -> None:
        snapshot = self._store.latest
        if snapshot is None:
            logger.info("No snapshot received from the gateway yet - not computing")
            return

        if snapshot.t_in is None or snapshot.h_in is None:
            logger.info("The gateway reports no fresh room corner - not computing, not commanding")
            return

        # Cross-check: recompute the median with the backend's own function. A
        # discrepancy means the C++ version on the gateway and the Python version in
        # the cloud have drifted apart, and that is a class of bug with no symptom
        # other than this log line.
        mine = self._store.indoor_check()
        if mine is not None and abs(mine.temp - snapshot.t_in) > _MEDIAN_DRIFT_C:
            logger.warning(
                "MEDIAN MISMATCH: the gateway says %.2f°C, the edge computes %.2f°C from %d "
                "corners - the aggregation rules in room-registry.cpp and room_aggregate.py "
                "have drifted apart",
                snapshot.t_in, mine.temp, mine.used,
            )

        if snapshot.t_out is None and self._tout_ema is None:
            logger.info("No outdoor reading yet - not computing")
            return
        tout = snapshot.t_out if snapshot.t_out is not None else self._tout_ema

        trends = {s: t for s in self._store.slots()
                  if (t := fit_trend([(x.ts, x.temp) for x in self._store.history(s)]))}
        latest = self._store.latest_per_room()
        anomalies = find_anomalies(latest, snapshot.t_in, trends)
        for a in anomalies:
            logger.warning("Anomaly at %s (%s): %s",
                           self._store.label(int(a.device_uuid)), a.kind, a.detail)

        # Settle due forecasts BEFORE placing a new one, so we never score ourselves
        # against a number produced in the same tick.
        self._score.settle(time.time(), snapshot.t_in)
        self._refresh_weather_if_due()
        forecast = self._forecast(snapshot, tout, trends)

        available = self._available_temps()

        result = None
        if available and not snapshot.override_active:
            try:
                result = compute(
                    ComfortInputs(
                        cfg=self._cfg,
                        tin=snapshot.t_in,
                        hin=snapshot.h_in,
                        tout=tout,
                        tout_ema_prev=self._tout_ema,
                        outdoor_stale=not snapshot.outdoor_online,
                        prev_mode=_to_ac_mode(self._prev_mode),
                        last_switch_ts=self._last_switch_ts,
                        now=datetime.now(timezone.utc),
                        override_active=False,
                        available_ir_temps=available,
                        # While the outdoor node still has a heartbeat this really is
                        # an outdoor reading, so it is allowed to advance the running
                        # mean. With the heartbeat lost, reuse the old EMA and do NOT
                        # mix anything in -- exactly comfort_engine's
                        # `is_outdoor_tick` gate.
                        is_outdoor_tick=snapshot.outdoor_online and snapshot.t_out is not None,
                    )
                )
            except NoIrCodesError:
                logger.warning("This household has learned no IR codes - cannot control anything")

        if result is not None and snapshot.outdoor_online and snapshot.t_out is not None:
            self._tout_ema = result.new_ema

        self._log_model()
        if self._dash.enabled:
            self._dash.publish(build_state(
                snapshot=snapshot, store=self._store, model=self._model,
                score=self._score, weather=self._weather, cloud=self._cloud,
                settings=self._s, result=result, forecast=forecast,
                anomalies=anomalies,
            ))
        await self._act(snapshot, result, forecast, len(anomalies))

    # -- forecasting -----------------------------------------------------------

    def _forecast(self, snapshot: Snapshot, tout: float, trends) -> float | None:
        """The indoor temperature 15 minutes from now.

        TWO SOURCES, preferring the thermal model and falling back to slope
        extrapolation while the model is immature. The two numbers are not blended:
        averaging a good estimate with a crude one produces something inexplicable,
        and when it is wrong there is no way to tell which half to fix.

        EVERY FORECAST IS RECORDED FOR SCORING, the crude ones included -- which turns
        "is the model better than the old approach" into a number rather than an
        opinion.
        """
        model_value = self._model.predict(
            minutes=_FORECAST_MIN,
            t_in=snapshot.t_in,
            t_out_now=tout,
            setpoint=snapshot.ac_setpoint,
            cooling=snapshot.ac_mode is Mode.COOL,
            t_out_at=self._weather.temp_at if self._weather is not None else None,
            now_ts=time.time(),
        )
        if model_value is None and trends:
            avg_slope = sum(t.slope_per_min for t in trends.values()) / len(trends)
            model_value = snapshot.t_in + avg_slope * _FORECAST_MIN

        if model_value is not None:
            self._score.record(time.time(), model_value, snapshot.t_in)
        return model_value

    def _refresh_weather_if_due(self) -> None:
        """Re-fetch the forecast if it is due. Does NOT wait for the result.

        Fetching a forecast is a nice-to-have, while a single network call can hang
        for up to 15 seconds. Waiting for it inside the control loop would mean a
        network outage doubles the control interval -- exactly when this node should
        be working hardest.
        """
        if self._weather is None or self._weather_busy or not self._weather.due():
            return
        self._weather_busy = True

        async def run() -> None:
            try:
                await asyncio.get_running_loop().run_in_executor(None, self._weather.refresh)
            finally:
                self._weather_busy = False

        asyncio.create_task(run())

    def _log_model(self) -> None:
        """Report on the model every 10 ticks (~5 minutes).

        Deliberately sparser than the control line: τ and the forecast error change by
        the hour, not by the minute, and printing every tick would scroll away the
        line you actually need to read.
        """
        self._log_countdown -= 1
        if self._log_countdown > 0:
            return
        self._log_countdown = 10

        parts = [f"model: {self._model.describe()}", f"forecast: {self._score.describe()}"]
        if self._weather is not None:
            peak = self._weather.peak_within(12.0)
            parts.append(
                "weather: none yet" if peak is None
                else f"weather: next-12h peak {peak[0]:.1f}°C"
            )
        parts.append(f"history: {self._history.count()} samples / {self._history.span_hours():.0f}h")
        logger.info(" · ".join(parts))

    # -- act ------------------------------------------------------------------

    async def _act(self, snapshot: Snapshot, result, forecast, anomaly_count: int) -> None:
        driving = self._cloud.driving
        silence = self._cloud.silence_sec
        rooms = f"{snapshot.room_count}/{sum(1 for r in snapshot.rooms if r.corner is not None)}"

        if result is None:
            logger.info("t_in=%.1f (%s corners) t_out=%s · %s · no decision yet",
                        snapshot.t_in, rooms,
                        "—" if snapshot.t_out is None else f"{snapshot.t_out:.1f}",
                        "EDGE IN CONTROL" if driving else "server in control")
            return

        logger.info(
            "t_in=%.1f (%s corners) t_out=%s · 15min forecast %s · %s -> %s %d°C · %s%s",
            snapshot.t_in, rooms,
            "—" if snapshot.t_out is None else f"{snapshot.t_out:.1f}",
            "—" if forecast is None else f"{forecast:.1f}",
            self._prev_mode.name_str, result.mode.value, result.t_set,
            "EDGE IN CONTROL" if driving else f"server in control (silent {silence:.0f}s)"
            if silence is not None else "the server has never issued a command",
            f" · {anomaly_count} anomalies" if anomaly_count else "",
        )

        mode = Mode[result.mode.value]

        # ADVICE is the default on every path. The gateway only fires infrared on
        # KIND_COMMAND, so every branch that does not qualify falls back here safely
        # rather than relying on someone remembering to block it.
        kind = KIND_ADVICE
        reason = None

        if not driving:
            reason = None                       # normal: the cloud is handling it
        elif self._s.advisory_only:
            reason = "EDGE_ADVISORY_ONLY is enabled"
        elif snapshot.override_active:
            reason = "the user is holding control"
        elif result.mode.value == self._prev_mode.name_str and result.t_set == self._prev_setpoint:
            reason = "the decision is unchanged"  # the same rule as the cloud: only send on a change
        else:
            kind = KIND_COMMAND

        if kind == KIND_COMMAND:
            sent = await self._link.send(kind=KIND_COMMAND, mode=mode, setpoint=result.t_set)
            if sent:
                logger.warning("COMMAND ISSUED (edge in control): %s %d°C", result.mode.value, result.t_set)
                self._prev_mode = mode
                self._prev_setpoint = result.t_set
            else:
                logger.warning("Wanted to command %s %d but the gateway is not connected yet",
                               result.mode.value, result.t_set)
            return

        if driving and reason:
            logger.info("In control but not commanding: %s", reason)
        await self._link.send(kind=KIND_ADVICE, mode=mode, setpoint=result.t_set)


def _to_ac_mode(mode: Mode):
    """Wire Mode -> the backend's AcMode (which is what comfort_engine accepts)."""
    from edge_ai.comfort_bridge import AcMode

    try:
        return AcMode(mode.name_str)
    except ValueError:
        return AcMode.OFF
