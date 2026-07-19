"""add users.last_login_at

Revision ID: b2f9c7e41a06
Revises: e8b4f26a913d
Create Date: 2026-07-18

Recency for the vendor staff list ("đăng nhập lần cuối / cách đây bao lâu").
Stamped in auth_service.login_user on every sign-in. Nullable with no backfill
— an account that has never signed in genuinely has no last-login, and
inventing one (e.g. created_at) would misreport it as recently active.
"""

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op

revision: str = "b2f9c7e41a06"
down_revision: Union[str, None] = "e8b4f26a913d"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.add_column("users", sa.Column("last_login_at", sa.DateTime(timezone=True), nullable=True))


def downgrade() -> None:
    op.drop_column("users", "last_login_at")
