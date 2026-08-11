"""Outdoor + indoor telemetry ingest (design §1.4 flow, phase-04 step 3-4).

Flow: validate -> persist to Postgres -> mirror into Redis -> gather every
``comfort_engine.compute()`` input from Redis/Postgres -> compute -> if the
decided (mode, setpoint) changed, hand off to ``command_publisher``.

The manual-override gate (design §1.2 step E) is NOT re-checked here — it is
read once as a plain input flag and handed to ``comfort_engine.compute()``,
which is the single source of truth for the gate (avoids checking it twice).
"""

import logging
from datetime import datetime, timezone

from app.comfort.comfort_constants import ComfortConfig
from app.comfort.comfort_engine import ComfortInputs, compute
from app.comfort.ir_snap import NoIrCodesError
from app.core.database import AsyncSessionLocal
from app.core.tenant import set_current_org
from app.models.enums import AcMode, NodeType
from app.services import (
    ir_code_service,
    live_events,
    redis_config_cache,
    redis_override_service,
    redis_room_state_service,
    redis_state_service,
    telemetry_service,
)
from app.utils.mac_address import parse_mac
from app.utils.mqtt_naming import ParsedTopic
from app.workers import command_publisher

logger = logging.getLogger("breezelink.worker.telemetry")

_TEMP_MIN, _TEMP_MAX = -10.0, 60.0
_HUMIDITY_MIN, _HUMIDITY_MAX = 0.0, 100.0

# H1 fix: plausibility floor for device-supplied ``ts``. The indoor node has
# no NTP/RTC — it sends ``millis()/1000`` (seconds since boot: 30, 300, ...),
# which is truthy but not a real epoch, so it slipped past the old
# `payload.get("ts")` truthiness check and got stored as ~1970-01-01. Outdoor
# already sends a real NTP epoch (or 0, which is falsy and safely replaced by
# now()) and already defines this same constant client-side
# (mqtt-publish.cpp:28, "~year 2023") -- this is the backend-side safety net
# for ANY device (indoor today, any future node) that sends a bogus/boot-
# relative ts.
_MIN_PLAUSIBLE_EPOCH = 1_700_000_000


class TelemetryValidationError(ValueError):
    """Raised when a reading is out of physically-plausible range."""


def _validate_ranges(payload: dict) -> tuple[float, float]:
    temp, humidity = payload.get("t"), payload.get("h")
    if temp is None or humidity is None:
        raise TelemetryValidationError("missing 't'/'h' field")
    temp, humidity = float(temp), float(humidity)
    if not (_TEMP_MIN <= temp <= _TEMP_MAX):
        raise TelemetryValidationError(f"temp out of range: {temp}")
    if not (_HUMIDITY_MIN <= humidity <= _HUMIDITY_MAX):
        raise TelemetryValidationError(f"humidity out of range: {humidity}")
    return temp, humidity


async def _gather_current_readings(
    org_id: str,
) -> tuple[float | None, float | None, float | None, float | None, bool]:
    """Return ``(tin, hin, tout_raw, hout_raw, outdoor_stale)`` from Redis."""
    indoor = await redis_state_service.get_indoor_state(org_id)
    outdoor = await redis_state_service.get_outdoor_state(org_id)
    stale = await redis_state_service.is_outdoor_stale(org_id)
    tin = float(indoor["t"]) if indoor and "t" in indoor else None
    hin = float(indoor["h"]) if indoor and "h" in indoor else None
    tout_raw = float(outdoor["t"]) if outdoor and "t" in outdoor else None
    hout_raw = float(outdoor["h"]) if outdoor and "h" in outdoor else None
    return tin, hin, tout_raw, hout_raw, stale


def _prev_mode_from_state(indoor: dict | None) -> AcMode:
    raw = indoor.get("mode") if indoor else None
    try:
        return AcMode(str(raw)) if raw is not None else AcMode.OFF
    except ValueError:
        return AcMode.OFF


def _prev_setpoint_from_state(indoor: dict | None) -> int | None:
    raw = indoor.get("setpoint") if indoor else None
    return int(raw) if raw is not None else None


async def handle_telemetry(client, topic: ParsedTopic, payload: dict) -> None:
    """Ingest one outdoor/indoor telemetry reading and drive the comfort loop."""
    temp, humidity = _validate_ranges(payload)
    raw_ts = payload.get("ts")
    ts = (
        datetime.fromtimestamp(raw_ts, tz=timezone.utc)
        if raw_ts and raw_ts >= _MIN_PLAUSIBLE_EPOCH
        else datetime.now(timezone.utc)
    )

    async with AsyncSessionLocal() as session:
        set_current_org(topic.org_id)

        # Verifies the node really belongs to the org named in the topic — a
        # mistyped ORG_ID would otherwise file this reading under a stranger's
        # household and drive THEIR comfort engine.
        device = await telemetry_service.get_device_for_topic(
            session, topic.org_id, topic.device_uuid
        )
        if device is None:
            logger.warning("No device registered for uuid=%s (org=%s)", topic.device_uuid, topic.org_id)
            return
        # Three node kinds now, and only ONE of them may advance the outdoor
        # running mean. Deriving that from "not indoor" (as this did while the
        # household had exactly two nodes) would let all four room sensors feed
        # indoor temperatures into the outdoor EMA — the setpoint would then
        # track the air conditioner's own output instead of the weather.
        is_outdoor = device.node_type == NodeType.outdoor
        is_room = device.node_type == NodeType.room

        # Set BEFORE persist_telemetry: that call commits, so the device row is
        # flushed in the same transaction as the reading.
        mac = parse_mac(payload.get("mac"))
        if mac is not None and device.mac != mac:
            device.mac = mac
            logger.info("Device %s reported MAC %012X", topic.device_uuid, mac)

        await telemetry_service.persist_telemetry(
            session,
            device_id=device.id,
            ts=ts,
            temp=temp,
            humidity=humidity,
            rssi=int(payload.get("rssi", 0)),
            batt=payload.get("batt"),
            watt=payload.get("watt"),
        )

        if is_outdoor:
            await redis_state_service.set_outdoor_state(topic.org_id, {"t": temp, "h": humidity})
        elif is_room:
            # A corner sensor is never the indoor reading on its own — it is one
            # vote. Record the vote, then re-derive the household's indoor value
            # from every fresh corner and write THAT into state:indoor, which is
            # the only thing comfort_engine reads. Everything downstream stays
            # untouched by the sensor count changing from 1 to 4 to 3-and-a-dead-one.
            await redis_room_state_service.set_room_state(
                topic.org_id, str(device.id), temp, humidity
            )
            rooms = await redis_room_state_service.aggregate_rooms(topic.org_id)
            if rooms is None:
                # Unreachable in practice (we just wrote a fresh sample), but the
                # alternative to checking is writing None into the state hash and
                # having the comfort engine read the string "None" as a temperature.
                logger.warning("Org %s: no fresh room sensors right after ingest", topic.org_id)
                return
            await redis_state_service.set_indoor_state(
                topic.org_id, {"t": rooms.temp, "h": rooms.humidity}
            )
            if rooms.stale:
                logger.info(
                    "Org %s indoor from %d room sensor(s), %d stale (>%.0fs)",
                    topic.org_id, rooms.used, rooms.stale,
                    redis_room_state_service.ROOM_MAX_AGE_SEC,
                )
        else:
            # node_type=indoor: the gateway. Post-split firmware publishes no
            # t/h of its own, so this branch only fires for boards still running
            # the older firmware that read a DHT22 on the panel itself.
            await redis_state_service.set_indoor_state(topic.org_id, {"t": temp, "h": humidity})

        # Realtime: nudge the WebSocket feed that this org's live state changed
        # (fresh telemetry, and a recomputed comfort preview downstream).
        await live_events.publish_change(topic.org_id)

        tin, hin, tout_raw, hout_raw, stale = await _gather_current_readings(topic.org_id)
        if tin is None or hin is None:
            logger.info("Skip compute org=%s: indoor state unknown yet", topic.org_id)
            return

        tout_ema_prev = await redis_state_service.get_tout_ema(topic.org_id)
        if tout_raw is None and tout_ema_prev is None:
            logger.info("Skip compute org=%s: no outdoor data yet", topic.org_id)
            return
        tout = tout_raw if tout_raw is not None else tout_ema_prev

        cfg_dict = await redis_config_cache.get_cfg(topic.org_id, session)
        cfg = ComfortConfig.from_dict(cfg_dict)

        indoor_state = await redis_state_service.get_indoor_state(topic.org_id)
        prev_mode = _prev_mode_from_state(indoor_state)
        prev_setpoint = _prev_setpoint_from_state(indoor_state)
        last_switch = await redis_state_service.get_last_switch(topic.org_id)
        last_switch_ts = last_switch[1] if last_switch else 0.0
        override_active = await redis_override_service.override_active(topic.org_id)
        available_temps = await ir_code_service.get_available_temps(session, topic.org_id)

        # H1 fix: only the outdoor node's own tick may advance/persist the
        # running-mean EMA. Indoor/room ticks (now ~4x more frequent still,
        # one per corner) trigger compute() every time but must reuse the
        # already-persisted EMA unchanged — see comfort_engine.compute()'s
        # ``is_outdoor_tick`` gate.
        is_outdoor_tick = is_outdoor

        inputs = ComfortInputs(
            cfg=cfg,
            tin=tin,
            hin=hin,
            tout=tout,
            tout_ema_prev=tout_ema_prev,
            outdoor_stale=stale,
            prev_mode=prev_mode,
            last_switch_ts=last_switch_ts,
            now=datetime.now(timezone.utc),
            override_active=override_active,
            available_ir_temps=available_temps,
            is_outdoor_tick=is_outdoor_tick,
        )

        try:
            result = compute(inputs)
        except NoIrCodesError:
            logger.warning("No IR codes captured yet org=%s — auto-control gated off", topic.org_id)
            return

        if result is None:
            return  # override active — auto-control skipped this cycle

        if is_outdoor_tick:
            await redis_state_service.set_tout_ema(topic.org_id, result.new_ema)

        changed = result.mode != prev_mode or prev_setpoint != result.t_set
        if not changed:
            return

        # ONE comfort decision per household, delivered to the ONE node holding
        # the IR blaster. Which node that is no longer follows from which node
        # sent this reading: a room sensor has no IR hardware, so resolving the
        # target must always go through the gateway lookup.
        gateway = await telemetry_service.get_gateway_device(session, topic.org_id)
        if gateway is None:
            logger.warning(
                "No gateway (indoor/master) device registered for org=%s — cannot publish command",
                topic.org_id,
            )
            return

        await command_publisher.publish_command(
            client,
            session,
            org_id=topic.org_id,
            device_id=gateway.id,
            device_uuid=gateway.device_uuid,
            result=result,
            t_out=tout_raw if tout_raw is not None else tout,
            # M1 fix: audit trail must reflect reality. Unlike t_out (which
            # has a defensible EMA fallback), there is no outdoor-humidity
            # equivalent to fall back to — falling back to indoor RH here
            # would silently mislabel the decision-history record as if it
            # were an outdoor reading. Write None (comfort_log.h_out is
            # nullable) instead of a fabricated value.
            h_out=hout_raw,
            t_in=tin,
            h_in=hin,
            # Only a real mode switch restarts the compressor dwell timer; a
            # setpoint-only change must not (it would freeze mode switching).
            mode_changed=result.mode != prev_mode,
        )
