"""Chấm điểm dự báo: so cái đã đoán 15 phút trước với cái thật sự xảy ra.

VÌ SAO ĐÂY LÀ PHẦN BẮT BUỘC, KHÔNG PHẢI THÊM CHO ĐẸP:
  Một mô hình chưa được đo thì không được phép lái máy lạnh. Bản hiện tại đã có
  sẵn một bài học về chuyện này — ``fit_trend`` tính ra dự báo 15 phút từ nhiều
  tuần trước, và con số đó tới giờ vẫn chỉ nằm trong một dòng log, chưa ai từng
  biết nó đúng hay sai. Không đo thì không có đường nào để tiến từ "chỉ đề xuất"
  sang "được ra lệnh".

  Có sai số trung bình rồi thì quyết định trở nên tầm thường: MAE dưới ~0.3 °C
  thì mô hình đáng tin để làm mát trước; trên 1 °C thì nó đang đoán bừa.

SO SÁNH VỚI MỘT MỐC NGÂY THƠ, không chỉ báo sai số tuyệt đối. "MAE 0.4 °C" nghe
có vẻ tốt cho tới khi biết rằng đoán "y nguyên như bây giờ" cũng cho 0.4 °C —
lúc đó mô hình không đóng góp gì cả. Cột ``so_với_đứng_yên`` mới là con số nói
lên mô hình có giá trị hay không.
"""

import logging
from collections import deque
from dataclasses import dataclass

logger = logging.getLogger("edge.score")

# Cửa sổ trung bình trượt. 96 lần chấm ở nhịp 30s ≈ nửa giờ dự báo đã chín — đủ
# để một con số ổn định, đủ ngắn để mô hình vừa cải thiện thì thấy ngay.
WINDOW = 96

# Dự báo quá hạn quá xa thì bỏ: gateway mất kết nối rồi quay lại sau một giờ,
# chấm điểm dựa vào nhiệt độ của một giờ sau thời điểm đáng lẽ phải chấm là vô
# nghĩa và sẽ bôi bẩn số liệu bằng một sai số khổng lồ không phải lỗi mô hình.
TOLERANCE_SEC = 120.0


@dataclass
class _Pending:
    due_ts: float
    predicted: float
    naive: float      # nhiệt độ lúc dự báo — mốc "phòng đứng yên"


class PredictionScore:
    """Giữ các dự báo chưa tới hạn, chấm khi tới, và báo cáo sai số trung bình."""

    def __init__(self, horizon_min: float) -> None:
        self._horizon_min = horizon_min
        self._pending: deque[_Pending] = deque()
        self._err: deque[float] = deque(maxlen=WINDOW)
        self._naive_err: deque[float] = deque(maxlen=WINDOW)

    def record(self, now_ts: float, predicted: float, current: float) -> None:
        """Ghi nhận một dự báo để chấm sau."""
        self._pending.append(
            _Pending(due_ts=now_ts + self._horizon_min * 60.0,
                     predicted=predicted, naive=current)
        )

    def settle(self, now_ts: float, actual: float) -> None:
        """Chấm mọi dự báo đã tới hạn. Gọi mỗi nhịp điều khiển."""
        while self._pending and self._pending[0].due_ts <= now_ts:
            p = self._pending.popleft()
            if now_ts - p.due_ts > TOLERANCE_SEC:
                continue        # tới muộn quá, bỏ — xem chú thích TOLERANCE_SEC
            self._err.append(abs(actual - p.predicted))
            self._naive_err.append(abs(actual - p.naive))

    @property
    def n(self) -> int:
        return len(self._err)

    @property
    def mae(self) -> float | None:
        return sum(self._err) / len(self._err) if self._err else None

    @property
    def naive_mae(self) -> float | None:
        return sum(self._naive_err) / len(self._naive_err) if self._naive_err else None

    @property
    def trustworthy(self) -> bool:
        """Đủ tin để lái máy lạnh chưa.

        Hai điều kiện, và điều kiện thứ hai mới là điều kiện thật: mô hình phải
        ĐÁNH BẠI mốc ngây thơ. Một mô hình chỉ ngang "phòng đứng yên" thì mọi
        phép tính của nó là công cốc, dù sai số tuyệt đối nghe có vẻ nhỏ.
        """
        if self.n < WINDOW // 2 or self.mae is None or self.naive_mae is None:
            return False
        return self.mae < 0.3 and self.mae < self.naive_mae * 0.8

    def describe(self) -> str:
        if self.mae is None:
            return f"chưa chấm được lần nào (còn {len(self._pending)} chờ tới hạn)"
        naive = "" if self.naive_mae is None else f" · đứng yên {self.naive_mae:.2f}"
        verdict = "ĐÁNG TIN" if self.trustworthy else "chưa đáng tin"
        return f"sai số {self.mae:.2f}°C{naive} · {self.n} lần · {verdict}"
