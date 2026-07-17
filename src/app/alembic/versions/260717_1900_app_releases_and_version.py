"""add app_releases + users.app_version

Revision ID: d5f1a83c27e9
Revises: a71e5d0c9b64
Create Date: 2026-07-17

Backs OTA updates for the customer app: the vendor publishes an APK
(app_releases) and the app discovers it via the public manifest. users.app_version
records which build each customer actually signed in from, which is the only
way to know whether a published update has reached anyone.

Both changes ride in one revision: they are one feature, and neither is
useful alone.

app_version is nullable with no backfill — every existing user genuinely has an
unknown build (nobody has ever sent the header), and inventing a default here
would fabricate a fact the customers page then displays as if measured.
"""

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op

revision: str = "d5f1a83c27e9"
down_revision: Union[str, None] = "a71e5d0c9b64"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.create_table(
        "app_releases",
        sa.Column("id", sa.Uuid(), nullable=False),
        sa.Column("version_code", sa.Integer(), nullable=False),
        sa.Column("version_name", sa.String(length=50), nullable=False),
        sa.Column("apk_filename", sa.String(length=255), nullable=False),
        sa.Column("apk_size", sa.BigInteger(), nullable=False),
        sa.Column("changelog", sa.Text(), nullable=True),
        sa.Column("min_supported_code", sa.Integer(), nullable=False),
        sa.Column("is_current", sa.Boolean(), nullable=False),
        sa.Column("published_by", sa.Uuid(), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.ForeignKeyConstraint(["published_by"], ["users.id"], ondelete="SET NULL"),
        sa.PrimaryKeyConstraint("id"),
    )
    # Unique: version_code is what the app compares against to decide "newer",
    # so two rows sharing one would make that answer ambiguous.
    op.create_index(
        op.f("ix_app_releases_version_code"), "app_releases", ["version_code"], unique=True
    )
    op.create_index(op.f("ix_app_releases_is_current"), "app_releases", ["is_current"])

    op.add_column("users", sa.Column("app_version", sa.String(length=32), nullable=True))


def downgrade() -> None:
    op.drop_column("users", "app_version")
    op.drop_index(op.f("ix_app_releases_is_current"), table_name="app_releases")
    op.drop_index(op.f("ix_app_releases_version_code"), table_name="app_releases")
    op.drop_table("app_releases")
