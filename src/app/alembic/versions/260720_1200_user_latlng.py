"""add users.latitude / longitude (home coordinates from the map picker)

Revision ID: d8b3f0a25e14
Revises: c2e7f19a4d6b
Create Date: 2026-07-20

WGS84 home coordinates the user picks on the OSM map. Nullable — the location
text keeps working on its own. See models/user.latitude/longitude.
"""

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op

revision: str = "d8b3f0a25e14"
down_revision: Union[str, None] = "c2e7f19a4d6b"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.add_column("users", sa.Column("latitude", sa.Numeric(9, 6), nullable=True))
    op.add_column("users", sa.Column("longitude", sa.Numeric(9, 6), nullable=True))


def downgrade() -> None:
    op.drop_column("users", "longitude")
    op.drop_column("users", "latitude")
