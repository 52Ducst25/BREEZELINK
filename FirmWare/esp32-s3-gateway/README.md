# Gateway trên ESP32-S3 — bản không màn hình

Bo **ESP32-S3-DevKitC-1 (N16R8)** đóng thế node indoor khi bo QR Box Advance
không nạp được.

## Vì sao có thư mục này

Bo QR Box hỏng mạch nạp: esptool dò được chip (`ESP32-D0WD-V3`, đọc đúng MAC)
nhưng **mọi lần ghi khối dữ liệu đều đứt giữa chừng** — cả hai chiều, ở mọi tốc
độ từ 460800 xuống 57600, chỗ đứt ngẫu nhiên. Tức là hỏng ở tầng vật lý, không
phải thứ sửa được bằng phần mềm.

Gateway nằm giữa **mọi** đường dữ liệu của hệ:

```
4 node góc phòng ─┐
                  ├─ ESP-NOW ─> GATEWAY ─ WiFi/MQTT ─> backend ─> web + app
node ngoài trời ──┘               │
                                  └─ BLE GATT <─> Arduino UNO Q (edge AI)
```

Nên một gateway không nạp được là chặn đứng việc kiểm thử cả bốn node góc phòng,
node ngoài trời, đường lên cloud, và đường sang UNO Q cùng lúc.

## Nó KHÔNG chứa logic gateway

Thư mục này chỉ có **hai file**: `platformio.ini` và `src/ui-headless.cpp`.

Nó biên dịch thẳng `../esp32-indoor/src/`, bỏ đúng thư mục `ui/`, và thay bằng
5 hàm in ra serial. Bề mặt giao diện vừa đủ nhỏ để làm được việc đó — `ui.h` chỉ
khai `begin` · `publish` · `pollCommand` · `reply` · `logCommand`, và không file
nào ngoài `ui/` chạm tới LVGL hay TFT_eSPI.

Chép mã sang đây sẽ tạo ra **hai gateway**. Logic chống trùng `req_id`, xin lại
mã IR khi NVS rỗng, ranh giới đề xuất/lệnh của UNO Q — mỗi chỗ đều đã trả giá
một lần để viết cho đúng. Hai bản sao lệch nhau ngay lần sửa thứ nhất, và triệu
chứng sẽ là "bo trên bàn chạy đúng, bo treo tường thì không" mà không có cách
nào biết bản nào mới là bản thật.

Nhờ vậy bo S3 là **người đóng thế thật**: nó chạy chính xác đường mã mà QR Box
sẽ chạy. Sửa xong mạch nạp thì nạp lại bo kia, không phải chuyển đổi gì.

**Đổi lại:** sửa `../esp32-indoor/src/*.cpp` là ảnh hưởng cả hai env. Thêm một
hàm `Ui::` mới thì build ở đây đứt lúc link (`undefined reference to Ui::...`) —
cách sửa là thêm bản rỗng vào `src/ui-headless.cpp`.

## Cấu hình

**Không có `config.h` riêng.** Dùng chung `../esp32-indoor/src/config.h`.

Cố ý: bo này đóng thế node indoor nên phải dùng **đúng `DEVICE_UUID` và đúng
token MQTT** của hàng devices đó trên web. Hai bo không bao giờ chạy cùng lúc.
Tạo bản sao ở đây là có hai file cùng chứa một mật khẩu MQTT, và chắc chắn sẽ
lệch nhau.

Trình biên dịch tự giải đúng: `#include "config.h"` trong `main.cpp` tìm theo
thư mục của chính file đó trước tiên. **Vì vậy `src/` ở đây tuyệt đối không được
có `config.h`** — hai file cùng tên thì không nhìn bằng mắt mà biết được bản nào
đang có hiệu lực.

## Nạp

```bash
cd FirmWare/esp32-s3-gateway
pio run -e esp32s3-gateway -t upload --upload-port COMx
```

Cắm vào cổng **USB** (cổng OTG gốc của S3), không phải cổng **UART**. Firmware
bật `ARDUINO_USB_CDC_ON_BOOT=1` nên `Serial` đi ra cổng USB gốc. Cắm nhầm cổng
UART thì màn hình serial im như bo chết trong khi nạp vẫn báo thành công — đã
dính đúng bẫy này trên node ESP32-C3. Muốn dùng cổng UART thì bỏ hai cờ
`ARDUINO_USB_*` trong `platformio.ini`.

## Đấu dây

| | |
|---|---|
| IR phát | GPIO4 |
| IR thu | GPIO5 |

Chân phải tránh trên S3 N16R8: `GPIO0/3/45/46` (strapping) · `GPIO19/20` (USB
D-/D+) · `GPIO26..32` (flash SPI) · `GPIO33..37` (**PSRAM octal của bản R8**) ·
`GPIO38` (LED RGB trên DevKitC-1) · `GPIO43/44` (UART0).

Chưa hàn LED hồng ngoại thì **vẫn chạy được hết luồng dữ liệu** — chỉ là máy
lạnh không nhúc nhích. Log vẫn ghi `da phat`: node có bắn xung ra chân thật, nó
không biết đầu kia có LED hay không.

## Đọc log

`Ui::publish()` in một bảng tóm tắt mỗi 10 giây — cùng nội dung màn hình sẽ
hiện, xếp dọc:

```
=== GATEWAY (S3, khong man) · up 5m12s · fw gateway-2.0 ===
  WiFi NOI  "TenWifiCuaBan" kenh 1  -62 dBm  ip=...  mac=...  |  MQTT NOI
  Phong: 2 goc biet, 2 tuoi, 2 phieu -> TRUNG VI 27.4C 63%
    goc 0    27.2C  64%  3s
    goc 1    27.6C  62%  5s
  Ngoai troi   31.0C  70%  8s
  May lanh COOL 25  ·  tu dong  ·  lenh may chu cuoi: 12s truoc
  UNO Q chua noi (da nhan 0)  |  ESP-NOW nhan 143, rot 0  |  ma IR trong NVS: 0
```

Ba con số về phòng khác nhau và phân biệt được chúng chính là cách chẩn đoán:

| | nghĩa là |
|---|---|
| `goc biet` | đã từng nghe thấy — nhiều hơn `tuoi` nghĩa là có góc vừa chết |
| `tuoi` | còn nghe thấy trong 20s qua |
| `phieu` | thật sự có số đo và được tính vào trung vị |

`tuoi` > `phieu` nghĩa là có node **còn sống nhưng cảm biến hỏng** — DHT22 tuột
dây hoặc thiếu trở kéo. Khác hẳn node mất điện, và cách xử lý cũng khác hẳn.

## Những chỗ dễ sai

- **Không thấy log nào** → cắm nhầm cổng UART (xem mục Nạp).
- **Lặp `rst:0x3` vô hạn, không in nổi dòng nào** → sai bảng phân vùng hoặc sai
  `board_upload.offset_address`. Cả hai khoá phải đi cùng nhau; lý do đầy đủ
  trong `../esp32-indoor/platformio.ini`.
- **`CHUA CO SO DO TRONG NHA` mãi** → không góc nào gửi được. Kiểm `WIFI_SSID`
  của node góc phòng có **giống hệt** của gateway không: ESP-NOW bắt buộc cùng
  kênh, mà node góc suy ra kênh bằng cách quét đúng chuỗi tên đó. Lệch một ký tự
  là gói bay vào khoảng không và **không một dòng log nào ở đâu báo lỗi** —
  broadcast không có ACK.
- **`undefined reference to Ui::...`** → `../esp32-indoor/src/ui/ui.h` vừa có hàm
  mới. Thêm bản rỗng vào `src/ui-headless.cpp`.

## Giới hạn đã biết

- **Không có nút bấm** nên không bao giờ phát ra `manual override` hay `resync`.
  Cờ `GHI DE` chỉ có thể do app hoặc web đặt. Đây là hành vi đúng, không phải
  thiếu sót.
- Bo này **không đo nhiệt/ẩm**, y như gateway thật: số "trong nhà" là trung vị
  của các node góc phòng.
- Chưa chạy trên phần cứng thật — mới chỉ biên dịch xong.
