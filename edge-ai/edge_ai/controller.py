"""The edge-AI control loop: ingest gateway snapshots, forecast, decide, act.

Runs on the Linux half of the Arduino UNO Q (Debian on the Dragonwing QRB2210).
The STM32 half takes no part in this path — "edge AI" here means a Python
service on a small computer, not a sketch on a microcontroller.

WHAT IT ADDS OVER THE CLOUD, given the cloud already runs the same algorithm:

  1. It keeps working when the house loses internet. The cloud comfort loop is
     the only thing that adapts the setpoint to the weather, so an outage
     freezes the air conditioner at whatever it was last told. This node hears
     the gateway say "the server has been quiet for 300 seconds" and takes over
     — over Bluetooth, a link that does not care whether the router is up.
  2. It keeps per-corner history the cloud only stores as a median, which is
     what makes a forecast and per-sensor anomaly detection possible at all.

WHAT IT DELIBERATELY DOES NOT DO: override the cloud while the cloud is alive.
See cloud_watch.py — two commanders is worse than a slow one.
"""

import json
import logging
import os
import time
from datetime import datetime, timezone

from edge_ai.cloud_watch import CloudWatch
from edge_ai.comfort_bridge import (
    ComfortConfig,
    ComfortInputs,
    NoIrCodesError,
    compute,
)
from edge_ai.predictor import find_anomalies, fit_trend
from edge_ai.protocol import KIND_ADVICE, KIND_COMMAND, Mode, Snapshot
from edge_ai.room_store import RoomStore

logger = logging.getLogger("edge.control")

# Mirrors src/app/services/config_service.DEFAULT_CONFIG.
#
# NOT IMPORTED FROM THERE ON PURPOSE, unlike the algorithm itself: that module
# pulls in SQLAlchemy and the ORM models, which is a heavy dependency to install
# on a field device for eleven numbers. The algorithm is imported (see
# comfort_bridge) because it is pure and because two implementations of the
# maths would be a real hazard; eleven constants that a human can diff are not.
#
# A HOUSEHOLD WHOSE ADMIN TUNED ITS CONFIG MUST PASTE ITS OWN via
# EDGE_COMFORT_CONFIG — otherwise this node computes with factory defaults and
# quietly disagrees with the cloud about the same room.
_FALLBACK_CFG = {
    "ema_alpha": 0.2,
    "deadband": 0.8,
    "dwell_sec": 600,
    "dry_rh": 72.0,
    "humid_slope": 0.05,
    "clamp_min": 24.0,
    "clamp_max": 28.0,
    "override_hours": 2,
    "night_start": 23,
    "night_end": 6,
    "night_offset": 0.5,
}

# How far ahead the forecast looks. 15 minutes is roughly how long this system
# takes to move a room a degree, so it is the horizon at which a prediction can
# still change what you would do now.
_FORECAST_MIN = 15.0

# Trung vị của edge và của gateway lệch quá mức này thì hai bản luật đã trôi
# khỏi nhau. 0.05 °C là dưới mức làm tròn của chính giao thức (số đo đi trên dây
# ở đơn vị 0.01 °C), nên bất cứ thứ gì lớn hơn đều là lệch thật, không phải nhiễu.
_MEDIAN_DRIFT_C = 0.05


class Controller:
    def __init__(self, settings, link) -> None:
        self._s = settings
        self._link = link
        self._store = RoomStore(settings.history_sec)
        self._cloud = CloudWatch(settings.takeover_after_sec)
        self._cfg = self._load_cfg()

        # Trạng thái mà bản MQTT trước phải tự dựng bằng cách nghe lén cả bốn
        # loại topic. Nay gateway gửi thẳng trong mỗi ảnh chụp — nó là bên THẬT
        # SỰ biết máy lạnh đang ở chế độ nào, vì chính nó bắn khung IR ra.
        self._prev_mode = Mode.OFF
        self._prev_setpoint: int | None = None
        self._last_switch_ts = 0.0
        self._tout_ema: float | None = None

        # Nhiệt độ mà hộ này thật sự có mã IR. Chỉ biết được bằng cách QUAN SÁT
        # máy chủ ra lệnh — đoán một dải liên tục sẽ sinh ra lệnh mà gateway
        # lặng lẽ không phát nổi vì chưa học mã.
        self._seen_temps: set[int] = set()

    # -- config ---------------------------------------------------------------

    def _load_cfg(self) -> ComfortConfig:
        raw = os.getenv("EDGE_COMFORT_CONFIG", "").strip()
        if not raw:
            logger.warning(
                "EDGE_COMFORT_CONFIG chưa đặt — dùng cấu hình mặc định. Nếu hộ này "
                "đã tinh chỉnh thuật toán trên web, edge sẽ tính LỆCH với máy chủ."
            )
            return ComfortConfig.from_dict(_FALLBACK_CFG)
        return ComfortConfig.from_dict({**_FALLBACK_CFG, **json.loads(raw)})

    # -- ingest ---------------------------------------------------------------

    def on_snapshot(self, snapshot: Snapshot) -> None:
        """Gọi từ tác vụ BLE mỗi lần gateway notify. Chỉ ghi nhận, không quyết
        định gì — quyết định nằm ở tick(), chạy theo nhịp riêng."""
        self._store.ingest(snapshot)
        self._cloud.update(snapshot.cloud_silence_sec)

        # Trạng thái máy lạnh: lấy từ gateway, không tự suy. Chỉ một MODE thật sự
        # đổi mới khởi động lại đồng hồ dwell của máy nén; đổi riêng nhiệt độ đặt
        # thì không, nếu không việc chuyển chế độ sẽ bị đóng băng.
        if snapshot.ac_mode is not Mode.UNKNOWN and snapshot.ac_mode != self._prev_mode:
            self._prev_mode = snapshot.ac_mode
            self._last_switch_ts = time.time()
        if snapshot.ac_setpoint is not None:
            self._prev_setpoint = snapshot.ac_setpoint
            # Máy chủ chỉ ra lệnh những tổ hợp nó có mã, nên mỗi setpoint gateway
            # báo đã thi hành là một bằng chứng "hộ này có mã cho nhiệt độ đó".
            if snapshot.ac_mode is Mode.COOL:
                self._seen_temps.add(snapshot.ac_setpoint)

    # -- decide ---------------------------------------------------------------

    async def tick(self) -> None:
        """One control cycle. Never raises — a field service that dies on an
        edge case is worse than one that logs and tries again in 30 seconds."""
        try:
            await self._tick()
        except Exception:  # noqa: BLE001
            logger.exception("Vòng điều khiển lỗi — bỏ nhịp này")

    async def _tick(self) -> None:
        snapshot = self._store.latest
        if snapshot is None:
            logger.info("Chưa nhận được ảnh chụp nào từ gateway — không tính")
            return

        if snapshot.t_in is None or snapshot.h_in is None:
            logger.info("Gateway báo chưa có góc phòng nào còn tươi — không tính, không ra lệnh")
            return

        # Kiểm chéo: tự tính lại trung vị bằng đúng hàm của backend. Lệch nghĩa
        # là bản C++ trên gateway và bản Python trên cloud đã trôi khỏi nhau, và
        # đó là loại lỗi không có triệu chứng nào khác ngoài dòng log này.
        mine = self._store.indoor_check()
        if mine is not None and abs(mine.temp - snapshot.t_in) > _MEDIAN_DRIFT_C:
            logger.warning(
                "TRUNG VỊ LỆCH: gateway %.2f°C, edge tính %.2f°C từ %d góc — "
                "luật gộp ở room-registry.cpp và room_aggregate.py đã trôi khỏi nhau",
                snapshot.t_in, mine.temp, mine.used,
            )

        if snapshot.t_out is None and self._tout_ema is None:
            logger.info("Chưa có số đo ngoài trời — không tính")
            return
        tout = snapshot.t_out if snapshot.t_out is not None else self._tout_ema

        trends = {s: t for s in self._store.slots()
                  if (t := fit_trend([(x.ts, x.temp) for x in self._store.history(s)]))}
        latest = self._store.latest_per_room()
        anomalies = find_anomalies(latest, snapshot.t_in, trends)
        for a in anomalies:
            logger.warning("Bất thường ở %s (%s): %s",
                           self._store.label(int(a.device_uuid)), a.kind, a.detail)

        forecast = None
        if trends:
            avg_slope = sum(t.slope_per_min for t in trends.values()) / len(trends)
            forecast = snapshot.t_in + avg_slope * _FORECAST_MIN

        available = sorted(self._seen_temps)
        if not available:
            logger.info("Chưa thấy máy chủ ra lệnh COOL lần nào — chưa biết hộ này "
                        "đã học những nhiệt độ nào, chỉ đề xuất")

        result = None
        if available and not snapshot.override_active:
            try:
                result = compute(
                    ComfortInputs(
                        cfg=self._cfg,
                        tin=snapshot.t_in,
                        hin=snapshot.h_in,
                        tout=tout,
                        tout_ema_prev=self._tout_ema,
                        outdoor_stale=not snapshot.outdoor_online,
                        prev_mode=_to_ac_mode(self._prev_mode),
                        last_switch_ts=self._last_switch_ts,
                        now=datetime.now(timezone.utc),
                        override_active=False,
                        available_ir_temps=available,
                        # Node ngoài trời còn nhịp tim thì số này là số ngoài trời
                        # thật, nên được phép đẩy trung bình trượt. Mất nhịp thì
                        # dùng lại EMA cũ mà KHÔNG trộn thêm — đúng cổng
                        # `is_outdoor_tick` của comfort_engine.
                        is_outdoor_tick=snapshot.outdoor_online and snapshot.t_out is not None,
                    )
                )
            except NoIrCodesError:
                logger.warning("Hộ này chưa học mã IR nào — không điều khiển được")

        if result is not None and snapshot.outdoor_online and snapshot.t_out is not None:
            self._tout_ema = result.new_ema

        await self._act(snapshot, result, forecast, len(anomalies))

    # -- act ------------------------------------------------------------------

    async def _act(self, snapshot: Snapshot, result, forecast, anomaly_count: int) -> None:
        driving = self._cloud.driving
        silence = self._cloud.silence_sec
        rooms = f"{snapshot.room_count}/{sum(1 for r in snapshot.rooms if r.corner is not None)}"

        if result is None:
            logger.info("t_in=%.1f (%s góc) t_out=%s · %s · chưa có quyết định",
                        snapshot.t_in, rooms,
                        "—" if snapshot.t_out is None else f"{snapshot.t_out:.1f}",
                        "EDGE CẦM LÁI" if driving else "máy chủ cầm lái")
            return

        logger.info(
            "t_in=%.1f (%s góc) t_out=%s · dự báo 15' %s · %s -> %s %d°C · %s%s",
            snapshot.t_in, rooms,
            "—" if snapshot.t_out is None else f"{snapshot.t_out:.1f}",
            "—" if forecast is None else f"{forecast:.1f}",
            self._prev_mode.name_str, result.mode.value, result.t_set,
            "EDGE CẦM LÁI" if driving else f"máy chủ cầm lái (im {silence:.0f}s)"
            if silence is not None else "máy chủ chưa từng ra lệnh",
            f" · {anomaly_count} bất thường" if anomaly_count else "",
        )

        mode = Mode[result.mode.value]

        # ĐỀ XUẤT là mặc định, ở mọi đường. Gateway chỉ bắn hồng ngoại khi nhận
        # KIND_COMMAND, nên mọi nhánh không đủ điều kiện đều rơi về đây một cách
        # an toàn thay vì phải nhớ chặn.
        kind = KIND_ADVICE
        reason = None

        if not driving:
            reason = None                       # bình thường: cloud đang lo
        elif self._s.advisory_only:
            reason = "EDGE_ADVISORY_ONLY đang bật"
        elif snapshot.override_active:
            reason = "người dùng đang giữ quyền điều khiển"
        elif result.mode.value == self._prev_mode.name_str and result.t_set == self._prev_setpoint:
            reason = "quyết định không đổi"      # cùng luật với cloud: chỉ gửi khi đổi
        else:
            kind = KIND_COMMAND

        if kind == KIND_COMMAND:
            sent = await self._link.send(kind=KIND_COMMAND, mode=mode, setpoint=result.t_set)
            if sent:
                logger.warning("ĐÃ RA LỆNH (edge cầm lái): %s %d°C", result.mode.value, result.t_set)
                self._prev_mode = mode
                self._prev_setpoint = result.t_set
            else:
                logger.warning("Muốn ra lệnh %s %d nhưng chưa nối được gateway",
                               result.mode.value, result.t_set)
            return

        if driving and reason:
            logger.info("Đang cầm lái nhưng không ra lệnh: %s", reason)
        await self._link.send(kind=KIND_ADVICE, mode=mode, setpoint=result.t_set)


def _to_ac_mode(mode: Mode):
    """Mode trên dây -> AcMode của backend (comfort_engine nhận kiểu đó)."""
    from edge_ai.comfort_bridge import AcMode

    try:
        return AcMode(mode.name_str)
    except ValueError:
        return AcMode.OFF
