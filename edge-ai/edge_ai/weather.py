"""Dự báo nhiệt độ ngoài trời theo giờ — để hệ thống nhìn tới trước, không chỉ nhìn lại.

VẤN ĐỀ NÓ GIẢI: ``T_rm`` trong thuật toán comfort là EMA của nhiệt độ ngoài trời
ĐÃ QUA. Nghĩa là mọi quyết định đều là phản ứng sau. Biết trước 14h sẽ 36 °C thì
làm mát sớm từ 13h, lúc chênh lệch trong/ngoài còn nhỏ nên máy nén chạy hiệu suất
cao hơn — cùng một độ mát, ít điện hơn.

VÌ SAO KHÔNG DÙNG BRICK ``weather_forecast`` CỦA ARDUINO:
  Đã đọc mã nguồn của nó trên bo. Nó chỉ gọi open-meteo với ``daily=weather_code``
  rồi trả về một chuỗi phân loại ("sunny" / "rainy"). KHÔNG CÓ NHIỆT ĐỘ — mà nhiệt
  độ là thứ duy nhất ở đây có ích. Nó cũng nằm trong ``arduino.app_bricks``, tức
  là chỉ tồn tại trong container App Lab, còn dịch vụ này phải chạy được cả dưới
  systemd.

  Gọi thẳng open-meteo thì lấy được ``hourly=temperature_2m``, không cần khoá API,
  và chỉ dùng ``urllib`` của thư viện chuẩn — không thêm phụ thuộc nào.

MẤT MẠNG LÀ CHUYỆN THƯỜNG, KHÔNG PHẢI NGOẠI LỆ. Cả node này sinh ra để chạy khi
mất mạng, nên một dự báo cũ vẫn được dùng tiếp (nhiệt độ ngoài trời hôm nay giống
hôm qua hơn là giống một con số bịa), nhưng có hạn: quá ``MAX_AGE_SEC`` thì trả
None và bên gọi phải tự xoay xở, chứ không lặng lẽ lái theo số của ba ngày trước.
"""

import json
import logging
import time
import urllib.error
import urllib.parse
import urllib.request
from bisect import bisect_left
from pathlib import Path

logger = logging.getLogger("edge.weather")

_API = "https://api.open-meteo.com/v1/forecast"

# Open-meteo cập nhật mỗi giờ. 30 phút là đủ dày mà vẫn lịch sự với một dịch vụ
# miễn phí — và dự báo nhiệt độ không đổi ý mỗi 5 phút.
REFRESH_SEC = 1800.0

# Dự báo cũ hơn mức này thì bỏ. 6 giờ: đủ để sống qua một lần mất mạng buổi
# chiều, không đủ để lái máy lạnh bằng thời tiết hôm kia.
MAX_AGE_SEC = 6 * 3600.0

# Lấy 2 ngày. Điều khiển dự báo chỉ nhìn trước 1-2 giờ, nhưng 2 ngày về cùng một
# lần gọi thì qua nửa đêm không bị hụt, mà tải thêm chỉ vài KB.
FORECAST_DAYS = 2

_TIMEOUT_SEC = 15.0


class WeatherForecast:
    """Nhiệt độ ngoài trời dự báo theo giờ, có nội suy tuyến tính giữa hai mốc."""

    def __init__(self, lat: float, lon: float, cache_path: Path | None = None) -> None:
        self._lat = lat
        self._lon = lon
        self._cache = cache_path
        self._times: list[float] = []       # epoch giây, tăng dần
        self._temps: list[float] = []
        self._fetched_at = 0.0
        self._fail_streak = 0

        if cache_path is not None:
            self._load_cache()

    # -- trạng thái ------------------------------------------------------------

    @property
    def fresh(self) -> bool:
        return bool(self._times) and (time.time() - self._fetched_at) < MAX_AGE_SEC

    @property
    def age_min(self) -> float | None:
        if not self._times:
            return None
        return (time.time() - self._fetched_at) / 60.0

    def due(self) -> bool:
        return (time.time() - self._fetched_at) >= REFRESH_SEC

    # -- tra cứu ---------------------------------------------------------------

    def temp_at(self, ts: float) -> float | None:
        """Nhiệt độ ngoài trời dự báo tại thời điểm ``ts`` (epoch giây).

        NỘI SUY chứ không lấy mốc gần nhất: dự báo cách nhau một giờ, mà máy lạnh
        quyết định mỗi 30 giây. Nhảy bậc mỗi đầu giờ sẽ hiện ra thành một cú giật
        trong nhiệt độ đặt mà phòng không hề có.

        KHÔNG NGOẠI SUY ra ngoài khoảng đã có: trả None. Ngoại suy nhiệt độ ngoài
        trời bằng đường thẳng là cách chắc chắn để "dự báo" 45 °C lúc rạng sáng.
        """
        if not self.fresh:
            return None
        if ts <= self._times[0] or ts >= self._times[-1]:
            return None

        i = bisect_left(self._times, ts)
        t0, t1 = self._times[i - 1], self._times[i]
        v0, v1 = self._temps[i - 1], self._temps[i]
        if t1 == t0:
            return v0
        return v0 + (v1 - v0) * (ts - t0) / (t1 - t0)

    def peak_within(self, hours: float) -> tuple[float, float] | None:
        """(nhiệt độ, epoch) cao nhất trong ``hours`` giờ tới. None nếu chưa có."""
        if not self.fresh:
            return None
        now = time.time()
        window = [(v, t) for t, v in zip(self._times, self._temps)
                  if now <= t <= now + hours * 3600.0]
        return max(window) if window else None

    # -- nạp -------------------------------------------------------------------

    def refresh(self) -> bool:
        """Gọi API. CHẶN — bên gọi phải đẩy vào luồng khác.

        Không bao giờ ném: mất mạng là trạng thái bình thường của thiết bị này,
        và một ngoại lệ ở đây sẽ giết vòng điều khiển vì một thứ chỉ-tốt-nếu-có.
        """
        url = f"{_API}?" + urllib.parse.urlencode({
            "latitude": f"{self._lat:.4f}",
            "longitude": f"{self._lon:.4f}",
            "hourly": "temperature_2m",
            "forecast_days": FORECAST_DAYS,
            # unixtime: khỏi phân tích chuỗi ISO và khỏi mọi rắc rối múi giờ.
            "timeformat": "unixtime",
        })
        try:
            with urllib.request.urlopen(url, timeout=_TIMEOUT_SEC) as resp:
                data = json.loads(resp.read().decode("utf-8"))
            times = [float(t) for t in data["hourly"]["time"]]
            temps = [float(v) for v in data["hourly"]["temperature_2m"]]
        except (urllib.error.URLError, OSError, ValueError, KeyError, TypeError) as exc:
            self._fail_streak += 1
            # Chỉ kêu ở lần đầu của mỗi chuỗi hỏng: mất mạng cả đêm sẽ sinh ra
            # 16 dòng giống hệt nhau, và log kín đặc là log không ai đọc.
            if self._fail_streak == 1:
                logger.warning("Không lấy được dự báo (%s) — dùng tiếp bản cũ nếu còn hạn", exc)
            return False

        if len(times) != len(temps) or not times:
            logger.warning("Dự báo trả về rỗng hoặc lệch độ dài — bỏ")
            return False

        self._times, self._temps = times, temps
        self._fetched_at = time.time()
        if self._fail_streak:
            logger.info("Đã lấy lại được dự báo sau %d lần hỏng", self._fail_streak)
        self._fail_streak = 0
        self._save_cache()

        peak = self.peak_within(12.0)
        logger.info("Dự báo %d mốc giờ%s", len(times),
                    "" if peak is None else f" · đỉnh 12h tới {peak[0]:.1f}°C")
        return True

    # -- nhớ qua lần khởi động lại ---------------------------------------------

    def _load_cache(self) -> None:
        """Dự báo cũ còn hơn không có gì lúc vừa bật mà chưa kịp gọi mạng."""
        try:
            raw = json.loads(self._cache.read_text(encoding="utf-8"))
            self._times = [float(t) for t in raw["times"]]
            self._temps = [float(v) for v in raw["temps"]]
            self._fetched_at = float(raw["fetched_at"])
        except (OSError, ValueError, KeyError, TypeError):
            return
        if self.fresh:
            logger.info("Dùng lại dự báo đã lưu (%.0f phút trước)", self.age_min or 0.0)

    def _save_cache(self) -> None:
        if self._cache is None:
            return
        try:
            self._cache.parent.mkdir(parents=True, exist_ok=True)
            self._cache.write_text(
                json.dumps({"times": self._times, "temps": self._temps,
                            "fetched_at": self._fetched_at}),
                encoding="utf-8",
            )
        except OSError as exc:
            logger.warning("Không ghi được bộ nhớ đệm dự báo: %s", exc)
