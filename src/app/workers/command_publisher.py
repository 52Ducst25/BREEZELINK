"""Build + publish one AC command downstream, then persist the audit trail
(design §1.2 step E / §2.2, phase-04 step 5).

Called by ``telemetry_handler`` exactly when the comfort engine's decided
(mode, setpoint) differs from the last known indoor state — never on every
telemetry tick (command-storm guard, on top of Phase 3's DWELL).
"""

import json
import logging
import uuid
from datetime import datetime, timezone

import aiomqtt
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.enums import AcMode, CommandSource
from app.schemas.comfort import ComfortResult
from app.services import command_service, comfort_log_service, ir_code_service, redis_ir_cache, redis_state_service
from app.utils import mqtt_naming

logger = logging.getLogger("aircon.worker.command_publisher")


async def _resolve_ir_raw(session: AsyncSession, ir_code_id: uuid.UUID) -> list[int] | None:
    """Return raw timing to attach to the cmd payload, only if not yet cached
    on the node (design §2.2: first command for a code includes ``ir_raw``;
    subsequent ones rely on the node's own NVS cache).

    Read-only by design (C1 fix): does NOT write the cache itself. Caching
    "the node has it now" before the MQTT publish has actually gone out
    would wrongly mark undelivered codes as delivered on a publish failure.
    Callers must call :func:`_mark_ir_delivered` themselves, and only after
    ``client.publish`` returns successfully.
    """
    cached = await redis_ir_cache.get_ir(str(ir_code_id))
    if cached and cached.get("raw_timing"):
        return None  # node (and our mirror) already has it — omit from payload

    return await ir_code_service.get_raw_timing(session, ir_code_id)


async def _mark_ir_delivered(ir_code_id: uuid.UUID, raw_timing: list[int]) -> None:
    """Mark a code as delivered to the node — call only after the cmd
    carrying ``ir_raw`` has actually been published (see C1 fix note above).
    """
    await redis_ir_cache.put_ir(str(ir_code_id), {"raw_timing": raw_timing})


async def publish_command(
    client: aiomqtt.Client,
    session: AsyncSession,
    *,
    org_id: str,
    device_id: uuid.UUID,
    result: ComfortResult,
    t_out: float,
    h_out: float | None,
    t_in: float,
    h_in: float,
) -> None:
    """Publish ``bl/{org}/indoor/cmd`` (QoS1, retain OFF) and persist
    ``commands`` + ``comfort_log`` rows for the same decision.
    """
    now = datetime.now(timezone.utc)
    req_id = f"c-{uuid.uuid4().hex[:8]}"

    ir_code_id = await ir_code_service.find_ir_code_id(session, org_id, result.mode, result.t_set)
    ir_raw = await _resolve_ir_raw(session, ir_code_id) if ir_code_id is not None else None
    if ir_code_id is None:
        logger.warning(
            "No learned IR code for org=%s mode=%s temp=%s — command sent without ir_code_id",
            org_id, result.mode.value, result.t_set,
        )

    cmd_payload: dict = {
        "req_id": req_id,
        "mode": result.mode.value,
        "setpoint": result.t_set,
        "ir_code_id": str(ir_code_id) if ir_code_id else None,
        "reason": f"auto:{result.mode.value}@{result.t_set}",
    }
    if ir_raw:
        cmd_payload["ir_raw"] = ir_raw

    topic = mqtt_naming.topic(org_id, "indoor", "cmd")
    await client.publish(topic, json.dumps(cmd_payload), qos=1, retain=False)
    if ir_raw and ir_code_id is not None:
        await _mark_ir_delivered(ir_code_id, ir_raw)

    await command_service.insert_command(
        session,
        device_id=device_id,
        ts=now,
        mode=result.mode,
        setpoint=result.t_set,
        ir_code_id=ir_code_id,
        source=CommandSource.auto,
        req_id=req_id,
    )
    await comfort_log_service.insert_comfort_log(
        session,
        org_id=uuid.UUID(org_id),
        ts=now,
        t_out=t_out,
        h_out=h_out,
        t_in=t_in,
        h_in=h_in,
        result=result,
    )
    await redis_state_service.set_last_switch(org_id, result.mode.value, now.timestamp())
    await redis_state_service.set_indoor_state(org_id, {"mode": result.mode.value, "setpoint": result.t_set})


async def publish_manual_command(
    client: aiomqtt.Client,
    session: AsyncSession,
    *,
    org_id: str,
    device_id: uuid.UUID,
    mode: AcMode,
    setpoint: int,
) -> dict:
    """Publish a user-issued manual override command (Phase 5 REST endpoint).

    Unlike :func:`publish_command`, this does NOT write a ``comfort_log`` row
    — that table audits the *automatic* algorithm's reasoning (design §3.1),
    which does not apply to a direct user tap. It still records a
    ``commands`` row with ``source=manual`` for history/ack matching.
    """
    now = datetime.now(timezone.utc)
    req_id = f"c-{uuid.uuid4().hex[:8]}"

    ir_code_id = await ir_code_service.find_ir_code_id(session, org_id, mode, setpoint)
    ir_raw = await _resolve_ir_raw(session, ir_code_id) if ir_code_id is not None else None
    if ir_code_id is None:
        logger.warning(
            "No learned IR code for manual org=%s mode=%s temp=%s — command sent without ir_code_id",
            org_id, mode.value, setpoint,
        )

    cmd_payload: dict = {
        "req_id": req_id,
        "mode": mode.value,
        "setpoint": setpoint,
        "ir_code_id": str(ir_code_id) if ir_code_id else None,
        "reason": "manual override",
    }
    if ir_raw:
        cmd_payload["ir_raw"] = ir_raw

    topic = mqtt_naming.topic(org_id, "indoor", "cmd")
    await client.publish(topic, json.dumps(cmd_payload), qos=1, retain=False)
    if ir_raw and ir_code_id is not None:
        await _mark_ir_delivered(ir_code_id, ir_raw)

    await command_service.insert_command(
        session,
        device_id=device_id,
        ts=now,
        mode=mode,
        setpoint=setpoint,
        ir_code_id=ir_code_id,
        source=CommandSource.manual,
        req_id=req_id,
    )
    await redis_state_service.set_last_switch(org_id, mode.value, now.timestamp())
    await redis_state_service.set_indoor_state(org_id, {"mode": mode.value, "setpoint": setpoint})
    return cmd_payload


async def publish_ir_action(
    client: aiomqtt.Client,
    session: AsyncSession,
    *,
    org_id: str,
    device_id: uuid.UUID,
    action: str,
    raw_timing: list[int],
    mode: AcMode,
    setpoint: int,
) -> None:
    """Blast a standalone learned action IR frame (e.g. FAN_SPEED) once.

    Unlike the (mode, setpoint) commands, this does NOT change the operating
    mode/setpoint — it just replays one learned button, so the AC advances its
    own internal fan speed. The current ``mode``/``setpoint`` are carried only
    as metadata so the node's state echo stays consistent; Redis indoor-state
    and last_switch are deliberately left untouched. ``ir_raw`` is always sent
    inline (action codes are not NVS-cached by id — there is no ir_code_id).
    """
    now = datetime.now(timezone.utc)
    req_id = f"c-{uuid.uuid4().hex[:8]}"

    cmd_payload = {
        "req_id": req_id,
        "mode": mode.value,
        "setpoint": setpoint,
        "ir_code_id": None,
        "ir_raw": raw_timing,
        "reason": f"action:{action}",
    }
    topic = mqtt_naming.topic(org_id, "indoor", "cmd")
    await client.publish(topic, json.dumps(cmd_payload), qos=1, retain=False)

    await command_service.insert_command(
        session,
        device_id=device_id,
        ts=now,
        mode=mode,
        setpoint=setpoint,
        ir_code_id=None,
        source=CommandSource.manual,
        req_id=req_id,
    )
