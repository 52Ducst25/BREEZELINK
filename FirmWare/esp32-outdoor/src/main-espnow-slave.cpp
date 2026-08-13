// ============================================================================
//  BreezeLink — ESP32 · node NGOÀI TRỜI · vai trò SLAVE (ESP-NOW)
// ----------------------------------------------------------------------------
//  KHÔNG dùng WiFi/MQTT. Đọc DHT rồi bắn gói ESP-NOW về gateway trong nhà;
//  gateway mới là bên chuyển tiếp lên cloud theo topic riêng của node này.
//
//  Toàn bộ phần radio (quét kênh router, bám kênh, bắn quảng bá) nằm ở
//  ../../shared/espnow-slave-radio.h — dùng chung với 4 node góc phòng. Trước
//  đây nó nằm hẳn trong file này; tách ra vì bốn node phòng cần y hệt, và cái
//  bẫy "lạc kênh sau mỗi lần quét" đã trả giá một lần thì không nên có hai bản.
//
//  Bản dự phòng nối WiFi/MQTT thẳng vẫn còn ở main-wifi-direct.cpp:
//      pio run -e esp32-wifi -t upload
// ============================================================================
#include <Arduino.h>
#include <DHT.h>

#include "config.h"
#include "espnow-message.h"
#include "espnow-slave-radio.h"

static DHT dht(DHT_PIN, DHT_TYPE);

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n== BreezeLink · ESP32 · NGOAI TROI (SLAVE / ESP-NOW) ==");
  dht.begin();

  if (!EspNowSlaveRadio::begin(WIFI_SSID)) {
    Serial.println("esp_now_init THAT BAI — khoi dong lai");
    delay(2000);
    ESP.restart();
  }
}

static unsigned long lastSend = 0;

void loop() {
  EspNowSlaveRadio::tickRescan(WIFI_SSID);

  const unsigned long now = millis();
  if (lastSend != 0 && now - lastSend < TELEMETRY_MS) {
    delay(50);
    return;
  }
  lastSend = now;

  // Cảm biến hỏng KHÔNG có nghĩa là node chết. Vẫn gửi nhịp tim, chỉ để giá trị
  // là NaN — gateway sẽ hiểu là "còn sống nhưng chưa có số đo", giữ node ở trạng
  // thái online thay vì báo mất kết nối oan. Trước đây chỗ này return thẳng nên
  // DHT lỗi là node bị coi như đã chết.
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  const bool sensorOk = !(isnan(t) || isnan(h));
  if (!sensorOk) {
    Serial.println("Doc cam bien loi (NaN) — van gui nhip tim, kiem tra day DHT");
    t = NAN;
    h = NAN;
  }

  AcEspNowPacket pkt;
  acEspNowFill(&pkt, DEVICE_UUID, t, h, AC_NODE_OUTDOOR, AC_CORNER_NONE);
  const bool sent = EspNowSlaveRadio::broadcast(&pkt, sizeof(pkt));

  if (sensorOk) {
    Serial.printf("[espnow] t=%.1f h=%.0f kenh=%d -> %s\n",
                  t, h, EspNowSlaveRadio::channel(), sent ? "da phat" : "RADIO LOI");
  } else {
    Serial.printf("[espnow] nhip tim (chua co so do) kenh=%d -> %s\n",
                  EspNowSlaveRadio::channel(), sent ? "da phat" : "RADIO LOI");
  }
}
