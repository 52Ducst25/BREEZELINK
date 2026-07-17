"""add invite_codes table

Revision ID: 9f2a7c4e1b83
Revises: 0784163c81b9
Create Date: 2026-07-17

Backs the vendor's customer flow: registration stops creating organizations on
demand and instead redeems a code that points at an org the vendor already
created (see models/invite_code.py).

``role`` reuses the EXISTING ``user_role`` enum with ``create_type=False``.
Without that flag Alembic emits a second ``CREATE TYPE user_role`` — the type
was already created by 0784163c81b9 for ``users.role`` — and the migration
dies with "type user_role already exists". For the same reason ``downgrade``
drops only the table: dropping the enum here would break ``users.role``.

Adds no data. Existing orgs/users are untouched; codes are issued from the
admin UI afterwards.
"""

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op
from sqlalchemy.dialects import postgresql

revision: str = "9f2a7c4e1b83"
down_revision: Union[str, None] = "0784163c81b9"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.create_table(
        "invite_codes",
        sa.Column("id", sa.Uuid(), nullable=False),
        sa.Column("code", sa.String(length=16), nullable=False),
        sa.Column("org_id", sa.Uuid(), nullable=False),
        sa.Column(
            "role",
            postgresql.ENUM("owner", "member", name="user_role", create_type=False),
            nullable=False,
        ),
        sa.Column("used_by_user_id", sa.Uuid(), nullable=True),
        sa.Column("used_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.ForeignKeyConstraint(["org_id"], ["organizations.id"], ondelete="CASCADE"),
        sa.ForeignKeyConstraint(["used_by_user_id"], ["users.id"], ondelete="SET NULL"),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index(op.f("ix_invite_codes_org_id"), "invite_codes", ["org_id"])
    # Unique, not just indexed: redemption resolves a code -> exactly one org.
    op.create_index(op.f("ix_invite_codes_code"), "invite_codes", ["code"], unique=True)


def downgrade() -> None:
    op.drop_index(op.f("ix_invite_codes_code"), table_name="invite_codes")
    op.drop_index(op.f("ix_invite_codes_org_id"), table_name="invite_codes")
    op.drop_table("invite_codes")
