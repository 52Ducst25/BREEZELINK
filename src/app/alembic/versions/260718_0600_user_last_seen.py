"""add users.last_seen_at

Revision ID: c4a1d8e93f27
Revises: b2f9c7e41a06
Create Date: 2026-07-18

Powers a real "Online" status for staff: updated (throttled) on every
authenticated admin request, so "online" = seen in the last couple of minutes.
See models/user.last_seen_at. Nullable, no backfill.
"""

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op

revision: str = "c4a1d8e93f27"
down_revision: Union[str, None] = "b2f9c7e41a06"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.add_column("users", sa.Column("last_seen_at", sa.DateTime(timezone=True), nullable=True))


def downgrade() -> None:
    op.drop_column("users", "last_seen_at")
