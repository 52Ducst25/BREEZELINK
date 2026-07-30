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


def _ac_state(state: dict | None) -> dict | None:
    """The AC's ACTUAL current (mode, setpoint) — or None if nothing set it yet.

    Lives in the same indoor hash as {t,h} (``_set_state`` uses HSET so the
    telemetry writer and the ``state`` writer merge into one key rather than
    clobbering each other), but it answers a DIFFERENT question and must not be
    confused with either of its neighbours in this payload:

      ``comfort.t_set``  what the algorithm WOULD choose — computed with
                         ``override_active=False`` on purpose (see
                         comfort_preview_service), so it keeps showing the auto
                         recommendation even while a manual override is winning.
      ``ac.setpoint``    what the unit is ACTUALLY set to right now, whoever set
                         it — the comfort loop, the app, or someone standing at
                         the wall panel.

    Without this field the app had no way to read the second one, so its dial
    fell back to a hardcoded COOL/25 and disagreed with the panel the moment
    anyone touched either surface. Reporting the recommendation as if it were
    the current setting would be worse than showing nothing.
    """
    if not state:
        return None
    mode, setpoint = state.get("mode"), state.get("setpoint")
    if mode is None or setpoint is None:
        return None
    try:
        # Redis hashes are all strings; coerce_hash may or may not have already
        # turned this one into a number. float() first so "26.0" doesn't explode.
        setpoint_int = int(float(setpoint))
    except (TypeError, ValueError):
        return None
    return {"mode": str(mode), "setpoint": setpoint_int}


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
        "ac": _ac_state(indoor),
        "devices": [DeviceRead.model_validate(d).model_dump(mode="json") for d in devices],
    }
