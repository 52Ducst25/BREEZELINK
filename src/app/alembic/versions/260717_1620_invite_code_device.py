"""bind invite codes to the sold device

Revision ID: c3d8b6a91f42
Revises: 9f2a7c4e1b83
Create Date: 2026-07-17

Separate from 9f2a7c4e1b83 (which created the table minutes earlier) rather
than folded into it: that revision is already applied, so an edit there would
never re-run — the column would silently never appear, which is exactly the
failure mode migrations exist to prevent.

Nullable + SET NULL on purpose; see models/invite_code.device_id for why the
device is provenance rather than an access rule.
"""

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op

revision: str = "c3d8b6a91f42"
down_revision: Union[str, None] = "9f2a7c4e1b83"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.add_column("invite_codes", sa.Column("device_id", sa.Uuid(), nullable=True))
    op.create_foreign_key(
        "fk_invite_codes_device_id",
        "invite_codes",
        "devices",
        ["device_id"],
        ["id"],
        ondelete="SET NULL",
    )
    op.create_index(op.f("ix_invite_codes_device_id"), "invite_codes", ["device_id"])


def downgrade() -> None:
    op.drop_index(op.f("ix_invite_codes_device_id"), table_name="invite_codes")
    op.drop_constraint("fk_invite_codes_device_id", "invite_codes", type_="foreignkey")
    op.drop_column("invite_codes", "device_id")
