"""add users.is_sysadmin — vendor staff flag

Revision ID: a71e5d0c9b64
Revises: c3d8b6a91f42
Create Date: 2026-07-17

Cross-org authority for the vendor's customer list. See models/user.is_sysadmin
for why this is a column and not a third ``user_role`` enum value.

server_default="false" so the column can be added NOT NULL to a table that
already has rows — existing users are customers, which is the correct reading:
nobody becomes vendor staff by accident. The default is then dropped so the
value has to come from the model, not silently from the schema.
"""

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op

revision: str = "a71e5d0c9b64"
down_revision: Union[str, None] = "c3d8b6a91f42"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.add_column(
        "users",
        sa.Column("is_sysadmin", sa.Boolean(), nullable=False, server_default=sa.false()),
    )
    op.alter_column("users", "is_sysadmin", server_default=None)


def downgrade() -> None:
    op.drop_column("users", "is_sysadmin")
