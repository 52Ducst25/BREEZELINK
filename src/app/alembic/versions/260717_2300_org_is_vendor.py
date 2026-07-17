"""add organizations.is_vendor — vendor back-office org flag

Revision ID: e8b4f26a913d
Revises: d5f1a83c27e9
Create Date: 2026-07-17

Separates vendor staff from customers at the org level: admins live in an
is_vendor=true org that never appears in a customer listing, instead of being
"the owner" of a demo household. See models/organization.is_vendor.

server_default="false" so the column can be added NOT NULL to a table that
already has rows — every existing org is a customer, which is the correct
reading. The default is then dropped so the value must come from the model.

Adds no data. The vendor org is created and the admin moved into it by a
separate one-off step against real data, not by this schema migration.
"""

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op

revision: str = "e8b4f26a913d"
down_revision: Union[str, None] = "d5f1a83c27e9"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.add_column(
        "organizations",
        sa.Column("is_vendor", sa.Boolean(), nullable=False, server_default=sa.false()),
    )
    op.alter_column("organizations", "is_vendor", server_default=None)


def downgrade() -> None:
    op.drop_column("organizations", "is_vendor")
