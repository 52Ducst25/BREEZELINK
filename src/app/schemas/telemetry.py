"""Telemetry read DTO — chart series for ``/telemetry`` (design §3.1)."""

import uuid
from datetime import datetime

from pydantic import BaseModel, ConfigDict


class TelemetryRead(BaseModel):
    """One sensor reading row."""

    model_config = ConfigDict(from_attributes=True)

    id: int
    device_id: uuid.UUID
    ts: datetime
    temp: float
    humidity: float
    rssi: int
    batt: float | None
