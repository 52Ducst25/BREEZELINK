"""Fold several room-corner sensors into the single indoor reading the comfort
engine consumes.

Pure functions — no Redis, no DB, no clock. That is deliberate: this is the one
piece of the new 4-sensor layout that decides what temperature the algorithm
believes, so it has to be testable without infrastructure, and the edge-AI
service on the UNO Q imports the very same code (``edge_ai/comfort_bridge.py``)
rather than reimplementing it. Two implementations of "what is the room
temperature" drifting apart is exactly the class of bug that ends with an air
conditioner running the wrong setpoint in someone's house.

WHY MEDIAN, NOT MEAN: the four sensors sit in four corners, and one of them will
end up near a window, a door, or the AC's own outlet. That corner reads 3-4 °C
off the room and a mean lets it drag the setpoint with it, permanently, with no
symptom other than "the house feels wrong". A median ignores a single outlier
outright as long as three sensors agree. With an even count the median is the
mean of the two middle samples, so it still moves smoothly as the room warms —
it is not a step function.

Below three sensors a median cannot reject anything (with two, the median IS
their mean), and the result is reported with ``used`` so callers can say how
much agreement is behind the number instead of implying four-sensor confidence.
"""

from dataclasses import dataclass


@dataclass(frozen=True)
class RoomReading:
    """One corner sensor's latest sample, with how old it is."""

    device_id: str
    temp: float
    humidity: float
    age_sec: float


@dataclass(frozen=True)
class RoomAggregate:
    """The household's indoor reading, plus how it was reached."""

    temp: float
    humidity: float
    used: int  # sensors that contributed
    stale: int  # sensors dropped for being too old


def median(values: list[float]) -> float:
    """Middle value; mean of the two middle values for an even count.

    Raises ``ValueError`` on an empty list rather than inventing a number — a
    fabricated indoor temperature is worse than no reading, because the caller
    can detect "no reading" and skip the cycle.
    """
    if not values:
        raise ValueError("median() of no values")
    ordered = sorted(values)
    mid = len(ordered) // 2
    if len(ordered) % 2 == 1:
        return ordered[mid]
    return (ordered[mid - 1] + ordered[mid]) / 2.0


def aggregate(readings: list[RoomReading], *, max_age_sec: float) -> RoomAggregate | None:
    """Median temp/humidity over the sensors fresher than ``max_age_sec``.

    Returns None when every sensor is stale or the list is empty — the caller
    must treat that as "indoor state unknown" and skip the comfort cycle, NOT
    fall back to the last known value: a node that died an hour ago reporting a
    comfortable 26 °C keeps the compressor off through a heatwave.

    Temperature and humidity are medianed INDEPENDENTLY, so the result can be a
    (temp, humidity) pair that no single sensor reported. That is intended: they
    are two separate physical quantities and the humidity outlier is rarely the
    same corner as the temperature outlier.
    """
    fresh = [r for r in readings if r.age_sec <= max_age_sec]
    if not fresh:
        return None
    return RoomAggregate(
        temp=median([r.temp for r in fresh]),
        humidity=median([r.humidity for r in fresh]),
        used=len(fresh),
        stale=len(readings) - len(fresh),
    )
