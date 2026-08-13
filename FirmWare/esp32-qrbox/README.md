# Bo QR Box Advance Touch Screen — panel cũ

**Không phải panel chính thức nữa.** Panel treo tường nay là bo 2.8" ESP32-S3:
[`../esp32-s3-panel/`](../esp32-s3-panel/).

| | |
|---|---|
| Chip | ESP32-WROOM-32E-N8 |
| Màn | 2.8" YT280S030, **ST7789V**, SPI |
| Nạp | `pio run -e qrbox-touch -t upload --upload-port COMx` (USB-TTL vào cổng P3) |

---

## Vì sao thư mục này chỉ có `platformio.ini`

Không thiếu gì cả — **mã nguồn cố ý không nằm ở đây.**

`platformio.ini` khai `src_dir = ../esp32-s3-panel/src`, nên bo này biên dịch
**đúng** mã mà panel chính thức chạy. Cả `config.h` cũng dùng chung file đó.

Ba bo panel chạy chung một bản mã, khác nhau đúng ở cờ build:

| Thư mục | Bo | Chân lấy từ |
|---|---|---|
| `../esp32-s3-panel/` | ESP32-S3 2.8" (chính thức) | `-D BOARD_S3_PANEL` |
| **`.` (đây)** | QR Box Advance | mặc định trong `board-pins.h` |
| `../esp32-s3-gateway/` | DevKitC-1, gỡ lỗi | mặc định, `ui/` không biên dịch |

Chép mã về đây cho "đủ bộ" là tạo ra hai panel. Chúng lệch nhau ngay lần sửa
thứ nhất, và triệu chứng là bo này chạy đúng còn bo kia thì không — không có
cách nào biết bên nào mới là bản thật.

**Không có `config.h.example` ở đây, và không được thêm vào.** Nó nằm ở
[`../esp32-s3-panel/src/config.h.example`](../esp32-s3-panel/src/config.h.example).
Hai bo panel thay thế nhau chứ không chạy cùng lúc, nên phải dùng đúng một
`DEVICE_UUID` và một token MQTT của cùng một hàng `devices` trên web. Thêm bản
sao ở đây là có hai file cùng chứa một mật khẩu MQTT.

---

## Bo này còn dùng được không

Còn — nhưng **mạch nạp đã hỏng**: esptool dò được chip, mọi lần ghi khối dữ liệu
đều đứt giữa chừng, cả hai chiều, không phụ thuộc baud. Đó là lý do dự án chuyển
sang bo S3. Sửa được mạch nạp thì `pio run -e qrbox-touch -t upload` chạy lại
ngay, không phải chuyển đổi gì.

⚠️ **Phải có nguồn riêng 9–24 VDC ở P2/P4.** Cổng P3 chỉ có TX/RX/GND cho debug
— cấp mỗi USB-TTL thì màn + ESP32 không đủ dòng, bo sụt áp và **reset lặp liên
tục** (`rst:0x3 (SW_RESET)` mỗi vài chục ms, không bao giờ in nổi một dòng của
firmware).

Sơ đồ chân, cách đọc ngược từ schematic, và ba ràng buộc hỏng-không-báo-lỗi của
bo này: [`../Interface/README.md`](../Interface/README.md).
