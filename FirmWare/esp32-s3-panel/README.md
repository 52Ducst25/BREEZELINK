# Panel treo tường — bo 2.8" ESP32-S3

**Panel chính thức của BreezeLink.** Thay cho bo QR Box Advance.

| | |
|---|---|
| Chip | ESP32-S3 |
| Màn | 2.8" IPS 240×320, **ILI9341V**, SPI 4 dây |
| Cảm ứng | **FT6336G** điện dung, I²C |
| Có thêm | mic + loa I2S, thẻ microSD, đèn RGB, đo pin |
| Nạp | `pio run -e esp32s3-panel -t upload --upload-port COMx` |

```
pio run -e esp32s3-panel -t upload --upload-port COM5   # nạp
pio device monitor -p COM5 -b 115200                    # xem log
```

---

## 1. Mã nguồn panel nằm ở đây — bản duy nhất

`src/` trong thư mục này là bản thật, kể cả `ui/`.

Từng có thêm hai env dùng chung đúng `src/` này — `esp32-qrbox` (bo QR Box cũ) và
`esp32-s3-gateway` (bo gỡ lỗi không màn) — **đã gỡ khỏi repo ngày 15/08/2026**. Cần
xem lại thì `git log --diff-filter=D -- FirmWare/esp32-qrbox`.

Chép mã sang thư mục khác cho "gọn" là tạo ra **hai panel**. Logic thi hành lệnh, chống
trùng `req_id`, xin lại mã IR, ranh giới đề xuất/lệnh của UNO Q — toàn những chỗ tinh vi
mà mỗi cái đều đã trả giá một lần để viết cho đúng. Hai bản sao lệch nhau ngay lần sửa
thứ nhất, và triệu chứng là bo này chạy đúng còn bo kia thì không, không có cách nào
biết bên nào mới là bản thật.

Chỗ khác nhau giữa các bo nằm ở [`src/board-pins.h`](src/board-pins.h), chọn bằng cờ
`-D BOARD_S3_PANEL`. **Không rải `#ifdef` vào mã.** Luật này giữ nguyên dù giờ chỉ còn
một bo — nó là thứ khiến thêm bo thứ hai sau này không phải chép mã.

Cấu hình (WiFi, token MQTT, `DEVICE_UUID`) ở [`src/config.h`](src/config.h.example),
**bị gitignore** vì chứa mật khẩu thật; bản được commit là `src/config.h.example`.
File đó kết thúc bằng `#include "board-pins.h"` — thiếu dòng ấy là đứt build ở
`I2C_SDA_PIN was not declared`.

---

## 2. Sơ đồ chân

Bo gần như kín chân. Bảng dưới là **toàn bộ** phần còn trống và cách chia:

| Chân | Dùng làm gì | Ghi chú |
|---|---|---|
| IO2 | IR **phát** | |
| IO3 | IR **thu** | strapping, xem §2.2 |
| IO43 | UART **TX** → UNO Q | xem §2.1 |
| IO44 | UART **RX** ← UNO Q | |
| IO14, IO21 | *để trống* | dành cho **PZEM** (Modbus-RTU = 2 chân) |

Đã có chủ, đừng đụng: màn (10/11/12/13/45/46) · cảm ứng (15/16/17/18) · thẻ SD
(38/39/40/41/47/48) · âm thanh I2S (1/4/5/6/7/8) · đèn RGB (42) · đo pin (9) · nút BOOT (0).

### Dây phải hàn

```
   Bo panel                        Thiết bị ngoài
  ┌──────────┐
  │ IO2      │───────────────────  module IR phát   (nuôi 3.3V)
  │ IO3      │───────────────────  mắt thu TSOP     (nuôi 3.3V)
  │ IO43  TX │──────────────────>  RX của Arduino UNO Q
  │ IO44  RX │<──────────────────  TX của Arduino UNO Q
  │ GND      │───────────────────  GND CHUNG  ← thiếu là ra byte rác
  └──────────┘
```

**Đấu chéo là bắt buộc** ở cặp UART: TX bo này vào RX bo kia. Nối TX-TX thì im lặng
hoàn toàn, không hỏng gì cả — nên rất dễ mất thời gian đi tìm ở phía phần mềm.

**Nuôi mắt thu bằng 3.3V, không 5V.** Ngưỡng tuyệt đối của chân là VDD+0.3 (~3.6V).
Ở 3.3V thì module phát nên có transistor riêng; loại chỉ có LED nối tiếp trở (kiểu
KY-005) vẫn chạy nhưng dòng thấp, tầm phát yếu.

### 2.1 Vì sao mượn được chân UART0

Chỉ vì cổng USB-C là **USB gốc** của S3 — `Serial` (console) đi qua USB, không qua
UART0, nên IO43/44 rảnh thật. Nếu bo hoá ra có chip cầu CH34x thì console nằm chính
trên hai chân đó, và nối UNO Q vào là mất cả console lẫn đường UNO Q.
**Kiểm trước khi hàn** — §4 bước 1.

Được gì: IO14 + IO21 rảnh ra, vừa đúng số chân PZEM cần cho giai đoạn sau. Không có
đường này thì tới lúc gắn PZEM sẽ hết chân và phải tháo bớt thứ khác.

**Chiều không được đảo.** Tài liệu bo ghi `RXD0(IO43) / TXD0(IO44)` — **ngược** với
datasheet ESP32-S3 (IOMUX mặc định: IO43 = U0TXD, IO44 = U0RXD). Tin datasheet, vì nó
quyết định ROM bootloader lái chân nào lúc khởi động. Đặt ngược thì mỗi lần reset,
ROM lái IO43 làm ngõ ra trong khi UNO Q cũng đang đẩy ngõ ra vào đó — hai ngõ ra đấu
nhau trên một dây.

Mỗi lần bo reset, UNO Q sẽ nhận một nhúm byte lạ (ROM + bootloader tầng 2 in log ra
IO43 trước khi `setup()` chạy). **Bình thường** — khung gói lọc sẵn bằng magic `0xAC`
+ CRC8 + trượt byte dò lại. Sau khi `Serial1.begin()` gắn UART1 vào hai chân này qua
ma trận GPIO thì UART0 không còn ra tới chân nữa.

Trả giá: còn dây UNO Q thì **không nạp được qua UART0** — nạp bằng USB-C.

### 2.2 Hai chân strapping phải nhớ

**IO45 (đèn nền)** — chọn mức điện áp VDD_SPI, **phải thấp lúc reset**. Cao là chip
chọn mức 1.8V cho flash và bo không boot. An toàn vì LEDC chỉ gắn vào chân sau khi
boot xong, nhưng: **tuyệt đối không hàn trở kéo lên vào chân này.**

**IO3 (IR thu)** — chọn nguồn tín hiệu JTAG, nhưng chỉ có tác dụng khi eFuse
`JTAG_SEL_ENABLE` đã đốt, mà mặc định thì chưa. Vẫn cố ý đặt IR **thu** (không phải
phát) lên đây: mắt thu TSOP giữ chân ở mức cao ngay từ lúc cấp nguồn, tức mức lúc
reset là xác định. Đầu phát thì thả nổi cho tới khi `setup()` chạy — và một chân
strapping thả nổi là thứ không nên có.

---

## 3. Khác gì bo QR Box

| | QR Box | Bo này |
|---|---|---|
| Chip màn | ST7789 | **ILI9341** |
| Còi | có (GPIO13) | **không** |
| Đồng hồ | DS1307 (có pin nuôi) | **không** — lấy từ NTP |
| Bộ dịch mức | TXS0104 (phải bật OE) | **không** — 3.3V thẳng |
| SPI màn | 27 MHz | 40 MHz (chân IOMUX của FSPI) |

**Mất còi** vì hết chân, và nó chỉ báo "đã nhận chạm" — toast trên màn làm được việc
đó. Bo có loa I2S nên tiếng bíp vẫn khả thi sau này, chỉ là phải phát mẫu PCM qua I2S
chứ không PWM một chân.

> Vì vậy **hàng `ÂM THANH` trong màn Cài đặt đã gỡ hẳn**. Một công tắc bật/tắt cho
> phần cứng không tồn tại là kiểu điều khiển tệ nhất: bấm được, đổi màu, và không
> làm gì cả — người dùng sẽ đi tìm lỗi ở loa. Đường phát tiếng
> (`Theme::setPressSound` → `BoardIo::beep`) thì GIỮ NGUYÊN: `beep()` tự im khi
> chân = 255, còn bo QR Box dùng chung mã nguồn này thì vẫn có còi thật.

**Mất RTC** nên mất điện là mất giờ: thanh trạng thái hiện `--:--` vài giây tới khi
SNTP trả lời. Đổi lại thoát được ca tệ hơn của DS1307 — pin nuôi giữ một giờ **sai**
vĩnh viễn mà `clockRead()` vẫn khẳng định hợp lệ, đúng ca đã gặp trên bo cũ.

**Mã IR đã học không mất** khi đổi bo: bảng phân vùng giữ nguyên nên NVS vẫn ở đúng
chỗ cũ. (Vẫn phải học lại nếu thay chip, vì NVS nằm trong flash của chip.)

---

## 4. Bring-up — làm đúng thứ tự

Ba thứ dưới đây **chưa đo được trên bo thật**, cấu hình đang là suy luận từ tài liệu.

### Bước 1 — USB gốc hay chip cầu? *(làm TRƯỚC KHI HÀN)*

```bash
esptool.py --port COM5 chip_id
```

- Có dòng `USB mode: USB-Serial/JTAG` → **USB gốc**, cấu hình hiện tại đúng, hàn được.
- Không có → bo dùng chip cầu. Phải **cả hai** việc:
  1. xoá `-D ARDUINO_USB_MODE=1` và `-D ARDUINO_USB_CDC_ON_BOOT=1`
  2. trả UART UNO Q về `UNOQ_TX_PIN=14 / UNOQ_RX_PIN=21` — và mất chân cho PZEM

Đoán sai mà vẫn nạp thì màn hình serial **im như bo chết** trong khi nạp báo thành
công. Node ESP32-C3 của dự án đã dính đúng bẫy này theo chiều ngược lại.

### Bước 2 — flash mấy MB?

```bash
esptool.py --port COM5 flash_id
```

`platformio.ini` đang khai **4MB, cố ý**: khai thiếu chỉ phí flash dư, khai thừa thì
bootloader thấy phân vùng nằm ngoài vùng flash nó biết và **bỏ cuộc trong im lặng** —
lặp `rst` vô hạn, không in nổi một dòng log, rất dễ chẩn đoán nhầm thành chết phần
cứng. Firmware hiện chiếm ~1,29 MB nên 4MB thừa sức.

Muốn nới thì sửa **cả hai** khoá `board_build.flash_size` và `board_upload.flash_size`
— esptool đọc khoá thứ hai để ghi header ảnh, khoá thứ nhất không thay được nó.

### Bước 3 — màu có đúng không?

`-D DISPLAY_SELFTEST=1` đang bật: lúc khởi động hiện 3 ô màu có ghi tên trong 3 giây.

- Ra **XANH DUONG / TRANG / XANH LA** → đúng. **Xoá dòng đó đi.**
- Ra âm bản (nền đen thành trắng, xanh thành cam) → thêm `-D DISPLAY_INVERT=1`.

Nhớ xoá sau khi chốt: đây là bảng treo tường, 3 giây màn kiểm tra kỹ thuật mỗi lần bật
nguồn nhìn như màn hình lỗi chứ không phải màn khởi động.

### Bước 4 — trục cảm ứng

Ba cờ trong [`board-pins.h`](src/board-pins.h) đang để y bo cũ, chưa đo.
Chạm góc **trên-trái** rồi xem con trỏ chạy đâu:

| Triệu chứng | Đổi cờ |
|---|---|
| chạm trái/phải mà con trỏ chạy lên/xuống | `TOUCH_SWAP_XY` |
| chạm trái ra phải | `TOUCH_INVERT_X` |
| chạm trên ra dưới | `TOUCH_INVERT_Y` |

**Sửa từng cờ một.** Ba cờ độc lập nhau; đổi hai cái cùng lúc thì không biết cái nào
có tác dụng.

Hình đúng nhưng **lộn ngược 180°** thì đổi `TFT_ROTATION` từ 1 sang 3.

### Ba thứ TFT_eSPI bắt trả giá trên ESP32-S3 — đã chốt trên bo thật (14/08/2026)

Cả ba đều đã nằm sẵn trong `platformio.ini`; ghi lại đây vì **cả ba đều hỏng theo
kiểu không nói ra nguyên nhân**, và hai cái đầu làm bo lặp khởi động vô hạn.

| Cờ | Vì sao bắt buộc |
|---|---|
| `-D USE_FSPI_PORT` | Thiếu nó, `Processors/TFT_eSPI_ESP32_S3.c` lấy **tham chiếu** tới `SPI` toàn cục (`SPIClass& spi = SPI`) thay vì tự dựng bus. `spi.begin()` không cấp được bus, `_spi` vẫn NULL, và lời gọi `beginTransaction()` đầu tiên ghi vào `0x10`. Sập ngay trong `tft.init()`. FSPI chứ không HSPI: FSPI mới có IOMUX cho IO10..13, giữ được 40 MHz. |
| `-D TFT_USE_DMA=0` | `dma_end_callback()` của thư viện ghi vào `SPI_DMA_CONF_REG(spi_host)`, thanh ghi này **không ánh xạ như trên ESP32 cổ điển**. Sập trong NGẮT ở lần truyền DMA đầu tiên (`EXCVADDR: 0x30`). `initDMA()` vẫn trả **true**, nên không thể phát hiện bằng giá trị trả về — phải chặn từ cấu hình. |
| `-D DISPLAY_INVERT=1` | **Ngược với bo QR Box.** TFT_eSPI gửi `INVON` vô điều kiện cho ST7789 nhưng không gửi cho ILI9341V, nên cùng một trị số cho hai kết quả trái ngược. |

Hai crash đầu rất dễ chẩn nhầm vì log **dừng ở một chỗ trông hoàn toàn bình
thường**: cái thứ nhất ngay sau `Cam ung: OK`, cái thứ hai ngay sau
`Bo dem ve: 2 x 48 dong ...` — tức là sau khi màn đã init xong. Nhìn log thì
giống hết RAM hoặc hỏng cấu hình LVGL; nó không phải cả hai. Giải mã backtrace là
đường nhanh nhất:

```bash
~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line \
    -pfiaC -e .pio/build/esp32s3-panel/firmware.elf 0x42016ea8 0x42016f81 ...
```

Cái giá của `TFT_USE_DMA=0`: mỗi lượt flush do CPU đẩy (`pushColors`) thay vì DMA.
Chấp nhận được — nhịp vẽ 200 ms và vùng bẩn thường nhỏ hơn nhiều một màn hình.

---

## 5. Sự cố hay gặp

| Triệu chứng | Nguyên nhân thường gặp nhất |
|---|---|
| Nạp xong, serial im hoàn toàn | Sai chế độ USB — §4 bước 1 |
| Lặp `rst:0x3 (SW_RESET)`, không log | Khai flash lớn hơn thật, hoặc thiếu `board_upload.offset_address` |
| Màn tối, log vẫn chạy | Thiếu `-D BOARD_S3_PANEL=1` → biên dịch nhầm chân bo QR Box |
| `fillScreen` đúng màu, chữ thì nhiễu/xé | Sai chip màn (ILI9341 ↔ ST7789) |
| Lặp khởi động ngay sau `Cam ung: OK` | Thiếu `-D USE_FSPI_PORT` — xem §4 |
| Lặp khởi động ngay sau `Bo dem ve: ...` | Thiếu `-D TFT_USE_DMA=0` — xem §4 |
| Toàn bộ giao diện âm bản (ô TRẮNG ra ĐEN) | `DISPLAY_INVERT` ngược |
| Ô TRẮNG vẫn trắng nhưng xanh dương ra đỏ/cam | Đảo thứ tự R↔B, **không** phải đảo màu → `-D TFT_RGB_ORDER=TFT_RGB` |
| Chạm lệch trục | §4 bước 4 |
| UNO Q không nhận gói | TX-TX (chưa đấu chéo), hoặc thiếu GND chung |
| Máy lạnh không nhúc nhích, log vẫn "da phat" | Chưa hàn LED IR, hoặc module phát thiếu transistor |

---

## 6. Liên quan

- [`src/board-pins.h`](src/board-pins.h) — sơ đồ chân theo bo, chọn bằng cờ `-D BOARD_*`
- [`../shared/unoq-link-protocol.h`](../shared/unoq-link-protocol.h) — khung gói UART sang UNO Q
- [`../shared/espnow-message.h`](../shared/espnow-message.h) — khuôn gói 4 góc phòng + ngoài trời
- [`../Interface/README.md`](../Interface/README.md) — wireframe, toạ độ, lý do từng quyết định bố cục
