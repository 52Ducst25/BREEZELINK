"""Admin-tunable comfort-algorithm constants (design §3.1 ``configs`` table).

These are the ONLY knobs that vary per-org at runtime — loaded from the
Postgres ``configs`` row and mirrored into Redis (``bl:{org}:cfg``) by Phase
2's ``redis_config_cache``. Fixed physics constants (ASHRAE regression
slope/intercept, humidity-penalty knee points) are NOT here — they live as
module constants in ``setpoint_calculator.py`` because they are science, not
site-tunable knobs (design §1.2 note).
"""

from dataclasses import dataclass

# ---------------------------------------------------------------------------
#  DẢI NHIỆT ĐỘ CỦA MÁY LẠNH — 16..30 °C, KHÔNG PHẢI MỘT KNOB
# ---------------------------------------------------------------------------
#  Đây là thuộc tính của PHẦN CỨNG, không phải tham số tinh chỉnh được: remote
#  điều hoà chỉ có ngần này mức, nên chỉ ngần này mức có mã IR để mà học. Panel
#  treo tường mã hoá đúng dải này thành 15 bit (`Ui::Model::coolMask`, bit i =
#  COOL 16+i) và lớp phủ "MÃ IR ĐÃ HỌC" dựng đúng 15 hàng COOL + DRY/FAN/OFF.
#
#  `clamp_min`/`clamp_max` là knob, nhưng chúng phải nằm TRONG dải này. Đặt
#  clamp_max = 32 thì hỏng theo kiểu im lặng, và đó là ca đã thật sự tồn tại:
#  app cho kéo dial tới 32, `/comfort/override` nhận vì nó chỉ đối chiếu với
#  clamp_max, rồi panel không có bit nào cho 31/32 nên nút hiện "chưa học mã" —
#  trong khi hộ đó đã học đủ mọi mức máy lạnh thật sự có.
#
#  ĐỂ Ở ĐÂY chứ không ở setpoint_calculator.py: file đó giữ hằng số KHOA HỌC
#  (hồi quy ASHRAE, điểm gãy độ ẩm), còn hai số này là giới hạn của cái máy.
#  Và để cạnh clamp_min/clamp_max vì chúng tồn tại để chặn đúng hai trường đó.
AC_TEMP_MIN = 16.0
AC_TEMP_MAX = 30.0


@dataclass(frozen=True)
class ComfortConfig:
    """Typed view over one org's ``configs`` row / Redis cfg-hash mirror."""

    ema_alpha: float  # T_rm EMA weight on the NEW outdoor sample (design §1.2 step A)
    deadband: float  # degC hysteresis band around T_set (design §1.3)
    dwell_sec: int  # min seconds between mode switches (compressor protection)
    dry_rh: float  # %RH threshold to trigger DRY mode
    humid_slope: float  # degC penalty per %RH above 60 (design §1.2 step C)
    clamp_min: float  # safety clamp lower bound, degC
    clamp_max: float  # safety clamp upper bound, degC
    override_hours: int  # manual-override auto-resume TTL (consumed by Phase 2 Redis; kept here for completeness)
    night_start: int  # hour 0-23, start of night-offset window
    night_end: int  # hour 0-23, end of night-offset window (exclusive), may wrap past midnight
    night_offset: float  # degC added to target during the night window

    @classmethod
    def from_dict(cls, cfg: dict) -> "ComfortConfig":
        """Build from a plain dict (Redis hash mirror or a DB row mapping).

        Values are cast explicitly because Redis hashes round-trip as
        strings (see ``redis_client.coerce_hash``), while a DB row mapping
        already carries native types — this works for either source.
        """
        return cls(
            ema_alpha=float(cfg["ema_alpha"]),
            deadband=float(cfg["deadband"]),
            dwell_sec=int(cfg["dwell_sec"]),
            dry_rh=float(cfg["dry_rh"]),
            humid_slope=float(cfg["humid_slope"]),
            clamp_min=float(cfg["clamp_min"]),
            clamp_max=float(cfg["clamp_max"]),
            override_hours=int(cfg["override_hours"]),
            night_start=int(cfg["night_start"]),
            night_end=int(cfg["night_end"]),
            night_offset=float(cfg["night_offset"]),
        )
