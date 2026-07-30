// ============================================================================
//  BreezeLink — ESP32 · node NGOÀI TRỜI (outdoor) · bản DỰ PHÒNG (WiFi + MQTT thẳng)
// ----------------------------------------------------------------------------
//  Đọc DHT rồi đẩy telemetry {t,h,ts,rssi,fw} lên topic
//  bl/{ORG_ID}/{DEVICE_UUID}/telemetry qua MQTT plaintext 1883 (EMQX tự host).
//  Node này là "outdoor" -> app/web hiện là "Ngoài trời".
//
//  Giống node trong nhà về hợp đồng backend (topic/payload/client-id/LWT).
//
//  CHẠY THẬT thì dùng bản ESP-NOW (main-espnow-slave.cpp): node ngoài trời gửi
//  về node trong nhà, không tự lên mạng. Bản này giữ làm ĐƯỜNG LÙI khi ESP-NOW
//  trục trặc — nó không phụ thuộc node trong nhà nên tách được lỗi rất nhanh:
//  nếu bản này lên số mà bản ESP-NOW không, lỗi nằm ở khâu ESP-NOW chứ không
//  phải cảm biến hay backend.
//      pio run -e esp32-wifi -t upload
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include "config.h"

// Broker chạy plaintext 1883 -> WiFiClient thường (không TLS), nhẹ RAM.
static WiFiClient   net;
static PubSubClient mqtt(net);
static DHT          dht(DHT_PIN, DHT_TYPE);

static String tTelemetry, tStatus, tCmd;
static void buildTopics() {
  String base = String("bl/") + ORG_ID + "/" + DEVICE_UUID + "/";
  tTelemetry = base + "telemetry";
  tStatus    = base + "status";
  tCmd       = base + "cmd";
}

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
    if (mqtt.connect(cid.c_str(), MQTT_USERNAME, MQTT_PASSWORD,
                     tStatus.c_str(), 1, true, "offline")) {
      Serial.println("connected");
      mqtt.publish(tStatus.c_str(), "online", true);
      mqtt.subscribe(tCmd.c_str(), 1);
    } else {
      Serial.printf("that bai rc=%d (thu lai sau 2s)\n", mqtt.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n== BreezeLink · ESP32 · NGOAI TROI (WiFi + MQTT thang) ==");
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
  if (lastPub != 0 && now - lastPub < TELEMETRY_MS) return;

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    // Chu kỳ lấy mẫu tối thiểu của DHT22 là 2s — chờ 3s để cảm biến hồi.
    Serial.println("Doc cam bien loi (NaN) — kiem tra day/nguon DHT");
    delay(3000);
    return;
  }
  lastPub = now;

  JsonDocument doc;
  doc["ts"]   = (uint32_t)(millis() / 1000);
  doc["t"]    = t;
  doc["h"]    = h;
  doc["rssi"] = (int)WiFi.RSSI();
  doc["fw"]   = FW_VERSION;
  char buf[192];
  size_t n = serializeJson(doc, buf);
  bool ok = mqtt.publish(tTelemetry.c_str(), (const uint8_t *)buf, n, false);
  Serial.printf("[telemetry] t=%.1f h=%.0f -> %s\n", t, h, ok ? "da gui" : "GUI LOI");
}
