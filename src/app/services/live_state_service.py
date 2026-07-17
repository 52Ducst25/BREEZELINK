"""Assemble the consolidated live-state payload pushed over the WebSocket feed.

Reuses the exact same services/schemas the REST endpoints use (device list,
comfort preview, Redis telemetry state) so the shape the app parses over WS is
identical to what it parses over HTTP (DRY). One snapshot carries everything the
Home + Control screens need: comfort decision, indoor/outdoor telemetry, devices.
"""

import uuid

from sqlalchemy.ext.asyncio import AsyncSession

from app.schemas.device import DeviceRead
from app.services import comfort_preview_service, device_service, redis_state_service


def _reading(state: dict | None) -> dict | None:
    """Reduce a Redis state hash to the {t, h} the app renders (or None)."""
    if not state or state.get("t") is None or state.get("h") is None:
        return None
    return {"t": float(state["t"]), "h": float(state["h"])}


async def build_live_state(session: AsyncSession, org_id: str) -> dict:
    """One JSON-serializable snapshot of everything the realtime screens show."""
    devices = await device_service.list_devices(session, uuid.UUID(org_id))
    comfort = await comfort_preview_service.build_preview(session, org_id)
    indoor = await redis_state_service.get_indoor_state(org_id)
    outdoor = await redis_state_service.get_outdoor_state(org_id)

    return {
        "type": "state",
        "comfort": comfort.model_dump(mode="json"),
        "indoor": _reading(indoor),
        "outdoor": _reading(outdoor),
        "devices": [DeviceRead.model_validate(d).model_dump(mode="json") for d in devices],
    }
