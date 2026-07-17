"""MQTT consumer — bidirectional ingest + command dispatch (Phase 4).

Subscribes to all 4 inbound topic kinds (telemetry, state, status, learn)
across every org/node (wildcarded — a single worker process serves every
tenant) and routes each message to its handler by parsed topic ``kind``.
Every message is isolated in its own try/except so one bad/absurd payload
never kills the consume loop (design §5 risk).
"""

import asyncio
import json
import logging

import aiomqtt

from app.core.mqtt import build_mqtt_client
from app.utils import mqtt_naming
from app.utils.mqtt_naming import TopicKind
from app.workers.handlers import learn_handler, state_handler, status_handler, telemetry_handler

logger = logging.getLogger("breezelink.worker.consumer")

_HANDLERS = {
    TopicKind.telemetry.value: telemetry_handler.handle_telemetry,
    TopicKind.state.value: state_handler.handle_state,
    TopicKind.status.value: status_handler.handle_status,
    TopicKind.learn.value: learn_handler.handle_learn,
}

_STATUS_TOPIC_KIND = TopicKind.status.value

# H3 fix: capped exponential backoff for the outer reconnect loop.
_RECONNECT_INITIAL_DELAY_S = 5
_RECONNECT_MAX_DELAY_S = 60


def _build_will() -> aiomqtt.Will:
    """Broker-published LWT: any consumer disconnect looks like the worker
    itself going offline on its own status topic (org 'worker', no real node).
    """
    return aiomqtt.Will(topic="bl/worker/consumer/status", payload=b"offline", qos=1, retain=True)


async def run_consumer() -> None:
    """Connect, subscribe every inbound pattern, and dispatch forever.

    H3 fix: wrapped in an outer reconnect loop with capped exponential
    backoff. Without it, a single transient broker blip/network drop ended
    ``client.messages`` and let ``run_consumer()`` return normally, which
    silently killed the whole comfort-control loop until a human/orchestrator
    restarted the worker (design §5 risk). ``asyncio.CancelledError`` from a
    graceful shutdown (SIGTERM/SIGINT, see ``workers/manager.py``) is a
    different exception than ``aiomqtt.MqttError`` and is intentionally NOT
    caught here — it propagates straight out so shutdown stays clean and does
    not trigger a reconnect attempt.
    """
    delay = _RECONNECT_INITIAL_DELAY_S
    while True:
        try:
            client = build_mqtt_client(identifier="breezelink_consumer", will=_build_will())
            async with client:
                await client.subscribe(mqtt_naming.TELEMETRY_WILDCARD, qos=1)
                await client.subscribe(mqtt_naming.STATE_WILDCARD, qos=1)
                await client.subscribe(mqtt_naming.STATUS_WILDCARD, qos=1)
                await client.subscribe(mqtt_naming.LEARN_WILDCARD, qos=1)
                logger.info("MQTT consumer subscribed; awaiting messages")
                delay = _RECONNECT_INITIAL_DELAY_S  # reset backoff after a clean (re)connect
                async for message in client.messages:
                    await _dispatch(client, message)
        except aiomqtt.MqttError as exc:
            logger.warning("MQTT connection lost (%s) — reconnecting in %ss", exc, delay)
            await asyncio.sleep(delay)
            delay = min(delay * 2, _RECONNECT_MAX_DELAY_S)


def _decode_payload(kind: str, raw: bytes | str) -> dict | str:
    """JSON-decode every kind except ``status`` (a plain LWT literal)."""
    if kind == _STATUS_TOPIC_KIND:
        return raw.decode() if isinstance(raw, (bytes, bytearray)) else str(raw)
    return json.loads(raw)


async def _dispatch(client: aiomqtt.Client, message: aiomqtt.Message) -> None:
    """Parse topic + payload, route to a handler; never raise past here."""
    topic_str = str(message.topic)
    try:
        parsed = mqtt_naming.parse(topic_str)
    except ValueError:
        logger.warning("Ignoring unrecognized topic: %s", topic_str)
        return

    handler = _HANDLERS.get(parsed.kind)
    if handler is None:
        logger.debug("No handler for kind=%s (topic=%s)", parsed.kind, topic_str)
        return

    try:
        payload = _decode_payload(parsed.kind, message.payload)
    except (json.JSONDecodeError, TypeError, UnicodeDecodeError, ValueError) as exc:
        logger.warning("Bad payload on %s: %s", topic_str, exc)
        return

    try:
        await handler(client, parsed, payload)
    except Exception:  # noqa: BLE001 — never crash the consume loop (design §5 risk)
        logger.exception("Handler failed for topic=%s", topic_str)
