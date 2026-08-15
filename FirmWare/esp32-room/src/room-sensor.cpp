#include "room-sensor.h"

#include <DHTesp.h>

namespace RoomSensor {
namespace {

DHTesp        g_dht;
unsigned long g_lastReadMs = 0;
bool          g_started = false;

float   g_temp = NAN, g_humidity = NAN;
uint8_t g_fails = 0;

}  // namespace

void begin(uint8_t pin) {
  // DHTesp CHỨ KHÔNG PHẢI thư viện DHT của Adafruit — cùng lý do đã ghi trong
  // lib_deps của platformio.ini: bản Adafruit tắt ngắt hơn một giây khi
  // chưa cắm cảm biến và làm panic cả lõi. Trên ESP32-C3 (một lõi duy nhất, và
  // ngăn xếp BLE chạy ngay trên đó) hậu quả nặng hơn hẳn ESP32 hai lõi: mất
  // ngắt một giây là ngăn xếp BLE lỡ nhịp và kết nối/quét đứt đoạn.
  g_dht.setup(pin, DHTesp::DHT22);
  g_started = true;
  g_lastReadMs = 0;
}

bool poll() {
  if (!g_started) return false;
  if (g_lastReadMs != 0 && millis() - g_lastReadMs < READ_PERIOD_MS) return false;
  g_lastReadMs = millis();

  // Một lượt lấy cả hai số — KHÔNG gọi getTemperature() rồi getHumidity() thành
  // hai lệnh: mỗi lệnh là một lần bắt tay với cảm biến, mà nhịp tối thiểu của
  // DHT22 là 2s nên lệnh thứ hai chỉ trả lại số cũ.
  const TempAndHumidity th = g_dht.getTempAndHumidity();
  const bool ok = g_dht.getStatus() == DHTesp::ERROR_NONE &&
                  !isnan(th.temperature) && !isnan(th.humidity);

  if (ok) {
    g_temp = th.temperature;
    g_humidity = th.humidity;
    g_fails = 0;
    return true;
  }

  if (g_fails < 0xFF) g_fails++;
  if (g_fails >= FAIL_LIMIT) {
    // Xoá số cũ, có chủ đích. Giữ lại số đo của 20 giây trước và tiếp tục phát
    // nó ra là node nói dối một cách thuyết phục: gateway thấy một góc phòng
    // "còn sống, nhiệt độ ổn định" đúng lúc cảm biến đã tuột dây.
    g_temp = NAN;
    g_humidity = NAN;
  }
  return true;
}

bool read(float &tempC, float &humidity) {
  if (isnan(g_temp) || isnan(g_humidity)) return false;
  tempC = g_temp;
  humidity = g_humidity;
  return true;
}

uint8_t consecutiveFailures() { return g_fails; }

}  // namespace RoomSensor
