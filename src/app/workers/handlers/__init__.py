"""MQTT message handlers — one module per topic ``kind`` (Phase 4).

Dispatched by ``workers.mqtt_consumer``; each handler owns its own
``AsyncSessionLocal`` (fresh session per message) and never raises past its
own boundary — the consumer's dispatch loop also wraps every call, but
handlers additionally guard their own DB/Redis calls so a partial failure
degrades gracefully (log + continue) instead of losing the whole message.
"""
