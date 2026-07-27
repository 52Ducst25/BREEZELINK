# Giao diện màn hình cho node TRONG NHÀ — bo `QR_Box_Advance_TouchScreen`

Thiết kế màn hình cảm ứng cho node indoor, thay bo ESP32 DevKit V1 trần hiện tại
(`../esp32-indoor/`). Trước bản này node indoor **không có hiển thị tại chỗ**:
mọi thứ chỉ nhìn được qua app/web, nên khi mất mạng người dùng đứng cạnh máy mà
không biết node còn sống hay không.

Tài liệu này là **nguồn sự thật của phần giao diện**. Code nằm ở
`../esp32-indoor/src/ui/`, build bằng env `qrbox-touch`.

- `Lopaka/` — bản phác Lopaka của một dự án khác, giữ lại làm **tham chiếu API**
  (`tft.drawRoundRect`, `tft.setFreeFont`, `tft.pushImage`…). Không dùng trực
  tiếp: nó vẽ nền bằng ảnh 320×240 nhúng cứng (537 KB source cho **một** màn),
  chữ tiếng Anh, và bố cục cho menu "GreenSystems" chứ không phải điều hoà.
  Xem §4 vì sao bỏ cách nhúng ảnh nền.

---

## 1. Bo mạch

`Research/Schematic/Touch_Screen_Extend_2026-03-11_13-31.PDF` — *QR Box Advance
Touch Screen*, rev 1.2, 9 trang.

| Khối | Linh kiện | Ghi chú |
|---|---|---|
| MCU | **ESP32‑WROOM‑32E‑N8** | 8 MB flash, **không PSRAM** |
| Màn hình | **YT280S030 2.8″ 240×320**, SPI 4 dây | J2 18 chân, **không nối MISO** · chip **ST7789V** — xem cảnh báo §2.2 |
| Cảm ứng | Điện dung, **I²C** (J1 6 chân: SCL/SDA/INT/RST) | chip nằm trên module màn |
| RTC | **DS1307Z** + pin VBAT + thạch anh 32.768 kHz | cùng bus I²C với cảm ứng |
| Còi | MLT‑8530 qua Q7 BSS138 | GPIO13 |
| 4G | **A7680C** (UART2) | *dự án này không dùng* → xem §3 |
| Vào/ra cách ly | 2 opto vào + 2 xung vào + 1 opto ra | *dự án này không dùng* |
| Nguồn | 9–24 VDC → TPS5430 → 5 V → 2× TLV75733 → 3.3 V | +5 V/1.2 A, dư cho module IR |
| Giám sát | TPS3823‑33 watchdog ngoài | tự reset nếu MCU treo |

Nguồn vào **9–24 VDC** (P2/P4, jack XH2.54) — khác hẳn bo DevKit cấp qua USB.
Ngoài UART0 debug (P3) bo **không có cổng USB**: nạp firmware bằng USB‑TTL cắm
vào P3, mạch AUTO BOOT (Q1/Q2 + DTR/RTS) tự đưa vào chế độ nạp như DevKit.

---

## 2. Bảng chân — đọc ngược từ schematic

Ánh xạ chân module U1 → GPIO lấy theo netlist trang 5 của schematic.

| GPIO | Net trên bo | Vai trò | Ghi chú |
|---|---|---|---|
| 0 | `ESP_BOOT` | strapping, mạch auto‑boot | |
| 1 / 3 | `UART_0_TX/RX` | Serial debug → P3 | log firmware ra đây |
| 2 | `UART_1_RX` | UART1 qua TXS0104 → P3 | strapping · mức 5 V ở đầu ngoài · **không dùng** |
| 4 | `TOUCH_SCREEN_SCL` | **I²C SCL** (cảm ứng + DS1307) | R2 kéo lên 10k |
| 5 | `UART_2_TX` → A7680C RX | strapping · R6 kéo lên 100k | **IR thu** (§3.1) — dùng được vì không hàn module 4G |
| 12 | `EN_LEVEL_SHIFT` | OE của TXS0104 | **strapping MTDI — phải LOW lúc boot** |
| 13 | `BUZZER` | còi | |
| 14 | `SIM_PWD_CNT` | bật/tắt module 4G (Q4) | R11 kéo **xuống** 10k |
| 15 | `UART_1_TX` | UART1 qua TXS0104 → P3 | strapping · mức 5 V ở đầu ngoài · **không dùng** |
| 16 | `TOUCH_SCREEN_SDA` | **I²C SDA** | R3 kéo lên 10k |
| 17 | `UART_2_RX` ← A7680C TX | R5 kéo lên 100k | **IR phát** (§3.1) — dùng được vì không hàn module 4G |
| 18 | `LCD_MOSI` | **TFT MOSI** | |
| 19 | `LCD_CS` | **TFT CS** | |
| 21 | `LCD_A0` | **TFT DC** | |
| 22 | `LCD_SCK` | **TFT SCLK** | |
| 23 | `LCD_RESET` | **TFT RST** (dùng chung cho module màn) | |
| 25 | `TOUCH_SCREEN_RST` | reset chip cảm ứng | |
| 26 | `SIGNAL_OUT_MCU` | opto ra TLP291 | |
| 27 | `LCD_BACKLIGHT` | **đèn nền**, Q5 BSS138 low‑side | mức CAO = sáng, PWM được |
| 32 | `DEVICE_PULSE_MCU` | opto vào | |
| 33 | `TOUCH_INT` | ngắt cảm ứng | |
| 34 | `OTA` | nút/jumper OTA | **chỉ vào** |
| 35 | `BILL_PULSE_MCU` | opto vào | **chỉ vào** |
| 36 | `SIGNAL_IN_1` | opto vào | **chỉ vào** |
| 39 | `SIGNAL_IN_2` | opto vào | **chỉ vào** |

> **Không còn một chân GPIO nào trống.** Mọi chân đưa ra khỏi module đều đã có
> chủ. Đây là ràng buộc chi phối toàn bộ §3 — bo này được thiết kế cho máy bán
> hàng QR, không phải cho node điều hoà.

### 2.1 Hai cái bẫy của bus I²C này

1. **DS1307 chỉ chịu 100 kHz.** Cảm ứng GT911 thích 400 kHz, nhưng hai con nằm
   chung bus nên **phải ghim `Wire.setClock(100000)`**. Chạy 400 kHz thì đồng hồ
   đọc ra giờ rác *không báo lỗi* — checksum BCD vẫn hợp lệ.
2. **Địa chỉ 0x38 đã bị chiếm** nếu module cảm ứng dùng FT6236. Đừng cắm thêm
   AHT20 (cũng 0x38) vào bus này — xem §3.
   *(Bo thực tế đo được là **GT911**, nằm ở 0x5D/0x14 — nhưng vẫn giữ SHT3x ở
   0x44 vì mỗi lô module cảm ứng một chip khác nhau, không đánh cược vào 0x38.)*

### 2.2 Chip màn là ST7789V, KHÔNG phải ILI9341 như schematic ghi

Schematic (trang 6) ghi linh kiện là `ILI9341SP4`, nhưng module YT280S030 được
nhà sản xuất bán với **hai tuỳ chọn chip — ILI9341 *hoặc* ST7789V**
([eya-display.com/yt280s030](https://www.eya-display.com/yt280s030/)) và bo này
lắp bản **ST7789V**. Firmware phải khai `-D ST7789_DRIVER=1`.

Khai nhầm `ILI9341_DRIVER` **rất dễ chẩn đoán sai**, vì màn không chết hẳn: hai
chip dùng chung nhiều lệnh nên `fillScreen`/`fillRect` vẫn ra đúng màu, chỉ chữ
và hình vẽ là nhiễu/xé. Triệu chứng đó trông y hệt nhiễu SPI hoặc tranh chấp đa
lõi, và đã từng làm mất nhiều giờ đi tìm nhầm hướng.

**Cách phân biệt trong 1 lần nạp:** bật `-D LCD_SELFTEST=1`. Nó vẽ hình mốc bằng
lệnh đơn giản rồi đóng băng, *ngay trong `setup()` ở lõi 1, trước khi tác vụ giao
diện lõi 0 chạy*. Mốc sạch mà khung giao diện vẽ ngay sau đó lại vỡ → loại hẳn
nghi vấn đa lõi và nhiễu đường truyền, chỉ còn khả năng sai driver.

---

## 3. Ba xung đột với firmware `esp32-indoor` hiện tại

Firmware indoor cần 3 chân mà bo này đã dùng hết:

| Chức năng | Chân cũ | Vướng gì trên bo mới |
|---|---|---|
| DHT | GPIO4 | = `TOUCH_SCREEN_SCL` |
| IR thu | GPIO27 | = `LCD_BACKLIGHT` |
| IR phát | GPIO26 | = `SIGNAL_OUT_MCU` (opto, không phát nổi sóng mang 38 kHz) |

### 3.1 Phương án đã chọn

**Cả hai chân IR mượn của module 4G A7680C — module mà dự án này không dùng
(§1) nên không hàn lên bo, hai chân của nó bỏ không:**

```
IR phát -> GPIO17 = UART_2_RX  (nối chân TX của A7680C), R5 kéo lên 100 kΩ
IR thu  -> GPIO5  = UART_2_TX  (nối chân RX của A7680C), R6 kéo lên 100 kΩ
```

Cả hai chạy **3.3 V thẳng**: đường `UART_2` **không** đi qua TXS0104, nên IR
không phụ thuộc `EN_LEVEL_SHIFT` — đây là lý do chính chọn cặp chân này thay vì
cặp `UART_1` (GPIO15/GPIO2) ra cổng P3 như bản trước tài liệu từng ghi. Đổi lại
không có tầm phát xa như đi 5 V, và **không chân nào ra header**: phải hàn dây
thẳng vào pad chân module trên bo.

**ĐIỀU KIỆN BẮT BUỘC: không hàn module A7680C lên bo.** Hàn vào là hai chân có
hai chủ, IR câm và module 4G cũng không chạy.

Ràng buộc lúc boot:

- GPIO5 là **chân strapping** (chọn timing SDIO slave), phải HIGH lúc reset —
  R6 100 kΩ kéo lên sẵn, và ngõ ra mắt thu IR ở trạng thái nghỉ cũng là HIGH,
  nên đúng yêu cầu. GPIO17 không phải chân strapping.
- **`EN_LEVEL_SHIFT` (GPIO12) vẫn phải kéo HIGH trong `setup()`** cho cổng
  `UART_1` ra P3, nhưng **IR không còn cần nó**. GPIO12 là **MTDI**: HIGH lúc
  reset thì ROM chọn mức flash 1.8 V và bo **không boot** — nên tuyệt đối không
  kéo lên bằng trở ngoài. Trên bo này **R7 10 kΩ kéo GPIO12 XUỐNG GND**, đúng
  chuẩn (đã đối chiếu schematic trang 5).

**DHT → bỏ, thay bằng cảm biến I²C.**
Không còn chân thứ ba cho DHT, và DHT là giao thức một dây hai chiều — ép qua
TXS0104 rất dễ hỏng theo kiểu khó tìm. Bus I²C sẵn có (GPIO4/16) đã có trở kéo,
đã chạy 100 kHz cho DS1307, thừa chỗ cho một con nữa:

| Cảm biến | Địa chỉ | Kết luận |
|---|---|---|
| **SHT30 / SHT31 / SHT35** | **0x44** | ✅ nên dùng — không đụng ai, chính xác hơn DHT22 nhiều |
| SHT40 | 0x44 | ✅ tương đương |
| AHT20 / AHT21 | 0x38 | ❌ **đụng FT6236 cảm ứng** |
| DS1307 (có sẵn) | 0x68 | — |
| Cảm ứng | 0x38 / 0x5D / 0x15 | — |

Đấu vào đâu: bus I²C không có header, phải câu từ **J1** (chân 2 = SCL, chân 3 =
SDA, chân 1 = 3V3, chân 6 = GND) hoặc từ chân DS1307. Đây là **sửa phần cứng**,
không tránh được — cần nói rõ với người lắp.

> GPIO17 (`UART_2_RX`, chân TX của A7680C) giờ đã dùng cho **IR phát** (§3.1),
> nên không còn là phương án dự phòng cho DHT nữa — giữ DHT thì bắt buộc phải
> qua đường I²C (SHT3x) như trên.

### 3.2 Bảng chân mới cho node indoor trên bo này

```
IR phát   -> GPIO17  (chân TX của A7680C — hàn thẳng vào pad, 3.3V)   ┐ KHÔNG hàn
IR thu    -> GPIO5   (chân RX của A7680C — hàn thẳng vào pad, 3.3V)   ┘ module 4G
EN dịch mức -> GPIO12 (đặt HIGH trong setup(), KHÔNG kéo lên bằng trở ngoài)
             — chỉ cho UART_1 ra P3; IR KHÔNG đi qua nó
Màn hình  -> ST7789V, MOSI 18 / SCK 22 / CS 19 / A0(DC) 21 / RST 23, xoay ngang (rotation 1)
Nhiệt/ẩm  -> SHT3x trên I²C 0x44 (SCL GPIO4 / SDA GPIO16, 100 kHz)
```

---

## 4. Hệ thiết kế

### 4.1 Nguyên tắc

Giao diện này bám **đúng bảng màu và hình học của app Flutter và web admin**
("Titanium Command": nền carbon tối, viền vuông, **góc vát 45°**, xanh kỹ thuật
`#0055FF`) — xem `app-flutter/lib/theme/ac_colors.dart`. Người dùng nhìn màn
trên tường và nhìn app trên điện thoại trong cùng một phút; hai thứ trông khác
hệ nhau thì bị đọc là hai sản phẩm.

Ba luật nội dung, kế thừa nguyên văn từ app:

1. **Không bịa số.** Thiếu số đo thì hiện `—`, không hiện `0.0`. Đây là lỗi mà
   app tiền nhiệm đã mắc và `ComfortPreview` được thiết kế để chặn ở mức kiểu dữ
   liệu — màn hình không được phép tái phạm.
2. **Màu nhiệt nói Ý NGHĨA, không nói NGUỒN.** `thermalCold/Neutral/Warm/Hot`
   áp theo *giá trị*, chưa bao giờ theo "đây là cảm biến trong hay ngoài".
3. **Không phím chết.** Nút chưa học mã IR thì hiện mờ + nói lý do, không im
   lặng bỏ qua.

### 4.2 Màu — RGB565 quy đổi từ `AcColors`

| Token | Hex app | RGB565 |
|---|---|---|
| `carbon` (nền) | `#0A0E14` | `0x0862` |
| `carbonUp` | `#121924` | `0x10C4` |
| `carbonPanel` | `#141C28` | `0x10E5` |
| `carbonLine` | `#2A3B4C` | `0x29C9` |
| `carbonLineBright` | `#3E5468` | `0x3AAD` |
| `ice` (nhấn) | `#0055FF` | `0x02BF` |
| `iceText` | `#4D8DFF` | `0x4C7F` |
| `white` | `#E7F1F8` | `0xE79F` |
| `whiteDim` | `#8DA2B5` | `0x8D16` |
| `success` | `#22C55E` | `0x262B` |
| `error` | `#FF4D4D` | `0xFA69` |
| `warning` | `#F5A623` | `0xF524` |
| `thermalCold` | `#3AA0FF` | `0x3D1F` |

### 4.3 Chữ — tiếng Việt CÓ DẤU bằng font VLW

Font GFX của TFT_eSPI (`FreeSansBold12pt7b`…) đánh chỉ số theo **một byte**, chỉ
phủ ASCII `0x20`–`0x7E`. Viết `LÀM LẠNH` bằng font GFX sẽ ra ô vuông hoặc rụng
dấu. Vì vậy màn hình dùng **font VLW** (smooth font, đánh chỉ số theo mã
Unicode) — bản trước của tài liệu này chốt "viết không dấu cho rẻ", nay đã đổi.

Font **nhúng thẳng vào flash** dưới dạng mảng `PROGMEM`, không để trong SPIFFS:
`tft.loadFont(const uint8_t*)` đọc được từ flash, nên không phải chia phân vùng
SPIFFS và không phải nhớ chạy thêm `pio run -t uploadfs` mỗi lần nạp — đúng loại
bước phụ mà người đi lắp sẽ quên, và quên thì màn trắng trơn không báo lỗi gì.

Sinh lại font: `python tools/make_vlw.py` (cần Pillow). Script đọc TTF Arial của
Windows, rasterise từng glyph rồi ghi ra `src/ui/fonts/*.h`. Chỉ phải chạy lại
khi đổi cỡ chữ hoặc đổi bộ ký tự.

> **BẪY LỚN NHẤT:** khi đã `loadFont()` thì `setFreeFont()` **và** `setTextFont()`
> đều **bị bỏ qua** — không trộn được VLW với font GFX trong cùng một khung hình.
> Nên toàn bộ phân cấp cỡ chữ phải dựng lại bằng VLW, và mọi chuỗi đưa lên màn
> phải là UTF-8 (`drawString` tự giải mã qua `decodeUTF8`).

| Vai trò | Font VLW | Nguồn | Bộ ký tự |
|---|---|---|---|
| Số lớn (nhiệt độ, setpoint) | `VietFontBig` 34px | Arial Bold | chỉ chữ số + `°C` (18 glyph) |
| Tiêu đề, nhãn nút | `VietFontLabel` 17px | Arial Bold | đủ 230 glyph |
| Nhãn phụ, thanh trạng thái | `VietFontSmall` 13px | Arial | đủ 230 glyph |

Font số lớn **cố ý không có glyph tiếng Việt** — nó chỉ hiện nhiệt độ nên cắt
xuống 18 glyph làm nó nhẹ đi ~15 lần. Tổng cả ba cỡ: **~70 KB flash**.

Bộ 230 glyph = ASCII in được + `°` + **134 ký tự tiếng Việt dựng sẵn**. Cố ý
KHÔNG cắt bớt theo "những chữ giao diện đang dùng": thêm một dòng chữ mới sau
này mà thiếu glyph thì chữ **biến mất trên màn trong khi build vẫn xanh** — lỗi
im lặng, rất khó lần ra.

Đổi cỡ chữ có chi phí thật (một lượt `malloc` + đọc lại bảng 230 glyph từ
flash), nên `Theme::useFont()` bỏ qua khi cỡ không đổi. Nhờ giao diện chỉ vẽ lại
những trường **đổi giá trị**, phần lớn khung hình không đổi font lần nào.

### 4.4 Vì sao KHÔNG nhúng ảnh nền như bản Lopaka

`Lopaka/Main.cpp` nhúng một mảng `uint16_t[76800]` = **150 KB flash cho một màn
hình**. Bốn màn + màn học = ~750 KB, và mỗi lần chuyển màn phải đẩy 150 KB qua
SPI (~40 ms ở 40 MHz) → chớp thấy rõ.

Thay bằng **panel phẳng vẽ bằng hình học**: `chamferRect()` (vát góc 45°) khớp
hình học thương hiệu, tốn 0 byte flash, và cho phép **vẽ lại từng ô** thay vì cả
màn. Xem §7.

---

## 5. Bộ màn hình

```
                          ┌──────────────┐
                 ┌────────┤ THANH TRẠNG THÁI (luôn hiện, y 0..21) │
                 │        └──────────────┘
   ┌─────────┬───┴─────┬──────────┬─────────┐
   │TRANG CHU│DIEU KHIEN│THONG TIN │ CAI DAT │   ← thanh điều hướng y 206..239
   └─────────┴─────────┴──────────┴─────────┘
                 │
                 └── HỌC REMOTE: lớp phủ toàn màn, tự bật khi server gửi
                     {"learn":"COOL 25"}, tự tắt khi xong/hết giờ
```

Lưới chung: màn 320×240, đệm 6 px, thanh trạng thái cao 22, thanh nav cao 34,
vùng nội dung `y = 24…203`.

### 5.1 TRANG CHU — mặc định

```
┌────────────────────────────────────────────────┐ 0
│ ▣ AIRCON            ◇ ◇ ((( )))          04:20 │ thanh trạng thái
├────────────────────────────────────────────────┤ 22
│ ┌─────────────────────┐  ┌─────────────────────┐│
│ │ TRONG NHA           │  │ NGOAI TROI       ● ││ ← ● xanh/xám = slave sống/chết
│ │                     │  │                     ││
│ │   28.4 °C           │  │   33.1 °C           ││ FreeSansBold24pt
│ │                     │  │                     ││
│ │ DO AM      62 %     │  │ DO AM      70 %     ││
│ └─────────────────────┘  └─────────────────────┘│
│  6,26,150,96              164,26,150,96         │
│ ┌────────────────────────────────────────────┐  │
│ │ ❄ LAM LANH        26 °C        [TU DONG]   │  │ 6,128,308,74
│ │ lenh cuoi 2 phut truoc                     │  │
│ └────────────────────────────────────────────┘  │
├────────────────────────────────────────────────┤ 205
│ TRANG CHU │DIEU KHIEN│THONG TIN │  CAI DAT     │
└────────────────────────────────────────────────┘ 239
```

- Số nhiệt độ tô theo **thang nhiệt** (`thermalCold…Hot`), không theo trong/ngoài.
- `NGOAI TROI` hiện `—` và chấm xám khi `SlaveWatch` báo mất nhịp tim — đúng
  thông tin mà web đang hiện, không phải số cũ đóng băng.
- Huy hiệu góc phải khối máy lạnh: `TU DONG` (xanh `ice`) hoặc `GHI DE` (cam
  `warning`) — xem §8.

### 5.2 DIEU KHIEN

```
├────────────────────────────────────────────────┤ 24
│ ┌──────┐ ┌────────────────────┐ ┌──────┐       │
│ │      │ │                    │ │      │       │
│ │  −   │ │       26 °C        │ │  +   │       │ 8,28,68,76 · 84,28,152,76 · 244,28,68,76
│ │      │ │                    │ │      │       │
│ └──────┘ └────────────────────┘ └──────┘       │
│ ┌──────┐┌──────┐┌──────┐┌──────┐               │
│ │ LANH ││ KHO  ││ QUAT ││ TAT  │               │ y=110 h=44 w=74 @ x 6/84/162/240
│ └──────┘└──────┘└──────┘└──────┘               │
│ ┌──────────────┐  ┌──────────────┐             │
│ │     GUI      │  │   TU DONG    │             │ 6,160,150,42 · 164,160,150,42
│ └──────────────┘  └──────────────┘             │
```

Thay đổi được **gom lại rồi mới gửi** khi bấm `GUI` — giống nút submit của
`OverridePanel` trong app, không bắn IR theo từng lần chạm `+`. Bấm `TU DONG` =
bỏ ghi đè cục bộ, trả quyền cho vòng lặp comfort của server.

Nút chế độ chưa có mã IR trong NVS → tô `carbonUp` + chữ `whiteDim`, chạm vào
hiện toast `CHUA HOC MA — vao app de hoc`. Không bao giờ im lặng.

### 5.3 THONG TIN — chẩn đoán tại chỗ

Tám dòng nhãn/giá trị, `y = 30` bước 21 px, nhãn trái x=14, giá trị phải x=306:

`WIFI` · `IP` · `SONG` (RSSI dBm) · `MQTT` · `ESP-NOW` (nhận/bỏ) · `NGOAI TROI`
(giây kể từ gói cuối) · `MA IR` (số mã trong NVS) · `FW / UPTIME`.
Dòng chân trang: `MAC xx:xx:… · KENH n`.

Màn này tồn tại để người đi lắp trả lời được "vì sao web không thấy node" mà
không phải cắm laptop — đúng những dòng đang phải đọc bằng `pio device monitor`.

### 5.4 CAI DAT

Bốn hàng đầy chiều rộng, `x=6 w=308 h=40`, `y = 28 / 74 / 120 / 166`:

| Hàng | Điều khiển | Ghi chú |
|---|---|---|
| `DO SANG` | `−` `70%` `+` | PWM LEDC lên GPIO27, bước 10 %, sàn 10 % (0 % = màn như hỏng) |
| `AM BAO` | `BAT` / `TAT` | còi GPIO13, bíp 40 ms mỗi lần chạm |
| `DONG BO GIO` | `CHAY` | nạp giờ NTP vào DS1307 |
| `KHOI DONG LAI` | `CHAY` | `ESP.restart()`, có bước xác nhận |

Không có mục "đổi WiFi" — cấu hình WiFi nằm trong `config.h`, thêm màn nhập mật
khẩu bằng bàn phím ảo là một dự án riêng.

### 5.5 HỌC REMOTE — lớp phủ

Tự bật khi `IrIo::learning()` = true (server gửi `{"learn":"COOL 25"}`), không
phải do người dùng bấm. Hộp `16,30,288,168`:

```
┌──────────────────────────────────────┐
│           DANG HOC REMOTE            │
│                                      │
│              COOL 25                 │  FreeSansBold24pt
│                                      │
│  Huong remote vao mat thu, bam nut   │
│  ████████████████░░░░░░░░░░    18s   │  thanh đếm ngược
└──────────────────────────────────────┘
```

Thành công → bíp 2 tiếng + `DA HOC XONG` 1.5 s. Hết giờ → `KHONG BAT DUOC TIN
HIEU` + gợi ý (pin remote / hướng / khoảng cách) — cùng nội dung với log serial.

---

## 6. Bản đồ chạm

| Vùng | Rect | Hành động |
|---|---|---|
| Tab nav | `(80·i, 206, 80, 34)`, i=0..3 | chuyển màn |
| Home · khối máy lạnh | `6,128,308,74` | tắt sang DIEU KHIEN |
| Điều khiển · `−` / `+` | `8,28,68,76` / `244,28,68,76` | setpoint ∓1, kẹp trong `[16,30]` |
| Điều khiển · chế độ | `(6+78·i, 110, 74, 44)` | chọn LANH/KHO/QUAT/TAT |
| Điều khiển · `GUI` | `6,160,150,42` | bắn IR + publish `state` |
| Điều khiển · `TU DONG` | `164,160,150,42` | bỏ ghi đè cục bộ |
| Cài đặt | 4 hàng §5.4 | |

Ô chạm nhỏ nhất là 44×44 px — trên màn 2.8″ (≈0.18 mm/px) tương đương 8 mm,
vừa đủ cho đầu ngón tay. **Chống dội**: chỉ nhận cạnh xuống, khoá 250 ms; màn
điện dung rất dễ sinh chuỗi chạm liên tiếp mà `+` bấm nhầm 5 lần là sai 5 °C.

---

## 7. Chạy song song — hai tác vụ, hai lõi

### 7.1 Vì sao không vẽ trong `loop()`

`loop()` của node **đứng hình hàng giây như chuyện bình thường**, không phải hãn
hữu:

| Chỗ chặn | Bao lâu |
|---|---|
| `connectWifi()` — `while` + `delay(500)` | tới khi vào được mạng |
| `connectMqtt()` — `delay(2000)` mỗi lần `rc != 0` | vô hạn khi broker chết |
| `IrIo::blast()` | 50–250 ms mỗi lệnh |
| DHT lỗi (bo cũ) | `delay(3000)` |

Vẽ trong `loop()` thì **đúng lúc mất mạng — lúc người dùng cần nhìn màn nhất —
màn đứng im như node đã chết**. Với bảng điều khiển treo tường, đó là kiểu hỏng
tệ nhất: nó khẳng định sai.

### 7.2 Lõi 0 cho giao diện, lõi 1 cho IR — bắt buộc

Không phải để "cho mượt". `IrIo::blast()` tự đếm nhịp bằng `delayMicroseconds()`
để dựng sóng mang 38 kHz (chu kỳ 26 µs). Một tác vụ khác **cùng lõi** bị bộ lập
lịch xen vào giữa là mark/space giãn ra vài chục µs → khung IR sai → **máy lạnh
im lặng không phản ứng, mà log vẫn báo `da phat`**.

Arduino chạy `loop()` ở lõi 1 (`ARDUINO_RUNNING_CORE`), nên giao diện phải sang
lõi 0. Dùng `xTaskCreatePinnedToCore(...,  UI_CORE=0)` — **không** dùng
`xTaskCreate`, vì thả cho bộ lập lịch chọn lõi là có ngày nó xếp vào lõi 1.

```
  lõi 1 — loopTask (ưu tiên 1)         lõi 0 — tác vụ "ui" (ưu tiên 2)
  ┌────────────────────────┐           ┌──────────────────────────────┐
  │ WiFi · MQTT · ESP-NOW  │  publish  │ SPI màn hình                 │
  │ IR phát/học            │ ────────► │ I2C: cảm ứng + DS1307 + SHT3x│
  │ NVS (IrStore)          │           │ LEDC: đèn nền + còi          │
  │                        │ ◄──────── │                              │
  └────────────────────────┘ pollCommand└──────────────────────────────┘
                             / reply
```

### 7.3 Ba đường qua ranh giới — và không có đường thứ tư

| Đường | Kiểu | Ai chờ ai |
|---|---|---|
| `Ui::publish(Model)` | mutex + `memcpy` | **không ai chờ ai**: `xSemaphoreTake(mx, 0)` — lấy không được thì bỏ ảnh chụp này, vòng sau (vài ms) lại có cái mới |
| `Ui::pollCommand()` | queue 4 phần tử | UI đặt hàng, `loop()` rút ra thi hành |
| `Ui::reply()` | queue 4 phần tử | `loop()` trả kết quả → toast |
| `Ui::readIndoor()` | spinlock, 2 float | UI đo SHT3x (nó sở hữu bus I2C), `loop()` lấy số đi gửi |

Sở hữu phần cứng chia dứt khoát, **không có tài nguyên nào hai bên cùng chạm**.
Đây đúng là khuôn đã dùng cho callback MQTT trong `main.cpp` ("callback chỉ bóc
gói ra rồi đặt hàng, còn `loop()` mới phát IR + gửi ack") — nay áp cho lõi thứ
hai.

Hệ quả thực tế: bấm `GUI` trên màn **không** bắn IR tại tác vụ UI. Nó đẩy một
`Command` vào queue; `loop()` tra `IrStore::loadAlias()`, bắn IR ở lõi 1, rồi
`Ui::reply()` cho biết kết quả. Người dùng thấy toast `DANG GUI...` đổi thành
`DA GUI...` — đúng chuỗi việc đang xảy ra, không phải hoạt ảnh giả.

### 7.4 Vẽ cái gì, khi nào

Vẽ lại cả màn là 320×240×2 byte qua SPI 40 MHz ≈ **40 ms**. Hai tầng:

1. **Đổi màn / đóng lớp phủ** → vẽ nền + khung tĩnh một lần.
2. **Đổi giá trị** → mỗi trường giữ bản sao "đã vẽ lần trước", chỉ trường nào
   khác mới tô lại. Nhịp 200 ms (mắt không phân biệt nhanh hơn).

Chống chớp **không cần sprite**: `tft.setTextPadding(w)` xoá chữ cũ ngay trong
lượt vẽ chữ mới, nên không có khoảnh khắc nào ô trống trên màn. Rẻ hơn hẳn
phương án đệm một `TFT_eSprite` 152×76 — vốn tốn ~23 KB RAM mà WROOM‑32E‑N8
không có PSRAM để bù.

Nhịp quét chạm là **15 ms** (~66 Hz), tách khỏi nhịp vẽ 200 ms: nút phản hồi
tức thì trong khi màn vẫn không bị vẽ thừa.

### 7.5 Những thứ chỉ làm được nhờ tách tác vụ

- **Nháy nút khi bấm** (`pressFlash`, 90 ms) — `vTaskDelay` ở lõi 0 không đụng
  gì tới MQTT/IR đang chạy ở lõi 1.
- **Tự hạ sáng** sau 60 s không ai chạm (xuống 15 %). Cú chạm đánh thức **không**
  tính là bấm nút: người dùng chạm màn tối để *nhìn*, không phải để đổi nhiệt độ.
- **Đồng bộ NTP không chặn**: `ntpBegin()` + `ntpPoll()` mỗi vòng thay vì một
  hàm chặn 8 giây. Giao diện đứng hình 8 s ngay sau khi bấm nút là dấu hiệu kinh
  điển của "máy treo" — người dùng sẽ bấm loạn hoặc rút điện.
- **Đo SHT3x mỗi 2 s** để màn phản ánh phòng gần như tức thì, trong khi
  telemetry lên cloud vẫn giữ nhịp `TELEMETRY_MS` riêng.

Ngân sách: ngăn xếp tác vụ UI 8 KB + bộ đệm MQTT 12 KB + `irBuf` 1.2 KB.

---

## 8. Ghi đè tại chỗ — và khoảng trống ở backend

### 8.1 Vấn đề

Node **không có bảng tra (mode, setpoint) → mã IR**. `IrStore` khoá theo
`ir_code_id` (UUID do server sinh); server gửi `ir_raw` kèm `mode` + `setpoint` +
`ir_code_id`, nhưng node chỉ lưu theo id. Nên khi người dùng bấm `26 °C` trên
màn, node **không biết phát khung nào**.

### 8.2 Cách giải: chỉ mục bí danh trong NVS

Thêm `IrStore::saveAlias(mode, temp, irCodeId)` / `loadAlias(...)`: mỗi lần
server gửi một lệnh, node lưu thêm khoá `aCOOL26` → `ir_code_id`. Sau đó màn
hình tra được ngược.

Hệ quả — **và phải nói thật trên giao diện**: panel chỉ điều khiển được những
tổ hợp (chế độ, nhiệt độ) mà **server đã từng gửi ít nhất một lần**. Bo mới nạp
firmware thì mọi nút chế độ đều mờ cho tới khi vòng lặp comfort chạy vài chu kỳ.
Đây là lý do §5.2 bắt buộc có trạng thái "mờ + giải thích".

### 8.3 Khoảng trống: server sẽ giành lại quyền

`mqtt_naming.py` chỉ có 5 topic (`telemetry` `cmd` `state` `status` `learn`) —
**không có đường để node xin đặt ghi đè**. Ghi đè thật sống trong Redis
(`redis_override_service`) và chỉ đặt được qua REST API.

Nên ghi đè từ màn hình hiện tại là **cục bộ**:

1. Bắn IR ngay → máy lạnh nghe lời liền.
2. Publish `state` **không kèm `ack`** → `state_handler` ghi mode/setpoint vào
   `redis_state_service`, app và web thấy trạng thái mới.
3. Nhưng `comfort_engine` **không dừng lại**: chu kỳ sau nó vẫn tự quyết và gửi
   `cmd` đè lên.

Giao diện nói đúng chuyện đó thay vì giấu: huy hiệu `GHI DE` kèm dòng chữ
`may chu se gianh lai quyen o chu ky sau`, và huy hiệu trở về `TU DONG` ngay khi
`cmd` kế tiếp tới.

**Để ghi đè từ panel thành thật sự**, backend cần một trong hai:
- topic mới `bl/{org}/{uuid}/override` (node → cloud) mà worker chuyển thẳng vào
  `redis_override_service`; hoặc
- node gọi REST `POST /control/override` — kéo theo phải nhét JWT vào firmware,
  nặng hơn hẳn.

Khuyến nghị: **topic mới**. Đây là việc backend, nằm ngoài phạm vi bản này, và
đã được ghi lại ở đây để không bị quên.

---

## 9. Build

```bash
cd FirmWare/esp32-indoor
cp src/config.h.example src/config.h      # điền như README §2
pio run -e qrbox-touch -t upload --upload-port COMx   # USB-TTL cắm vào P3
pio device monitor -p COMx -b 115200
```

Env `qrbox-touch` đặt sẵn: chân TFT/I²C/IR, `HAS_DISPLAY`, và toàn bộ cờ
`USER_SETUP_LOADED` của TFT_eSPI (không sửa `User_Setup.h` trong thư viện — file
đó nằm trong `.pio/`, `pio pkg update` là mất).

Ba env dùng chung **một** mã nguồn, đúng lý do đã ghi ở đầu `platformio.ini`:
khác biệt giữa các bo chỉ là sơ đồ chân.

### 9.1 Nếu màn trắng / nhiễu sọc

| Triệu chứng | Nguyên nhân thường gặp |
|---|---|
| Trắng tinh, không gì hiện | Đèn nền OK nhưng SPI chết → kiểm tra `TFT_MOSI=18` `TFT_SCLK=22` (không phải chân VSPI mặc định) |
| **Nền/khối màu đúng nhưng CHỮ và hình vẽ nhiễu/xé** | **Sai driver** — phải là `ST7789_DRIVER`, không phải `ILI9341_DRIVER` (§2.2). Đây KHÔNG phải nhiễu SPI: hạ tần số bao nhiêu cũng không hết |
| Sọc/nhiễu khi vẽ nhanh | 40 MHz qua ma trận GPIO là sát trần → hạ `SPI_FREQUENCY` xuống 27 MHz |
| Hình đúng nhưng lộn ngược 180° | Đổi `TFT_ROTATION` giữa 1 và 3 (cả hai đều là ngang 320×240) |
| **Lặp `rst:0x3 (SW_RESET)` mỗi ~28 ms, không in nổi dòng log nào** | KHÔNG phải chết phần cứng. Bảng phân vùng vượt mốc 4 MB — dùng `huge_app.csv`, đừng dùng `default_8MB.csv` (xem `platformio.ini`) |
| Tối om | Chưa đặt PWM đèn nền, hoặc `DO SANG` = 0 |
| Chạm lệch trục | Đổi `TOUCH_SWAP_XY` / `TOUCH_INVERT_X` / `TOUCH_INVERT_Y` trong `config.h` |
| Cảm ứng không nhận | `Touch::chip()` in ra lúc boot; `NONE` = sai địa chỉ hoặc chưa nhả RST (GPIO25) |
| Giờ ra số vô lý | Bus I²C đang chạy >100 kHz — DS1307 không chịu nổi (§2.1) |

---

## 10. Còn lại phải làm

- [ ] **Đấu dây** SHT3x vào J1 (chân 1 = 3V3, 2 = SCL, 3 = SDA, 6 = GND), module
      IR thu vào P3, và module IR phát hàn trực tiếp vào pad TX của A7680C
      (GPIO17, §3.1 — **không hàn module 4G** lên bo) — *driver và firmware đã
      xong, đây là việc phần cứng*
- [ ] Chỉnh `TOUCH_SWAP_XY` / `TOUCH_INVERT_*` sau khi chạm thử trên bo thật (§9.1)
- [ ] Topic `override` phía backend (§8.3)
- [x] ~~Font VLW tiếng Việt có dấu~~ — xong, xem §4.3 và `tools/make_vlw.py`
- [ ] Đo lại SW1/SW2 trên bo: schematic ghi "INTERNAL BUTTON" nhưng dây đi vào
      khối `PULSE OUT CONFIG`, chưa chắc là nút bấm cho người dùng
