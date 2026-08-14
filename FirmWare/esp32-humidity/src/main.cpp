// ============================================================================
//  BreezeLink — NODE ĐỘ ẨM
//  Tự bật máy xông tinh dầu khi phòng khô, tắt khi đủ ẩm. Không cloud.
// ----------------------------------------------------------------------------
//  Bo: ESP32 DevKit V1 (WROOM-32 / D0WD-V3, cầu USB CP210x).
//  Đọc ../README.md trước khi hàn — có sơ đồ dây và bảng nháy LED.
//
//  VÌ SAO KHÔNG NỐI CLOUD:
//  Việc của bo này là một vòng kín hoàn toàn cục bộ — đo phòng, quyết định,
//  bắn IR. Không có bước nào cần tới server. Nối MQTT vào thì phải cấp cho nó
//  một hàng `devices`, một token, một loại thiết bị mới ở backend, và đổi lại
//  được đúng một thứ: nó sẽ NGỪNG hoạt động khi rớt mạng. Đó là cái giá sai
//  cho một thiết bị mà toàn bộ giá trị nằm ở chỗ nó tự chạy.
//
//  Muốn thêm cloud sau này thì chỗ để móc vào là DiffuserControl::status() —
//  nó đã trả về đủ mọi thứ cần báo cáo.
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <esp_bt.h>

#include "diffuser-control.h"
#include "diffuser-ir.h"
#include "humidity-sensor.h"
#include "ir-remote.h"
#include "ir-slots.h"
#include "learn-session.h"
#include "manual-button.h"
#include "serial-console.h"
#include "settings.h"

namespace {

/// In một dòng tóm tắt định kỳ (ms). Bo chạy hàng tháng không ai ngồi xem, nên
/// log phải THƯA — nhưng không được im hẳn, vì "im" và "treo" nhìn giống nhau.
const uint32_t HEARTBEAT_MS = 60000;
uint32_t g_lastBeatMs = 0;

/// Nhịp nháy LED theo tình huống (ms). 0 = không nháy.
const uint32_t BLINK_LEARN_MS = 100;
const uint32_t BLINK_FAULT_MS = 500;

uint32_t g_lastBlinkMs = 0;
bool     g_ledOn = false;

/// LED là thứ DUY NHẤT nhìn được khi không cắm serial — bảng ý nghĩa ở
/// ../README.md §5. Giữ đúng bốn trạng thái: thêm nữa là không ai phân biệt nổi.
void updateLed(const DiffuserControl::Status &st) {
  uint32_t period = 0;

  if (LearnSession::active()) {
    period = BLINK_LEARN_MS;
  } else if (st.reason == DiffuserControl::Reason::SENSOR_LOST ||
             st.reason == DiffuserControl::Reason::NO_CODE) {
    period = BLINK_FAULT_MS;
  } else {
    // Không nháy: sáng khi máy đang chạy, tắt khi không.
    if (g_ledOn != st.on) {
      g_ledOn = st.on;
      digitalWrite(LED_PIN, g_ledOn ? HIGH : LOW);
    }
    return;
  }

  const uint32_t now = millis();
  if (now - g_lastBlinkMs < period) return;
  g_lastBlinkMs = now;
  g_ledOn = !g_ledOn;
  digitalWrite(LED_PIN, g_ledOn ? HIGH : LOW);
}

void heartbeat() {
  const uint32_t now = millis();

  // `watch` chỉ rút NHỊP IN, không đụng gì tới điều khiển. 5 giây = đúng nhịp
  // đọc cảm biến; in dày hơn cũng chỉ lặp lại đúng con số cũ.
  const uint32_t period = SerialConsole::watching() ? SAMPLE_MS : HEARTBEAT_MS;
  if (g_lastBeatMs != 0 && now - g_lastBeatMs < period) return;
  g_lastBeatMs = now;

  const DiffuserControl::Status st = DiffuserControl::status(now);
  float rh = NAN;
  const bool have = HumiditySensor::humidity(rh);

  if (have) {
    Serial.printf("[%4lus] %.1f%%RH  %.1fC | MAY: %-4s | %-7s | %s\n",
                  (unsigned long)(now / 1000), (double)rh,
                  (double)HumiditySensor::temperature(), st.on ? "BAT" : "TAT",
                  st.overriding ? "GHI DE" : "tu dong",
                  DiffuserControl::reasonText(st.reason));
  } else {
    Serial.printf("[%4lus] KHONG CO SO DO | MAY: %-4s | %s\n", (unsigned long)(now / 1000),
                  st.on ? "BAT" : "TAT", DiffuserControl::reasonText(st.reason));
  }
}

void shutDownRadios() {
  // TẮT HẲN WiFi VÀ BLUETOOTH. Bo này không dùng cả hai, và tắt đi được ba thứ:
  //
  //  1. Bớt ~80mA và bớt toả nhiệt. Đây mới là lý do chính: DHT22 nằm cách con
  //     ESP32 vài centimet, mà module tự sinh nhiệt thì đẩy nhiệt độ đo lên và
  //     KÉO ĐỘ ẨM TƯƠNG ĐỐI XUỐNG. Sai lệch đó không ngẫu nhiên — nó lệch một
  //     chiều, nên trung bình hay làm mượt bao nhiêu cũng không gỡ ra được, và
  //     hậu quả là máy xông chạy nhiều hơn cần thiết mà không có triệu chứng.
  //  2. Không tranh sóng 2.4GHz với gateway và bốn node góc phòng ở gần đó.
  //  3. Không có ngăn xếp mạng nào chen ngang giữa lúc đọc DHT22 (giao thức một
  //     dây, nhạy với trễ ngắt) hay lúc bắn IR (đo bằng micro-giây).
  WiFi.mode(WIFI_OFF);
  btStop();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);  // cho cầu CP210x kịp mở cổng, không thì mất mấy dòng đầu

  Serial.println("\n\n=== BreezeLink | NODE DO AM ===");
  Serial.printf("bo: ESP32 DevKit V1 | DHT22@GPIO%d | IR phat@GPIO%d thu@GPIO%d | nut@GPIO%d\n",
                DHT_PIN, IR_TX_PIN, IR_RX_PIN, BUTTON_PIN);
#if DIFFUSER_IR_TOGGLE
  Serial.println("remote: MOT NUT BAP BENH (DIFFUSER_IR_TOGGLE=1)");
#else
  Serial.println("remote: HAI NUT ROI NHAU (DIFFUSER_IR_TOGGLE=0)");
#endif

  shutDownRadios();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  IrSlots::begin();
  IrRemote::begin(IR_TX_PIN, IR_RX_PIN);
  HumiditySensor::begin(DHT_PIN);
  ManualButton::begin(BUTTON_PIN);

  // Truyền DiffuserIr::send làm Emitter: bộ điều khiển quyết định, module kia
  // bắn. Nhờ tách vậy mà thử được phần quyết định bằng một emitter giả mà
  // không cần cắm LED nào.
  DiffuserControl::begin(DiffuserIr::send);

  SerialConsole::printHelp();
  SerialConsole::printStatus();
}

void loop() {
  SerialConsole::poll();

  switch (ManualButton::poll()) {
    case ManualButton::Event::SHORT_PRESS:
      Serial.println("[nut] nhan nhanh -> bat/tat tay");
      DiffuserControl::manualToggle(millis());
      SerialConsole::printStatus();
      break;
    case ManualButton::Event::LONG_PRESS:
      Serial.println("[nut] nhan giu -> vao che do HOC MA (tha tay ra duoc roi)");
      LearnSession::start(LearnSession::suggestNext());
      break;
    case ManualButton::Event::NONE:
      break;
  }

  if (LearnSession::active()) {
    // TẠM DỪNG VÒNG QUYẾT ĐỊNH trong lúc học. Không dừng thì bo có thể bắn IR
    // đúng lúc mắt thu đang mở và tự học lại khung của chính mình.
    if (LearnSession::poll()) SerialConsole::printStatus();
  } else {
    HumiditySensor::poll();

    float      rh = NAN;
    const bool have = HumiditySensor::humidity(rh);

    // "Có số đo" KHÔNG chỉ là có giá trị — số đó còn phải chưa quá cũ. Hai điều
    // kiện tách nhau vì chúng hỏng theo hai kiểu: cảm biến trượt vài lần thì
    // giá trị bị xoá (have = false), còn cảm biến tuột dây hẳn giữa chừng thì
    // giá trị cuối vẫn nằm đó cho tới khi đủ FAIL_LIMIT. Ngưỡng thời gian bắt
    // được cả hai.
    const bool fresh = HumiditySensor::secSinceGoodRead() <= SENSOR_STALE_SEC;

    DiffuserControl::update(rh, have && fresh, millis());
  }

  updateLed(DiffuserControl::status(millis()));
  heartbeat();
}
