#pragma once
// ============================================================================
//  BreezeLink — SƠ ĐỒ CHÂN THEO BO. Chọn bằng cờ -D trong platformio.ini.
// ----------------------------------------------------------------------------
//  VÌ SAO TÁCH KHỎI config.h: config.h chứa BÍ MẬT (mật khẩu WiFi, token MQTT)
//  và ĐỊNH DANH (device_uuid) — những thứ gắn với MỘT HỘ, không phải một bo.
//  Hai bo panel khác nhau chạy cùng một hàng `devices` trên web nên phải dùng
//  CHUNG config.h, nhưng chân thì khác hoàn toàn. Để lẫn hai loại vào một file
//  thì mỗi lần đổi bo là phải sửa file đang chứa mật khẩu — và file đó bị
//  gitignore, tức là sửa sai không ai review được và git không cứu lại được.
//
//  THÊM MỘT BO MỚI: thêm một nhánh #elif ở dưới, KHÔNG sửa config.h.
//
//  CHÂN IR KHÔNG NẰM Ở ĐÂY. `IR_TX_PIN`/`IR_RX_PIN` phải ở build_flags cùng chỗ
//  với cờ TFT_*, vì cả hai nhóm được đọc lúc biên dịch THƯ VIỆN chứ không chỉ
//  lúc biên dịch src/.
// ============================================================================

/// Không có phần cứng đó trên bo. 255 chứ không phải -1: mọi API chân trong dự
/// án nhận uint8_t, nên -1 lặng lẽ thành 255 ở chỗ này mà thành 0xFF ở chỗ kia.
#define PIN_NONE 255

#if defined(BOARD_S3_PANEL)
// ============================================================================
//  Bo panel 2.8" ESP32-S3  (màn ILI9341V + cảm ứng FT6336G + mic/loa I2S + thẻ SD)
// ----------------------------------------------------------------------------
//  THAY CHO QR Box Advance. Bo QR Box hỏng mạch nạp (esptool dò được chip nhưng
//  mọi lần ghi đều đứt giữa chừng), và bo này có sẵn cảm ứng điện dung + màn
//  cùng cỡ 2.8" 240x320 nên giao diện LVGL bê nguyên sang được.
//
//  ĐIỀU PHẢI BIẾT TRƯỚC KHI HÀN: bo gần như kín chân. Mọi chân đã có chủ trừ:
//    IO2, IO3, IO14, IO21   — hàng "Expand pin" trong tài liệu bo
//    IO43, IO44             — UART0, CHỈ rảnh vì console đi qua USB gốc
//  Đã có chủ: màn (10/11/12/13/45/46), cảm ứng (15/16/17/18), thẻ SD
//  (38/39/40/41/47/48), âm thanh I2S (1/4/5/6/7/8), đèn RGB (42), đo pin (9),
//  nút BOOT (0).
//
//  Chia như sau:
//    IO2  IR phát        IO43/44  UART sang UNO Q (xem platformio.ini: chiều
//    IO3  IR thu                  KHÔNG được đảo, và điều kiện USB gốc)
//    IO14 IO21  ĐỂ TRỐNG cho PZEM giai đoạn sau — Modbus-RTU qua UART, 2 chân.
//
//  Nên VẪN KHÔNG CÒN CHÂN CHO CÒI (xem BUZZER_PIN).
// ============================================================================

//  Bus I2C: chỉ có chip cảm ứng FT6336G (0x38). KHÔNG có DS1307, KHÔNG có SHT3x.
//  Vì bus này giờ chỉ còn một chip chịu được 400kHz, trần 100kHz trong touch.cpp
//  là thừa — nhưng cứ để, nó không làm chậm gì thấy được (đọc 5 byte mỗi 5ms).
#define I2C_SDA_PIN        16
#define I2C_SCL_PIN        15
#define TOUCH_RST_PIN      18
#define TOUCH_INT_PIN      17

//  Đèn nền: mức CAO = sáng, PWM được.
//
//  IO45 LÀ CHÂN STRAPPING (VDD_SPI) — PHẢI THẤP LÚC RESET, nếu cao thì chip chọn
//  mức 1.8V cho flash và bo KHÔNG BOOT. An toàn ở đây vì LEDC chỉ gắn vào chân
//  sau khi đã boot xong. NHƯNG: TUYỆT ĐỐI KHÔNG HÀN TRỞ KÉO LÊN vào chân này,
//  và đừng để firmware nào kéo nó cao rồi reset mềm.
#define LCD_BACKLIGHT_PIN  45

//  KHÔNG CÓ CÒI TRÊN BO NÀY.
//
//  Không phải quên: bốn chân tự do đã dùng hết cho IR + UART UNO Q (xem đầu
//  khối), và còi là thứ đáng hy sinh nhất trong số đó — nó chỉ báo "đã nhận
//  chạm", mà màn hình cũng làm được việc đó bằng toast.
//
//  Bo CÓ loa qua I2S (IO1/4/5/6/7/8) nên tiếng bíp vẫn khả thi sau này, chỉ là
//  phải phát mẫu PCM qua I2S chứ không PWM một chân — việc riêng, không gộp vào
//  lần chuyển bo này.
#define BUZZER_PIN         PIN_NONE

//  KHÔNG CÓ TXS0104 (bộ dịch mức). Bo này 3.3V thẳng ra header, không có IC đệm
//  nào — nên đường UART sang UNO Q KHÔNG phụ thuộc chân cho phép nào cả. Đây là
//  ĐIỀU TỐT: cái bẫy lớn nhất của bo QR Box (quên kéo OE lên là UART câm hoàn
//  toàn mà không có lỗi nào) biến mất.
#define EN_LEVEL_SHIFT_PIN PIN_NONE

//  KHÔNG CÓ DS1307. Đồng hồ lấy từ NTP vào đồng hồ hệ thống của chip.
//  Hệ quả PHẢI BIẾT: mất điện là mất giờ (không có pin nuôi), nên sau mỗi lần
//  khởi động thanh trạng thái hiện "--:--" cho tới khi có mạng và SNTP trả lời —
//  thường vài giây. Đó là hành vi đúng, không phải lỗi.
#define HAS_RTC_DS1307     0

//  1 hoặc 3 = nằm ngang 320x240 (panel gốc 240x320 dọc, xoay bằng phần mềm).
//  CHƯA ĐO TRÊN BO THẬT — nếu hình đúng nhưng lộn ngược 180 độ thì đổi sang 3.
#define TFT_ROTATION       1

//  Xoay/lật toạ độ cảm ứng về hệ của TFT.
//  CHƯA ĐO TRÊN BO THẬT. Cách chỉnh: chạm góc TRÊN-TRÁI của màn rồi xem log.
//    - chạm trái/phải mà con trỏ chạy lên/xuống  -> đổi TOUCH_SWAP_XY
//    - chạm trái ra phải                          -> đổi TOUCH_INVERT_X
//    - chạm trên ra dưới                          -> đổi TOUCH_INVERT_Y
//  Ba cờ độc lập nhau; sửa từng cái một, đừng sửa hai cái cùng lúc.
#define TOUCH_SWAP_XY      1
#define TOUCH_INVERT_X     0
#define TOUCH_INVERT_Y     1

#else
// ============================================================================
//  Bo QR Box Advance Touch Screen  (ESP32-WROOM-32E-N8 + màn YT280S030/ST7789)
// ----------------------------------------------------------------------------
//  Mặc định khi không khai cờ bo nào — giữ nguyên hành vi cũ.
//  Cách đọc ngược sơ đồ chân từ schematic: ../../Interface/README.md
// ============================================================================

//  Bus I2C dùng chung: cảm ứng màn (0x38/0x5D/0x15) + DS1307 (0x68) + SHT3x (0x44).
//  ĐỪNG nâng lên 400kHz — DS1307 chỉ chịu 100kHz, xem Interface/README.md §2.1.
#define I2C_SCL_PIN        4
#define I2C_SDA_PIN        16
#define TOUCH_RST_PIN      25
#define TOUCH_INT_PIN      33

//  Đèn nền qua Q5 (BSS138 hạ áp phía mát): mức CAO = sáng, PWM được.
#define LCD_BACKLIGHT_PIN  27
#define BUZZER_PIN         13

//  OE của TXS0104 (bộ dịch mức cho UART_1 ra cổng P3).
//
//  ĐƯỜNG UART SANG UNO Q (GPIO2/15) ĐI QUA IC NÀY — nên chân này là điều kiện
//  sống còn của nó. OE thấp thì ngõ ra treo lơ lửng và UART câm hoàn toàn mà
//  không có lỗi nào. (IR thì không: cả phát GPIO5 lẫn thu GPIO17 đều 3.3V thẳng.)
//
//  PHẢI đặt trong setup() chứ không kéo bằng trở ngoài: GPIO12 = MTDI, HIGH
//  lúc reset thì ROM chọn mức flash 1.8V và bo không boot. Trên bo này R7 10k
//  kéo GPIO12 XUỐNG GND — đúng chuẩn (đã đối chiếu schematic).
#define EN_LEVEL_SHIFT_PIN 12

//  DS1307 có pin nuôi riêng -> giữ giờ qua cả lúc mất điện.
#define HAS_RTC_DS1307     1

//  1 hoặc 3 = nằm ngang 320x240 (panel gốc là 240x320 dọc, xoay bằng phần mềm).
//  Bo này dùng 1 — đã đo trên bo thật: 3 cho ra hình ĐÚNG NHƯNG LỘN NGƯỢC 180 độ.
#define TFT_ROTATION       1

//  Xoay/lật toạ độ cảm ứng về hệ của TFT. Mỗi lô module dán tấm cảm ứng lệch
//  trục khác nhau và schematic không nói gì — chạm thử rồi chỉnh ba cờ này.
#define TOUCH_SWAP_XY      1
#define TOUCH_INVERT_X     0
#define TOUCH_INVERT_Y     1

#endif
