"""Declarative Base + timestamp mixin.

TRAP #5: datetimes are ALWAYS timezone-aware UTC from the start
(``DateTime(timezone=True)`` + ``datetime.now(timezone.utc)``). Retrofitting
tz-awareness later is painful.
"""

from datetime import datetime, timezone

from sqlalchemy import DateTime
from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column


def utcnow() -> datetime:
    """Timezone-aware current UTC timestamp."""
    return datetime.now(timezone.utc)


class Base(DeclarativeBase):
    """Shared declarative base for all ORM models."""


class TimestampMixin:
    """Adds tz-aware created_at / updated_at columns."""

    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, nullable=False
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utcnow, onupdate=utcnow, nullable=False
    )
