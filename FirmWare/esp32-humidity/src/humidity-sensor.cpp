#include "humidity-sensor.h"

#include <DHTesp.h>
#include <math.h>

#include "settings.h"

namespace HumiditySensor {
namespace {

DHTesp g_dht;
bool   g_started = false;

uint32_t g_lastPollMs = 0;
uint32_t g_lastGoodMs = 0;
bool     g_everGood = false;

float   g_rawH = NAN, g_rawT = NAN;
float   g_ema = NAN;
uint8_t g_fails = 0;

const char *g_failReason = "";

/// Khoảng %RH coi là hợp lệ — PHỤ THUỘC LOẠI CẢM BIẾN, và đó là cả mục đích.
///
/// DHT11 theo datasheet chỉ đo được 20..90 %RH. Nên cận dưới 20 không phải là
/// hạn chế, nó là một PHÉP THỬ: một con DHT22 bị giải mã theo khung DHT11 sẽ
/// cho ra ~3 %RH (78,2 -> byte 0x03 đọc thành phần nguyên). Số đó nằm gọn
/// trong 0..100 nên bộ lọc cũ cho lọt, và bo sẽ tin phòng khô cong rồi cho máy
/// xông chạy mãi — hỏng CÂM, không log, không triệu chứng.
///
/// Siết xuống 20 thì đúng ca đó rơi vào nhánh "số vô lý" và hét lên. Không mất
/// gì: phòng ở có máy lạnh tại VN không xuống dưới 30 %RH, mà nếu có thì DHT11
/// cũng không đo nổi.
///
/// Cận TRÊN vẫn để 100 chứ không phải 90: mùa mưa chạm 90+ là chuyện thật, và
/// từ chối số đo thật thì tệ hơn là nhận một số hơi kém chính xác.
#if DHT_SENSOR_IS_DHT11
const float RH_MIN = 20.0f;
#else
const float RH_MIN = 0.0f;
#endif
const float RH_MAX = 100.0f;

/// Nhắc lý do trượt thưa thôi (ms) — nhịp đọc 5 giây mà in mỗi lần thì log
/// trôi mất mọi thứ khác. Nhưng KHÔNG im hẳn: im là mất luôn manh mối.
const uint32_t FAIL_LOG_EVERY_MS = 20000;
uint32_t g_lastFailLogMs = 0;

void logFail(const char *why) {
  g_failReason = why;
  const uint32_t now = millis();
  if (g_lastFailLogMs != 0 && now - g_lastFailLogMs < FAIL_LOG_EVERY_MS) return;
  g_lastFailLogMs = now;
  Serial.printf("[dht] doc truot: %s\n", why);
}

/// Số giây đã trôi qua kể từ [sinceMs]. Trừ số học CÓ DẤU để vẫn đúng khi
/// millis() tràn ở ~49 ngày — cùng khuôn với IrIo::expired() của panel. Bo này
/// treo tường chạy liên tục nên 49 ngày là mốc sẽ tới, không phải giả thuyết.
uint32_t elapsedSec(uint32_t sinceMs) {
  const int32_t d = (int32_t)(millis() - sinceMs);
  return d > 0 ? (uint32_t)d / 1000u : 0u;
}

}  // namespace

void begin(uint8_t pin) {
  // DHTesp CHỨ KHÔNG PHẢI thư viện DHT của Adafruit — lý do đầy đủ ở
  // ../platformio.ini phần lib_deps. Tóm tắt: bản Adafruit tắt ngắt hơn một
  // giây khi chưa cắm cảm biến, và trên đúng loại ESP32 hai lõi này việc đó đã
  // gây panic lõi 1.
#if DHT_SENSOR_IS_DHT11
  g_dht.setup(pin, DHTesp::DHT11);
  Serial.println("[dht] cau hinh: DHT11 (so nguyen, sai so +-5%RH, dai 20..90)");
#else
  g_dht.setup(pin, DHTesp::DHT22);
  Serial.println("[dht] cau hinh: DHT22 / AM2302");
#endif
  g_started = true;
  g_lastPollMs = 0;
}

bool poll() {
  if (!g_started) return false;
  if (g_lastPollMs != 0 && millis() - g_lastPollMs < SAMPLE_MS) return false;
  g_lastPollMs = millis();

  // MỘT LƯỢT LẤY CẢ HAI SỐ. Không gọi getTemperature() rồi getHumidity() thành
  // hai lệnh: mỗi lệnh là một lần bắt tay với cảm biến, mà nhịp tối thiểu của
  // DHT22 là 2 giây nên lệnh thứ hai chỉ trả lại đúng số cũ.
  const TempAndHumidity th = g_dht.getTempAndHumidity();
  const auto st = g_dht.getStatus();

  bool ok = false;
  if (st == DHTesp::ERROR_TIMEOUT) {
    logFail("cam bien KHONG TRA LOI - kiem day DATA, nguon, dung chan GPIO4?");
  } else if (st == DHTesp::ERROR_CHECKSUM) {
    logFail("SAI CHECKSUM - nhieu tren duong DATA, thieu tro keo, hay day qua dai");
  } else if (isnan(th.temperature) || isnan(th.humidity)) {
    logFail("thu vien tra ve NaN");
  } else if (th.humidity < RH_MIN || th.humidity > RH_MAX) {
    // Ngoài dải thì KHÔNG phải cảm biến hỏng — khung đọc được và checksum
    // ĐÚNG, chỉ là giải mã sai khuôn. Gần như luôn là khai nhầm loại.
    logFail("SO NGOAI DAI -> gan nhu chac chan KHAI SAI LOAI CAM BIEN. "
            "Sua DHT_SENSOR_IS_DHT11 trong settings.h roi nap lai");
    Serial.printf("      doc duoc %.1f %%RH / %.1f C, dai hop le %.0f..%.0f %%RH\n",
                  th.humidity, th.temperature, (double)RH_MIN, (double)RH_MAX);
#if DHT_SENSOR_IS_DHT11
    Serial.println("      Vua thay cam bien phai khong? DHT22 doc theo khung DHT11 "
                   "cho ra ~3%RH.\n"
                   "      Dat DHT_SENSOR_IS_DHT11 ve 0.");
#endif
  } else {
    ok = true;
  }

  if (ok) {
    g_failReason = "";
    g_rawH = th.humidity;
    g_rawT = th.temperature;

    // Mẫu đầu tiên nạp thẳng vào EMA thay vì để nó bò lên từ 0: khởi động xong
    // mà phải chờ mấy chục giây cho EMA đuổi kịp thì bo sẽ thấy "phòng cực khô"
    // và bật máy ngay, dù phòng vốn đang ẩm.
    g_ema = isnan(g_ema) ? th.humidity
                         : (EMA_ALPHA * th.humidity + (1.0f - EMA_ALPHA) * g_ema);

    g_lastGoodMs = millis();
    g_everGood = true;
    g_fails = 0;
    return true;
  }

  if (g_fails < 0xFF) g_fails++;
  if (g_fails >= FAIL_LIMIT) {
    // XOÁ SỐ CŨ, CÓ CHỦ ĐÍCH. Xem chú thích quy ước ở humidity-sensor.h.
    g_rawH = NAN;
    g_rawT = NAN;
    g_ema = NAN;
  }
  return true;
}

bool humidity(float &rhOut) {
  if (isnan(g_ema)) return false;
  rhOut = g_ema;
  return true;
}

float rawHumidity() { return g_rawH; }
float temperature() { return g_rawT; }

uint32_t secSinceGoodRead() {
  if (!g_everGood) return UINT32_MAX;
  return elapsedSec(g_lastGoodMs);
}

uint8_t consecutiveFailures() { return g_fails; }

const char *lastFailReason() { return g_failReason; }

}  // namespace HumiditySensor
