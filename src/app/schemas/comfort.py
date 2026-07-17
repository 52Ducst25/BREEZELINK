"""Comfort-algorithm result/preview DTOs (Phase 3 design §1.2, §6).

``ComfortResult`` carries every pipeline intermediate so Phase 4 can persist
a full ``comfort_log`` row (audit trail proving *why* a setpoint was chosen)
and Phase 5's ``/comfort/preview`` endpoint can render ``ComfortPreview`` for
the app. ``OverrideRequest``/``OverrideResponse``/``ComfortLogRead`` support
the Phase 5 manual-override and decision-history endpoints.
"""

import uuid
from datetime import datetime

from pydantic import BaseModel, ConfigDict

from app.models.enums import AcMode


class ComfortResult(BaseModel):
    """Full pipeline output for one ``comfort_engine.compute()`` cycle."""

    t_rm: float  # running-mean outdoor temp (EMA) used this cycle
    t_neutral: float  # adaptive neutral point, 0.31*t_rm + 17.8
    humid_penalty: float  # degrees subtracted for indoor humidity
    t_target: float  # continuous target after offset + clamp, pre-snap
    t_set: int  # discrete setpoint snapped to a learned IR temp
    mode: AcMode  # decided AC mode after hysteresis + dwell
    stale: bool  # True if outdoor telemetry was stale this cycle
    new_ema: float  # value to persist back to bl:{org}:tout_ema


class ComfortPreview(BaseModel):
    """Read-only projection for ``/comfort/preview`` (Phase 5) — same shape
    as ``ComfortResult`` minus the persistence-only ``new_ema`` bookkeeping
    field, plus a human-readable ``reason`` explaining the decision.

    Every metric is optional because "nothing to decide yet" is a normal state,
    not an error:
      * ``data_available=False`` — no telemetry has arrived yet, so there is no
        honest decision to show. Previously these fields were computed from
        defaulted 0.0 readings, which fabricated a confident-looking setpoint
        out of an empty database.
      * ``t_set``/``mode`` are ``None`` while the org has not LEARNed any IR
        code yet (auto-control gate, design §5) — the target math is still
        returned so the UI can show what it *would* aim for.
    """

    data_available: bool = True
    t_rm: float | None = None
    t_neutral: float | None = None
    humid_penalty: float | None = None
    t_target: float | None = None
    t_set: int | None = None
    mode: AcMode | None = None
    stale: bool = False
    reason: str


class OverrideRequest(BaseModel):
    """Manual-override payload — user picks mode + setpoint directly."""

    mode: AcMode
    setpoint: int


class OverrideResponse(BaseModel):
    """Result of setting a manual override — flags IR-coverage gaps (design
    §5 risk) without blocking the override itself.
    """

    detail: str
    missing_ir_codes: list[str] = []


class ComfortLogRead(BaseModel):
    """One ``comfort_log`` audit row for the decision-history chart."""

    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    ts: datetime
    t_out: float
    h_out: float | None
    t_in: float
    h_in: float
    t_rm: float
    t_neutral: float
    humid_penalty: float
    t_target: float
    t_set: int
    mode: AcMode
