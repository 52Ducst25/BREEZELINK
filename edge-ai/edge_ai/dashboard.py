"""Trang theo dõi trên cổng 7000 — nhìn thấy mô hình đang nghĩ gì.

VÌ SAO CẦN: mọi thứ node này tính ra hiện chỉ nằm trong log. Muốn biết τ đang là
bao nhiêu, dự báo có chuẩn không, góc nào đang hỏng thì phải mở tab Python rồi
đọc chữ. Với một thiết bị treo tường thì đó không phải là cách trả lời câu "nhà
đang thế nào".

DÙNG REST + HỎI LẠI MỖI VÀI GIÂY, KHÔNG DÙNG WEBSOCKET, dù brick có sẵn cả hai.
Kênh WebSocket của nó chạy trên Socket.IO, mà Socket.IO cần thư viện JavaScript
riêng ở phía trình duyệt — thư viện đó bình thường tải từ CDN, và đây là thiết bị
phải chạy được khi mất mạng. Dữ liệu chỉ đổi mỗi 30 giây nên hỏi lại mỗi 3 giây
là quá đủ; đổi lấy một trang không phụ thuộc gì bên ngoài.

TẮT ĐƯỢC VÀ TỰ TẮT: brick ``web_ui`` chỉ tồn tại trong container của App Lab.
Chạy dưới systemd thì import hỏng, và lúc đó dịch vụ vẫn phải chạy bình thường —
trang theo dõi là thứ chỉ-tốt-nếu-có, không phải điều kiện để điều khiển máy lạnh.
"""

import logging
import threading
import time
from typing import Any

logger = logging.getLogger("edge.web")

# Số điểm tối đa trả về cho biểu đồ. 240 điểm trên một khung 1000px là mỗi điểm
# hơn 4px — vẽ dày hơn thì trình duyệt bận hơn mà mắt không thấy khác.
MAX_CHART_POINTS = 240


class Dashboard:
    """Máy chủ web nhỏ phục vụ trang theo dõi. Không có nó dịch vụ vẫn chạy."""

    def __init__(self, history) -> None:
        self._history = history
        self._web: Any = None
        self._state: dict[str, Any] = {"ready": False}

    @property
    def enabled(self) -> bool:
        return self._web is not None

    def start(self) -> None:
        """Dựng máy chủ. Nuốt mọi lỗi — xem chú thích đầu file."""
        try:
            from arduino.app_bricks.web_ui import WebUI
        except ImportError:
            logger.info("Không có brick web_ui (không chạy trong App Lab) — bỏ trang theo dõi")
            return

        try:
            web = WebUI()
            web.expose_api("GET", "/api/state", lambda: self._state)
            web.expose_api("GET", "/api/history", self._history_json)
            web.start()
        except Exception as exc:  # noqa: BLE001
            # RuntimeError nếu thiếu assets/index.html, OSError nếu cổng đã bận.
            logger.warning("Không dựng được trang theo dõi: %s", exc)
            return

        # `start()` MỚI CHỈ CHUẨN BỊ — vòng phục vụ nằm ở `execute()`.
        #
        # Ứng dụng Arduino bình thường gọi `App.run()`, và AppController tự tìm
        # các phương thức tên `execute`/`loop` của mọi brick rồi chạy mỗi cái
        # trong một luồng. Dịch vụ này có vòng đời asyncio riêng nên không dùng
        # `App.run()`, và hệ quả là không ai chạy `execute()` hộ.
        #
        # Triệu chứng lúc quên: `start()` trả về êm đẹp, không lỗi, không cảnh
        # báo — chỉ là không có gì lắng nghe ở cổng 7000. Đã dính đúng một lần.
        #
        # Luồng nền (daemon): nó phải chết theo tiến trình. Không thì nút Stop
        # của App Lab gửi SIGTERM, phần asyncio thoát sạch, còn uvicorn giữ tiến
        # trình sống mãi và App Lab phải giết cứng.
        #
        # GÁN self._web TRƯỚC khi mở luồng: luồng đọc thuộc tính đó, và mở trước
        # là một cuộc đua mà phần lớn thời gian ta thắng — loại lỗi tệ nhất.
        self._web = web
        threading.Thread(target=self._serve, name="web-ui", daemon=True).start()
        logger.info("Trang theo dõi: %s", getattr(web, "url", "http://<ip-bo>:7000"))

    def _serve(self) -> None:
        try:
            self._web.execute()
        except Exception as exc:  # noqa: BLE001
            logger.warning("Trang theo dõi dừng: %s", exc)

    def publish(self, state: dict[str, Any]) -> None:
        """Thay ảnh chụp trạng thái mà trang sẽ đọc ở lần hỏi kế tiếp.

        GÁN NGUYÊN MỘT DICT MỚI chứ không sửa tại chỗ: hàm phục vụ HTTP chạy trên
        luồng khác, và sửa tại chỗ có thể để nó bắt gặp một trạng thái nửa vời.
        Gán một tham chiếu thì nguyên tử.
        """
        self._state = state

    def _history_json(self) -> dict[str, Any]:
        """6 giờ gần nhất cho biểu đồ, đã lấy thưa."""
        rows = self._history.recent(hours=6.0)
        if not rows:
            return {"points": []}
        step = max(1, len(rows) // MAX_CHART_POINTS)
        return {
            "points": [
                {"ts": r.ts, "t_in": round(r.t_in, 2),
                 "t_out": None if r.t_out is None else round(r.t_out, 2),
                 "sp": r.setpoint, "cool": r.cooling_demand > 0}
                for r in rows[::step]
            ]
        }


def build_state(*, snapshot, store, model, score, weather, cloud, settings,
                result, forecast, anomalies) -> dict[str, Any]:
    """Gom mọi thứ trang cần thành một dict thuần JSON.

    HÀM THUẦN, tách khỏi Controller có chủ đích: phần trình bày đổi thường xuyên
    hơn phần điều khiển rất nhiều, và trộn chúng lại thì mỗi lần sửa màu chữ đều
    phải đọc lại đoạn mã ra lệnh cho máy nén.
    """
    now = time.time()
    peak = weather.peak_within(12.0) if weather is not None else None

    return {
        "ready": True,
        "ts": now,
        "indoor": {"t": snapshot.t_in, "h": snapshot.h_in},
        "outdoor": {"t": snapshot.t_out, "h": snapshot.h_out,
                    "online": snapshot.outdoor_online},
        "rooms": [
            {"label": store.label(slot), "t": temp}
            for slot, temp in sorted(store.latest_per_room().items())
        ],
        "ac": {"mode": snapshot.ac_mode.name_str, "setpoint": snapshot.ac_setpoint},
        "link": {"wifi": snapshot.wifi_up, "mqtt": snapshot.mqtt_up,
                 "override": snapshot.override_active},
        "control": {
            "driving": cloud.driving,
            "silence_sec": cloud.silence_sec,
            "takeover_after_sec": settings.takeover_after_sec,
            "advisory_only": settings.advisory_only,
            "want_mode": None if result is None else result.mode.value,
            "want_setpoint": None if result is None else result.t_set,
        },
        "model": {
            "ready": model.ready,
            "samples": model.samples,
            "tau_min": model.tau_min,
            "outdoor_gain": model.outdoor_gain,
            "cool_gain": model.cool_gain,
            "describe": model.describe(),
        },
        "score": {
            "n": score.n, "mae": score.mae, "naive_mae": score.naive_mae,
            "trustworthy": score.trustworthy, "describe": score.describe(),
        },
        "forecast_15": forecast,
        "weather": {
            "fresh": weather.fresh if weather is not None else False,
            "age_min": weather.age_min if weather is not None else None,
            "peak_t": None if peak is None else peak[0],
            "peak_ts": None if peak is None else peak[1],
            # Đường dự báo 12 giờ tới, mỗi giờ một điểm — đủ để thấy hình dáng.
            "hours": [] if weather is None else [
                {"ts": now + h * 3600, "t": weather.temp_at(now + h * 3600)}
                for h in range(13)
            ],
        },
        "anomalies": [f"{a.kind}: {a.detail}" for a in anomalies],
    }
