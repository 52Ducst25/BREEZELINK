"""add 'room' to the node_type enum (4 corner sensors per household)

Revision ID: f6a2d914c7b0
Revises: d8b3f0a25e14
Create Date: 2026-08-11

The household grew from 2 nodes to 6: the indoor board lost its DHT22 and became
a pure gateway, and four ESP32-C3 corner sensors took over the indoor reading.
Those four need their own ``node_type`` so the worker can tell "one of several
room sensors to be averaged" apart from "the single node that carries the IR
blaster". See models/enums.NodeType.

WHY ``COMMIT`` BEFORE ``ALTER TYPE``: Postgres refuses ``ALTER TYPE ... ADD
VALUE`` inside a transaction block on versions before 12, and even on 12+ the
new label cannot be USED in the same transaction that added it. Alembic wraps
every migration in a transaction, so without dropping out of it this migration
fails with "ALTER TYPE ... ADD cannot run inside a transaction block" — a
failure that only shows up against a real Postgres, never against SQLite.

``IF NOT EXISTS`` makes the upgrade idempotent: a half-applied deploy (the
commit above lands, a later step fails) would otherwise leave the migration
permanently un-rerunnable.

DOWNGRADE IS A DELIBERATE NO-OP. Postgres has no ``ALTER TYPE ... DROP VALUE``;
removing a label means recreating the type, rewriting every dependent column and
default, and it is only safe if no row uses it. Silently succeeding while doing
nothing is better than a downgrade that destroys device rows.
"""

from typing import Sequence, Union

from alembic import op

revision: str = "f6a2d914c7b0"
down_revision: Union[str, None] = "d8b3f0a25e14"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    bind = op.get_bind()
    if bind.dialect.name != "postgresql":
        return  # SQLite (tests) stores enums as VARCHAR — nothing to alter
    op.execute("COMMIT")
    op.execute("ALTER TYPE node_type ADD VALUE IF NOT EXISTS 'room'")


def downgrade() -> None:
    """Intentionally empty — see the module docstring."""
