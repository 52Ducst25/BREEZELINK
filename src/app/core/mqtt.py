"""MQTT client factory (aiomqtt 2.x).

Thin wrapper: builds a configured ``aiomqtt.Client`` from Settings, with MQTTS
(TLS) support for the prod EMQX Serverless broker. The worker owns the
connection lifecycle (``async with client:``); the API process stays MQTT-free.
"""

import ssl

import aiomqtt

from app.config import get_settings


def build_mqtt_client(
    *,
    identifier: str | None = None,
    will: aiomqtt.Will | None = None,
) -> aiomqtt.Client:
    """Return a configured (not yet connected) aiomqtt Client.

    Args:
        identifier: MQTT client id. Defaults to a worker id.
        will: Optional Last Will & Testament (used for device online/offline).
    """
    settings = get_settings()
    tls_params = None
    if settings.mqtt_tls:
        # System CA trust store; EMQX Serverless serves a publicly-trusted cert.
        tls_params = aiomqtt.TLSParameters(cert_reqs=ssl.CERT_REQUIRED)

    return aiomqtt.Client(
        hostname=settings.mqtt_host,
        port=settings.mqtt_port,
        username=settings.mqtt_user,
        password=settings.mqtt_pass,
        identifier=identifier or "aircon_worker",
        keepalive=settings.mqtt_keepalive,
        will=will,
        tls_params=tls_params,
    )
