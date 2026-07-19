"""add users.avatar / avatar_mime / avatar_updated_at

Revision ID: f3b8d5619ac2
Revises: e7c1b4a20d95
Create Date: 2026-07-18

Profile picture for the SSR admin's top bar, stored in-row (see
models/user.avatar for why a column rather than a volume). All three columns
are nullable with no backfill: an account without a picture renders initials.
"""

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op

revision: str = "f3b8d5619ac2"
down_revision: Union[str, None] = "e7c1b4a20d95"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.add_column("users", sa.Column("avatar", sa.LargeBinary(), nullable=True))
    op.add_column("users", sa.Column("avatar_mime", sa.String(length=64), nullable=True))
    op.add_column(
        "users", sa.Column("avatar_updated_at", sa.DateTime(timezone=True), nullable=True)
    )


def downgrade() -> None:
    op.drop_column("users", "avatar_updated_at")
    op.drop_column("users", "avatar_mime")
    op.drop_column("users", "avatar")
