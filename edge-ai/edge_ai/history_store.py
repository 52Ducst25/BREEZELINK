"""Lịch sử cục bộ trên chính thiết bị — nền cho mọi thứ học được.

VÌ SAO PHẢI CÓ: toàn bộ lịch sử đang nằm trên VPS, còn node này chỉ giữ một cửa
sổ RAM 30 phút (``RoomStore``). Mất mạng là mất luôn khả năng học — mà chạy tiếp
khi mất mạng chính là lý do tồn tại của node này. Cửa sổ RAM cũng biến mất mỗi
lần khởi động lại dịch vụ.

VÌ SAO SQLITE CHỨ KHÔNG PHẢI INFLUXDB (brick ``dbstorage_tsstore``):
  Ảnh ``influxdb:2.7-alpine`` có sẵn trên bo, nhưng nó là một máy chủ cơ sở dữ
  liệu 188 MB, một container nữa phải sống, và một phụ thuộc client nữa. Cho
  KHỐI LƯỢNG NÀY thì đó là lắp cần cẩu để nhấc một viên gạch:

      1 mẫu/phút × 8 số  ≈ 60 byte/phút  ≈ 30 MB/năm

  SQLite nằm trong thư viện chuẩn Python, không có tiến trình nào phải trông,
  và chạy được ở CẢ hai kiểu triển khai — trong container của App Lab lẫn dưới
  systemd. Brick ``dbstorage_tsstore`` chỉ dùng được ở kiểu thứ nhất.

LẤY MẪU 60 GIÂY, KHÔNG PHẢI 5. Gateway đẩy ảnh chụp mỗi 5 giây, nhưng hai số đo
cách nhau 5 giây trong một phòng có hằng số thời gian hàng chục phút thì gần như
là cùng một số. Giữ cả là phình tệp gấp 12 lần mà không thêm một chút thông tin
nào — và tệ hơn, mô hình ARX ở nhịp 5 giây có hệ số ``a`` ≈ 0.999, tức là toàn bộ
động học dồn vào chữ số cuối và phép khớp mất điều kiện.
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

# Nhịp ghi. Xem chú thích đầu file — 60s là nhịp mà mô hình nhiệt dùng.
SAMPLE_SEC = 60.0

_SCHEMA = """
CREATE TABLE IF NOT EXISTS samples (
    ts       REAL PRIMARY KEY,   -- epoch giây, UTC
    t_in     REAL NOT NULL,
    h_in     REAL NOT NULL,
    t_out    REAL,               -- NULL khi node ngoài trời mất nhịp
    h_out    REAL,
    mode     INTEGER NOT NULL,   -- AcUnoQMode
    setpoint INTEGER,            -- NULL = gateway chưa biết
    flags    INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_samples_ts ON samples(ts);
"""


@dataclass(frozen=True)
class Row:
    """Một mẫu đã ghi. Chỉ những trường mô hình nhiệt cần."""

    ts: float
    t_in: float
    h_in: float
    t_out: float | None
    mode: Mode
    setpoint: int | None

    @property
    def cooling_demand(self) -> float:
        """``u`` của mô hình ARX: nhu cầu làm lạnh.

        Chỉ COOL mới sinh nhu cầu. FAN/DRY/OFF đều trả 0 — không phải vì chúng
        không làm gì, mà vì chúng không rút nhiệt theo cách mà một hệ số duy
        nhất mô tả được, và nhét chúng vào cùng một ``c`` sẽ làm hệ số đó vô
        nghĩa cho cả ba.

        Tỉ lệ với ĐỘ VƯỢT so với nhiệt độ đặt, không phải nhị phân bật/tắt: máy
        lạnh inverter chạy mạnh hơn khi phòng còn xa mục tiêu, và một biến nhị
        phân sẽ ép mô hình coi "vượt 5 độ" giống hệt "vượt 0.1 độ".
        """
        if self.mode is not Mode.COOL or self.setpoint is None:
            return 0.0
        return max(0.0, self.t_in - float(self.setpoint))


class _Bucket:
    """Gom mọi ảnh chụp trong một phút rồi ghi TRUNG BÌNH, không phải mẫu tức thời.

    ĐÂY LÀ CHỐNG NHIỄU, VÀ NÓ QUAN TRỌNG HƠN VẺ NGOÀI. Gateway đẩy ảnh chụp mỗi
    5 giây, tức là 12 lần mỗi phút. Lấy trung bình 12 số làm sai số ngẫu nhiên
    giảm √12 ≈ 3.5 lần.

    Vì sao đáng làm: nhiễu nằm trong CHÍNH BIẾN HỒI QUY ``T_in[k]`` của mô hình
    ARX, và loại nhiễu đó không chỉ làm kết quả kém chính xác — nó kéo hệ số
    ``a`` xuống một cách có hệ thống (sai lệch suy giảm, errors-in-variables).
    Đã đo trên dữ liệu giả có đáp án: nhiễu ±0.15 °C làm τ tụt từ 45 phút xuống
    24 phút, tức là sai 47% theo đúng một hướng. Hai hệ số kia gần như không hề
    hấn gì, nên triệu chứng rất dễ bị bỏ qua.

    Ngoài ra, một mẫu phút ĐÚNG RA phải đại diện cho cả phút chứ không phải cho
    một khoảnh khắc bất kỳ trong phút đó.
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
        """Trả mẫu trung bình rồi xoá bộ gom.

        ``mode`` và ``setpoint`` lấy giá trị MỚI NHẤT, không trung bình: chúng là
        trạng thái rời rạc, và "trung bình của COOL và OFF" không có nghĩa gì.
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
    """Lịch sử lấy mẫu 60 giây, ghi xuống SQLite cạnh dịch vụ."""

    def __init__(self, path: str | os.PathLike[str], keep_days: float) -> None:
        self._path = Path(path)
        self._keep_days = keep_days
        self._last_write = 0.0
        self._last_prune = 0.0
        self._acc = _Bucket()

        self._path.parent.mkdir(parents=True, exist_ok=True)
        self._db = sqlite3.connect(self._path, isolation_level=None)
        # WAL: ghi không chặn đọc, và quan trọng hơn — nó sống sót qua một lần
        # mất điện đột ngột, chuyện thường ngày với thiết bị cắm tường không UPS.
        self._db.execute("PRAGMA journal_mode=WAL")
        # FULL, KHÔNG PHẢI NORMAL. Bản trước để NORMAL với lý lẽ "bớt fsync cho
        # đỡ mòn thẻ nhớ, cùng lắm mất vài mẫu cuối". Đo trên chính bo thì lý lẽ
        # đó sai ở cả hai vế:
        #
        #   WAL ở chế độ NORMAL chỉ được fsync trước mỗi lần GỘP (checkpoint), mà
        #   ngưỡng gộp mặc định là 1000 trang = 4 MB. Đo thực tế: mỗi lần ghi tốn
        #   ~12.5 KB WAL, tức là ~327 lần ghi mới có một lần gộp.
        #
        #       327 lần ghi × 1 phút  =  ~5,5 GIỜ dữ liệu nằm chờ trong bộ nhớ
        #       đệm của hệ điều hành, chưa xuống đĩa. Mất điện là mất sạch.
        #
        #   "Vài mẫu cuối" hoá ra là gần sáu tiếng. Mà mô hình nhiệt cần 120 mẫu
        #   (2 giờ) mới bắt đầu dùng được — nghĩa là toàn bộ quá trình học nằm
        #   TRỌN trong vùng rủi ro, và mỗi lần cúp điện là học lại từ đầu.
        #
        #   Vế thứ hai — mòn thẻ — cũng không đứng vững: ở nhịp 1 lần ghi/phút,
        #   FULL tốn 1440 lần fsync mỗi ngày. Lo mòn eMMC vì con số đó là áp một
        #   nỗi lo của hệ ghi tần suất cao vào một hệ ghi mỗi phút một lần.
        #
        # FULL fsync ngay khi commit, nên mất điện chỉ mất tối đa mẫu đang ghi dở.
        self._db.execute("PRAGMA synchronous=FULL")
        self._db.executescript(_SCHEMA)

        logger.info("Lịch sử cục bộ: %s (%d mẫu, giữ %.0f ngày)",
                    self._path, self.count(), self._keep_days)

    # -- ghi -------------------------------------------------------------------

    def ingest(self, snapshot: Snapshot, ts: float | None = None) -> Row | None:
        """Ghi một ảnh chụp nếu đã tới nhịp. Trả hàng vừa ghi, hoặc None.

        TRẢ VỀ HÀNG chứ không phải True/False để mô hình nhiệt nạp thẳng được
        cùng một mẫu — nếu không thì nó phải đọc ngược lại từ đĩa đúng cái vừa
        ghi, và hai đường đó sẽ có lúc lệch nhau.

        Bỏ ảnh chụp KHÔNG CÓ SỐ TRONG NHÀ: mô hình cần một chuỗi liên tục, và
        một hàng thiếu ``t_in`` không dùng được vào việc gì ngoài việc tạo ra
        một lỗ hổng phải xử lý ở mọi nơi đọc nó.
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
        """Xoá mẫu quá hạn. Mỗi giờ một lần, không phải mỗi lần ghi."""
        if now - self._last_prune < 3600.0:
            return
        self._last_prune = now
        cutoff = now - self._keep_days * 86400.0
        cur = self._db.execute("DELETE FROM samples WHERE ts < ?", (cutoff,))
        if cur.rowcount > 0:
            logger.info("Dọn %d mẫu cũ hơn %.0f ngày", cur.rowcount, self._keep_days)

    # -- đọc -------------------------------------------------------------------
    #
    # MỖI LẦN ĐỌC MỞ MỘT KẾT NỐI RIÊNG, không dùng lại ``self._db``.
    #
    # sqlite3 của Python trói kết nối vào đúng luồng đã tạo ra nó, còn trang theo
    # dõi chạy trên FastAPI — mà FastAPI đẩy handler đồng bộ sang một threadpool.
    # Dùng chung kết nối thì mỗi lần trình duyệt hỏi lịch sử sẽ nổ:
    #
    #     sqlite3.ProgrammingError: SQLite objects created in a thread can only
    #     be used in that same thread.
    #
    # Đây đúng là chỗ WAL sinh ra để giải quyết: một bên ghi, nhiều bên đọc, các
    # kết nối độc lập. Mở kết nối SQLite tốn cỡ micro-giây, còn ba hàm dưới đây
    # được gọi vài lần mỗi phút — cái giá đó không đáng để đổi lấy một lớp khoá.
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
        """Lịch sử trải bao nhiêu giờ. 0 nếu chưa có gì."""
        with self._read() as db:
            row = db.execute("SELECT min(ts), max(ts) FROM samples").fetchone()
        if not row or row[0] is None:
            return 0.0
        return (row[1] - row[0]) / 3600.0

    def recent(self, hours: float) -> list[Row]:
        """Các mẫu trong ``hours`` giờ gần nhất, theo thứ tự thời gian."""
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
