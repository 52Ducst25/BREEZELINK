"""Local history on the device itself -- the foundation of everything it learns.

WHY IT IS NEEDED: the whole history lives on the VPS, while this node only keeps a
30-minute RAM window (``RoomStore``). Losing the network means losing the ability to
learn -- and continuing to run without a network is this node's entire reason for
existing. The RAM window also disappears every time the service restarts.

WHY SQLITE AND NOT INFLUXDB (the ``dbstorage_tsstore`` brick):
  The ``influxdb:2.7-alpine`` image is available on the board, but it is a 188 MB
  database server, one more container to keep alive, and one more client
  dependency. For THIS VOLUME that is a crane hired to lift one brick:

      1 sample/minute x 8 numbers  ~= 60 bytes/minute  ~= 30 MB/year

  SQLite is in the Python standard library, has no process to supervise, and works
  in BOTH deployment styles -- inside App Lab's container and under systemd. The
  ``dbstorage_tsstore`` brick only works in the first.

SAMPLED AT 60 SECONDS, NOT 5. The gateway pushes a snapshot every 5 seconds, but two
readings 5 seconds apart in a room whose time constant is tens of minutes are
essentially the same number. Keeping them all makes the file 12x larger without
adding any information -- and worse, an ARX model at a 5-second interval has ``a`` ≈
0.999, meaning all the dynamics collapse into the last digit and the fit loses its
conditioning.
"""

import logging
import os
import sqlite3
import time
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path

from edge_ai.protocol import Mode, Snapshot

logger = logging.getLogger("edge.history")

# The write interval. See the note at the top of the file -- 60s is the interval the
# thermal model works at.
SAMPLE_SEC = 60.0

_SCHEMA = """
CREATE TABLE IF NOT EXISTS samples (
    ts       REAL PRIMARY KEY,   -- epoch seconds, UTC
    t_in     REAL NOT NULL,
    h_in     REAL NOT NULL,
    t_out    REAL,               -- NULL when the outdoor node misses its beat
    h_out    REAL,
    mode     INTEGER NOT NULL,   -- AcUnoQMode
    setpoint INTEGER,            -- NULL = the gateway does not know yet
    flags    INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_samples_ts ON samples(ts);
"""


@dataclass(frozen=True)
class Row:
    """One stored sample. Only the fields the thermal model needs."""

    ts: float
    t_in: float
    h_in: float
    t_out: float | None
    mode: Mode
    setpoint: int | None

    @property
    def cooling_demand(self) -> float:
        """The ARX model's ``u``: cooling demand.

        Only COOL produces demand. FAN/DRY/OFF all return 0 -- not because they do
        nothing, but because they do not remove heat in a way a single coefficient
        can describe, and folding them into the same ``c`` would make that
        coefficient meaningless for all three.

        Proportional to the EXCESS over the setpoint rather than a binary on/off: an
        inverter air conditioner works harder when the room is further from target,
        and a binary variable would force the model to treat "5 degrees over"
        identically to "0.1 degrees over".
        """
        if self.mode is not Mode.COOL or self.setpoint is None:
            return 0.0
        return max(0.0, self.t_in - float(self.setpoint))


class _Bucket:
    """Collect every snapshot within a minute and write the AVERAGE, not an instant
    sample.

    THIS IS NOISE REJECTION, AND IT MATTERS MORE THAN IT LOOKS. The gateway pushes a
    snapshot every 5 seconds, i.e. 12 times a minute. Averaging 12 numbers reduces
    the random error by √12 ≈ 3.5x.

    Why it is worth doing: the noise sits inside the ARX model's REGRESSOR ``T_in[k]``
    itself, and that kind of noise does not merely make the result less accurate --
    it pulls the coefficient ``a`` down systematically (attenuation bias,
    errors-in-variables). Measured on synthetic data with a known answer: ±0.15 °C of
    noise drops τ from 45 minutes to 24 minutes, a 47% error always in the same
    direction. The other two coefficients are almost untouched, which makes the
    symptom very easy to overlook.

    Beyond that, a per-minute sample SHOULD represent the whole minute rather than an
    arbitrary instant within it.
    """

    def __init__(self) -> None:
        self._t_in: list[float] = []
        self._h_in: list[float] = []
        self._t_out: list[float] = []
        self.last_h_out: float | None = None

    def add(self, snapshot: Snapshot) -> None:
        self._t_in.append(snapshot.t_in)
        self._h_in.append(snapshot.h_in)
        if snapshot.t_out is not None:
            self._t_out.append(snapshot.t_out)
        if snapshot.h_out is not None:
            self.last_h_out = snapshot.h_out

    def flush(self, ts: float, latest: Snapshot) -> Row:
        """Return the averaged sample and clear the bucket.

        ``mode`` and ``setpoint`` take the LATEST value rather than an average: they
        are discrete states, and "the average of COOL and OFF" means nothing.
        """
        row = Row(
            ts=ts,
            t_in=sum(self._t_in) / len(self._t_in),
            h_in=sum(self._h_in) / len(self._h_in),
            t_out=(sum(self._t_out) / len(self._t_out)) if self._t_out else None,
            mode=latest.ac_mode,
            setpoint=latest.ac_setpoint,
        )
        self._t_in.clear()
        self._h_in.clear()
        self._t_out.clear()
        return row


class HistoryStore:
    """A 60-second-sampled history, written to SQLite next to the service."""

    def __init__(self, path: str | os.PathLike[str], keep_days: float) -> None:
        self._path = Path(path)
        self._keep_days = keep_days
        self._last_write = 0.0
        self._last_prune = 0.0
        self._acc = _Bucket()

        self._path.parent.mkdir(parents=True, exist_ok=True)
        self._db = sqlite3.connect(self._path, isolation_level=None)
        # WAL: writes do not block reads, and more importantly it survives a sudden
        # power cut -- an everyday event for a wall-powered device with no UPS.
        self._db.execute("PRAGMA journal_mode=WAL")
        # FULL, NOT NORMAL. An earlier version used NORMAL with the argument "fewer
        # fsyncs to reduce card wear, and at worst we lose the last few samples".
        # Measured on the actual board, that argument is wrong on both counts:
        #
        #   WAL in NORMAL mode is only fsynced before each CHECKPOINT, and the default
        #   checkpoint threshold is 1000 pages = 4 MB. Measured in practice: each
        #   write costs ~12.5 KB of WAL, i.e. ~327 writes per checkpoint.
        #
        #       327 writes x 1 minute  =  ~5.5 HOURS of data sitting in the OS page
        #       cache, not yet on disk. A power cut loses all of it.
        #
        #   "The last few samples" turns out to be nearly six hours. And the thermal
        #   model needs 120 samples (2 hours) before it becomes usable -- meaning the
        #   entire learning process sits ENTIRELY inside the risk window, and every
        #   power cut restarts it from scratch.
        #
        #   The second claim -- card wear -- does not hold either: at one write per
        #   minute, FULL costs 1440 fsyncs a day. Worrying about eMMC wear at that
        #   figure is applying a high-write-rate system's anxiety to a system that
        #   writes once a minute.
        #
        # FULL fsyncs on commit, so a power cut loses at most the sample being written.
        self._db.execute("PRAGMA synchronous=FULL")
        self._db.executescript(_SCHEMA)

        logger.info("Local history: %s (%d samples, keeping %.0f days)",
                    self._path, self.count(), self._keep_days)

    # -- writing ---------------------------------------------------------------

    def ingest(self, snapshot: Snapshot, ts: float | None = None) -> Row | None:
        """Write a snapshot if it is time. Returns the row just written, or None.

        IT RETURNS THE ROW rather than True/False so the thermal model can ingest the
        very same sample directly -- otherwise it would have to read back from disk
        exactly what was just written, and those two paths would eventually diverge.

        Snapshots with NO INDOOR READING are dropped: the model needs a continuous
        series, and a row missing ``t_in`` is useless for anything except creating a
        hole that every reader then has to handle.
        """
        now = time.time() if ts is None else ts
        if snapshot.t_in is None or snapshot.h_in is None:
            return None

        self._acc.add(snapshot)
        if now - self._last_write < SAMPLE_SEC:
            return None
        self._last_write = now

        row = self._acc.flush(now, snapshot)
        self._db.execute(
            "INSERT OR REPLACE INTO samples "
            "(ts, t_in, h_in, t_out, h_out, mode, setpoint, flags) "
            "VALUES (?,?,?,?,?,?,?,?)",
            (row.ts, row.t_in, row.h_in, row.t_out, self._acc.last_h_out,
             int(row.mode), row.setpoint, snapshot.flags),
        )
        self._maybe_prune(now)
        return row

    def _maybe_prune(self, now: float) -> None:
        """Delete expired samples. Once an hour, not on every write."""
        if now - self._last_prune < 3600.0:
            return
        self._last_prune = now
        cutoff = now - self._keep_days * 86400.0
        cur = self._db.execute("DELETE FROM samples WHERE ts < ?", (cutoff,))
        if cur.rowcount > 0:
            logger.info("Pruned %d samples older than %.0f days", cur.rowcount, self._keep_days)

    # -- reading ---------------------------------------------------------------
    #
    # EVERY READ OPENS ITS OWN CONNECTION rather than reusing ``self._db``.
    #
    # Python's sqlite3 binds a connection to the thread that created it, while the
    # monitoring page runs on FastAPI -- which dispatches synchronous handlers to a
    # threadpool. Sharing the connection makes every browser request for history blow
    # up with:
    #
    #     sqlite3.ProgrammingError: SQLite objects created in a thread can only
    #     be used in that same thread.
    #
    # This is exactly what WAL exists to solve: one writer, many readers, independent
    # connections. Opening a SQLite connection costs microseconds, and the three
    # functions below are called a few times a minute -- that price is not worth
    # trading for a locking layer.
    @contextmanager
    def _read(self):
        db = sqlite3.connect(self._path)
        try:
            yield db
        finally:
            db.close()

    def count(self) -> int:
        with self._read() as db:
            return int(db.execute("SELECT count(*) FROM samples").fetchone()[0])

    def span_hours(self) -> float:
        """How many hours the history spans. 0 if there is nothing."""
        with self._read() as db:
            row = db.execute("SELECT min(ts), max(ts) FROM samples").fetchone()
        if not row or row[0] is None:
            return 0.0
        return (row[1] - row[0]) / 3600.0

    def recent(self, hours: float) -> list[Row]:
        """The samples from the last ``hours`` hours, in chronological order."""
        cutoff = time.time() - hours * 3600.0
        with self._read() as db:
            rows = db.execute(
                "SELECT ts, t_in, h_in, t_out, mode, setpoint FROM samples "
                "WHERE ts >= ? ORDER BY ts",
                (cutoff,),
            ).fetchall()
        return [
            Row(ts=r[0], t_in=r[1], h_in=r[2], t_out=r[3],
                mode=_mode(r[4]), setpoint=r[5])
            for r in rows
        ]

    def close(self) -> None:
        try:
            self._db.close()
        except Exception:  # noqa: BLE001
            pass


def _mode(raw: int) -> Mode:
    try:
        return Mode(raw)
    except ValueError:
        return Mode.UNKNOWN
