#include "board-io.h"
#include <Wire.h>
#include <time.h>
#include "../config.h"

namespace BoardIo {

// LEDC kênh 4 và 5, KHÔNG phải 0/1: IRremoteESP8266 chiếm kênh thấp cho sóng
// mang 38kHz trên ESP32. Đè lên nhau thì đèn nền nhấp nháy đúng lúc bắn IR —
// một lỗi chỉ xuất hiện khi điều khiển máy lạnh, rất mất công tìm.
static const uint8_t LEDC_BL   = 4;
static const uint8_t LEDC_BUZZ = 5;

static uint8_t blPin = 255, buzzPin = 255;
static uint8_t blPercent = 70;
static bool    buzzOn = true;
static uint32_t buzzUntil = 0;

// Arduino-ESP32 3.x đổi hẳn API LEDC (ledcAttach thay cho ledcSetup +
// ledcAttachPin). Bọc lại để cùng mã nguồn chạy được cả hai đời core.
static void pwmAttach(uint8_t pin, uint8_t ch, uint32_t freq, uint8_t bits) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)ch;
  ledcAttach(pin, freq, bits);
#else
  ledcSetup(ch, freq, bits);
  ledcAttachPin(pin, ch);
#endif
}

static void pwmWrite(uint8_t pin, uint8_t ch, uint32_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)ch;
  ledcWrite(pin, duty);
#else
  (void)pin;
  ledcWrite(ch, duty);
#endif
}

// ---------------------------------------------------------------------------
//  Đèn nền
// ---------------------------------------------------------------------------
void backlightBegin(uint8_t pin) {
  blPin = pin;
  pwmAttach(blPin, LEDC_BL, 5000, 8);   // 5kHz: trên ngưỡng nghe được của cuộn cảm
  backlightSet(blPercent);
}

void backlightSet(uint8_t percent) {
  if (percent > 100) percent = 100;
  if (percent < 10)  percent = 10;
  blPercent = percent;
  if (blPin != 255) pwmWrite(blPin, LEDC_BL, (uint32_t)blPercent * 255 / 100);
}

uint8_t backlightGet() { return blPercent; }

// ---------------------------------------------------------------------------
//  Còi
// ---------------------------------------------------------------------------
void buzzerBegin(uint8_t pin) {
  buzzPin = pin;
  pinMode(buzzPin, OUTPUT);
  digitalWrite(buzzPin, LOW);
}

void buzzerEnable(bool on) {
  buzzOn = on;
  if (!on) buzzUntil = 0;
}

bool buzzerEnabled() { return buzzOn; }

void beep(uint16_t ms, uint16_t freq) {
  if (!buzzOn || buzzPin == 255) return;
  pwmAttach(buzzPin, LEDC_BUZZ, freq, 8);
  pwmWrite(buzzPin, LEDC_BUZZ, 128);    // 50% cho biên độ lớn nhất
  buzzUntil = millis() + ms;
}

void buzzerTick() {
  if (buzzUntil == 0 || millis() < buzzUntil) return;
  buzzUntil = 0;
  pwmWrite(buzzPin, LEDC_BUZZ, 0);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcDetach(buzzPin);
#else
  ledcDetachPin(buzzPin);
#endif
  pinMode(buzzPin, OUTPUT);
  digitalWrite(buzzPin, LOW);
}

// ---------------------------------------------------------------------------
//  SHT3x
// ---------------------------------------------------------------------------
static const uint8_t SHT3X_ADDR = 0x44;
static bool sht3xOk = false;

/// CRC8 của Sensirion (đa thức 0x31). Kiểm chứ không bỏ qua: bus này chạy
/// chung với cảm ứng và DS1307, nhiễu một bit là ra nhiệt độ vô lý mà không có
/// dấu hiệu nào khác — đúng kiểu sai âm thầm mà cả dự án đang cố tránh.
static uint8_t crc8(const uint8_t *d, uint8_t n) {
  uint8_t c = 0xFF;
  for (uint8_t i = 0; i < n; i++) {
    c ^= d[i];
    for (uint8_t b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
  }
  return c;
}

bool sht3xBegin() {
  Wire.beginTransmission(SHT3X_ADDR);
  sht3xOk = (Wire.endTransmission() == 0);
  return sht3xOk;
}

bool sht3xPresent() { return sht3xOk; }

bool sht3xRead(float &tempC, float &humidity) {
  if (!sht3xOk) return false;

  // 0x2400 = đo một lần, độ lặp lại cao, KHÔNG giữ nhịp bus (clock stretching).
  // Giữ nhịp bus sẽ ghim SCL ~15ms, mà bus này còn có cảm ứng và đồng hồ.
  Wire.beginTransmission(SHT3X_ADDR);
  Wire.write(0x24);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;

  delay(20);   // chuyển đổi mất ~15ms ở độ lặp lại cao
  if (Wire.requestFrom(SHT3X_ADDR, (uint8_t)6) != 6) return false;

  uint8_t b[6];
  for (uint8_t i = 0; i < 6; i++) b[i] = Wire.read();
  if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) return false;

  const uint16_t rt = (uint16_t)((b[0] << 8) | b[1]);
  const uint16_t rh = (uint16_t)((b[3] << 8) | b[4]);
  tempC    = -45.0f + 175.0f * (float)rt / 65535.0f;
  humidity = 100.0f * (float)rh / 65535.0f;
  return true;
}

// ---------------------------------------------------------------------------
//  DS1307
// ---------------------------------------------------------------------------
static const uint8_t DS1307_ADDR = 0x68;

static uint8_t bcd2dec(uint8_t b) { return (uint8_t)((b >> 4) * 10 + (b & 0x0F)); }
static uint8_t dec2bcd(uint8_t d) { return (uint8_t)(((d / 10) << 4) | (d % 10)); }

bool clockRead(Clock &out) {
  Wire.beginTransmission(DS1307_ADDR);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)DS1307_ADDR, (uint8_t)3) != 3) return false;

  const uint8_t s = Wire.read(), m = Wire.read(), h = Wire.read();
  if (s & 0x80) return false;          // bit CH: dao động đang dừng -> chưa đặt giờ bao giờ

  out.ss = bcd2dec(s & 0x7F);
  out.mm = bcd2dec(m & 0x7F);
  // Bit 6 = cờ 12h. Bo này không dùng, nhưng nếu ai đó nạp giờ bằng công cụ
  // khác thì phải đọc đúng, không thì 1 giờ chiều thành 1 giờ sáng.
  out.hh = (h & 0x40) ? (uint8_t)(bcd2dec(h & 0x1F) % 12 + ((h & 0x20) ? 12 : 0))
                      : bcd2dec(h & 0x3F);
  return out.hh < 24 && out.mm < 60 && out.ss < 60;
}

bool clockWrite(uint8_t hh, uint8_t mm, uint8_t ss) {
  Wire.beginTransmission(DS1307_ADDR);
  Wire.write((uint8_t)0x00);
  Wire.write(dec2bcd(ss) & 0x7F);      // CH = 0 -> dao động chạy
  Wire.write(dec2bcd(mm));
  Wire.write(dec2bcd(hh) & 0x3F);      // ép chế độ 24h
  return Wire.endTransmission() == 0;
}

void ntpBegin() {
  // SNTP của lwIP chạy trong tác vụ riêng của nó và tự cập nhật đồng hồ hệ
  // thống — ở đây chỉ khởi động rồi thoát, việc chờ là của ntpPoll().
  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
}

bool ntpPoll() {
  struct tm tm_now;
  // timeout 0: hỏi đồng hồ hệ thống rồi trả lời ngay, KHÔNG chờ trong này.
  if (!getLocalTime(&tm_now, 0)) return false;
  if (tm_now.tm_year < 120) return false;   // < năm 2020 -> SNTP chưa về, mới là giá trị mặc định
  return clockWrite((uint8_t)tm_now.tm_hour, (uint8_t)tm_now.tm_min,
                    (uint8_t)tm_now.tm_sec);
}

} // namespace BoardIo
