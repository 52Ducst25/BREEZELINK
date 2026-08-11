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

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.models.device import Device
from app.models.enums import NodeRole, NodeType
from app.services import comfort_preview_service, telemetry_service
from app.web import charts

_HISTORY_ROWS = 60
_CHART_SAMPLES = 120


async def build_context(session: AsyncSession, org_id: uuid.UUID, device: Device) -> dict:
    """Readings + chart series + history table for one node."""
    readings = await telemetry_service.list_telemetry(
        session, device_id=device.id, limit=_CHART_SAMPLES
    )

    # A setpoint reference line only makes sense where the target applies:
    # inside. Room sensors get it too — the setpoint is what the algorithm is
    # steering THEIR readings toward, so a corner sitting stubbornly above the
    # line is the whole point of charting it separately. Outdoor has no target
    # (see dashboard_view.classify_outdoor).
    setpoint = None
    if device.node_type in (NodeType.indoor, NodeType.room):
        comfort = await comfort_preview_service.build_preview(session, str(org_id))
        setpoint = comfort.t_set

    # The household's master node — a slave's firmware pairs to it over ESP-NOW,
    # so the provisioning panel shows the master's MAC on a slave's page.
    master = (
        await session.execute(
            select(Device).where(Device.org_id == org_id, Device.role == NodeRole.master)
        )
    ).scalars().first()

    # Every corner sensor of this household, oldest first. The position in this
    # list is only a SUGGESTED ``ROOM_CORNER`` label — the node's real identity is
    # its ``device_uuid``, which travels inside every ESP-NOW packet.
    #
    # That distinction matters: two corners flashed with the same label are
    # HARMLESS (each still has its own topic and still votes in the median, the
    # panel just prints the same label twice). Suggesting a distinct number
    # anyway saves the installer from having to invent one, and makes the wall
    # display readable without looking up uuids.
    room_nodes = list(
        (
            await session.execute(
                select(Device)
                .where(Device.org_id == org_id, Device.node_type == NodeType.room)
                .order_by(Device.created_at.asc(), Device.id.asc())
            )
        ).scalars().all()
    )
    room_index = next(
        (i for i, r in enumerate(room_nodes) if r.id == device.id), None
    )

    settings = get_settings()
    return {
        "device": device,
        "room_nodes": room_nodes,
        "room_index": room_index,
        "latest": readings[-1] if readings else None,
        "temp_series": charts.build_series([r.temp for r in readings], setpoint, "°C"),
        "hum_series": charts.build_series([r.humidity for r in readings], None, "%"),
        "history": list(reversed(readings))[:_HISTORY_ROWS],  # newest-first for the table
        "sample_count": len(readings),
        # Firmware-provisioning context (sysadmin-only page).
        #
        # Địa chỉ CÔNG KHAI, không phải settings.mqtt_host: cái sau là "emqx",
        # tên service Docker mà thiết bị ngoài mạng không phân giải được. Panel
        # này tồn tại để người lắp chép thẳng vào config.h, nên in sai ở đây là
        # node không bao giờ kết nối được.
        "mqtt_host": settings.mqtt_public_host or settings.mqtt_host,
        "mqtt_port": settings.mqtt_public_port or settings.mqtt_port,
        "master_node": master,
    }
