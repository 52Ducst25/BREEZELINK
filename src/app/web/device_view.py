"""Shared builder for the device-detail page.

Two routes render the same page from different angles: a household member
opening their own node (/web/devices/{id}) and vendor staff opening a
customer's node (/web/customers/{org}/devices/{id}). The only difference is
WHICH org the caller is allowed to reach — the readings, charts and history are
identical work.

It lives here rather than being copy-pasted into the second route because the
two would drift: the setpoint line is only drawn for indoor nodes, and that
rule silently becoming true in one view and not the other is exactly the kind
of split-brain rendering this project already got bitten by (see app.js's
docstring on why the SSR admin refetches instead of re-rendering in JS).

``org_id`` is passed in, never read off the device: callers must have already
proven the device belongs to an org they may see.
"""

import uuid

from sqlalchemy.ext.asyncio import AsyncSession

from app.models.device import Device
from app.services import comfort_preview_service, telemetry_service
from app.web import charts

_HISTORY_ROWS = 60
_CHART_SAMPLES = 120


async def build_context(session: AsyncSession, org_id: uuid.UUID, device: Device) -> dict:
    """Readings + chart series + history table for one node."""
    readings = await telemetry_service.list_telemetry(
        session, device_id=device.id, limit=_CHART_SAMPLES
    )

    # A setpoint reference line only makes sense on the indoor node's temp
    # chart (outdoor has no target — see dashboard_view.classify_outdoor).
    setpoint = None
    if device.node_type.value == "indoor":
        comfort = await comfort_preview_service.build_preview(session, str(org_id))
        setpoint = comfort.t_set

    return {
        "device": device,
        "latest": readings[-1] if readings else None,
        "temp_series": charts.build_series([r.temp for r in readings], setpoint, "°C"),
        "hum_series": charts.build_series([r.humidity for r in readings], None, "%"),
        "history": list(reversed(readings))[:_HISTORY_ROWS],  # newest-first for the table
        "sample_count": len(readings),
    }
