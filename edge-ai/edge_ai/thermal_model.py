"""Mô hình nhiệt của phòng: ARX bậc nhất, 4 tham số, khớp trực tuyến.

    T_in[k+1] = a·T_in[k] + b·T_out[k] + c·u[k] + d

    u[k] = max(0, T_in[k] − T_set[k])  khi COOL, ngược lại 0   (xem history_store)

VÌ SAO ĐÚNG 4 THAM SỐ — ĐÂY LÀ RÀNG BUỘC CỦA DỮ LIỆU, KHÔNG PHẢI SỞ THÍCH:
  Nhìn "12.644 điểm đo" rồi kết luận đủ cho mạng nơ-ron là sai, vì các điểm không
  độc lập. Hai số đo cách nhau 60 giây trong một phòng có hằng số thời gian hàng
  chục phút thì gần như là cùng một số. Số mẫu ĐỘC LẬP ≈ thời lượng ÷ τ:

      2 ngày dữ liệu 4 góc ÷ τ≈45 phút  ≈  60 sự kiện nhiệt độc lập

  4 tham số / 60 mẫu độc lập là tỉ lệ lành mạnh. Một MLP 32→64→3 có ~2.400 tham
  số — cần khoảng hai NĂM tích luỹ với nhịp hiện tại. Xem thêm: chỉ 24 đoạn COOL
  dài trên 20 phút trong toàn bộ lịch sử.

BA CON SỐ VẬT LÝ RÚT RA ĐƯỢC, và đây là thứ một mô hình hộp đen không cho:
  τ = −Δt/ln(a)     hằng số thời gian phòng
  b/(1−a)           nắng ngoài trời ăn vào phòng bao nhiêu
  −c/(1−a)          công suất lạnh thực tế — tụt dần nghĩa là máy yếu đi/bẩn lọc

TÍNH TRÊN ĐỘ LỆCH so với T_REF chứ không trên số tuyệt đối: T_in và T_out đều
quanh 32 °C và tương quan rất mạnh với số hạng hằng, nên ma trận chuẩn hoá gần
suy biến. Trừ đi 30 là điều kiện số tốt lên hẳn mà không đổi ý nghĩa tham số nào
ngoài ``d``.
"""

import logging
import math
from collections.abc import Callable

import numpy as np

from edge_ai.history_store import Row

logger = logging.getLogger("edge.model")

T_REF = 30.0            # °C, gốc quy chiếu — xem chú thích đầu file

# NHỊP MÔ HÌNH ≠ NHỊP LƯU TRỮ. Lịch sử ghi mỗi 60 giây (đủ dày cho phát hiện bất
# thường và cho biểu đồ), còn mô hình chỉ lấy mỗi mẫu thứ 5.
#
# VÌ SAO PHẢI THƯA HƠN: cái xác định `a` là lượng nhiệt độ THAY ĐỔI được giữa hai
# mẫu. Ở 60 giây với τ≈45 phút thì a≈0.978, tức là mỗi bước phòng chỉ đổi 2.2%
# quãng đường — nhỏ ngang nhiễu cảm biến. Nhiễu đó nằm ngay trong biến hồi quy
# nên nó kéo `a` xuống một cách CÓ HỆ THỐNG (sai lệch suy giảm).
#
# Đo trên dữ liệu giả có đáp án (τ thật 45 phút):
#     Δt = 60s,  không trung bình:  τ = 24 phút   (sai 47%)
#     Δt = 300s, có trung bình:     xem check-thermal-model.py
#
# Ở 300 giây a≈0.895 — mỗi bước đổi 10.5% quãng đường, gấp 5 lần tín hiệu so với
# cùng mức nhiễu. Vẫn còn 9 điểm trong mỗi τ, thừa để mô tả một hàm mũ.
DT_SEC = 300.0

# Cặp mẫu lệch nhịp quá mức này thì BỎ, không co giãn. Mô hình ARX giả định Δt cố
# định; nhét một khoảng 20 phút vào cùng phương trình với khoảng 5 phút sẽ làm `a`
# sai lệch một cách âm thầm. Gateway mất mạng vài phút là chuyện thường.
DT_MIN_SEC, DT_MAX_SEC = 270.0, 330.0

# Hệ số quên. 0.995 ở nhịp 300s → nửa đời ~11.5 giờ: đủ chậm để không đuổi theo
# nhiễu cảm biến, đủ nhanh để mùa đổi hay đổi phòng thì mô hình theo kịp trong
# khoảng một ngày.
FORGET = 0.995

# Dưới ngưỡng này thì mọi con số rút ra chỉ là nhiễu đội lốt tham số. 120 bước ×
# 5 phút = 10 giờ, tức là phải thấy ít nhất một chu kỳ nóng-mát trong ngày.
MIN_SAMPLES = 120

# ============================================================================
#  VÙNG CÓ NGHĨA VẬT LÝ — chặn theo τ VÀ theo hai hệ số dẫn xuất
# ----------------------------------------------------------------------------
#  CHẶN THEO τ CHỨ KHÔNG THEO `a`, và đây là sửa một lỗi đã xảy ra thật.
#
#  Bản trước viết thẳng `0.5 < a < 0.9999`. Hai vấn đề, cái thứ hai mới nặng:
#
#  1. Ý NGHĨA CỦA `a` PHỤ THUỘC DT_SEC. Ai đổi nhịp mô hình thì cùng một ngưỡng
#     `a` lặng lẽ mang một τ khác hẳn, mà không dòng nào trong điều kiện nói ra
#     điều đó. Chặn thẳng τ thì ý định nằm ngay trong biểu thức.
#
#  2. 0.9999 LỎNG TỚI MỨC VÔ DỤNG. Ở Δt = 5 phút:
#         a < 0.9999  ->  τ tối đa 49.998 phút = 34,7 NGÀY
#     Không phòng nào có hằng số thời gian 34 ngày, nhưng mọi giá trị tới đó đều
#     được nhận là "có nghĩa vật lý".
#
#  ĐÃ DÍNH ĐÚNG CA ĐÓ, và triệu chứng trông như mô hình đang chạy tốt:
#         τ = 49.502 phút · ngoài trời ×303,33 · lạnh 283,45 °C/đv · 332 mẫu
#         huy hiệu trên trang theo dõi: "đã khớp"
#     Tính ngược ra thì a = 0,9998990 — lọt qua ngưỡng cũ đúng ở chữ số thứ tư.
#     `b` = 0,0306 và `c` = −0,0286 đều BÌNH THƯỜNG; cả ba con số hiển thị nổ
#     tung chỉ vì chúng đều chia cho (1−a) = 0,0001.
#
#  `a → 1` nghĩa là mô hình kết luận "nhiệt độ trong nhà không bao giờ đổi" —
#  T_in[k+1] = T_in[k]. Đó không phải một mô hình, đó là phép đo lặp lại chính
#  nó. Nguyên nhân thường là dữ liệu KHÔNG CÓ KÍCH THÍCH: phòng phẳng lì suốt
#  lịch sử, hoặc hai cảm biến trong/ngoài đang đo cùng một chỗ nên hai cột hồi
#  quy trùng nhau và ma trận thiết kế suy biến.
TAU_MIN_MIN, TAU_MAX_MIN = 10.0, 300.0

# Hai hệ số dẫn xuất cũng phải nằm trong vùng vật lý. Chặn τ đã đủ cho ca trên
# (cả ba cùng chia (1−a)), nhưng hai ngưỡng này bắt được những kiểu hỏng mà τ
# không thấy — ví dụ `b` sai dấu: τ đẹp mà "ngoài trời nóng lên thì phòng lạnh đi".
#
# Nới một chút về phía âm thay vì chặn ở 0: lúc mới khớp, hệ số của một tác động
# yếu có thể lệch qua 0 vì nhiễu. Chặn cứng ở 0 sẽ khiến một phòng gần như không
# chịu ảnh hưởng ngoài trời KHÔNG BAO GIỜ khớp được, mà không ai đoán ra vì sao.
OUTDOOR_GAIN_LO, OUTDOOR_GAIN_HI = -0.2, 1.5     # ×, ở trạng thái dừng
COOL_GAIN_LO, COOL_GAIN_HI = -0.5, 10.0          # °C cho mỗi đơn vị nhu cầu lạnh


def _a_from_tau(tau_min: float) -> float:
    """`a` tương ứng với một hằng số thời gian, ở nhịp DT_SEC."""
    return math.exp(-(DT_SEC / 60.0) / tau_min)


class ThermalModel:
    """RLS cho ARX bậc nhất. Khớp mẻ lúc khởi động, rồi cập nhật trực tuyến."""

    def __init__(self, forget: float = FORGET) -> None:
        self._forget = forget
        # Tiên nghiệm "nhiệt độ giữ nguyên": a=1, còn lại 0. Đây là giả thuyết
        # vô hại nhất — nó không khẳng định điều gì về phòng, và mọi dữ liệu thật
        # đều kéo nó ra khỏi đây.
        self._theta = np.array([1.0, 0.0, 0.0, 0.0])
        self._p = np.eye(4)
        self._n = 0
        self._prev: Row | None = None

    # -- trạng thái ------------------------------------------------------------

    @property
    def samples(self) -> int:
        return self._n

    @staticmethod
    def _reject(theta) -> str | None:
        """Vì sao bộ tham số này không dùng được. ``None`` = dùng được.

        TRẢ VỀ LÝ DO CHỨ KHÔNG PHẢI True/False, có chủ đích: cùng một hàm vừa
        làm cổng chặn (``ready``) vừa làm lời giải thích (``describe`` và log
        khớp mẻ). Tách thành hai chỗ là đúng cái đã sinh ra lỗi cũ — điều kiện
        ``0.5 < a < 0.9999`` từng được chép ở hai nơi và sửa một nơi là quên nơi
        kia.
        """
        a, b, c, _ = (float(v) for v in theta)

        # Ba ca hỏng của `a` cần ba câu khác nhau: chúng dẫn tới ba chỗ phải đi
        # tìm khác nhau, gộp thành "a ngoài khoảng" là ném đi manh mối.
        if a >= 1.0:
            return f"a={a:.5f} ≥ 1 — mô hình tin phòng tự nóng lên vô hạn"
        if a <= 0.0:
            return f"a={a:.5f} ≤ 0 — mô hình dao động đổi dấu mỗi bước"

        tau = -(DT_SEC / 60.0) / math.log(a)
        if not (TAU_MIN_MIN <= tau <= TAU_MAX_MIN):
            return (f"τ={tau:.0f}' ngoài khoảng {TAU_MIN_MIN:.0f}'..{TAU_MAX_MIN:.0f}' "
                    f"(a={a:.5f}) — dữ liệu có thể chưa đủ kích thích")

        inv = 1.0 - a
        og, cg = b / inv, -c / inv
        if not (OUTDOOR_GAIN_LO <= og <= OUTDOOR_GAIN_HI):
            return f"ngoài trời ăn vào ×{og:.2f}, ngoài khoảng {OUTDOOR_GAIN_LO}..{OUTDOOR_GAIN_HI}"
        if not (COOL_GAIN_LO <= cg <= COOL_GAIN_HI):
            return f"công suất lạnh {cg:.2f}°C/đv, ngoài khoảng {COOL_GAIN_LO}..{COOL_GAIN_HI}"
        return None

    @property
    def ready(self) -> bool:
        """Đã đủ dữ liệu VÀ tham số nằm trong vùng có nghĩa vật lý.

        Vùng đó do ``_reject()`` định nghĩa — xem khối TAU_MIN_MIN ở đầu file
        cho lý do chặn theo τ chứ không theo `a`.
        """
        return self._n >= MIN_SAMPLES and self._reject(self._theta) is None

    @property
    def tau_min(self) -> float | None:
        """Hằng số thời gian của phòng, phút."""
        if not self.ready:
            return None
        return -(DT_SEC / 60.0) / math.log(self._theta[0])

    @property
    def outdoor_gain(self) -> float | None:
        """Ở trạng thái dừng, ngoài trời tăng 1 °C thì trong nhà tăng bao nhiêu."""
        if not self.ready:
            return None
        return float(self._theta[1] / (1.0 - self._theta[0]))

    @property
    def cool_gain(self) -> float | None:
        """°C hạ được cho mỗi đơn vị nhu cầu làm lạnh, ở trạng thái dừng.

        DƯƠNG nghĩa là làm lạnh có tác dụng (c âm). Số này tụt dần theo tuần là
        tín hiệu máy yếu đi — thứ không cảm biến nào đo trực tiếp được.
        """
        if not self.ready:
            return None
        return float(-self._theta[2] / (1.0 - self._theta[0]))

    def describe(self) -> str:
        """Một câu nói ĐÚNG chuyện đang xảy ra — trang theo dõi hiện thẳng câu này.

        PHẢI TÁCH "thiếu mẫu" KHỎI "đủ mẫu nhưng tham số vô nghĩa". Bản trước gộp
        cả hai vào một câu, nên ca đã gặp thật hiện ra là:

            "chưa đủ dữ liệu (332/120 mẫu)"

        — một câu tự mâu thuẫn, và nó gửi người đọc đi chờ thêm dữ liệu trong khi
        vấn đề là dữ liệu KHÔNG CÓ KÍCH THÍCH, chờ bao lâu cũng không tự khỏi.
        """
        if self._n < MIN_SAMPLES:
            return f"chưa đủ dữ liệu ({self._n}/{MIN_SAMPLES} mẫu)"
        why = self._reject(self._theta)
        if why is not None:
            return f"đủ mẫu ({self._n}) nhưng tham số vô nghĩa — {why}"
        return (f"τ={self.tau_min:.0f}' · ngoài trời×{self.outdoor_gain:.2f} · "
                f"lạnh {self.cool_gain:.2f}°C/đv · {self._n} mẫu")

    # -- khớp ------------------------------------------------------------------

    @staticmethod
    def _design(rows: list[Row]) -> tuple[np.ndarray, np.ndarray]:
        """Dựng ma trận hồi quy, lấy thưa về nhịp DT_SEC.

        Duyệt tiến và chỉ nhận cặp cách nhau đúng một bước mô hình. Mẫu ở giữa
        KHÔNG bị bỏ phí — chúng đã góp vào trung bình phút ở HistoryStore, và
        chúng vẫn ở đó cho phát hiện bất thường.
        """
        phis, ys = [], []
        prev: Row | None = None
        for cur in rows:
            if prev is None:
                prev = cur
                continue
            dt = cur.ts - prev.ts
            if dt < DT_MIN_SEC:
                continue                    # chưa đủ một bước, chờ tiếp
            if dt > DT_MAX_SEC or prev.t_out is None:
                prev = cur                  # đứt quãng: bỏ cặp, bắt đầu lại từ đây
                continue
            phis.append([prev.t_in - T_REF, prev.t_out - T_REF, prev.cooling_demand, 1.0])
            ys.append(cur.t_in - T_REF)
            prev = cur
        return np.array(phis), np.array(ys)

    def load_history(self, rows: list[Row]) -> bool:
        """Nạp toàn bộ lịch sử đã lưu. Gọi lúc khởi động.

        HAI ĐƯỜNG, VÀ ĐƯỜNG THỨ HAI MỚI LÀ ĐƯỜNG HAY DÙNG:

          đủ ≥120 cặp  -> khớp bình phương tối thiểu một phát, xong ngay
          chưa đủ      -> PHÁT LẠI từng mẫu qua observe(), học được gì hay nấy

        Bản trước chỉ có đường thứ nhất và trả False ở mọi trường hợp khác — tức
        là VỨT SẠCH lịch sử. Hậu quả nặng hơn vẻ ngoài rất nhiều:

            Chạy 9 tiếng, có 108 cặp trên đĩa, cúp điện một cái là `_n` về 0.
            Dữ liệu vẫn nằm nguyên trong file, nhưng mô hình coi như chưa từng
            thấy, và đồng hồ 10 tiếng chạy lại từ đầu.

        Nghĩa là lịch sử cục bộ chỉ có ích khi nó đã tự vượt 120 cặp — đúng cái
        ngưỡng mà nó sinh ra để giúp đạt tới. Phát lại phá được vòng luẩn quẩn
        đó: 13 cặp trên đĩa cho ra 13/120, không phải 0/120.

        Vì sao không luôn luôn phát lại cho gọn: khớp mẻ dùng TOÀN BỘ dữ liệu
        cùng lúc nên không bị thứ tự chi phối, còn RLS có hệ số quên nên mẫu cũ
        nhẹ dần. Khi đã đủ dữ liệu thì khớp mẻ cho tham số tốt hơn.
        """
        phi, y = self._design(rows)

        if len(y) >= MIN_SAMPLES:
            theta, *_ = np.linalg.lstsq(phi, y, rcond=None)
            # CÙNG MỘT PHÉP KIỂM với `ready`, gọi chung một hàm. Trước đây chỗ này
            # chép lại điều kiện `0.5 < theta[0] < 0.9999` — hai bản của cùng một
            # luật, và sửa một bên là quên bên kia.
            why = self._reject(theta)
            if why is None:
                self._theta = theta
                # P ≈ nghịch đảo ma trận thông tin: nói với RLS rằng "đã biết
                # chừng này rồi", nên nó không nhảy dựng lên vì một mẫu nhiễu.
                self._p = np.linalg.inv(phi.T @ phi + 1e-6 * np.eye(4))
                self._n = len(y)
                logger.info("Khớp mẻ từ %d cặp mẫu · %s", len(y), self.describe())
                return True
            logger.warning("Khớp mẻ từ %d cặp bị loại — %s. Chuyển sang phát lại.",
                           len(y), why)

        # Phát lại. observe() tự lọc nhịp và tự xử lý chỗ đứt quãng, nên không
        # cần chuẩn bị gì thêm — chỉ cần bảo đảm bắt đầu từ trạng thái sạch.
        self._prev = None
        used = sum(1 for row in rows if self.observe(row))
        logger.info("Phát lại %d/%d mẫu lịch sử · %s", used, len(rows), self.describe())
        return self.ready

    def observe(self, row: Row) -> bool:
        """Nạp một mẫu 60 giây. Trả True nếu đã dùng nó để cập nhật tham số.

        TỰ LẤY THƯA: mẫu vào mỗi phút nhưng chỉ mỗi bước 5 phút mới sinh ra một
        lần cập nhật. Cùng đúng quy tắc với ``_design`` — hai đường phải khớp
        nhau, nếu không thì khớp mẻ và khớp trực tuyến sẽ ước lượng hai mô hình
        khác nhau trên cùng một dữ liệu.
        """
        prev = self._prev
        if prev is None:
            self._prev = row
            return False

        dt = row.ts - prev.ts
        if dt < DT_MIN_SEC:
            return False                    # chưa đủ một bước — GIỮ NGUYÊN prev
        if dt > DT_MAX_SEC or prev.t_out is None:
            self._prev = row                # đứt quãng: bắt đầu lại từ mẫu này
            return False
        self._prev = row

        phi = np.array([prev.t_in - T_REF, prev.t_out - T_REF, prev.cooling_demand, 1.0])
        y = row.t_in - T_REF

        # RLS có hệ số quên.
        p_phi = self._p @ phi
        denom = self._forget + float(phi @ p_phi)
        gain = p_phi / denom
        self._theta = self._theta + gain * (y - float(phi @ self._theta))
        self._p = (self._p - np.outer(gain, p_phi)) / self._forget
        self._n += 1
        return True

    # -- dự báo ----------------------------------------------------------------

    def predict(
        self,
        minutes: float,
        t_in: float,
        t_out_now: float,
        setpoint: int | None,
        cooling: bool,
        t_out_at: Callable[[float], float | None] | None = None,
        now_ts: float = 0.0,
    ) -> float | None:
        """Nhiệt độ trong nhà sau ``minutes`` phút.

        VÒNG KÍN, không phải vòng hở: mỗi bước tính lại ``u`` từ nhiệt độ vừa dự
        báo. Giữ ``u`` cố định sẽ cho ra "phòng lạnh mãi không dừng", vì trong
        thực tế nhu cầu tự teo đi khi phòng tiến về nhiệt độ đặt.

        ``t_out_at`` là chỗ DỰ BÁO THỜI TIẾT cắm vào (phần B). Không có nó thì
        giữ nguyên nhiệt độ ngoài trời hiện tại — đúng cho 15 phút, sai dần cho
        những khoảng dài hơn, và đó chính là lý do phần B đáng làm.
        """
        if not self.ready:
            return None

        a, b, c, d = (float(v) for v in self._theta)
        steps = max(1, int(round(minutes * 60.0 / DT_SEC)))
        x = t_in - T_REF

        for i in range(steps):
            t_out = None
            if t_out_at is not None:
                t_out = t_out_at(now_ts + i * DT_SEC)
            w = (t_out if t_out is not None else t_out_now) - T_REF

            u = 0.0
            if cooling and setpoint is not None:
                u = max(0.0, (x + T_REF) - float(setpoint))

            x = a * x + b * w + c * u + d

        return x + T_REF
