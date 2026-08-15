"""Hourly outdoor temperature forecast -- so the system looks forward, not only back.

THE PROBLEM IT SOLVES: ``T_rm`` in the comfort algorithm is an EMA of PAST outdoor
temperatures. That makes every decision a reaction. Knowing that 2pm will be 36 °C
lets you cool early from 1pm, while the indoor/outdoor difference is still small and
the compressor runs more efficiently -- the same coolness for less electricity.

WHY NOT ARDUINO'S ``weather_forecast`` BRICK:
  Its source on the board has been read. It only calls open-meteo with
  ``daily=weather_code`` and returns a classification string ("sunny" / "rainy").
  THERE IS NO TEMPERATURE -- and temperature is the only thing useful here. It also
  lives in ``arduino.app_bricks``, i.e. it only exists inside the App Lab container,
  while this service has to run under systemd too.

  Calling open-meteo directly gets ``hourly=temperature_2m``, needs no API key, and
  uses only the standard library's ``urllib`` -- no added dependency.

LOSING THE NETWORK IS ROUTINE, NOT EXCEPTIONAL. This whole node exists to run when
the network is gone, so a stale forecast keeps being used (today's outdoor
temperature resembles yesterday's more than it resembles a made-up number) -- but with
a limit: past ``MAX_AGE_SEC`` it returns None and the caller has to cope, rather than
quietly steering by a figure from three days ago.
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

# Open-meteo updates hourly. 30 minutes is frequent enough while still being polite
# to a free service -- and a temperature forecast does not change its mind every 5
# minutes.
REFRESH_SEC = 1800.0

# Discard a forecast older than this. 6 hours: enough to survive an afternoon network
# outage, not enough to drive an air conditioner from the day before yesterday's
# weather.
MAX_AGE_SEC = 6 * 3600.0

# Fetch 2 days. Forecast-driven control only looks 1-2 hours ahead, but getting 2 days
# in the same call means midnight does not leave a gap, and the extra download is only
# a few KB.
FORECAST_DAYS = 2

_TIMEOUT_SEC = 15.0


class WeatherForecast:
    """Hourly forecast outdoor temperature, linearly interpolated between points."""

    def __init__(self, lat: float, lon: float, cache_path: Path | None = None) -> None:
        self._lat = lat
        self._lon = lon
        self._cache = cache_path
        self._times: list[float] = []       # epoch seconds, ascending
        self._temps: list[float] = []
        self._fetched_at = 0.0
        self._fail_streak = 0

        if cache_path is not None:
            self._load_cache()

    # -- state -----------------------------------------------------------------

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

    # -- lookup ----------------------------------------------------------------

    def temp_at(self, ts: float) -> float | None:
        """The forecast outdoor temperature at time ``ts`` (epoch seconds).

        INTERPOLATED rather than nearest-point: the forecast points are an hour apart
        while the air conditioner decides every 30 seconds. Stepping at each hour
        boundary would surface as a jolt in the setpoint that the room never
        experienced.

        NO EXTRAPOLATION beyond the available range: it returns None. Extrapolating an
        outdoor temperature with a straight line is a reliable way to "forecast" 45 °C
        at dawn.
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
        """The highest (temperature, epoch) within the next ``hours``. None if unavailable."""
        if not self.fresh:
            return None
        now = time.time()
        window = [(v, t) for t, v in zip(self._times, self._temps)
                  if now <= t <= now + hours * 3600.0]
        return max(window) if window else None

    # -- fetching --------------------------------------------------------------

    def refresh(self) -> bool:
        """Call the API. BLOCKING -- the caller must push it onto another thread.

        It never raises: losing the network is a normal state for this device, and an
        exception here would kill the control loop over a nice-to-have.
        """
        url = f"{_API}?" + urllib.parse.urlencode({
            "latitude": f"{self._lat:.4f}",
            "longitude": f"{self._lon:.4f}",
            "hourly": "temperature_2m",
            "forecast_days": FORECAST_DAYS,
            # unixtime: no ISO string parsing and no timezone trouble at all.
            "timeformat": "unixtime",
        })
        try:
            with urllib.request.urlopen(url, timeout=_TIMEOUT_SEC) as resp:
                data = json.loads(resp.read().decode("utf-8"))
            times = [float(t) for t in data["hourly"]["time"]]
            temps = [float(v) for v in data["hourly"]["temperature_2m"]]
        except (urllib.error.URLError, OSError, ValueError, KeyError, TypeError) as exc:
            self._fail_streak += 1
            # Only complain on the first failure of each streak: an overnight outage
            # would otherwise produce 16 identical lines, and a log full of noise is a
            # log nobody reads.
            if self._fail_streak == 1:
                logger.warning("Could not fetch the forecast (%s) - reusing the cached one if still valid", exc)
            return False

        if len(times) != len(temps) or not times:
            logger.warning("The forecast came back empty or with mismatched lengths - discarded")
            return False

        self._times, self._temps = times, temps
        self._fetched_at = time.time()
        if self._fail_streak:
            logger.info("Forecast recovered after %d failures", self._fail_streak)
        self._fail_streak = 0
        self._save_cache()

        peak = self.peak_within(12.0)
        logger.info("Forecast has %d hourly points%s", len(times),
                    "" if peak is None else f" · next-12h peak {peak[0]:.1f}°C")
        return True

    # -- surviving a restart ---------------------------------------------------

    def _load_cache(self) -> None:
        """A stale forecast beats nothing at all just after boot, before the first call."""
        try:
            raw = json.loads(self._cache.read_text(encoding="utf-8"))
            self._times = [float(t) for t in raw["times"]]
            self._temps = [float(v) for v in raw["temps"]]
            self._fetched_at = float(raw["fetched_at"])
        except (OSError, ValueError, KeyError, TypeError):
            return
        if self.fresh:
            logger.info("Reusing the cached forecast (%.0f minutes old)", self.age_min or 0.0)

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
            logger.warning("Could not write the forecast cache: %s", exc)
