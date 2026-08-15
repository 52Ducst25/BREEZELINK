"""Scoring the forecast: comparing what was predicted 15 minutes ago against what
actually happened.

WHY THIS IS MANDATORY RATHER THAN A NICE EXTRA:
  A model that has not been measured must not be allowed to drive an air
  conditioner. The current codebase already carries a lesson about this --
  ``fit_trend`` has been computing a 15-minute forecast for weeks, and to this day
  that number has only ever appeared in a log line with nobody knowing whether it was
  right. Without measurement there is no route from "advice only" to "allowed to
  command".

  Once there is a mean error, the decision becomes trivial: an MAE below ~0.3 °C
  means the model can be trusted to pre-cool; above 1 °C it is guessing.

COMPARED AGAINST A NAIVE BASELINE, not just reported as an absolute error. "MAE
0.4 °C" sounds good until you learn that predicting "exactly as it is now" also gives
0.4 °C -- at which point the model contributes nothing. The ``vs_standing_still``
column is the number that says whether the model has any value.
"""

import logging
from collections import deque
from dataclasses import dataclass

logger = logging.getLogger("edge.score")

# The moving-average window. 96 scored forecasts at a 30s cadence ≈ half an hour of
# matured predictions -- enough for a stable number, short enough that an improvement
# to the model shows up straight away.
WINDOW = 96

# Discard forecasts settled far too late: if the gateway disconnects and comes back an
# hour later, scoring against the temperature an hour after the due point is
# meaningless and would pollute the statistics with an enormous error that is not the
# model's fault.
TOLERANCE_SEC = 120.0


@dataclass
class _Pending:
    due_ts: float
    predicted: float
    naive: float      # the temperature at prediction time -- the "room stands still" baseline


class PredictionScore:
    """Holds forecasts until they mature, scores them, and reports the mean error."""

    def __init__(self, horizon_min: float) -> None:
        self._horizon_min = horizon_min
        self._pending: deque[_Pending] = deque()
        self._err: deque[float] = deque(maxlen=WINDOW)
        self._naive_err: deque[float] = deque(maxlen=WINDOW)

    def record(self, now_ts: float, predicted: float, current: float) -> None:
        """Register a forecast to be scored later."""
        self._pending.append(
            _Pending(due_ts=now_ts + self._horizon_min * 60.0,
                     predicted=predicted, naive=current)
        )

    def settle(self, now_ts: float, actual: float) -> None:
        """Score every forecast that has matured. Call once per control tick."""
        while self._pending and self._pending[0].due_ts <= now_ts:
            p = self._pending.popleft()
            if now_ts - p.due_ts > TOLERANCE_SEC:
                continue        # settled far too late, discard -- see the TOLERANCE_SEC note
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
        """Is it trustworthy enough to drive the air conditioner.

        Two conditions, and the second is the real one: the model has to BEAT the
        naive baseline. A model that merely matches "the room stands still" makes all
        of its computation pointless, however small the absolute error sounds.
        """
        if self.n < WINDOW // 2 or self.mae is None or self.naive_mae is None:
            return False
        return self.mae < 0.3 and self.mae < self.naive_mae * 0.8

    def describe(self) -> str:
        if self.mae is None:
            return f"nothing scored yet ({len(self._pending)} still maturing)"
        naive = "" if self.naive_mae is None else f" · standing-still {self.naive_mae:.2f}"
        verdict = "TRUSTWORTHY" if self.trustworthy else "not yet trustworthy"
        return f"error {self.mae:.2f}°C{naive} · {self.n} scored · {verdict}"
