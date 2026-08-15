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
        """Ingest one snapshot. Slots with no reading record NOTHING -- inserting an
        empty sample into the history would make the regression think the temperature
        had just jumped to 0."""
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
        """A human-readable label for a slot -- "corner 2" if the node declared one,
        otherwise the slot number. Do not invent a name that sounds precise when we do
        not know."""
        corner = self._corner_label.get(slot)
        return f"corner {corner + 1}" if corner is not None else f"slot {slot}"

    def history(self, slot: int) -> list[Sample]:
        return list(self._rooms.get(slot, ()))

    def outdoor_history(self) -> list[Sample]:
        return list(self._outdoor)

    def latest_per_room(self) -> dict[int, float]:
        return {s: w[-1].temp for s, w in self._rooms.items() if w}

    def indoor_check(self):
        """Recompute the median ourselves from the per-corner values in the latest
        snapshot.

        It uses THE SAME function the cloud worker uses (comfort_bridge ->
        app.comfort), so if this number differs from the `snapshot.t_in` the gateway is
        showing on the wall, two copies of the rule have drifted apart -- and knowing
        that is this function's entire reason for existing. Returns None when the
        snapshot has no corners.
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
        # age_sec = 0 for every sample: the gateway has ALREADY filtered out stale
        # corners before sending (it sends INVALID for a disconnected corner), so
        # filtering by age again here would apply two thresholds to the same data and
        # the stricter one would silently win.
        return aggregate_rooms(readings, max_age_sec=float("inf"))
