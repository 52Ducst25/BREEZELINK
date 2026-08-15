"""The room's thermal model: a first-order ARX with 4 parameters, fitted online.

    T_in[k+1] = a·T_in[k] + b·T_out[k] + c·u[k] + d

    u[k] = max(0, T_in[k] − T_set[k])  when COOL, otherwise 0   (see history_store)

WHY EXACTLY 4 PARAMETERS -- THIS IS A CONSTRAINT OF THE DATA, NOT A PREFERENCE:
  Looking at "12,644 data points" and concluding that is enough for a neural network
  is wrong, because the points are not independent. Two readings 60 seconds apart in
  a room whose time constant is tens of minutes are essentially the same number. The
  number of INDEPENDENT samples ≈ duration ÷ τ:

      2 days of four-corner data ÷ τ≈45 minutes  ≈  60 independent thermal events

  4 parameters per 60 independent samples is a healthy ratio. An MLP of 32→64→3 has
  ~2,400 parameters -- roughly two YEARS of accumulation at the current rate. Also
  worth noting: only 24 COOL segments longer than 20 minutes exist in the entire
  history.

THREE PHYSICAL QUANTITIES CAN BE EXTRACTED, and this is what a black-box model
cannot give you:
  τ = −Δt/ln(a)     the room's time constant
  b/(1−a)           how much outdoor sun makes it into the room
  −c/(1−a)          actual cooling power -- a steady decline means the unit is
                    weakening or the filter is dirty

COMPUTED ON DEVIATIONS from T_REF rather than on absolute values: T_in and T_out are
both around 32 °C and correlate very strongly with the constant term, making the
normal matrix nearly singular. Subtracting 30 improves the conditioning dramatically
without changing the meaning of any parameter except ``d``.
"""

import logging
import math
from collections.abc import Callable

import numpy as np

from edge_ai.history_store import Row

logger = logging.getLogger("edge.model")

T_REF = 30.0            # °C, the reference origin -- see the note at the top of the file

# THE MODEL INTERVAL IS NOT THE STORAGE INTERVAL. History is written every 60 seconds
# (dense enough for anomaly detection and for the charts), while the model only takes
# every 5th sample.
#
# WHY IT HAS TO BE SPARSER: what determines `a` is how much the temperature MANAGES TO
# CHANGE between two samples. At 60 seconds with τ≈45 minutes, a≈0.978 -- each step
# covers only 2.2% of the journey, comparable to the sensor noise. That noise sits
# inside the regressor itself, so it pulls `a` down SYSTEMATICALLY (attenuation bias).
#
# Measured on synthetic data with a known answer (true τ of 45 minutes):
#     Δt = 60s,  no averaging:    τ = 24 minutes   (47% error)
#     Δt = 300s, with averaging:  see check-thermal-model.py
#
# At 300 seconds a≈0.895 -- each step covers 10.5% of the journey, 5× the signal for
# the same noise. There are still 9 points within each τ, more than enough to describe
# an exponential.
DT_SEC = 300.0

# A sample pair whose spacing is too far off is DISCARDED rather than rescaled. The
# ARX model assumes a fixed Δt; feeding a 20-minute gap into the same equation as a
# 5-minute one skews `a` silently. The gateway losing the network for a few minutes is
# routine.
DT_MIN_SEC, DT_MAX_SEC = 270.0, 330.0

# Forgetting factor. 0.995 at a 300s interval → a half-life of ~11.5 hours: slow
# enough not to chase sensor noise, fast enough that a change of season or of room
# lets the model catch up within about a day.
FORGET = 0.995

# Below this threshold every extracted number is just noise dressed up as a parameter.
# 120 steps × 5 minutes = 10 hours, i.e. it must have seen at least one warm-cool
# cycle in the day.
MIN_SAMPLES = 120

# ============================================================================
#  THE PHYSICALLY MEANINGFUL REGION -- bounded by τ AND by the two derived gains
# ----------------------------------------------------------------------------
#  BOUNDED BY τ RATHER THAN BY `a`, and this fixes a bug that really happened.
#
#  An earlier version wrote `0.5 < a < 0.9999` directly. Two problems, the second one
#  much worse:
#
#  1. THE MEANING OF `a` DEPENDS ON DT_SEC. Anyone changing the model interval makes
#     the same `a` threshold quietly carry a completely different τ, with no line in
#     the condition saying so. Bounding τ directly puts the intent right in the
#     expression.
#
#  2. 0.9999 IS SO LOOSE AS TO BE USELESS. At Δt = 5 minutes:
#         a < 0.9999  ->  a maximum τ of 49,998 minutes = 34.7 DAYS
#     No room has a time constant of 34 days, yet every value up to that was accepted
#     as "physically meaningful".
#
#  WE HIT EXACTLY THAT CASE, and the symptom looked like a model working fine:
#         τ = 49,502 minutes · outdoor ×303.33 · cooling 283.45 °C/unit · 332 samples
#         the badge on the monitoring page: "fitted"
#     Working backwards, a = 0.9998990 -- passing the old threshold at the fourth
#     decimal place. `b` = 0.0306 and `c` = −0.0286 were both NORMAL; all three
#     displayed numbers exploded purely because they are all divided by
#     (1−a) = 0.0001.
#
#  `a → 1` means the model has concluded "the indoor temperature never changes" --
#  T_in[k+1] = T_in[k]. That is not a model, that is a measurement repeating itself.
#  The usual cause is data with NO EXCITATION: a room that stayed flat throughout the
#  history, or the indoor and outdoor sensors measuring the same place so the two
#  regressor columns coincide and the design matrix becomes singular.
TAU_MIN_MIN, TAU_MAX_MIN = 10.0, 300.0

# The two derived gains must also lie in a physical region. Bounding τ already covers
# the case above (all three are divided by (1−a)), but these two thresholds catch
# failure modes τ cannot see -- for example a sign error in `b`: a fine τ alongside
# "when it gets hotter outside the room gets colder".
#
# Slightly loosened on the negative side rather than bounded at 0: early in the fit,
# the coefficient of a weak influence can cross 0 because of noise. A hard bound at 0
# would mean a room barely affected by the outdoors could NEVER fit, with nobody able
# to work out why.
OUTDOOR_GAIN_LO, OUTDOOR_GAIN_HI = -0.2, 1.5     # ×, at steady state
COOL_GAIN_LO, COOL_GAIN_HI = -0.5, 10.0          # °C per unit of cooling demand


def _a_from_tau(tau_min: float) -> float:
    """The `a` corresponding to a given time constant, at the DT_SEC interval."""
    return math.exp(-(DT_SEC / 60.0) / tau_min)


class ThermalModel:
    """RLS for a first-order ARX. Batch fit at startup, then updated online."""

    def __init__(self, forget: float = FORGET) -> None:
        self._forget = forget
        # The "temperature stays as it is" prior: a=1, everything else 0. This is the
        # most harmless hypothesis -- it asserts nothing about the room, and any real
        # data pulls it away from here.
        self._theta = np.array([1.0, 0.0, 0.0, 0.0])
        self._p = np.eye(4)
        self._n = 0
        self._prev: Row | None = None

    # -- state -----------------------------------------------------------------

    @property
    def samples(self) -> int:
        return self._n

    @staticmethod
    def _reject(theta) -> str | None:
        """Why this parameter set is unusable. ``None`` = usable.

        IT RETURNS A REASON RATHER THAN True/False, deliberately: one function serves
        both as the gate (``ready``) and as the explanation (``describe`` and the
        batch-fit log). Splitting it in two is exactly what caused the old bug -- the
        condition ``0.5 < a < 0.9999`` was duplicated in two places and fixing one
        meant forgetting the other.
        """
        a, b, c, _ = (float(v) for v in theta)

        # The three failure modes of `a` need three different sentences: they lead to
        # three different places to go looking, and collapsing them into "a is out of
        # range" throws the clue away.
        if a >= 1.0:
            return f"a={a:.5f} ≥ 1 -- the model believes the room heats up without limit"
        if a <= 0.0:
            return f"a={a:.5f} ≤ 0 -- the model oscillates, flipping sign every step"

        tau = -(DT_SEC / 60.0) / math.log(a)
        if not (TAU_MIN_MIN <= tau <= TAU_MAX_MIN):
            return (f"τ={tau:.0f}min is outside {TAU_MIN_MIN:.0f}min..{TAU_MAX_MIN:.0f}min "
                    f"(a={a:.5f}) -- the data may not be sufficiently exciting")

        inv = 1.0 - a
        og, cg = b / inv, -c / inv
        if not (OUTDOOR_GAIN_LO <= og <= OUTDOOR_GAIN_HI):
            return f"outdoor influence ×{og:.2f}, outside {OUTDOOR_GAIN_LO}..{OUTDOOR_GAIN_HI}"
        if not (COOL_GAIN_LO <= cg <= COOL_GAIN_HI):
            return f"cooling power {cg:.2f}°C/unit, outside {COOL_GAIN_LO}..{COOL_GAIN_HI}"
        return None

    @property
    def ready(self) -> bool:
        """Enough data AND parameters inside the physically meaningful region.

        That region is defined by ``_reject()`` -- see the TAU_MIN_MIN block at the top
        of the file for why it bounds τ rather than `a`.
        """
        return self._n >= MIN_SAMPLES and self._reject(self._theta) is None

    @property
    def tau_min(self) -> float | None:
        """The room's time constant, in minutes."""
        if not self.ready:
            return None
        return -(DT_SEC / 60.0) / math.log(self._theta[0])

    @property
    def outdoor_gain(self) -> float | None:
        """At steady state, how much the indoor temperature rises per 1 °C outdoors."""
        if not self.ready:
            return None
        return float(self._theta[1] / (1.0 - self._theta[0]))

    @property
    def cool_gain(self) -> float | None:
        """°C removed per unit of cooling demand, at steady state.

        POSITIVE means cooling is effective (c is negative). This number declining
        week by week is a sign the unit is weakening -- something no sensor measures
        directly.
        """
        if not self.ready:
            return None
        return float(-self._theta[2] / (1.0 - self._theta[0]))

    def describe(self) -> str:
        """One sentence stating what is ACTUALLY happening -- the monitoring page shows
        this verbatim.

        IT MUST SEPARATE "not enough samples" FROM "enough samples but meaningless
        parameters". An earlier version merged the two into one sentence, so the case
        we actually hit came out as:

            "not enough data (332/120 samples)"

        -- a self-contradictory sentence, and one that sends the reader off to wait
        for more data when the problem is that the data has NO EXCITATION, which no
        amount of waiting fixes.
        """
        if self._n < MIN_SAMPLES:
            return f"not enough data ({self._n}/{MIN_SAMPLES} samples)"
        why = self._reject(self._theta)
        if why is not None:
            return f"enough samples ({self._n}) but the parameters are meaningless -- {why}"
        return (f"τ={self.tau_min:.0f}min · outdoor×{self.outdoor_gain:.2f} · "
                f"cooling {self.cool_gain:.2f}°C/unit · {self._n} samples")

    # -- fitting ---------------------------------------------------------------

    @staticmethod
    def _design(rows: list[Row]) -> tuple[np.ndarray, np.ndarray]:
        """Build the regression matrix, decimated to the DT_SEC interval.

        Walks forward and only accepts pairs exactly one model step apart. The samples
        in between are NOT wasted -- they already contributed to the per-minute average
        in HistoryStore, and they are still there for anomaly detection.
        """
        phis, ys = [], []
        prev: Row | None = None
        for cur in rows:
            if prev is None:
                prev = cur
                continue
            dt = cur.ts - prev.ts
            if dt < DT_MIN_SEC:
                continue                    # not a full step yet, keep waiting
            if dt > DT_MAX_SEC or prev.t_out is None:
                prev = cur                  # a gap: drop the pair and restart from here
                continue
            phis.append([prev.t_in - T_REF, prev.t_out - T_REF, prev.cooling_demand, 1.0])
            ys.append(cur.t_in - T_REF)
            prev = cur
        return np.array(phis), np.array(ys)

    def load_history(self, rows: list[Row]) -> bool:
        """Load the whole stored history. Called at startup.

        TWO PATHS, AND THE SECOND IS THE ONE MOST OFTEN TAKEN:

          ≥120 pairs available -> a single least-squares fit, done immediately
          fewer                -> REPLAY each sample through observe(), learning
                                  whatever can be learned

        An earlier version only had the first path and returned False in every other
        case -- i.e. it DISCARDED THE HISTORY ENTIRELY. The consequence is far worse
        than it looks:

            Run for 9 hours with 108 pairs on disk, one power cut and `_n` is back to
            0. The data is still sitting in the file, but the model treats it as never
            seen, and the 10-hour clock restarts from scratch.

        In other words the local history was only useful once it had already passed
        120 pairs by itself -- the very threshold it exists to help reach. Replaying
        breaks that circularity: 13 pairs on disk give 13/120, not 0/120.

        Why not always replay for simplicity: a batch fit uses ALL the data at once so
        it is not affected by ordering, while RLS has a forgetting factor so older
        samples fade. Once there is enough data, a batch fit gives better parameters.
        """
        phi, y = self._design(rows)

        if len(y) >= MIN_SAMPLES:
            theta, *_ = np.linalg.lstsq(phi, y, rcond=None)
            # THE SAME CHECK as `ready`, calling the same function. This used to
            # duplicate the condition `0.5 < theta[0] < 0.9999` -- two copies of one
            # rule, and fixing one side meant forgetting the other.
            why = self._reject(theta)
            if why is None:
                self._theta = theta
                # P ≈ the inverse information matrix: it tells RLS "this much is
                # already known", so it does not leap at a single noisy sample.
                self._p = np.linalg.inv(phi.T @ phi + 1e-6 * np.eye(4))
                self._n = len(y)
                logger.info("Batch fit from %d sample pairs · %s", len(y), self.describe())
                return True
            logger.warning("Batch fit from %d pairs rejected -- %s. Falling back to replay.",
                           len(y), why)

        # Replay. observe() filters the interval itself and handles gaps itself, so
        # nothing more needs preparing -- only making sure we start from a clean state.
        self._prev = None
        used = sum(1 for row in rows if self.observe(row))
        logger.info("Replayed %d/%d history samples · %s", used, len(rows), self.describe())
        return self.ready

    def observe(self, row: Row) -> bool:
        """Feed in one 60-second sample. Returns True if it was used to update the
        parameters.

        IT DECIMATES ITSELF: samples arrive every minute but only every 5-minute step
        produces an update. Exactly the same rule as ``_design`` -- the two paths have
        to match, otherwise the batch fit and the online fit would estimate two
        different models from the same data.
        """
        prev = self._prev
        if prev is None:
            self._prev = row
            return False

        dt = row.ts - prev.ts
        if dt < DT_MIN_SEC:
            return False                    # not a full step yet -- KEEP prev as it is
        if dt > DT_MAX_SEC or prev.t_out is None:
            self._prev = row                # a gap: restart from this sample
            return False
        self._prev = row

        phi = np.array([prev.t_in - T_REF, prev.t_out - T_REF, prev.cooling_demand, 1.0])
        y = row.t_in - T_REF

        # RLS with a forgetting factor.
        p_phi = self._p @ phi
        denom = self._forget + float(phi @ p_phi)
        gain = p_phi / denom
        self._theta = self._theta + gain * (y - float(phi @ self._theta))
        self._p = (self._p - np.outer(gain, p_phi)) / self._forget
        self._n += 1
        return True

    # -- prediction ------------------------------------------------------------

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
        """The indoor temperature ``minutes`` minutes from now.

        CLOSED LOOP, not open loop: each step recomputes ``u`` from the temperature it
        has just predicted. Holding ``u`` fixed would produce "the room cools forever",
        because in reality the demand shrinks by itself as the room approaches the
        setpoint.

        ``t_out_at`` is where THE WEATHER FORECAST plugs in (part B). Without it, the
        current outdoor temperature is held constant -- correct for 15 minutes,
        increasingly wrong for longer horizons, and that is precisely why part B is
        worth doing.
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
