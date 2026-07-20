"""add telemetry.watt (instantaneous AC power draw)

Revision ID: c2e7f19a4d6b
Revises: a1c9e42f7b83
Create Date: 2026-07-20

Nullable, no backfill: real values arrive only once a node carries a power
sensor and the firmware reports "watt". energy_service integrates the samples
into kWh for the app's energy screen. See models/telemetry.watt.
"""

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op

revision: str = "c2e7f19a4d6b"
down_revision: Union[str, None] = "a1c9e42f7b83"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.add_column("telemetry", sa.Column("watt", sa.REAL(), nullable=True))


def downgrade() -> None:
    op.drop_column("telemetry", "watt")
