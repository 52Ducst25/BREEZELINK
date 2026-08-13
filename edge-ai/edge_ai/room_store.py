"""Sliding window of the readings the gateway has reported.

The gateway already sends the household median in every snapshot, so why keep
history at all? Because the median is a single number about *now*, and the two
things this service adds on top of the cloud both need more than that:

  - forecasting needs the SHAPE of the last half hour, per corner
  - anomaly detection needs to compare corners against each other over time

The gateway cannot keep that — it has 320 KB of RAM and a screen to draw. This
node has a quad-core A53 and a filesystem.

Memory is bounded by time, not by count: at a 5 s snapshot cadence a 30-minute
window is ~360 samples per corner. Trimming by age rather than by a fixed length
means a burst of snapshots (reconnect, gateway restart) cannot push the older
history out of the window.
"""

import time
from collections import defaultdict, deque
from dataclasses import dataclass

from edge_ai.comfort_bridge import RoomReading, aggregate_rooms
from edge_ai.protocol import Snapshot


@dataclass(frozen=True)
class Sample:
    ts: float
    temp: float
    humidity: float


class RoomStore:
    """Per-corner history, fed one gateway snapshot at a time."""

    def __init__(self, history_sec: float) -> None:
        self._history_sec = history_sec
        self._rooms: dict[int, deque[Sample]] = defaultdict(deque)
        self._corner_label: dict[int, int | None] = {}
        self._outdoor: deque[Sample] = deque()
        self._latest: Snapshot | None = None

    # -- ingest ---------------------------------------------------------------

    def ingest(self, snapshot: Snapshot, ts: float | None = None) -> None:
        """Nạp một ảnh chụp. Ô nào không có số đo thì KHÔNG ghi gì cả — chèn một
        mẫu rỗng vào lịch sử sẽ làm hồi quy tưởng nhiệt độ vừa nhảy về 0."""
        now = time.time() if ts is None else ts
        self._latest = snapshot

        for slot, room in enumerate(snapshot.rooms):
            self._corner_label[slot] = room.corner
            if room.temp is None or room.humidity is None:
                continue
            self._append(self._rooms[slot], room.temp, room.humidity, now)

        if snapshot.t_out is not None and snapshot.h_out is not None:
            self._append(self._outdoor, snapshot.t_out, snapshot.h_out, now)

    def _append(self, window: deque[Sample], temp: float, humidity: float, now: float) -> None:
        window.append(Sample(now, temp, humidity))
        cutoff = now - self._history_sec
        while window and window[0].ts < cutoff:
            window.popleft()

    # -- read -----------------------------------------------------------------

    @property
    def latest(self) -> Snapshot | None:
        return self._latest

    def slots(self) -> list[int]:
        return sorted(self._rooms)

    def label(self, slot: int) -> str:
        """Nhãn người đọc được cho một ô — "góc 2" nếu node có khai, còn không
        thì số ô. Không bịa ra một tên nghe có vẻ chính xác khi không biết."""
        corner = self._corner_label.get(slot)
        return f"góc {corner + 1}" if corner is not None else f"ô {slot}"

    def history(self, slot: int) -> list[Sample]:
        return list(self._rooms.get(slot, ()))

    def outdoor_history(self) -> list[Sample]:
        return list(self._outdoor)

    def latest_per_room(self) -> dict[int, float]:
        return {s: w[-1].temp for s, w in self._rooms.items() if w}

    def indoor_check(self):
        """Tự tính lại trung vị từ số của từng góc trong ảnh chụp mới nhất.

        Dùng ĐÚNG hàm mà worker trên cloud dùng (comfort_bridge -> app.comfort),
        nên nếu con số này lệch với `snapshot.t_in` mà gateway đang hiện trên
        tường thì có hai bản luật đã trôi khỏi nhau — và biết được điều đó là lý
        do tồn tại của hàm này. Trả None khi ảnh chụp không có góc nào.
        """
        if self._latest is None:
            return None
        readings = [
            RoomReading(device_id=str(i), temp=r.temp, humidity=r.humidity, age_sec=0.0)
            for i, r in enumerate(self._latest.rooms)
            if r.temp is not None and r.humidity is not None
        ]
        if not readings:
            return None
        # age_sec = 0 cho mọi mẫu: gateway ĐÃ lọc góc quá hạn trước khi gửi (nó
        # gửi INVALID cho góc mất kết nối), nên lọc tuổi lần nữa ở đây là áp hai
        # lớp ngưỡng lên cùng một dữ liệu và ngưỡng chặt hơn sẽ âm thầm thắng.
        return aggregate_rooms(readings, max_age_sec=float("inf"))
