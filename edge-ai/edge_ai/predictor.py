"""Short-horizon forecasting and anomaly detection over the sensor history.

WHY LINEAR REGRESSION AND NOT A NEURAL NETWORK: over a 15-30 minute horizon a
room's temperature is dominated by one thing — whether the air conditioner is
removing heat faster than the outside is adding it — and that is close to
linear. A learned model would need labelled data this project does not have, a
training pipeline nobody would maintain, and it would be far harder to explain
to the person asking "why did it turn the compressor on". The interface here is
deliberately narrow so a heavier model can replace the internals later without
touching the controller.

Everything is plain Python (no numpy): the UNO Q's Linux side runs Debian on a
quad-core A53, but keeping the dependency list to bleak alone is what makes this
installable on a device somebody has to maintain in the field.
"""

from dataclasses import dataclass


@dataclass(frozen=True)
class Trend:
    """Least-squares fit over one sensor's recent samples."""

    slope_per_min: float  # °C per minute; positive = warming
    intercept: float
    samples: int
    span_sec: float

    def project(self, minutes: float, last_value: float) -> float:
        """Where the value lands in ``minutes``, extrapolating the slope.

        Extrapolates from the LAST OBSERVED value, not from the fitted
        intercept: the fit's job is to estimate the rate of change, and anchoring
        to the fit would make the forecast jump whenever the line sits a little
        above or below the newest reading — a discontinuity the room never had.
        """
        return last_value + self.slope_per_min * minutes


# Below this many samples a slope is noise wearing a slope's clothes. Six
# samples at a 15 s cadence is 90 seconds — enough that DHT22's ±0.5 °C jitter
# averages out instead of being read as a trend.
_MIN_SAMPLES = 6


def fit_trend(points: list[tuple[float, float]]) -> Trend | None:
    """Least-squares slope of ``(timestamp_sec, value)`` pairs.

    Returns None when there is too little data or every sample shares one
    timestamp (zero variance in x, which would divide by zero). Refusing is
    correct: a fabricated slope of 0.0 reads as "the room is stable", which is a
    claim, not an absence of one.
    """
    if len(points) < _MIN_SAMPLES:
        return None

    t0 = points[0][0]
    xs = [(t - t0) / 60.0 for t, _ in points]  # minutes, so slope is °C/min
    ys = [v for _, v in points]
    n = float(len(points))

    mean_x = sum(xs) / n
    mean_y = sum(ys) / n
    var_x = sum((x - mean_x) ** 2 for x in xs)
    if var_x <= 1e-9:
        return None

    slope = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys)) / var_x
    return Trend(
        slope_per_min=slope,
        intercept=mean_y - slope * mean_x,
        samples=len(points),
        span_sec=points[-1][0] - t0,
    )


# A corner this far from the household median is reporting about itself, not
# about the room: 2.5 °C is beyond what four points in one room differ by under
# normal circulation, and beyond DHT22's ±0.5 °C accuracy several times over.
_OUTLIER_DELTA_C = 2.5

# A room does not change this fast on its own. Above it, suspect the sensor
# (a loose wire reads as a step change) rather than the weather.
_IMPLAUSIBLE_SLOPE_C_PER_MIN = 1.5


@dataclass(frozen=True)
class Anomaly:
    device_uuid: str
    kind: str  # "outlier" | "runaway"
    detail: str


def find_anomalies(
    per_room_latest: dict[str, float], median_temp: float, trends: dict[str, Trend]
) -> list[Anomaly]:
    """Corners worth flagging to a human.

    Reporting only — nothing here changes what the controller decides. The
    median already protects the setpoint from a single bad corner; the value of
    naming the anomaly is that somebody can go and fix the sensor instead of
    living with three-quarters of the system they paid for.
    """
    found: list[Anomaly] = []
    for uuid, temp in sorted(per_room_latest.items()):
        delta = temp - median_temp
        if abs(delta) >= _OUTLIER_DELTA_C:
            found.append(
                Anomaly(uuid, "outlier", f"lệch {delta:+.1f}°C so với trung vị {median_temp:.1f}°C")
            )
        trend = trends.get(uuid)
        if trend and abs(trend.slope_per_min) >= _IMPLAUSIBLE_SLOPE_C_PER_MIN:
            found.append(
                Anomaly(uuid, "runaway", f"đổi {trend.slope_per_min:+.2f}°C/phút — nghi lỗi cảm biến")
            )
    return found
