"""Config DTOs: admin-tunable comfort-algorithm runtime constants (design §3.1).

Range constraints below mirror the physical/algorithmic bounds documented in
``comfort_constants.py`` — reject nonsense values (e.g. clamp_min>=clamp_max,
night_start>23) at the API boundary instead of trusting the admin client.
"""

import uuid
from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field, model_validator

from app.comfort.comfort_constants import AC_TEMP_MAX, AC_TEMP_MIN


class ConfigUpdate(BaseModel):
    """Partial update — every field optional; only provided ones change."""

    ema_alpha: float | None = Field(default=None, gt=0, le=1)
    deadband: float | None = Field(default=None, ge=0)
    dwell_sec: int | None = Field(default=None, ge=0)
    dry_rh: float | None = Field(default=None, ge=0, le=100)
    humid_slope: float | None = Field(default=None, ge=0)
    # KẸP TRONG DẢI CỦA MÁY LẠNH (16..30), không phải [-10, 50] như trước.
    #
    # Dải cũ rộng tới mức nó không chặn được gì có thật: clamp_max = 32 vẫn qua,
    # và hỏng ở mãi đầu kia của hệ — panel không có bit mã IR nào cho 31/32 nên
    # nút hiện "chưa học mã" ở một hộ đã học đủ. Xem AC_TEMP_MIN/MAX.
    #
    # Cả API `/configs` lẫn form web quản trị đều đi qua đây (customer_routes
    # dựng ConfigUpdate rồi mới gọi service), nên một chỗ này là đủ.
    clamp_min: float | None = Field(default=None, ge=AC_TEMP_MIN, le=AC_TEMP_MAX)
    clamp_max: float | None = Field(default=None, ge=AC_TEMP_MIN, le=AC_TEMP_MAX)
    override_hours: int | None = Field(default=None, gt=0)
    night_start: int | None = Field(default=None, ge=0, le=23)
    night_end: int | None = Field(default=None, ge=0, le=23)
    night_offset: float | None = None

    @model_validator(mode="after")
    def _check_clamp_range(self) -> "ConfigUpdate":
        """When both clamp bounds are given together, min must be < max —
        the single-request case this schema can validate without hitting the
        DB (a partial update touching only one bound is checked again at the
        service layer against the persisted row, see ``config_service``).
        """
        if (
            self.clamp_min is not None
            and self.clamp_max is not None
            and self.clamp_min >= self.clamp_max
        ):
            raise ValueError("clamp_min must be less than clamp_max")
        return self


class ConfigRead(BaseModel):
    """Full config row returned by the API."""

    model_config = ConfigDict(from_attributes=True)

    org_id: uuid.UUID
    ema_alpha: float
    deadband: float
    dwell_sec: int
    dry_rh: float
    humid_slope: float
    clamp_min: float
    clamp_max: float
    override_hours: int
    night_start: int
    night_end: int
    night_offset: float
    updated_at: datetime
