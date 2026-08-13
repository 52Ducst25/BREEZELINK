#include "board-io.h"
#include <Wire.h>
#include <time.h>       // configTzTime/getLocalTime cho ntpBegin()/ntpPoll()
#include "../config.h"

namespace BoardIo {

// LEDC kênh 4 và 6, KHÔNG phải 0/1: IRremoteESP8266 chiếm kênh thấp cho sóng
// mang 38kHz trên ESP32. Đè lên nhau thì đèn nền nhấp nháy đúng lúc bắn IR —
// một lỗi chỉ xuất hiện khi điều khiển máy lạnh, rất mất công tìm.
//
// VÌ SAO 6 CHỨ KHÔNG PHẢI 5: trên ESP32 mỗi TIMER dùng chung cho HAI kênh liền
// nhau — Arduino tính timer = (kênh / 2) % 4. Vậy kênh 4 và 5 CÙNG timer 2.
// Buzzer gọi ledcSetup(2700 Hz) là đặt lại timer đó, kéo tần số đèn nền từ
// 5 kHz tụt xuống 2.7 kHz — mỗi lần bấm nút là đèn nền đổi tần số. Kênh 6 nằm
// ở timer 3, tách hẳn. Vẫn cao hơn dải IR nên giữ nguyên ý đồ ban đầu.
//   kênh 4 -> timer 2      kênh 5 -> timer 2 (ĐỤNG)      kênh 6 -> timer 3
static const uint8_t LEDC_BL   = 4;
static const uint8_t LEDC_BUZZ = 6;

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

/// Ngắt tiếng NGAY LẬP TỨC. Tách ra vì có hai đường dẫn tới đây và trước đây
/// chỉ một đường làm đúng.
static void silence() {
  if (buzzPin == 255) return;
  pwmWrite(buzzPin, LEDC_BUZZ, 0);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcDetach(buzzPin);
  pinMode(buzzPin, OUTPUT);
  digitalWrite(buzzPin, LOW);
#endif
}

void buzzerEnable(bool on) {
  buzzOn = on;
  if (!on) {
    // PHẢI tắt PWM tại chỗ, không được chỉ xoá hẹn giờ.
    //
    // LỖI CŨ, đáng nhớ vì nó tự phá chính mình: hàm này chỉ đặt buzzUntil = 0.
    // Mà buzzerTick() mở đầu bằng `if (buzzUntil == 0) return;` — nên xoá hẹn
    // giờ chính là VÔ HIỆU HOÁ CÁI THỨ CÓ NHIỆM VỤ TẮT CÒI. Bấm ÂM BÁO -> TẮT
    // đúng lúc tiếng bíp xác nhận đang kêu, và còi kêu mãi không dừng.
    //
    // Thứ tự trong onSetting() làm lỗi này chắc chắn xảy ra chứ không phải hoạ
    // hoằn: beep() chạy TRƯỚC, buzzerEnable(false) chạy SAU — luôn luôn còn một
    // tiếng bíp đang dở khi lệnh tắt tới.
    buzzUntil = 0;
    silence();
  }
}

bool buzzerEnabled() { return buzzOn; }

void beep(uint16_t ms, uint16_t freq) {
  if (!buzzOn || buzzPin == 255) return;
  pwmAttach(buzzPin, LEDC_BUZZ, freq, 8);
  // 90/255 ≈ 35% chứ KHÔNG phải 128 (=50%, biên độ lớn nhất).
  //
  // Còi này gắn ngay sau tấm mặt panel treo tường, cách tai người bấm chừng
  // 30 cm — mức to nhất là chói. Với còi từ, biên độ đi theo độ rộng xung, nên
  // hạ từ 50% xuống 35% là bớt khoảng một phần ba độ to mà vẫn nghe rõ trong
  // phòng có điều hoà đang chạy.
  pwmWrite(buzzPin, LEDC_BUZZ, 90);
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
//  DS1307
// ---------------------------------------------------------------------------
static const uint8_t DS1307_ADDR = 0x68;

static uint8_t bcd2dec(uint8_t b) { return (uint8_t)((b >> 4) * 10 + (b & 0x0F)); }
static uint8_t dec2bcd(uint8_t d) { return (uint8_t)(((d / 10) << 4) | (d % 10)); }

/// Múi giờ Việt Nam, UTC+7, KHÔNG có giờ mùa hè.
///
/// Dấu NGƯỢC là đúng, không phải lỗi gõ: chuỗi TZ kiểu POSIX ghi số giờ phải
/// CỘNG vào giờ địa phương để ra UTC, nên UTC+7 viết là "ICT-7". Ghi "ICT+7" thì
/// đồng hồ lệch 14 tiếng — sai đủ nhiều để thấy ngay, nhưng cũng đủ giống một
/// giờ hợp lệ để không ai nghi ngờ chuỗi cấu hình.
///
/// Hằng số ở đây chứ không ở config.h: sản phẩm bán trong nước, mà đưa vào
/// config.h là thêm một ô nữa người lắp phải điền đúng — và điền sai thì hỏng
/// theo kiểu im lặng.
static const char *const TZ_INFO = "ICT-7";

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
  Wire.write(dec2bcd(ss) & 0x7F);      // bit CH = 0 -> dao động chạy
  Wire.write(dec2bcd(mm));
  Wire.write(dec2bcd(hh) & 0x3F);      // ép chế độ 24h (xoá bit 6)
  return Wire.endTransmission() == 0;
}

void ntpBegin() {
  // SNTP của lwIP chạy trong tác vụ riêng của nó và tự cập nhật đồng hồ hệ
  // thống — ở đây chỉ khởi động rồi thoát, việc chờ là của ntpPoll().
  //
  // Hai máy chủ chứ không một: pool.ntp.org phân giải qua DNS và có nhà mạng
  // chặn/chuyển hướng UDP 123, còn time.google.com thì anycast nên hầu như luôn
  // tới được. Mất một cái vẫn còn cái kia.
  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
}

bool ntpPoll() {
  struct tm tm_now;
  // timeout 0: hỏi đồng hồ hệ thống rồi trả lời NGAY. Chờ trong này là chặn tác
  // vụ UI, tức là màn hình đứng hình — đúng thứ kiến trúc hai lõi sinh ra để
  // tránh, và đứng hình vì một cái đồng hồ thì càng không đáng.
  if (!getLocalTime(&tm_now, 0)) return false;
  // < năm 2020 nghĩa là SNTP chưa trả lời, đây mới là giá trị mặc định của
  // đồng hồ hệ thống. Ghi nó xuống DS1307 là thay một giờ sai bằng một giờ sai
  // khác, lại còn xoá bit CH nên clockRead() sẽ khẳng định nó hợp lệ.
  if (tm_now.tm_year < 120) return false;
  return clockWrite((uint8_t)tm_now.tm_hour, (uint8_t)tm_now.tm_min,
                    (uint8_t)tm_now.tm_sec);
}

} // namespace BoardIo
