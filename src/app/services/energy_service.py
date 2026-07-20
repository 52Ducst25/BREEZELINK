"""Energy (kWh) derived from the per-node ``telemetry.watt`` power samples.

There is no separate energy meter table: kWh is integrated on read from the raw
watt samples (trapezoidal, gaps > 2 min treated as the AC being off so they add
nothing). Everything returns ``None``/empty until a node actually reports watts,
so the app renders "chưa có dữ liệu" rather than a fabricated 0.
"""

import uuid
from datetime import datetime, timedelta, timezone

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.telemetry import Telemetry
from app.services import device_service

# Two consecutive samples more than this apart are NOT integrated — the node was
# asleep/off/offline, so counting the gap would invent energy that wasn't used.
_MAX_GAP_S = 120.0


async def _samples(
    session: AsyncSession, device_id: uuid.UUID, start: datetime, end: datetime
) -> list[tuple[datetime, float]]:
    stmt = (
        select(Telemetry.ts, Telemetry.watt)
        .where(
            Telemetry.device_id == device_id,
            Telemetry.watt.is_not(None),
            Telemetry.ts >= start,
            Telemetry.ts < end,
        )
        .order_by(Telemetry.ts.asc())
    )
    return [(ts, float(w)) for ts, w in (await session.execute(stmt)).all()]


def _integrate_kwh(rows: list[tuple[datetime, float]]) -> float | None:
    """Trapezoidal Watt·seconds -> kWh, or None when there are no samples."""
    if not rows:
        return None
    watt_seconds = 0.0
    for (t0, w0), (t1, w1) in zip(rows, rows[1:]):
        dt = (t1 - t0).total_seconds()
        if 0 < dt <= _MAX_GAP_S:
            watt_seconds += (w0 + w1) / 2.0 * dt
    return round(watt_seconds / 3_600_000.0, 3)


def _pct(now_val: float | None, prev_val: float | None) -> float | None:
    if now_val is None or prev_val is None or prev_val == 0:
        return None
    return round((now_val - prev_val) / prev_val * 100.0, 2)


async def overview(session: AsyncSession, org_id: str) -> dict:
    """Today's kWh per device + vs-yesterday %, for the home 'Power' card."""
    now = datetime.now(timezone.utc)
    today = now.replace(hour=0, minute=0, second=0, microsecond=0)
    yesterday = today - timedelta(days=1)

    devices = await device_service.list_devices(session, uuid.UUID(org_id))
    rows: list[dict] = []
    sum_today: float | None = None
    sum_yest: float | None = None
    for d in devices:
        k_today = await _kwh(session, d.id, today, now)
        k_yest = await _kwh(session, d.id, yesterday, today)
        rows.append({
            "device_id": str(d.id),
            "name": d.name,
            "kwh_today": k_today,
            "pct_vs_yesterday": _pct(k_today, k_yest),
        })
        if k_today is not None:
            sum_today = (sum_today or 0.0) + k_today
        if k_yest is not None:
            sum_yest = (sum_yest or 0.0) + k_yest

    return {
        "total_kwh_today": round(sum_today, 3) if sum_today is not None else None,
        "pct_vs_yesterday": _pct(sum_today, sum_yest),
        "devices": rows,
    }


async def _kwh(session: AsyncSession, device_id: uuid.UUID, start: datetime, end: datetime) -> float | None:
    return _integrate_kwh(await _samples(session, device_id, start, end))


async def series(
    session: AsyncSession, device_id: uuid.UUID, start: datetime, end: datetime
) -> dict:
    """Watt samples + integrated kWh over a window, for the energy chart."""
    rows = await _samples(session, device_id, start, end)
    return {
        "watt_series": [{"ts": ts.isoformat(), "watt": w} for ts, w in rows],
        "total_kwh": _integrate_kwh(rows),
    }
