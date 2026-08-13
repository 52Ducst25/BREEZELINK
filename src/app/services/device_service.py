"""Device registration/listing for the ``/devices`` API (Phase 5).

A device IS an MQTT identity (see ``models/device.py``): registration
generates ``device_uuid``/``mqtt_token`` server-side — the client only
supplies ``name``/``node_type``/``location``. Credentials are never returned
by list/read endpoints (only used for firmware provisioning out-of-band).
"""

import secrets
import uuid

from sqlalchemy import select, update
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.exceptions import NotFoundError
from app.models.device import Device
from app.models.enums import NodeRole, NodeType


async def create_device(
    session: AsyncSession,
    *,
    org_id: uuid.UUID,
    name: str,
    node_type: NodeType,
    location: str | None = None,
) -> Device:
    """Build a device with fresh MQTT credentials and flush — no commit.

    Split out of ``register_device`` for the vendor's "sell a customer N
    nodes" flow, which creates org + config + nodes + codes and must commit
    them as one unit: a mid-way commit would leave a half-built customer
    behind if a later step failed.
    """
    device = Device(
        org_id=org_id,
        name=name,
        node_type=node_type,
        location=location,
        device_uuid=uuid.uuid4().hex,
        mqtt_token=secrets.token_urlsafe(32),
    )
    session.add(device)
    await session.flush()
    return device


async def register_device(
    session: AsyncSession,
    *,
    org_id: uuid.UUID,
    name: str,
    node_type: NodeType,
    location: str | None = None,
) -> Device:
    """Create a new device row with freshly generated MQTT credentials."""
    device = await create_device(
        session, org_id=org_id, name=name, node_type=node_type, location=location
    )
    await session.commit()
    return device


async def update_device(
    session: AsyncSession,
    device: Device,
    *,
    name: str | None = None,
    node_type: NodeType | None = None,
    location: str | None = None,
) -> Device:
    """Edit the human-facing fields. Caller commits.

    device_uuid/mqtt_token are deliberately not editable: they are the node's
    MQTT identity, already flashed into firmware. Rotating them from an edit
    form would silently disconnect the hardware.
    """
    if name is not None:
        device.name = name
    if node_type is not None:
        device.node_type = node_type
    if location is not None:
        device.location = location or None
    await session.flush()
    return device


async def set_role(session: AsyncSession, device: Device, role: NodeRole | None) -> Device:
    """Assign the network role. Caller commits.

    At most ONE master per household: promoting a node to master demotes any
    OTHER current master in the same org to slave in the same flush, so the
    "which node bridges to the cloud" answer is always unambiguous.

    A room sensor can never be master. It reaches the gateway over BLE and never
    opens an MQTT session, so making it master would point every outgoing
    command at a node that is not subscribed — the commands would be published
    successfully, never executed, and never nacked. Refusing here is the only
    place that catches a mis-click on the admin form.
    """
    if role == NodeRole.master and device.node_type == NodeType.room:
        raise ValueError(
            "Node cảm biến phòng (room) không thể làm master — nó không nối MQTT. "
            "Chọn node trong nhà (gateway) làm master."
        )
    if role == NodeRole.master:
        await session.execute(
            update(Device)
            .where(
                Device.org_id == device.org_id,
                Device.id != device.id,
                Device.role == NodeRole.master,
            )
            .values(role=NodeRole.slave)
        )
    device.role = role
    await session.flush()
    return device


async def rotate_credentials(session: AsyncSession, device: Device) -> Device:
    """Issue a fresh device_uuid + mqtt_token. Caller commits.

    For re-flashing or replacing a board: the OLD credentials stop working the
    moment this commits, so only run it when the firmware is about to be
    re-provisioned with the new values (the provisioning page shows them).
    """
    device.device_uuid = uuid.uuid4().hex
    device.mqtt_token = secrets.token_urlsafe(32)
    await session.flush()
    return device


async def delete_device(session: AsyncSession, device: Device) -> None:
    """Remove a node. Caller commits."""
    await session.delete(device)
    await session.flush()


async def list_devices(session: AsyncSession, org_id: uuid.UUID) -> list[Device]:
    """All devices for one org, oldest first (outdoor+indoor pair)."""
    stmt = select(Device).where(Device.org_id == org_id).order_by(Device.created_at.asc())
    return list((await session.execute(stmt)).scalars().all())


async def get_device(session: AsyncSession, org_id: uuid.UUID, device_id: uuid.UUID) -> Device:
    """Org-scoped single device lookup — raises if missing/foreign-org."""
    stmt = select(Device).where(Device.org_id == org_id, Device.id == device_id)
    device = (await session.execute(stmt)).scalar_one_or_none()
    if device is None:
        raise NotFoundError("Device not found")
    return device


async def list_all_devices(session: AsyncSession) -> list[Device]:
    """Every device of every org — the vendor's fleet view.

    NOT org-scoped, unlike everything else here. Only reachable behind
    web/dependencies.require_sysadmin; never call it from a customer-facing
    path or one household will read another's hardware.

    Returns bare devices: no model in this project declares a ``relationship``
    (every association is an explicit query), so the caller pairs these with
    their organizations itself — see web/fleet_view, which does it with one
    grouped lookup rather than one per device.
    """
    stmt = select(Device).order_by(Device.org_id, Device.created_at.asc())
    return list((await session.execute(stmt)).scalars().all())


async def get_any_device(session: AsyncSession, device_id: uuid.UUID) -> Device:
    """Cross-org single device lookup for vendor staff. See list_all_devices."""
    device = await session.get(Device, device_id)
    if device is None:
        raise NotFoundError("Device not found")
    return device
