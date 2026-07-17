"""create aircon domain tables

Revision ID: 0784163c81b9
Revises:
Create Date: 2026-07-14 21:05:00

Initial schema for Aircon. Phase 0 left ``versions/`` empty (no migration
was generated for the scaffolded org/user/device models), so this single
migration creates the FULL schema: ``organizations``, ``users``, ``devices``
(Phase 0 models, now including ``node_type``) PLUS the 6 Phase 1 domain
tables (``ir_codes``, ``commands``, ``telemetry``, ``comfort_log``,
``configs``).

Table order follows FK dependencies:
organizations -> users -> devices -> ir_codes -> commands -> telemetry
-> comfort_log -> configs. Downgrade drops in exact reverse order.

Enum reuse: ``ac_mode`` is shared by ir_codes/commands/comfort_log. The first
CREATE TABLE that references it creates the Postgres ENUM type (default
``create_type=True``); the following two pass ``create_type=False`` so the
type is not recreated (matches Alembic's own autogenerate convention for a
shared enum).

Seed data: seeds one demo organization + one default ``configs`` row (design
§1.2/§1.3 constants) so the app has a usable org out of the box. NOTE: in a
real multi-tenant deploy, seed a ``configs`` row per org at org-creation time
instead (see app/services/organization_service.py) — this seed exists purely
for local/demo readiness before Phase 2/3 land.

DB apply deferred: no Postgres available in this environment. Verified via
``python -m py_compile`` + manual SQL review only; run
``alembic upgrade head`` / ``alembic downgrade base`` against a real Postgres
before merging.
"""

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op
from sqlalchemy.dialects import postgresql

revision: str = "0784163c81b9"
down_revision: Union[str, None] = None
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None

# Fixed demo org id — referenced by the seeded configs row below.
_DEMO_ORG_ID = "00000000-0000-0000-0000-000000000001"


def upgrade() -> None:
    # ---- organizations (Phase 0) ----
    op.create_table(
        "organizations",
        sa.Column("id", sa.Uuid(), nullable=False),
        sa.Column("name", sa.String(length=200), nullable=False),
        sa.Column(
            "type",
            postgresql.ENUM("household", "company", name="org_type"),
            nullable=False,
        ),
        sa.Column("contact", sa.String(length=200), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False),
        sa.PrimaryKeyConstraint("id"),
    )

    # ---- users (Phase 0) ----
    op.create_table(
        "users",
        sa.Column("id", sa.Uuid(), nullable=False),
        sa.Column("org_id", sa.Uuid(), nullable=False),
        sa.Column("email", sa.String(length=255), nullable=False),
        sa.Column("full_name", sa.String(length=200), nullable=True),
        sa.Column("phone", sa.String(length=20), nullable=True),
        sa.Column("location", sa.String(length=200), nullable=True),
        sa.Column("password_hash", sa.String(length=255), nullable=False),
        sa.Column(
            "role",
            postgresql.ENUM("owner", "member", name="user_role"),
            nullable=False,
        ),
        sa.Column("is_active", sa.Boolean(), nullable=False),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False),
        sa.ForeignKeyConstraint(["org_id"], ["organizations.id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("org_id", "email", name="uq_user_org_email"),
    )
    op.create_index(op.f("ix_users_org_id"), "users", ["org_id"])

    # ---- devices (Phase 0 + node_type from Phase 1) ----
    op.create_table(
        "devices",
        sa.Column("id", sa.Uuid(), nullable=False),
        sa.Column("org_id", sa.Uuid(), nullable=False),
        sa.Column("name", sa.String(length=200), nullable=False),
        sa.Column(
            "node_type",
            postgresql.ENUM("outdoor", "indoor", name="node_type"),
            nullable=False,
        ),
        sa.Column("location", sa.String(length=200), nullable=True),
        sa.Column("latitude", sa.Numeric(precision=9, scale=6), nullable=True),
        sa.Column("longitude", sa.Numeric(precision=9, scale=6), nullable=True),
        sa.Column("device_uuid", sa.String(length=64), nullable=False),
        sa.Column("mqtt_token", sa.String(length=128), nullable=False),
        sa.Column("mac", sa.BigInteger(), nullable=True),
        sa.Column(
            "status",
            postgresql.ENUM("online", "offline", name="device_status"),
            nullable=False,
        ),
        sa.Column("last_seen_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False),
        sa.ForeignKeyConstraint(["org_id"], ["organizations.id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index(op.f("ix_devices_org_id"), "devices", ["org_id"])
    # unique=True: this single index also enforces the device_uuid uniqueness
    # (matches the model's unique=True, index=True — no separate UniqueConstraint).
    op.create_index(
        op.f("ix_devices_device_uuid"), "devices", ["device_uuid"], unique=True
    )
    op.create_index(op.f("ix_devices_mac"), "devices", ["mac"])

    # ---- ir_codes (Phase 1) ----
    op.create_table(
        "ir_codes",
        sa.Column("id", sa.Uuid(), nullable=False),
        sa.Column("org_id", sa.Uuid(), nullable=False),
        sa.Column("brand", sa.String(length=100), nullable=True),
        sa.Column("button_label", sa.String(length=100), nullable=False),
        sa.Column(
            "mode",
            postgresql.ENUM("COOL", "DRY", "FAN", "OFF", name="ac_mode"),
            nullable=False,
        ),
        sa.Column("temp", sa.SmallInteger(), nullable=False),
        sa.Column("protocol", sa.String(length=50), nullable=True),
        sa.Column("state_bytes", sa.LargeBinary(), nullable=True),
        sa.Column("raw_timing", postgresql.ARRAY(sa.Integer()), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.ForeignKeyConstraint(["org_id"], ["organizations.id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("org_id", "mode", "temp", name="uq_ircode_snap"),
    )
    op.create_index(op.f("ix_ir_codes_org_id"), "ir_codes", ["org_id"])

    # ---- commands (Phase 1) — reuses ac_mode, must not recreate the type ----
    op.create_table(
        "commands",
        sa.Column("id", sa.Uuid(), nullable=False),
        sa.Column("device_id", sa.Uuid(), nullable=False),
        sa.Column("ts", sa.DateTime(timezone=True), nullable=False),
        sa.Column(
            "mode",
            postgresql.ENUM(
                "COOL", "DRY", "FAN", "OFF", name="ac_mode", create_type=False
            ),
            nullable=False,
        ),
        sa.Column("setpoint", sa.SmallInteger(), nullable=False),
        sa.Column("ir_code_id", sa.Uuid(), nullable=True),
        sa.Column(
            "source",
            postgresql.ENUM("auto", "manual", name="command_source"),
            nullable=False,
        ),
        sa.Column("req_id", sa.String(length=64), nullable=False),
        sa.Column("ack_ts", sa.DateTime(timezone=True), nullable=True),
        sa.ForeignKeyConstraint(["device_id"], ["devices.id"], ondelete="CASCADE"),
        sa.ForeignKeyConstraint(["ir_code_id"], ["ir_codes.id"], ondelete="SET NULL"),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index(op.f("ix_commands_device_id"), "commands", ["device_id"])

    # ---- telemetry (Phase 1) ----
    op.create_table(
        "telemetry",
        sa.Column("id", sa.BigInteger(), autoincrement=True, nullable=False),
        sa.Column("device_id", sa.Uuid(), nullable=False),
        sa.Column("ts", sa.DateTime(timezone=True), nullable=False),
        sa.Column("temp", sa.REAL(), nullable=False),
        sa.Column("humidity", sa.REAL(), nullable=False),
        sa.Column("rssi", sa.SmallInteger(), nullable=False),
        sa.Column("batt", sa.REAL(), nullable=True),
        sa.ForeignKeyConstraint(["device_id"], ["devices.id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("id"),
    )
    # Composite DESC index — "latest readings per device" is the hot query path.
    op.create_index(
        "ix_telemetry_device_ts", "telemetry", ["device_id", sa.text("ts DESC")]
    )

    # ---- comfort_log (Phase 1) — reuses ac_mode ----
    op.create_table(
        "comfort_log",
        sa.Column("id", sa.Uuid(), nullable=False),
        sa.Column("org_id", sa.Uuid(), nullable=False),
        sa.Column("ts", sa.DateTime(timezone=True), nullable=False),
        sa.Column("t_out", sa.REAL(), nullable=False),
        sa.Column("h_out", sa.REAL(), nullable=True),
        sa.Column("t_in", sa.REAL(), nullable=False),
        sa.Column("h_in", sa.REAL(), nullable=False),
        sa.Column("t_rm", sa.REAL(), nullable=False),
        sa.Column("t_neutral", sa.REAL(), nullable=False),
        sa.Column("humid_penalty", sa.REAL(), nullable=False),
        sa.Column("t_target", sa.REAL(), nullable=False),
        sa.Column("t_set", sa.SmallInteger(), nullable=False),
        sa.Column(
            "mode",
            postgresql.ENUM(
                "COOL", "DRY", "FAN", "OFF", name="ac_mode", create_type=False
            ),
            nullable=False,
        ),
        sa.ForeignKeyConstraint(["org_id"], ["organizations.id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index(op.f("ix_comfort_log_org_id"), "comfort_log", ["org_id"])

    # ---- configs (Phase 1) — one row per org ----
    op.create_table(
        "configs",
        sa.Column("org_id", sa.Uuid(), nullable=False),
        sa.Column("ema_alpha", sa.REAL(), nullable=False),
        sa.Column("deadband", sa.REAL(), nullable=False),
        sa.Column("dwell_sec", sa.Integer(), nullable=False),
        sa.Column("dry_rh", sa.REAL(), nullable=False),
        sa.Column("humid_slope", sa.REAL(), nullable=False),
        sa.Column("clamp_min", sa.REAL(), nullable=False),
        sa.Column("clamp_max", sa.REAL(), nullable=False),
        sa.Column("override_hours", sa.Integer(), nullable=False),
        sa.Column("night_start", sa.SmallInteger(), nullable=False),
        sa.Column("night_end", sa.SmallInteger(), nullable=False),
        sa.Column("night_offset", sa.REAL(), nullable=False),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False),
        sa.ForeignKeyConstraint(["org_id"], ["organizations.id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("org_id"),
    )

    # ---- seed: demo org + default configs row (design §1.2/§1.3 constants) ----
    op.execute(
        f"""
        INSERT INTO organizations (id, name, type, contact, created_at, updated_at)
        VALUES ('{_DEMO_ORG_ID}', 'Demo Household', 'household', NULL, now(), now())
        """
    )
    op.execute(
        f"""
        INSERT INTO configs (
            org_id, ema_alpha, deadband, dwell_sec, dry_rh, humid_slope,
            clamp_min, clamp_max, override_hours, night_start, night_end,
            night_offset, updated_at
        )
        VALUES (
            '{_DEMO_ORG_ID}', 0.2, 0.8, 600, 72, 0.05,
            24, 28, 2, 23, 6,
            0.5, now()
        )
        """
    )


def downgrade() -> None:
    # Reverse FK dependency order.
    op.drop_table("configs")
    op.drop_index(op.f("ix_comfort_log_org_id"), table_name="comfort_log")
    op.drop_table("comfort_log")
    op.drop_index("ix_telemetry_device_ts", table_name="telemetry")
    op.drop_table("telemetry")
    op.drop_index(op.f("ix_commands_device_id"), table_name="commands")
    op.drop_table("commands")
    op.drop_index(op.f("ix_ir_codes_org_id"), table_name="ir_codes")
    op.drop_table("ir_codes")
    op.drop_index(op.f("ix_devices_mac"), table_name="devices")
    op.drop_index(op.f("ix_devices_device_uuid"), table_name="devices")
    op.drop_index(op.f("ix_devices_org_id"), table_name="devices")
    op.drop_table("devices")
    op.drop_index(op.f("ix_users_org_id"), table_name="users")
    op.drop_table("users")
    op.drop_table("organizations")

    # Drop enum types last (tables that used them are already gone).
    bind = op.get_bind()
    postgresql.ENUM(name="command_source").drop(bind, checkfirst=True)
    postgresql.ENUM(name="ac_mode").drop(bind, checkfirst=True)
    postgresql.ENUM(name="device_status").drop(bind, checkfirst=True)
    postgresql.ENUM(name="node_type").drop(bind, checkfirst=True)
    postgresql.ENUM(name="user_role").drop(bind, checkfirst=True)
    postgresql.ENUM(name="org_type").drop(bind, checkfirst=True)
