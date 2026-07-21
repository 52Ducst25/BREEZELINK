// ============================================================================
//  Aircon — ESP32 DevKit V1 · node TRONG NHÀ (indoor) + MASTER + IR blaster
// ----------------------------------------------------------------------------
//  Node này gộp cả 3 vai trò của một hộ vào một bo:
//    1. Đo DHT -> đẩy t_in/h_in lên cloud (đầu vào của thuật toán comfort)
//    2. Nhận lệnh từ cloud -> phát hồng ngoại điều khiển máy lạnh + học remote
//    3. Nhận ESP-NOW từ node outdoor -> chuyển tiếp lên MQTT hộ nó
//
//  Vì sao gộp: backend chỉ chấp nhận DUY NHẤT một node node_type=indoor cho mỗi
//  org (telemetry_service.get_device_by_org_and_node dùng scalar_one_or_none),
//  nên "node indoor" và "node indoor-master" không thể là hai hàng devices khác
//  nhau. Node này mang chính DEVICE_UUID của node ESP32-S3 cũ và thay thế nó.
//
//  4 topic đang dùng, khớp CHÍNH XÁC backend (src/app/utils/mqtt_naming.py):
//    telemetry  node -> cloud   {ts,t,h,rssi,mac,fw}      (telemetry_handler.py)
//    status     node -> cloud   "online"/"offline" retain (status_handler.py)
//    cmd        cloud -> node   lệnh IR HOẶC lệnh học     (command_publisher.py)
//    state      node -> cloud   {ack,mode,setpoint} retain (state_handler.py)
//    learn      node -> cloud   {raw_timing,mode/action,temp} (learn_handler.py)
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include "config.h"
#include "espnow-relay.h"
#include "slave-watch.h"
#include "ir-io.h"
#include "ir-store.h"

// Broker EMQX tự host trên VPS chạy plaintext 1883 (MQTT_TLS=false trong
// docker-compose), nên dùng WiFiClient thường — KHÔNG phải WiFiClientSecure.
static WiFiClient   net;
static PubSubClient mqtt(net);
static DHT          dht(DHT_PIN, DHT_TYPE);

/// PubSubClient mặc định chỉ có bộ đệm 256 byte và ÂM THẦM VỨT mọi gói lớn hơn.
/// Lệnh IR mang `ir_raw` vài trăm số -> vài KB JSON, và gói learn node gửi lên
/// cũng vậy. Không nới chỗ này thì mọi lệnh có ir_raw biến mất không dấu vết:
/// log không báo gì, máy lạnh không nhúc nhích. 12KB dư cho 600 mốc.
static const uint16_t MQTT_BUFFER_BYTES = 12288;

static String tTelemetry, tStatus, tCmd, tState, tLearn;
static void buildTopics() {
  String base = String("bl/") + ORG_ID + "/" + DEVICE_UUID + "/";
  tTelemetry = base + "telemetry";
  tStatus    = base + "status";
  tCmd       = base + "cmd";
  tState     = base + "state";
  tLearn     = base + "learn";
}

/// Bộ đệm khung IR dùng chung cho cả phát lẫn học. Một node chỉ làm một việc
/// tại một thời điểm nên không cần hai bộ đệm 1.2KB.
static uint16_t irBuf[IrIo::RAW_MAX];

// --- Nhãn đang học -----------------------------------------------------------
// "COOL 25" -> label="COOL", temp=25   |   "FAN_SPEED" -> label="FAN_SPEED", temp=-1
static char learnLabel[24] = "";
static int  learnTemp = -1;

// --- Lệnh chờ thi hành -------------------------------------------------------
// Callback của PubSubClient chạy NGAY GIỮA lúc thư viện đang đọc gói vào bộ đệm
// nội bộ của nó. Gọi mqtt.publish() ở đó là ghi đè lên chính bộ đệm đang đọc.
// Nên callback chỉ bóc gói ra rồi đặt hàng, còn loop() mới phát IR + gửi ack.
static struct {
  bool     hasFrame;      // có khung IR chờ phát
  uint16_t frameLen;
  bool     needAck;       // có ack chờ gửi (kể cả khi không phát được gì)
  char     reqId[24];
  char     mode[8];
  int      setpoint;
} pending;

static String macToText(const uint8_t m[6]) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(buf);
}

/// Chép chuỗi từ JSON ra bộ đệm riêng.
/// BẮT BUỘC chép chứ không giữ con trỏ: ArduinoJson bóc gói ở chế độ zero-copy
/// (chuỗi trỏ thẳng vào bộ đệm của PubSubClient), mà bộ đệm đó bị dùng lại ngay
/// ở gói kế tiếp — giữ con trỏ thì tới lúc loop() gửi ack, req_id đã thành rác.
static void copyStr(char *dst, size_t dstSize, const char *src) {
  if (src == nullptr) { dst[0] = '\0'; return; }
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

// ---------------------------------------------------------------------------
//  ESP-NOW: chuyển tiếp số đo của node outdoor
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
  // MỌI gói đều tính là nhịp tim -> phát hiện mất kết nối nhanh. Kể cả gói
  // KHÔNG có số đo (NaN, do cảm biến slave lỗi): node vẫn sống, chỉ cảm biến
  // hỏng — hai chuyện khác nhau, không được gộp thành "mất kết nối".
  SlaveWatch::heard(uuid, publishSlaveStatus);

  if (isnan(t) || isnan(h)) {
    Serial.printf("[slave] %s con song nhung cam bien loi (NaN)\n", uuid);
    return;
  }
  if (SlaveWatch::dueForRelay(uuid)) publishSlaveTelemetry(uuid, mac, t, h);
  if (SlaveWatch::dueForStatusRefresh(uuid)) publishSlaveStatus(uuid, true);
}

// ---------------------------------------------------------------------------
//  HỌC remote
// ---------------------------------------------------------------------------
static bool isAcMode(const char *s) {
  return strcmp(s, "COOL") == 0 || strcmp(s, "DRY") == 0 ||
         strcmp(s, "FAN")  == 0 || strcmp(s, "OFF") == 0;
}

static void startLearn(const char *label) {
  const char *space = strchr(label, ' ');
  size_t nameLen = space ? (size_t)(space - label) : strlen(label);
  if (nameLen >= sizeof(learnLabel)) nameLen = sizeof(learnLabel) - 1;
  memcpy(learnLabel, label, nameLen);
  learnLabel[nameLen] = '\0';
  learnTemp = space ? atoi(space + 1) : -1;

  IrIo::learnStart(LEARN_TIMEOUT_MS);
  Serial.printf("[learn] \"%s\" — huong remote vao mat thu roi bam nut (toi da %lus)\n",
                label, (unsigned long)(LEARN_TIMEOUT_MS / 1000));
}

static void publishLearned(const uint16_t *raw, uint16_t len) {
  JsonDocument doc;
  JsonArray arr = doc["raw_timing"].to<JsonArray>();
  for (uint16_t i = 0; i < len; i++) arr.add(raw[i]);

  // learn_handler.py đọc nhãn ở "action" HOẶC "mode", và định tuyến theo đó:
  // nút rời (FAN_SPEED, SLEEP, SWING_V...) vào bảng ir_action_codes, còn
  // COOL/DRY/FAN/OFF vào ma trận (mode, temp) của ir_codes. Gửi sai khoá là
  // học xong nhưng mã nằm nhầm bảng, thuật toán comfort không bao giờ thấy.
  //
  // Phân biệt bằng danh sách 4 mode thay vì chép cả danh sách nút rời
  // (ir_action_service.KNOWN_ACTIONS): backend thêm nút mới thì node không phải
  // nạp lại firmware, còn 4 mode thì cố định theo AcMode.
  if (isAcMode(learnLabel)) {
    doc["mode"] = learnLabel;
    // DRY/FAN/OFF không có nhiệt độ -> BỎ HẲN khoá "temp". Backend coi thiếu
    // temp là mã cố định (upsert_learned_code); gửi -1 sẽ lưu thành nhiệt độ rác.
    if (learnTemp >= 0) doc["temp"] = learnTemp;
  } else {
    doc["action"] = learnLabel;
  }

  String out;
  serializeJson(doc, out);
  bool ok = mqtt.publish(tLearn.c_str(), out.c_str(), false);
  Serial.printf("[learn] \"%s\" %u moc (%u byte) -> %s\n",
                learnLabel, len, (unsigned)out.length(),
                ok ? "da gui len cloud" : "GUI LOI (payload vuot bo dem MQTT?)");
}

// ---------------------------------------------------------------------------
//  Nhận lệnh
// ---------------------------------------------------------------------------
static char lastReqId[24] = "";

static void takeCommand(JsonDocument &doc) {
  copyStr(pending.reqId, sizeof(pending.reqId), doc["req_id"]);
  copyStr(pending.mode,  sizeof(pending.mode),  doc["mode"]);
  pending.setpoint = doc["setpoint"] | -1;
  pending.hasFrame = false;
  pending.frameLen = 0;
  pending.needAck  = false;

  const char *reason = doc["reason"] | "";

  // MQTT QoS1 cho phép broker gửi LẠI cùng một lệnh nếu ack chưa kịp về. Phát
  // lại khung IR = bấm remote hai lần; với các nút xoay vòng (tốc độ quạt, đảo
  // gió) lần hai sẽ nhảy sang nấc khác, tức là lặp lại KHÔNG vô hại. Chặn theo
  // req_id, nhưng vẫn ack lại vì rất có thể chính cái ack cũ đã rơi.
  if (pending.reqId[0] && strcmp(pending.reqId, lastReqId) == 0) {
    Serial.printf("[cmd] %s da thi hanh roi — bo qua ban lap, ack lai\n", pending.reqId);
    pending.needAck = true;
    return;
  }

  const char *codeId = doc["ir_code_id"];   // JSON null -> nullptr
  JsonArray irRaw = doc["ir_raw"];

  if (!irRaw.isNull()) {
    if (irRaw.size() > IrIo::RAW_MAX) {
      // Cắt bớt rồi phát thì máy lạnh nhận một lệnh KHÁC hẳn, không phải lệnh
      // thiếu. Thà không làm gì và để log nói rõ.
      Serial.printf("[cmd] ir_raw %u moc > gioi han %u — KHONG phat (khung cut la lenh sai)\n",
                    (unsigned)irRaw.size(), IrIo::RAW_MAX);
      return;
    }
    for (JsonVariant v : irRaw) irBuf[pending.frameLen++] = (uint16_t)v.as<uint32_t>();

    // Chỉ mã theo (mode,temp) mới có ir_code_id để cache. Nút rời không có id,
    // backend luôn gửi kèm ir_raw nên chúng không cần lưu.
    if (codeId != nullptr && pending.frameLen > 0) {
      if (IrStore::save(codeId, irBuf, pending.frameLen)) {
        Serial.printf("[cmd] da luu ma %s vao NVS (%u moc)\n", codeId, pending.frameLen);
      }
    }
  } else if (codeId != nullptr) {
    pending.frameLen = IrStore::load(codeId, irBuf, IrIo::RAW_MAX);
    if (pending.frameLen == 0) {
      // Backend tưởng node còn giữ mã này nên cố tình KHÔNG gửi kèm ir_raw
      // (command_publisher._resolve_ir_raw + redis_ir_cache). Node vừa bị
      // erase_flash / đổi bo thì hai bên lệch nhau, và không có kênh nào để xin
      // lại. CỐ Ý không ack: lệnh chưa thi hành thì không được báo là xong, để
      // commands.acked_at trên web phản ánh đúng sự thật.
      Serial.printf("[cmd] ir_code_id=%s khong co trong NVS ma server khong gui kem ir_raw\n", codeId);
      Serial.println("      -> xoa redis ir cache cua org de server gui lai (xem README §6)");
      return;
    }
  } else {
    // Chưa học mã cho (mode, setpoint) này — command_publisher đã ghi warning
    // "No learned IR code" ở phía server rồi.
    Serial.printf("[cmd] %s %s %d: khong co ir_raw lan ir_code_id — chua hoc ma nay\n",
                  pending.reqId, pending.mode, pending.setpoint);
    return;
  }

  Serial.printf("[cmd] %s -> %s %d (%s) · %u moc, cho phat\n",
                pending.reqId, pending.mode, pending.setpoint, reason, pending.frameLen);
  pending.hasFrame = true;
  pending.needAck  = true;
}

static void onMessage(char *topic, byte *payload, unsigned int len) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) {
    Serial.printf("[cmd] JSON hong (%s) — bo qua\n", err.c_str());
    return;
  }

  // Cùng một topic cmd chở HAI khuôn payload khác hẳn nhau:
  //   {"learn":"COOL 25"}                  -> vào chế độ học (ir_service.py)
  //   {"req_id","mode","setpoint",...}     -> phát một khung (command_publisher.py)
  const char *learn = doc["learn"];
  if (learn != nullptr) {
    startLearn(learn);
    return;
  }
  takeCommand(doc);
}

/// Ack + đồng bộ trạng thái. retain=true: state_handler khớp ack với hàng
/// commands, còn web/app mở lên là thấy ngay mode/setpoint cuối cùng mà không
/// phải chờ tới lệnh kế tiếp.
static void publishState() {
  JsonDocument doc;
  if (pending.reqId[0]) doc["ack"] = pending.reqId;
  if (pending.mode[0])  doc["mode"] = pending.mode;
  if (pending.setpoint >= 0) doc["setpoint"] = pending.setpoint;
  char buf[128];
  size_t n = serializeJson(doc, buf);
  bool ok = mqtt.publish(tState.c_str(), (const uint8_t *)buf, n, true);
  Serial.printf("[state] ack=%s mode=%s setpoint=%d -> %s\n",
                pending.reqId, pending.mode, pending.setpoint, ok ? "da gui" : "GUI LOI");
}

// ---------------------------------------------------------------------------
//  Kết nối
// ---------------------------------------------------------------------------
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
      // QoS1: lệnh điều khiển máy lạnh không được phép rơi âm thầm.
      mqtt.subscribe(tCmd.c_str(), 1);
    } else {
      // rc=-2 mạng lỗi; rc=4 sai user/pass; rc=5 chưa được cấp quyền trên broker.
      Serial.printf("that bai rc=%d (thu lai sau 2s)\n", mqtt.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  // Tên bo lấy theo đích biên dịch, KHÔNG viết cứng: cùng mã nguồn này build
  // cho hai bo, mà log ghi sai bo là thứ đánh lừa đúng lúc đang tìm lỗi.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  const char *board = "ESP32-S3";
#elif defined(CONFIG_IDF_TARGET_ESP32)
  const char *board = "ESP32 DevKit V1";
#else
  const char *board = "ESP32 (khong ro bien the)";
#endif
  Serial.printf("\n== Aircon · %s · TRONG NHA (indoor + master + IR) ==\n", board);

  dht.begin();
  IrIo::begin(IR_TX_PIN, IR_RX_PIN);
  if (!IrStore::begin()) {
    // Không chặn khởi động: node vẫn chạy được, chỉ là mọi lệnh phải kèm ir_raw.
    Serial.println("NVS loi — se khong cache duoc ma IR, moi lenh deu phai co ir_raw");
  }
  buildTopics();

  connectWifi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  if (!mqtt.setBufferSize(MQTT_BUFFER_BYTES)) {
    Serial.println("Khong cap phat duoc bo dem MQTT — lenh co ir_raw se bi bo am tham!");
  }
  // Giữ keepalive mặc định 15s của PubSubClient -> broker kết luận node chết
  // sau ~22s. Ưu tiên ỔN ĐỊNH: hạ xuống 3-5s thì chỉ cần mạng chớp một nhịp là
  // broker cắt phiên rồi client nối lại, trạng thái lật online/offline liên tục.
  mqtt.setKeepAlive(15);
  connectMqtt();

  // ESP-NOW khởi tạo SAU khi WiFi đã kết nối: nó dùng đúng kênh WiFi đang bám,
  // nên phải để WiFi chốt kênh trước thì slave (đang dò kênh router) mới gặp.
  if (EspNowRelay::begin()) {
    Serial.printf("ESP-NOW san sang · MAC master = %s · kenh %d\n",
                  WiFi.macAddress().c_str(), WiFi.channel());
  } else {
    Serial.println("ESP-NOW KHOI TAO LOI — se khong nhan duoc so lieu tu node ngoai troi");
  }
  Serial.printf("IR: phat GPIO%d · thu GPIO%d\n", IR_TX_PIN, IR_RX_PIN);
}

static unsigned long lastPub = 0;

void loop() {
  connectWifi();
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();

  // Thi hành lệnh Ở ĐÂY chứ không trong callback: xem ghi chú ở struct pending.
  if (pending.hasFrame) {
    pending.hasFrame = false;
    IrIo::blast(irBuf, pending.frameLen);
    strncpy(lastReqId, pending.reqId, sizeof(lastReqId) - 1);
    lastReqId[sizeof(lastReqId) - 1] = '\0';
    Serial.printf("[ir] da phat %u moc ra may lanh\n", pending.frameLen);
  }
  if (pending.needAck) {
    pending.needAck = false;
    publishState();
  }

  // Đang học thì chờ người dùng bấm remote.
  if (IrIo::learning()) {
    uint16_t n = IrIo::learnPoll(irBuf, IrIo::RAW_MAX);
    if (n > 0) publishLearned(irBuf, n);
  }
  if (IrIo::learnTimedOut()) {
    Serial.printf("[learn] het gio cho \"%s\" — khong bat duoc tin hieu nao. "
                  "Kiem tra: remote co pin? co huong dung mat thu? cach < 1m?\n", learnLabel);
  }

  // Rút hàng đợi ESP-NOW: chuyển tiếp số đo + cập nhật nhịp tim của node outdoor.
  EspNowRelay::poll(onSlavePacket);
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
  doc["mac"]  = WiFi.macAddress();
  doc["fw"]   = FW_VERSION;
  char buf[192];
  size_t n = serializeJson(doc, buf);
  bool ok = mqtt.publish(tTelemetry.c_str(), (const uint8_t *)buf, n, false);
  Serial.printf("[telemetry] t=%.1f°C h=%.0f%% -> %s · espnow nhan=%lu bo=%lu · kenh=%d\n",
                t, h, ok ? "da gui" : "GUI LOI",
                (unsigned long)EspNowRelay::receivedCount(),
                (unsigned long)EspNowRelay::droppedCount(),
                WiFi.channel());
}
