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
#include "espnow-relay.h"
#include "slave-watch.h"

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

static String macToText(const uint8_t m[6]) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(buf);
}

// ---------------------------------------------------------------------------
//  CHUYỂN TIẾP: số đo của slave đi lên topic RIÊNG của slave, không phải của
//  master — backend nhận diện node theo uuid trong topic, nên số vào đúng hồ sơ.
//  Kèm "mac" để web hiện MAC thật, và "via" để biết gói đã đi qua master.
// ---------------------------------------------------------------------------
static void publishSlaveTelemetry(const char *uuid, const uint8_t mac[6],
                                  float t, float h) {
  JsonDocument doc;
  doc["ts"]   = (uint32_t)(millis() / 1000);
  doc["t"]    = t;
  doc["h"]    = h;
  doc["rssi"] = 0;                    // slave không nối WiFi nên không có RSSI
  doc["fw"]   = FW_VERSION;
  doc["mac"]  = macToText(mac);
  doc["via"]  = "espnow";
  char buf[224];
  size_t n = serializeJson(doc, buf);
  String topic = String("bl/") + ORG_ID + "/" + uuid + "/telemetry";
  bool ok = mqtt.publish(topic.c_str(), (const uint8_t *)buf, n, false);
  Serial.printf("[relay] %s t=%.1f h=%.0f -> %s\n", uuid, t, h, ok ? "da chuyen" : "LOI");
}

/// Master ĐỨNG TÊN slave báo trạng thái: slave không có kết nối MQTT nên broker
/// không thể sinh Last Will cho nó. Retained để web/app mở lên là thấy ngay.
static void publishSlaveStatus(const char *uuid, bool online) {
  String topic = String("bl/") + ORG_ID + "/" + uuid + "/status";
  mqtt.publish(topic.c_str(), online ? "online" : "offline", true);
  Serial.printf("[slave] %s -> %s\n", uuid, online ? "ONLINE" : "OFFLINE (mat nhip tim)");
}

static void onSlavePacket(const char *uuid, const uint8_t mac[6], float t, float h) {
  // MỌI gói đều tính là nhịp tim (5s/lần) -> phát hiện mất kết nối nhanh.
  // Kể cả gói KHÔNG có số đo (NaN, do cảm biến slave lỗi): node vẫn sống, chỉ
  // cảm biến hỏng — hai chuyện khác nhau, không được gộp thành "mất kết nối".
  SlaveWatch::heard(uuid, publishSlaveStatus);

  if (isnan(t) || isnan(h)) {
    Serial.printf("[slave] %s con song nhung cam bien loi (NaN)\n", uuid);
    return;   // còn sống -> giữ online, nhưng KHÔNG đẩy số rác lên cloud
  }
  // Chỉ đẩy số đo lên cloud mỗi 15s, khỏi phồng DB vô ích.
  if (SlaveWatch::dueForRelay(uuid)) publishSlaveTelemetry(uuid, mac, t, h);
  // Khẳng định lại "online" mỗi phút để tự sửa nếu một Last Will đến muộn đã
  // đè nhầm trạng thái slave thành offline.
  if (SlaveWatch::dueForStatusRefresh(uuid)) publishSlaveStatus(uuid, true);
}

static void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("WiFi -> \"%s\" ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }

  // BẮT BUỘC cho node master: tắt tiết kiệm điện WiFi.
  // ESP32 mặc định bật modem sleep khi đã vào mạng — radio ngủ giữa các beacon.
  // Lưu lượng WiFi thường không sao vì router ĐỆM HỘ trong lúc ngủ, nhưng gói
  // ESP-NOW từ slave thì KHÔNG ai đệm: đến đúng lúc radio ngủ là mất luôn, mà
  // broadcast lại không có ACK nên slave vẫn tưởng gửi thành công. Triệu chứng:
  // chạy tốt vài phút rồi master "điếc" hẳn dù MQTT vẫn bình thường.
  WiFi.setSleep(false);

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
  // Giữ keepalive mặc định 15s của PubSubClient -> broker kết luận master chết
  // sau ~22s. Ưu tiên ỔN ĐỊNH: hạ xuống 3-5s thì chỉ cần mạng chớp một nhịp là
  // broker cắt phiên rồi client nối lại, trạng thái lật online/offline liên tục.
  // 15s là giá trị mặc định đã được kiểm nghiệm rộng rãi — không đụng vào.
  mqtt.setKeepAlive(15);
  connectMqtt();

  // ESP-NOW khởi tạo SAU khi WiFi đã kết nối: nó dùng đúng kênh WiFi đang bám,
  // nên phải để WiFi chốt kênh trước thì slave (đang dò kênh router) mới gặp.
  if (EspNowRelay::begin()) {
    Serial.printf("ESP-NOW san sang · MAC master = %s · kenh %d\n",
                  WiFi.macAddress().c_str(), WiFi.channel());
  } else {
    Serial.println("ESP-NOW KHOI TAO LOI — se khong nhan duoc so lieu tu slave");
  }
}

static unsigned long lastPub = 0;
void loop() {
  connectWifi();
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();

  // Rút hàng đợi ESP-NOW: chuyển tiếp số đo + cập nhật nhịp tim của slave.
  EspNowRelay::poll(onSlavePacket);
  // Slave im quá lâu -> master đứng tên nó báo offline (broker không có LWT
  // cho slave vì slave không hề kết nối MQTT).
  SlaveWatch::checkTimeouts(publishSlaveStatus);

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
  // MAC của chính master: gửi được nghĩa là master ĐANG có WiFi + MQTT, và web
  // hiện được MAC thật thay vì "—" (ô "MAC node này" ở panel Nạp firmware).
  doc["mac"]  = WiFi.macAddress();
  doc["fw"]   = FW_VERSION;
  char buf[192];
  size_t n = serializeJson(doc, buf);
  bool ok = mqtt.publish(tTelemetry.c_str(), (const uint8_t *)buf, n, false);
  // Kèm bộ đếm ESP-NOW: nếu "nhan" đứng yên trong khi slave vẫn báo "da phat"
  // thì lỗi nằm ở đường thu của master (kênh lệch / radio ngủ), không phải slave.
  Serial.printf("[telemetry] t=%.1f°C h=%.0f%% -> %s · espnow nhan=%lu bo=%lu · kenh=%d\n",
                t, h, ok ? "da gui" : "GUI LOI",
                (unsigned long)EspNowRelay::receivedCount(),
                (unsigned long)EspNowRelay::droppedCount(),
                WiFi.channel());
}
