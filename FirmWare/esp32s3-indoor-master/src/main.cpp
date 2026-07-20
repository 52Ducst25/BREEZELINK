// ============================================================================
//  Aircon — ESP32-S3 · node MASTER / TRONG NHÀ (indoor) · firmware TEST
// ----------------------------------------------------------------------------
//  Đọc DHT11 rồi đẩy telemetry {t,h,ts,rssi,fw} lên topic
//  bl/{ORG_ID}/{DEVICE_UUID}/telemetry qua MQTTS (EMQX Serverless, TLS 8883).
//  Backend nhận -> vì node này là "indoor" nên app/web hiện là "Trong nhà".
//
//  Topic + client-id + payload khớp CHÍNH XÁC backend:
//    - topic:  src/app/utils/mqtt_naming.py  ->  bl/{org}/{uuid}/{kind}
//    - payload: telemetry_handler.py         ->  cần "t","h" ; "ts" nếu < ~1.7e9
//               thì backend tự đóng dấu giờ nhận (node không có RTC — OK).
//    - client-id: breezelink_{DEVICE_UUID}
//    - LWT: broker publish "offline" (retained) khi rớt -> status_handler.py.
//
//  IR blaster + relay ESP-NOW cho node slave: BƯỚC SAU (xem ../README.md).
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include "config.h"

// Broker EMQX tự host trên VPS chạy plaintext 1883 (MQTT_TLS=false trong
// docker-compose), nên dùng WiFiClient thường — KHÔNG phải WiFiClientSecure.
static WiFiClient   net;
static PubSubClient mqtt(net);
static DHT              dht(DHT_PIN, DHT_TYPE);

// Topic per-device, dựng 1 lần trong setup().
static String tTelemetry, tStatus, tCmd;
static void buildTopics() {
  String base = String("bl/") + ORG_ID + "/" + DEVICE_UUID + "/";
  tTelemetry = base + "telemetry";
  tStatus    = base + "status";
  tCmd       = base + "cmd";
}

// Bản test chưa thi hành lệnh AC — chỉ in ra để xác nhận đã nhận được cmd.
static void onMessage(char *topic, byte *payload, unsigned int len) {
  Serial.printf("[cmd] %.*s\n", (int)len, (const char *)payload);
}

static void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("WiFi -> \"%s\" ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf(" OK  IP=%s  RSSI=%d dBm\n", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
}

static void connectMqtt() {
  String cid = String("breezelink_") + DEVICE_UUID;  // = mqtt_naming.client_id()
  while (!mqtt.connected()) {
    Serial.print("MQTT ... ");
    // LWT (will): topic=status, qos=1, retain=true, payload="offline".
    if (mqtt.connect(cid.c_str(), MQTT_USERNAME, MQTT_PASSWORD,
                     tStatus.c_str(), 1, true, "offline")) {
      Serial.println("connected");
      mqtt.publish(tStatus.c_str(), "online", true);  // retained -> web thấy "Trực tuyến"
      mqtt.subscribe(tCmd.c_str(), 1);
    } else {
      // rc=-2 mạng/TLS lỗi; rc=4 sai user/pass; rc=5 chưa được cấp quyền trên broker.
      Serial.printf("that bai rc=%d (thu lai sau 2s)\n", mqtt.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n== Aircon TEST · ESP32-S3 · TRONG NHA (indoor/master) ==");
  dht.begin();
  buildTopics();
  connectWifi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  connectMqtt();
}

static unsigned long lastPub = 0;
void loop() {
  connectWifi();
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();

  unsigned long now = millis();
  if (lastPub != 0 && now - lastPub < TELEMETRY_MS) return;  // chưa tới nhịp

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    // DHT22 có chu kỳ lấy mẫu tối thiểu 2s; thử lại đúng 2s là sát ngưỡng nên
    // dễ hỏng liên tiếp. Chờ 3s để cảm biến có cơ hội hồi.
    // NaN kéo dài = lỗi phần cứng (dây lỏng/mất nguồn), KHÔNG gửi số bịa.
    Serial.println("Doc cam bien loi (NaN) — kiem tra day/nguon DHT");
    delay(3000);
    return;
  }
  lastPub = now;

  JsonDocument doc;
  doc["ts"]   = (uint32_t)(millis() / 1000);  // không RTC -> backend tự đóng dấu giờ nhận
  doc["t"]    = t;
  doc["h"]    = h;
  doc["rssi"] = (int)WiFi.RSSI();
  doc["fw"]   = FW_VERSION;
  char buf[192];
  size_t n = serializeJson(doc, buf);
  bool ok = mqtt.publish(tTelemetry.c_str(), (const uint8_t *)buf, n, false);
  Serial.printf("[telemetry] t=%.1f°C h=%.0f%% -> %s\n", t, h, ok ? "da gui" : "GUI LOI");
}
