"""Seed the demo org's owner user + 2 devices (outdoor/indoor) for the
software-in-the-loop (SIL) demo. Idempotent — safe to re-run.

Run inside the api container (has DB + models on path):
    docker compose -f docker/docker-compose.yml exec -T api python scripts/seed_demo.py

The demo organization + default ``configs`` row are already seeded by the
initial Alembic migration; this only adds a login user + the two devices the
telemetry handler needs to resolve ``(org, node) -> device_id``.
"""

import asyncio
import uuid

from sqlalchemy import select

from app.core.database import AsyncSessionLocal
from app.core.security import hash_password
from app.models.device import Device, DeviceStatus
from app.models.enums import NodeType
from app.models.organization import Organization
from app.models.user import User, UserRole

DEMO_ORG_ID = uuid.UUID("00000000-0000-0000-0000-000000000001")
DEMO_EMAIL = "demo@vi-du.com"
DEMO_PASSWORD = "DEMO_PASSWORD"  # dev-only demo credential


async def _seed_user(session) -> None:
    existing = (
        await session.execute(
            select(User).where(User.org_id == DEMO_ORG_ID, User.email == DEMO_EMAIL)
        )
    ).scalar_one_or_none()
    if existing is not None:
        print(f"user exists: {DEMO_EMAIL}")
        return
    session.add(
        User(
            org_id=DEMO_ORG_ID,
            email=DEMO_EMAIL,
            full_name="Demo Admin",
            password_hash=hash_password(DEMO_PASSWORD),
            role=UserRole.owner,
            is_active=True,
        )
    )
    print(f"user created: {DEMO_EMAIL} / {DEMO_PASSWORD} (role=owner)")


async def _seed_devices(session) -> None:
    for node in (NodeType.outdoor, NodeType.indoor):
        existing = (
            await session.execute(
                select(Device).where(Device.org_id == DEMO_ORG_ID, Device.node_type == node)
            )
        ).scalar_one_or_none()
        if existing is not None:
            print(f"device exists: {node.value}")
            continue
        session.add(
            Device(
                org_id=DEMO_ORG_ID,
                name=f"Demo {node.value} node",
                node_type=node,
                device_uuid=f"demo-{node.value}",
                mqtt_token=f"tok-{node.value}",
                status=DeviceStatus.offline,
            )
        )
        print(f"device created: {node.value} (device_uuid=demo-{node.value})")


async def main() -> None:
    async with AsyncSessionLocal() as session:
        org = await session.get(Organization, DEMO_ORG_ID)
        if org is None:
            print("ERROR: demo org missing — run 'alembic upgrade head' first")
            return
        await _seed_user(session)
        await _seed_devices(session)
        await session.commit()
    print("seed done")


if __name__ == "__main__":
    asyncio.run(main())
