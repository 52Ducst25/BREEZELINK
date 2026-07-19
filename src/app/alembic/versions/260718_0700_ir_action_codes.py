"""create ir_action_codes (standalone learned actions, e.g. FAN_SPEED)

Revision ID: e7c1b4a20d95
Revises: c4a1d8e93f27
Create Date: 2026-07-18

A learned remote button outside the (mode, temp) comfort matrix. Kept in its
own table so it never collides with ir_codes' UNIQUE(org, mode, temp) snap key
nor satisfies the auto-control coverage check. See models/ir_action_code.py.
"""

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op
from sqlalchemy.dialects import postgresql

revision: str = "e7c1b4a20d95"
down_revision: Union[str, None] = "c4a1d8e93f27"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.create_table(
        "ir_action_codes",
        sa.Column("id", sa.Uuid(), nullable=False),
        sa.Column("org_id", sa.Uuid(), nullable=False),
        sa.Column("action", sa.String(length=40), nullable=False),
        sa.Column("raw_timing", postgresql.ARRAY(sa.Integer()), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.ForeignKeyConstraint(["org_id"], ["organizations.id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("org_id", "action", name="uq_iraction_snap"),
    )
    op.create_index(op.f("ix_ir_action_codes_org_id"), "ir_action_codes", ["org_id"])


def downgrade() -> None:
    op.drop_index(op.f("ix_ir_action_codes_org_id"), table_name="ir_action_codes")
    op.drop_table("ir_action_codes")
