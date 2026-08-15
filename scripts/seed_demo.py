"""Seed the demo org's owner user + 2 devices (outdoor/indoor) for the
software-in-the-loop (SIL) demo. Idempotent — safe to re-run.

Run inside the api container (has DB + models on path):
    docker compose -f docker/docker-compose.yml exec -T api python scripts/seed_demo.py

The demo organization + default ``configs`` row are already seeded by the
initial Alembic migration; this only adds a login user + the two devices the
telemetry handler needs to resolve ``(org, node) -> device_id``.
"""

import asyncio
import os
import secrets
import uuid

from sqlalchemy import select

from app.core.database import AsyncSessionLocal
from app.core.security import hash_password
from app.models.device import Device, DeviceStatus
from app.models.enums import NodeType
from app.models.organization import Organization
from app.models.user import User, UserRole

DEMO_ORG_ID = uuid.UUID("00000000-0000-0000-0000-000000000001")

# KHONG CO MAT KHAU MAC DINH, VA DAY LA MOT QUYET DINH BAO MAT.
#
# Ban truoc ghi thang mot mat khau co dinh vao day, kem chu thich "dev-only".
# Chu thich do khong rang buoc duoc gi: script chay duoc o bat ky dau co bien
# moi truong tro toi CSDL, ke ca may chu that. Va khi repo cong khai thi cap
# dang nhap cua mot tai khoan ROLE=OWNER nam san trong ma nguon cho bat ky ai
# doc — khong con la "chi dung khi phat trien" nua.
#
# De trong thi main() dung ngay voi mot dong noi ro thieu bien nao, chu KHONG
# chay tiep bang mot gia tri doan. Cung luat da ap cho EDGE_ORG_ID ben edge-ai.
DEMO_EMAIL = os.getenv("DEMO_EMAIL", "demo@example.com")
DEMO_PASSWORD = os.getenv("DEMO_PASSWORD", "")


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
    # KHONG in mat khau ra. Nguoi chay script vua tu dat no qua bien moi truong
    # nen ho da biet; con stdout cua container thi di thang vao nhat ky, noi no
    # nam lai rat lau sau khi ai do quen mat.
    print(f"user created: {DEMO_EMAIL} (role=owner)")


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
        # TOKEN SINH NGAU NHIEN, khong con suy ra duoc tu ten node nhu ban truoc.
        # Day la mat khau MQTT that cua thiet bi: doan duoc no la publish duoc
        # so do gia len topic cua ho do, hoac doc lenh dieu khien may lanh.
        # Ghi cung trong mot repo cong khai nghia la ai cung biet truoc.
        #
        # In ra MOT LAN o day vi day la lan duy nhat doc duoc: cot trong CSDL la
        # thu de nap vao config.h cua bo, khong co man hinh nao hien lai.
        token = secrets.token_hex(16)
        session.add(
            Device(
                org_id=DEMO_ORG_ID,
                name=f"Demo {node.value} node",
                node_type=node,
                device_uuid=f"demo-{node.value}",
                mqtt_token=token,
                status=DeviceStatus.offline,
            )
        )
        print(f"device created: {node.value} (device_uuid=demo-{node.value} mqtt_token={token})")


async def main() -> None:
    # Dung TRUOC khi mo ket noi CSDL: khong co ly do gi de cham vao co so du lieu
    # roi moi phat hien thieu cau hinh.
    if not DEMO_PASSWORD:
        print("ERROR: thieu DEMO_PASSWORD — dat mot mat khau roi chay lai, vi du:\n"
              "  DEMO_PASSWORD='...' python scripts/seed_demo.py\n"
              "Script CO Y khong co mat khau mac dinh: mot cap dang nhap role=owner\n"
              "ghi cung trong ma nguon la thu ai doc repo cung dung duoc.")
        return

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
