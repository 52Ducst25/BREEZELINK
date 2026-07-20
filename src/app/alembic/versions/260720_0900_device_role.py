"""add devices.role (master/slave network role)

Revision ID: a1c9e42f7b83
Revises: f3b8d5619ac2
Create Date: 2026-07-20

Network role independent of node_type: the master node bridges to the cloud
(WiFi+MQTT), slaves reach it over ESP-NOW. Nullable, no backfill — the vendor
assigns it per node during provisioning. See models/device.role.
"""

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op
from sqlalchemy.dialects import postgresql

revision: str = "a1c9e42f7b83"
down_revision: Union[str, None] = "f3b8d5619ac2"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    # Create the enum TYPE explicitly first: on the async (asyncpg) engine,
    # op.add_column does NOT auto-create it, so the ALTER TABLE fails with
    # "type node_role does not exist". create_type=False on the column stops a
    # second create attempt.
    node_role = postgresql.ENUM("master", "slave", name="node_role", create_type=False)
    node_role.create(op.get_bind(), checkfirst=True)
    op.add_column("devices", sa.Column("role", node_role, nullable=True))


def downgrade() -> None:
    op.drop_column("devices", "role")
    postgresql.ENUM(name="node_role").drop(op.get_bind(), checkfirst=True)
