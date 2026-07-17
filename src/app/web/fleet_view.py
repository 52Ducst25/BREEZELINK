"""Row assembly for the vendor's fleet dashboard.

The vendor's landing page answers one question: "is anything I sold in
trouble right now?" — so it groups every node under its customer, sorted by
customer name, with a status summary to read at a glance.

That is a different question from the household dashboard the Flutter app
shows (which is "what is my AC doing and why", per-household comfort maths).
Trying to serve both from one template is what made the old /web dashboard
useless here: it rendered a comfort hero for the VENDOR's own org, which is
not a household anyone lives in.

Live values come from Redis per org, not from Postgres per device: that is
where the worker keeps current state (services/redis_state_service), and it
is keyed by org — so we look up each org once, not each device.
"""

import uuid
from dataclasses import dataclass, field

from sqlalchemy.ext.asyncio import AsyncSession

from app.models.device import Device, DeviceStatus
from app.models.organization import Organization
from app.services import device_service, organization_service, redis_state_service
from app.web import dashboard_view


@dataclass
class FleetNode:
    """One node, as seen by the vendor."""

    device: Device
    reading: dict | None
    level: str

    @property
    def online(self) -> bool:
        return self.device.status == DeviceStatus.online


@dataclass
class FleetGroup:
    """One customer and all their nodes — the unit the dashboard lists."""

    org: Organization
    nodes: list[FleetNode] = field(default_factory=list)

    @property
    def online_count(self) -> int:
        return sum(1 for n in self.nodes if n.online)

    @property
    def offline_count(self) -> int:
        return len(self.nodes) - self.online_count

    @property
    def all_offline(self) -> bool:
        """Every node dark. Worth surfacing: one offline node in a house is a
        node problem, the whole house offline is usually power or internet."""
        return bool(self.nodes) and self.online_count == 0


async def build(session: AsyncSession) -> dict:
    """Customers (A→Z) each with their nodes, plus a fleet-wide summary."""
    devices = await device_service.list_all_devices(session)
    orgs = {o.id: o for o in await organization_service.list_orgs(session)}

    # One Redis round-trip per ORG, not per device: both nodes of a household
    # read from the same two keys, so caching per org collapses the fan-out.
    state_cache: dict[uuid.UUID, tuple[dict | None, dict | None]] = {}
    groups: dict[uuid.UUID, FleetGroup] = {}

    for device in devices:
        org = orgs.get(device.org_id)
        if org is None:
            continue  # org deleted mid-read; the cascade will take the device

        if device.org_id not in state_cache:
            org_key = str(device.org_id)
            state_cache[device.org_id] = (
                dashboard_view.reading_from_state(
                    await redis_state_service.get_indoor_state(org_key)
                ),
                dashboard_view.reading_from_state(
                    await redis_state_service.get_outdoor_state(org_key)
                ),
            )
        indoor, outdoor = state_cache[device.org_id]
        is_outdoor = device.node_type.value == "outdoor"
        reading = outdoor if is_outdoor else indoor

        # Judged on the plain hot/cold scale, not against a comfort target: the
        # fleet page deliberately does not run the comfort pipeline per
        # household (that is the app's job) and only encodes "how hot is it".
        groups.setdefault(device.org_id, FleetGroup(org=org)).nodes.append(
            FleetNode(
                device=device,
                reading=reading,
                level=dashboard_view.classify_outdoor(reading["t"] if reading else None),
            )
        )

    # Customers with no nodes still belong on the list — "sold, nothing
    # installed yet" is exactly the state a vendor needs to chase.
    for org in orgs.values():
        groups.setdefault(org.id, FleetGroup(org=org))

    # Alphabetical BY CUSTOMER, case- and accent-insensitively enough for
    # Vietnamese names: casefold puts "ánh" next to "Ánh" instead of banishing
    # every uppercase name to the top, which a plain sort would do.
    ordered = sorted(groups.values(), key=lambda g: (g.org.name or "").casefold())

    nodes_total = sum(len(g.nodes) for g in ordered)
    online_total = sum(g.online_count for g in ordered)

    return {
        "groups": ordered,
        "summary": {
            "customers": len(ordered),
            "nodes": nodes_total,
            "online": online_total,
            "offline": nodes_total - online_total,
            # Precomputed so the template never does arithmetic — and so the
            # 0-node case yields 0 instead of a ZeroDivisionError.
            "online_pct": round(online_total / nodes_total * 100) if nodes_total else 0,
        },
    }
