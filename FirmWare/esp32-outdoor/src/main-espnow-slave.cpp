// ============================================================================
//  BreezeLink — ESP32-C3 · node NGOÀI TRỜI · vai trò SLAVE (ESP-NOW)
// ----------------------------------------------------------------------------
//  KHÔNG dùng WiFi/MQTT. Đọc DHT rồi bắn gói ESP-NOW về gateway trong nhà;
//  gateway mới là bên chuyển tiếp lên cloud theo topic riêng của node này.
//
//  Toàn bộ phần radio (quét kênh router, bám kênh, bắn quảng bá) nằm ở
//  ../../shared/espnow-slave-radio.h — dùng chung với 4 node góc phòng. Trước
//  đây nó nằm hẳn trong file này; tách ra vì bốn node phòng cần y hệt, và cái
//  bẫy "lạc kênh sau mỗi lần quét" đã trả giá một lần thì không nên có hai bản.
//
//  DHTesp CHỨ KHÔNG PHẢI thư viện DHT của Adafruit — bắt buộc kể từ khi bo đổi
//  sang ESP32-C3 (15/08/2026). Bản Adafruit tắt ngắt hơn một giây khi chưa cắm
//  cảm biến, mà C3 chỉ có MỘT lõi và ngăn xếp WiFi/ESP-NOW chạy ngay trên đó.
//  Lý do đầy đủ ở platformio.ini, khối lib_deps.
// ============================================================================
#include <Arduino.h>
#include <DHTesp.h>

#include "config.h"
#include "espnow-message.h"
#include "espnow-slave-radio.h"

static DHTesp dht;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n== BreezeLink · ESP32-C3 · NGOAI TROI (SLAVE / ESP-NOW) ==");
  dht.setup(DHT_PIN, DHT_TYPE);

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
  // DHTesp trả cả hai số trong MỘT lần đọc, khác Adafruit (hai lời gọi, hai lần
  // đánh thức cảm biến). Vẫn kiểm `getStatus()` chứ không chỉ isnan: thư viện
  // này báo lỗi checksum/timeout qua đó, và một khung hỏng checksum có thể ra
  // số nằm trong dải hợp lệ.
  const TempAndHumidity th = dht.getTempAndHumidity();
  float t = th.temperature;
  float h = th.humidity;
  const bool sensorOk =
      dht.getStatus() == DHTesp::ERROR_NONE && !(isnan(t) || isnan(h));
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
