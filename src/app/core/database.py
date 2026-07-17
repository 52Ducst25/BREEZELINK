"""Async database engine + session factory.

The ``AsyncSessionLocal`` factory is shared by:
- the API (via the ``get_db`` FastAPI dependency), and
- the MQTT worker (a fresh session per message — see workers/mqtt_consumer.py).
"""

from collections.abc import AsyncGenerator

from sqlalchemy.ext.asyncio import (
    AsyncSession,
    async_sessionmaker,
    create_async_engine,
)

from app.config import get_settings

_settings = get_settings()

engine = create_async_engine(
    _settings.db_url,
    echo=False,
    pool_pre_ping=True,
)

# expire_on_commit=False so objects stay usable after commit (worker + API).
AsyncSessionLocal = async_sessionmaker(
    engine, class_=AsyncSession, expire_on_commit=False
)


async def get_db() -> AsyncGenerator[AsyncSession, None]:
    """FastAPI dependency yielding an AsyncSession, closed after the request."""
    async with AsyncSessionLocal() as session:
        yield session
