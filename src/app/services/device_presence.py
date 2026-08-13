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

from sqlalchemy import and_

from app.models.device import Device, DeviceStatus

#  Bao lâu không nghe thấy thì coi như mất kết nối. Xem docstring đầu file trước
#  khi đổi — con số này gắn với STATUS_REFRESH_MS của firmware.
PRESENCE_TTL = timedelta(seconds=150)


def is_online(device: Device) -> bool:
    """Node có đang trực tuyến không — dùng cho code Python.

    Hai điều kiện, cả hai đều cần: cờ nói online, VÀ lần nghe cuối còn tươi.
    """
    if device.status != DeviceStatus.online:
        return False
    if device.last_seen_at is None:
        return False
    seen = device.last_seen_at.astimezone(timezone.utc)
    return datetime.now(timezone.utc) - seen < PRESENCE_TTL


def online_filter():
    """Cùng luật trên, dạng biểu thức SQL — dùng cho câu đếm trong SQLAlchemy.

    PHẢI ĐỂ CẠNH [is_online]: hai bản này trả lời cùng một câu hỏi ở hai tầng, và
    tách chúng ra hai file là lệch nhau ngay lần sửa đầu tiên — triệu chứng sẽ là
    trang tổng quan đếm 4/6 trong khi danh sách bên dưới hiện 2 node trực tuyến.
    """
    return and_(
        Device.status == DeviceStatus.online,
        Device.last_seen_at.is_not(None),
        Device.last_seen_at > datetime.now(timezone.utc) - PRESENCE_TTL,
    )
