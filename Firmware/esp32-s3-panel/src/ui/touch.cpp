#include "touch.h"
#include <Wire.h>
#include "../config.h"

namespace Touch {

static Chip found = NONE;
static uint8_t addr = 0;

// Địa chỉ I2C của ba họ chip hay gặp trên module ILI9341 2.8" cảm ứng điện dung.
static const uint8_t ADDR_FT6236 = 0x38;
static const uint8_t ADDR_GT911_A = 0x5D;   // INT giữ LOW khi nhả RST
static const uint8_t ADDR_GT911_B = 0x14;   // INT giữ HIGH khi nhả RST
static const uint8_t ADDR_CST816  = 0x15;

static bool ping(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

/// Đọc [n] byte từ thanh ghi 8-bit (FT6236 / CST816).
static bool read8(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

/// Đọc [n] byte từ thanh ghi 16-bit (GT911).
static bool read16(uint16_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static void write16(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(val);
  Wire.endTransmission();
}

bool begin(uint8_t sdaPin, uint8_t sclPin, uint8_t rstPin, uint8_t intPin) {
  Wire.begin(sdaPin, sclPin);
  // 100kHz là TRẦN CỦA DS1307 nằm chung bus, không phải lựa chọn thận trọng
  // thừa: ở 400kHz đồng hồ trả về BCD rác mà checksum vẫn hợp lệ, tức là sai
  // âm thầm. Xem ../../Interface/README.md §2.1.
  Wire.setClock(100000);

  // Chuỗi reset của GT911 quyết định luôn địa chỉ của nó: mức INT lúc nhả RST
  // chọn 0x5D (LOW) hay 0x14 (HIGH). Giữ INT LOW để ưu tiên 0x5D, sau đó thả
  // INT về đầu vào cho chế độ chạy. FT6236/CST816 bỏ qua đoạn này vô hại.
  pinMode(rstPin, OUTPUT);
  pinMode(intPin, OUTPUT);
  digitalWrite(intPin, LOW);
  digitalWrite(rstPin, LOW);
  delay(10);
  digitalWrite(rstPin, HIGH);
  delay(60);
  pinMode(intPin, INPUT);
  delay(50);

  if      (ping(ADDR_GT911_A)) { found = GT911;  addr = ADDR_GT911_A; }
  else if (ping(ADDR_GT911_B)) { found = GT911;  addr = ADDR_GT911_B; }
  else if (ping(ADDR_FT6236))  { found = FT6236; addr = ADDR_FT6236; }
  else if (ping(ADDR_CST816))  { found = CST816; addr = ADDR_CST816; }
  else                         { found = NONE;   addr = 0; }

  return found != NONE;
}

Chip chip() { return found; }

const char *chipName() {
  switch (found) {
    case FT6236: return "FT6236";
    case GT911:  return "GT911";
    case CST816: return "CST816";
    default:     return "KHONG THAY";
  }
}

/// Xoay toạ độ gốc (module 240x320 dọc) về hệ 320x240 nằm ngang của TFT.
/// Ba cờ trong config.h để chỉnh mà không phải dịch lại logic: lô module khác
/// nhau dán tấm cảm ứng lệch trục nhau, và không có cách nào đoán từ schematic.
static void orient(uint16_t rx, uint16_t ry, int16_t &x, int16_t &y) {
#if TOUCH_SWAP_XY
  int32_t px = ry, py = rx;
#else
  int32_t px = rx, py = ry;
#endif
#if TOUCH_INVERT_X
  px = 319 - px;
#endif
#if TOUCH_INVERT_Y
  py = 239 - py;
#endif
  x = (int16_t)constrain(px, 0, 319);
  y = (int16_t)constrain(py, 0, 239);
}

bool read(int16_t &x, int16_t &y) {
  if (found == NONE) return false;

  if (found == GT911) {
    uint8_t st;
    if (!read16(0x814E, &st, 1)) return false;
    if ((st & 0x80) == 0) return false;             // chưa có dữ liệu mới
    const uint8_t n = st & 0x0F;
    uint8_t p[4] = {0};
    bool ok = (n >= 1) && read16(0x8150, p, 4);
    write16(0x814E, 0);                             // BẮT BUỘC xoá cờ, không thì kẹt luôn
    if (!ok) return false;
    orient((uint16_t)(p[0] | (p[1] << 8)), (uint16_t)(p[2] | (p[3] << 8)), x, y);
    return true;
  }

  // FT6236 và CST816S dùng chung khuôn thanh ghi: 0x02 = số điểm chạm,
  // 0x03..0x06 = toạ độ điểm 1 (4 bit cao của X/Y là cờ sự kiện, phải che đi).
  uint8_t b[5];
  if (!read8(0x02, b, 5)) return false;
  if ((b[0] & 0x0F) == 0) return false;
  orient((uint16_t)(((b[1] & 0x0F) << 8) | b[2]),
         (uint16_t)(((b[3] & 0x0F) << 8) | b[4]), x, y);
  return true;
}

} // namespace Touch
