"""IR-code DTOs: LEARN trigger request + captured-code / coverage read models
(design §4.2 LEARN flow, §5 risk — must capture COOL 24-28/DRY/FAN/OFF)."""

import uuid
from datetime import datetime

from pydantic import BaseModel, ConfigDict

from app.models.enums import AcMode


class LearnRequest(BaseModel):
    """Trigger LEARN mode on the indoor node for one (mode, temp) button.

    ``temp`` is required for COOL (continuous range) and ignored/optional
    for DRY/FAN/OFF (single fixed button, no setpoint).
    """

    mode: AcMode
    temp: int | None = None


class IrCodeRead(BaseModel):
    """One captured/known IR code row."""

    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    mode: AcMode
    temp: int
    brand: str | None
    button_label: str
    created_at: datetime


class IrCoverageResponse(BaseModel):
    """Captured codes + which required buttons are still missing (design §5:
    COOL 24-28, DRY, FAN, OFF must all exist before auto-control is safe).
    """

    codes: list[IrCodeRead]
    missing: list[str]
