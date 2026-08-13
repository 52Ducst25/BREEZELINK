"""Luật DUY NHẤT quyết định một node có đang trực tuyến hay không.

VÌ SAO KHÔNG ĐỌC THẲNG ``devices.status``
------------------------------------------
Cột đó là một lá cờ DÍNH: chỉ ``status_handler`` ghi nó, và chỉ khi có bản tin
MQTT ``status`` tới. Node phòng và node ngoài trời **không có phiên MQTT nào** —
chúng nói ESP-NOW với gateway, và chính **gateway publish trạng thái hộ** chúng
(``publishSlaveStatus`` trong esp32-indoor/src/main.cpp).

Hệ quả: gateway chết thì không còn ai publish hộ, nên cờ của các slave **đóng
băng vĩnh viễn** ở giá trị cuối cùng. Đã quan sát trên máy chủ thật:

    Gateway  offline   last_seen 02:23:54.639
    SS1      online    last_seen 02:23:54.617
    SS2      online    last_seen 02:23:54.599
    SS3      online    last_seen 02:23:54.627
    outdoor  online    last_seen 02:23:54.610

Cả năm nằm trong 50 mili-giây — một lượt quét cuối cùng của gateway rồi nó chết.
Bảng điều khiển hiện bốn node "sống" trong khi con đường duy nhất ra internet của
chúng đã tắt. Đó là trạng thái không thể tồn tại.

VÌ SAO KHÔNG VÁ Ở FIRMWARE
---------------------------
MQTT chỉ cho **một Will Message cho mỗi kết nối**. Gateway không thể đăng ký 5 di
chúc hộ 5 slave — giới hạn giao thức, không phải thiếu sót của ai.

NGƯỠNG 150 GIÂY LẤY TỪ ĐÂU
---------------------------
``last_seen_at`` chỉ được ghi bởi ``status_handler`` — telemetry KHÔNG đụng vào.
Với một slave còn sống, mốc đó được làm mới mỗi ``STATUS_REFRESH_MS = 60s``
(FirmWare/esp32-indoor/src/slave-watch.h). Nên ngưỡng phải lớn hơn 60 giây, nếu
không mọi node sẽ nhấp nháy offline giữa hai lượt làm mới.

150 giây = 2,5 lần nhịp đó. Đủ biên để không báo nhầm, mà vẫn phát hiện gateway
chết trong vòng 1–2,5 phút.

ĐỔI ``STATUS_REFRESH_MS`` Ở FIRMWARE THÌ PHẢI ĐỔI CẢ SỐ NÀY.
"""

from datetime import datetime, timedelta, timezone

from sqlalchemy import and_, func

from app.models.device import Device, DeviceStatus

#  Bao lâu không nghe thấy thì coi như mất kết nối.
#
#  210s = 3,5 lần nhịp 60s. Bản đầu đặt 150s (2,5 lần) và ĐÓ LÀ CON SỐ QUÁ SÁT:
#  `publishSlaveStatus` ở firmware BỎ QUA giá trị trả về của `mqtt.publish`
#  (main.cpp:190), nên một lần publish hỏng lặng lẽ đã ăn 60s, hai lần là 120s,
#  cộng jitter gói là vượt 150s — node sống bị nhấp nháy offline đúng lúc mạng
#  chập chờn, tức đúng lúc người ta nhìn vào bảng điều khiển.
#
#  Đổi STATUS_REFRESH_MS ở firmware thì phải đổi số này.
PRESENCE_TTL = timedelta(seconds=210)


def is_fresh(last_seen_at: datetime | None) -> bool:
    """Mốc nghe-lần-cuối này còn trong ngưỡng không.

    Tách riêng khỏi [is_online] để DTO (schemas/device.py) dùng lại được — nó chỉ
    có mốc thời gian trong tay, không có đối tượng Device.
    """
    if last_seen_at is None:
        return False
    #  CHẶN datetime NGÂY THƠ (naive) — nổ to còn hơn sai âm thầm.
    #
    #  `astimezone()` KHÔNG ném lỗi với datetime naive: từ Python 3.6 nó lặng lẽ
    #  coi đó là giờ HỆ THỐNG. Máy chủ chạy giờ VN thì một mốc UTC naive sẽ được
    #  đọc thành UTC+7 — tức "tương lai 7 tiếng" — nên `is_fresh` luôn trả True
    #  và một node chết sẽ hiện trực tuyến MÃI MÃI, không một dòng log nào báo.
    #
    #  Hôm nay mọi mốc đều aware (cột là `DateTime(timezone=True)`, người ghi duy
    #  nhất là utcnow()). Chốt chặn này là để ngày ai đó đưa vào một chuỗi ISO
    #  không offset, hoặc chạy test trên SQLite, thì hỏng ngay chỗ đó.
    if last_seen_at.tzinfo is None:
        raise ValueError(
            "last_seen_at phải là datetime có múi giờ; nhận được naive "
            f"{last_seen_at!r} — xem chú thích trong device_presence.is_fresh"
        )
    seen = last_seen_at.astimezone(timezone.utc)
    return datetime.now(timezone.utc) - seen < PRESENCE_TTL


def is_online(device: Device) -> bool:
    """Node có đang trực tuyến không — dùng cho code Python.

    Hai điều kiện, cả hai đều cần: cờ nói online, VÀ lần nghe cuối còn tươi.
    """
    return device.status == DeviceStatus.online and is_fresh(device.last_seen_at)


def online_filter():
    """Cùng luật trên, dạng biểu thức SQL — dùng cho câu đếm trong SQLAlchemy.

    PHẢI ĐỂ CẠNH [is_online]: hai bản này trả lời cùng một câu hỏi ở hai tầng, và
    tách chúng ra hai file là lệch nhau ngay lần sửa đầu tiên — triệu chứng sẽ là
    trang tổng quan đếm 4/6 trong khi danh sách bên dưới hiện 2 node trực tuyến.
    """
    #  MỐC THỜI GIAN TÍNH BẰNG ĐỒNG HỒ POSTGRES (`func.now()`), không phải đồng
    #  hồ tiến trình Python. Hai lý do, cả hai đều thật:
    #
    #  1. `last_seen_at` do Postgres lưu; so nó với đồng hồ của app chỉ đúng khi
    #     hai bên cùng máy — hiện đúng do tình cờ, không do thiết kế.
    #  2. `datetime.now()` bị tính lúc DỰNG biểu thức. Hàm này gọi mới mỗi lần
    #     nên hôm nay không sao, nhưng chỉ cần ai đó viết
    #     `ONLINE = online_filter()` ở mức module là mốc đóng băng lúc import:
    #     mọi node online vĩnh viễn, rồi 210s sau offline vĩnh viễn, không có gì
    #     báo lỗi. `func.now()` không có cửa cho tai nạn đó.
    return and_(
        Device.status == DeviceStatus.online,
        Device.last_seen_at.is_not(None),
        Device.last_seen_at > func.now() - PRESENCE_TTL,
    )
