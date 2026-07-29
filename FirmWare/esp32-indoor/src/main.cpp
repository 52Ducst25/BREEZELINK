// ============================================================================
//  Aircon — QR Box Advance Touch · node TRONG NHÀ (indoor) + MASTER + IR blaster
// ----------------------------------------------------------------------------
//  Node này gộp cả 4 vai trò của một hộ vào một bo:
//    1. Đo nhiệt/ẩm -> đẩy t_in/h_in lên cloud (đầu vào của thuật toán comfort).
//       DHT22 trên GPIO17 đọc ngay tại loop() này; nếu bo có thêm SHT3x trên I2C
//       thì tác vụ UI lấy hộ (bus I2C thuộc về nó) và số của SHT3x được ưu tiên.
//    2. Nhận lệnh từ cloud -> phát hồng ngoại điều khiển máy lạnh + học remote
//    3. Nhận ESP-NOW từ node outdoor -> chuyển tiếp lên MQTT hộ nó
//    4. Hiển thị + cho điều khiển tại chỗ trên màn cảm ứng 2.8" (tác vụ lõi 0)
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
#include <DHTesp.h>
#include "config.h"
#include "espnow-relay.h"
#include "slave-watch.h"
#include "ir-io.h"
#include "ir-store.h"
// Bo QR Box Advance Touch Screen: màn 2.8" chạy trên TÁC VỤ RIÊNG Ở LÕI 0.
// Thiết kế giao diện + lý do phải tách lõi: ../../Interface/README.md và ui.h.
// loop() dưới đây không vẽ một pixel nào — nó chỉ đổ số liệu sang và rút lệnh về.
#include "ui/ui.h"
#include "ui/board-io.h"   // chỉ để hỏi sht3xPresent() — xem readDht() bên dưới

// --- Cảm biến nhiệt/ẩm trong nhà --------------------------------------------
// DHT22 trên GPIO17 (pad chân TX của module A7680C không hàn).
//
// ĐỌC Ở LÕI 1 (trong loop()), không ở tác vụ giao diện: giải mã DHT22 phải tắt
// ngắt ~5ms để tự bắt nhịp bit, mà tác vụ giao diện nằm ở lõi 0 — đúng lõi ngăn
// xếp WiFi chạy. loop() ở lõi 1 chỉ có nó và IR nên vừa an toàn cho WiFi vừa ít
// bị xen vào giữa, tỉ lệ đọc hỏng thấp hơn.
//
// DÙNG DHTesp CHỨ KHÔNG PHẢI thư viện DHT của Adafruit — lý do dài, xem lib_deps
// trong platformio.ini. Tóm tắt: bản Adafruit tắt ngắt hơn 1 giây khi chưa cắm
// cảm biến và làm panic lõi 1.
static DHTesp dht;
static unsigned long lastDht = 0;

/// GPIO34..39 là chân CHỈ VÀO: không có mạch lái ngõ ra, không có trở kéo trong
/// chip. DHT22 là giao thức một dây HAI CHIỀU — MCU phải tự kéo dây xuống thấp
/// ~1ms mở đầu mỗi lượt đọc rồi mới nhả ra nghe — nên đặt DHT_PIN vào dải này
/// thì cảm biến không bao giờ trả lời.
///
/// Phải kiểm ở đây vì trình biên dịch KHÔNG bắt được: pinMode(36, OUTPUT) là mã
/// hợp lệ và trên ESP32 nó lặng lẽ không làm gì. Không có dòng cảnh báo này thì
/// triệu chứng duy nhất là NaN vĩnh viễn — nhìn y hệt đứt dây hoặc chết cảm
/// biến, và người ta sẽ đi thay cảm biến trước khi nghĩ tới sơ đồ chân.
static const bool DHT_PIN_OK = !(DHT_PIN >= 34 && DHT_PIN <= 39);

/// Nhịp đọc DHT22 (ms). Datasheet ghi TỐI THIỂU 2s giữa hai lần đọc — dưới mức
/// đó chip trả lại số của lần trước chứ không đo mới. 2.5s cho dư biên.
static const unsigned long DHT_PERIOD_MS = 2500;
static void readDht();

// Broker EMQX tự host trên VPS chạy plaintext 1883 (MQTT_TLS=false trong
// docker-compose), nên dùng WiFiClient thường — KHÔNG phải WiFiClientSecure.
static WiFiClient   net;
static PubSubClient mqtt(net);

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

// --- Dấu vết quyền điều khiển ------------------------------------------------
// Ai đang cầm lái: máy chủ (mặc định) hay người vừa bấm trên màn cảm ứng.
// `overrideLocal` chỉ có ý nghĩa HIỂN THỊ — nó KHÔNG dừng vòng lặp comfort của
// máy chủ, vì node không có kênh nào để xin đặt ghi đè (Interface/README.md
// §8.3). Giao diện nói đúng chuyện đó thay vì hứa hão.
static uint32_t lastCmdMs = 0;
static bool     overrideLocal = false;

// Bảng "chế độ nào đã có mã IR trong NVS", dùng để làm mờ nút chưa học trên màn.
// PHẢI cache: tra thẳng NVS trong pushUiModel() là 18 khoá MỖI VÒNG loop(), và
// mỗi khoá TRƯỢT lại khiến thư viện Preferences in một dòng ERROR — trên bo chưa
// học mã nào thì log serial bị nhấn chìm hoàn toàn, đúng lúc cần log nhất để tìm
// lỗi lắp đặt. Bảng chỉ đổi khi học được mã mới, nên tính lại đúng lúc đó.
static bool     aliasDirty = true;   // true = phải quét lại NVS ở lần đẩy kế tiếp

// Số đo ngoài trời và trạng thái máy lạnh gần nhất — CHỈ để hiển thị. Giữ riêng
// chứ không đọc ké `pending`: pending.mode được điền ngay khi bóc gói, kể cả
// những lệnh sau đó bị bỏ vì chưa học mã, nên lấy nó ra hiển thị là màn hình
// khoe một trạng thái máy lạnh chưa bao giờ xảy ra.
static float    lastSlaveT = NAN, lastSlaveH = NAN;
static uint32_t lastSlaveMs = 0;
static char     actMode[8] = "";
static int      actSetpoint = -1;

/// Nhiệt độ có tham gia vào khoá tra mã IR không.
/// Chỉ COOL mới có ma trận (mode, temp); DRY/FAN/OFF là mã cố định — đúng theo
/// ir_service._REQUIRED_* ("COOL 24..28 + DRY + FAN + OFF").
static int aliasTemp(const char *mode, int setpoint) {
  return (strcmp(mode, "COOL") == 0) ? setpoint : -1;
}

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
  lastSlaveT = t; lastSlaveH = h; lastSlaveMs = millis();
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
        // Ghi thêm bí danh (mode, temp) -> id. Đây là thứ DUY NHẤT cho phép màn
        // cảm ứng tự tra ra khung IR khi người dùng bấm tại chỗ: kho chính khoá
        // theo UUID của server, node không có bảng tra ngược. Xem ir-store.h.
        if (pending.mode[0]) {
          IrStore::saveAlias(pending.mode, aliasTemp(pending.mode, pending.setpoint), codeId);
          aliasDirty = true;   // bảng "đã có mã" đổi -> màn phải tính lại (xem pushUiModel)
        }
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
  // Máy chủ vừa ra lệnh -> nó đã giành lại quyền, huy hiệu trên màn trở về
  // "TU DONG". Đây chính là điều giao diện đã cảnh báo lúc người dùng ghi đè.
  lastCmdMs = millis();
  overrideLocal = false;
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
// Quét và in ra những mạng NHÌN THẤY được khi không vào được mạng đã cấu hình.
//
// Vòng lặp cũ chỉ in dấu chấm mãi mãi. Dấu chấm không phân biệt được ba nguyên
// nhân hoàn toàn khác nhau, mà cách xử lý thì khác hẳn nhau:
//   - không thấy SSID  -> sai tên, hoặc router phát 5 GHz (ESP32 chỉ bắt
//                         2.4 GHz — đây là ca hay gặp nhất khi lắp tại nhà dân)
//   - thấy nhưng yếu   -> đặt node sai chỗ
//   - thấy và mạnh     -> sai mật khẩu
// Người đi lắp đứng trước tủ điện cần biết NGAY là nên đổi tên mạng, dời node,
// hay gõ lại mật khẩu.
// Không đưa vào config.h: đây là hằng số của firmware, không phải thứ đổi theo
// từng nơi lắp. 20 s đủ cho router chậm bắt tay xong, mà vẫn không bắt người
// đứng đợi quá lâu mới thấy được lý do.
constexpr uint32_t WIFI_ATTEMPT_MS = 20000UL;

static void wifiDiagnose() {
  Serial.printf("\n  Khong vao duoc \"%s\". Quet xem xung quanh co gi:\n", WIFI_SSID);
  // Dừng hẳn lần kết nối đang dở: quét trong lúc đang bắt tay cho kết quả thiếu.
  WiFi.disconnect(true);
  delay(100);

  const int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("  (khong thay mang 2.4 GHz nao — kiem tra anten hoac cho dat node)");
    return;
  }
  bool found = false;
  for (int i = 0; i < n; i++) {
    const bool me = (WiFi.SSID(i) == String(WIFI_SSID));
    found = found || me;
    Serial.printf("  %-22s kenh %2d  %4d dBm  %-11s%s\n",
                  WiFi.SSID(i).c_str(), WiFi.channel(i), (int)WiFi.RSSI(i),
                  WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "mo" : "co mat khau",
                  me ? "  <== DANG TIM MANG NAY" : "");
  }
  if (!found) {
    Serial.printf("  => KHONG CO \"%s\" trong danh sach. ESP32 chi bat duoc 2.4 GHz;\n"
                  "     neu router tach bang 5 GHz ra ten rieng thi phai dien ten\n"
                  "     cua bang 2.4 GHz vao WIFI_SSID trong src/config.h.\n", WIFI_SSID);
  } else {
    Serial.println("  => Thay mang, van khong vao: gan nhu chac chan SAI MAT KHAU.");
  }
  WiFi.scanDelete();
}

static void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);

  // Vẫn chặn tới khi vào được mạng — không có mạng thì node chẳng làm được gì —
  // nhưng cứ 20 giây lại nói ra MỘT LÝ DO thay vì rải dấu chấm im lặng.
  for (uint8_t attempt = 1;; attempt++) {
    Serial.printf("WiFi -> \"%s\" (lan %u) ", WIFI_SSID, attempt);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    const uint32_t deadline = millis() + WIFI_ATTEMPT_MS;
    while (WiFi.status() != WL_CONNECTED && (int32_t)(millis() - deadline) < 0) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) break;
    wifiDiagnose();
  }

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
  Serial.println("\n== Aircon · QR Box Advance Touch · TRONG NHA (indoor + master + IR) ==");

  // Dựng màn TRƯỚC WiFi, có chủ đích: tác vụ giao diện chạy ở lõi 0 nên nó vẫn
  // vẽ bình thường suốt lúc connectWifi()/connectMqtt() đang chặn lõi 1 hàng
  // chục giây. Người đi lắp nhìn thấy "MAT KET NOI" nhấp nháy — tức là node
  // sống và đang dò mạng — thay vì một màn đen không nói gì.
  //
  // Ui::begin() cũng là nơi dò SHT3x trên I2C và kéo EN_LEVEL_SHIFT lên HIGH —
  // mà IR phát nay đi qua bộ dịch mức đó, nên nó phải chạy TRƯỚC IrIo::begin().
  Ui::begin();
  dht.setup(DHT_PIN, DHTesp::DHT22);
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
  // Tự suy ra đường đi thay vì ghi cứng: cặp UART_1 (GPIO2/15) ra P3 qua TXS0104
  // nên phụ thuộc EN_LEVEL_SHIFT, còn lại nối 3.3V thẳng. Bản trước ghi cứng
  // "qua TXS0104" từ hồi IR phát còn ở GPIO15, và dòng log đó tiếp tục khẳng
  // định sai sau khi chân đã dời — đúng kiểu log tự tin mà dối, tốn thời gian
  // của người đọc hơn là không in gì.
  auto irPath = [](int pin) {
    return (pin == 2 || pin == 15) ? "P3 qua TXS0104, can EN_LEVEL_SHIFT" : "3.3V thang";
  };
  Serial.printf("IR: phat GPIO%d (%s) · thu GPIO%d (%s)\n",
                IR_TX_PIN, irPath(IR_TX_PIN), IR_RX_PIN, irPath(IR_RX_PIN));
  if (BoardIo::sht3xPresent()) {
    Serial.println("Cam bien nhiet/am: SHT3x @0x44 (I2C) — DHT22 bi bo qua");
  } else if (!DHT_PIN_OK) {
    Serial.printf(
        "\n!!! SAI SO DO CHAN: DHT22 dang dat o GPIO%d, ma GPIO34..39 la chan CHI VAO.\n"
        "    DHT22 can MCU keo day xuong thap ~1ms de mo dau moi luot doc, chan chi vao\n"
        "    khong lam duoc -> se KHONG BAO GIO co so do (chi ra NaN, khong bao loi).\n"
        "    Sua: doi DHT_PIN trong platformio.ini sang mot chan lai duoc ngo ra.\n"
        "    Da TAT han viec doc DHT de khoi ton thoi gian.\n\n", DHT_PIN);
  } else {
    Serial.printf("Cam bien nhiet/am: DHT22 @GPIO%d (khong thay SHT3x @0x44)\n", DHT_PIN);
  }
}

static unsigned long lastPub = 0;

// --- Cầu nối sang tác vụ giao diện ------------------------------------------
// Hai chiều, cả hai đều không chặn:
//   UI -> loop(): Ui::pollCommand()  (người dùng vừa bấm GUI / TU DONG)
//   loop() -> UI: Ui::publish()      (ảnh chụp trạng thái để vẽ)
// Việc THI HÀNH lệnh nằm ở đây chứ không ở tác vụ UI, vì đúng hai lý do đã ghi
// trong ui.h: PubSubClient không an toàn đa luồng, và IR bit-bang 38kHz phải
// chạy ở lõi 1 để không bị bộ lập lịch xen giữa.

/// Người dùng bấm GUI trên màn: tra mã theo bí danh rồi bắn ngay tại chỗ.
static void runPanelCommand(const Ui::Command &c) {
  if (c.kind == Ui::Command::AUTO) {
    overrideLocal = false;
    Serial.println("[panel] tra quyen ve cho may chu");
    return;
  }

  // Ghi nhận quyền điều khiển TRƯỚC khi thử bắn mã, không phải sau.
  //
  // Trước đây dòng này nằm ở cuối hàm, sau nhánh thoát sớm "chưa có mã". Hậu
  // quả: node chưa học mã nào thì bấm THỦ CÔNG không bao giờ đổi được trạng
  // thái — nút TỰ ĐỘNG sáng vĩnh viễn và người dùng kết luận là hỏng.
  //
  // Tách hai chuyện vốn khác nhau ra:
  //   AI ĐANG CẦM LÁI   -> đổi ngay khi người dùng bấm; đây là Ý ĐỊNH của họ và
  //                        nó có thật bất kể mã IR có hay không.
  //   CÓ BẮN ĐƯỢC KHÔNG -> báo riêng bằng toast ở dưới.
  // Gộp hai thứ này làm một là lý do một nút vừa-là-hành-động vừa-là-đèn-báo trở
  // nên không bấm được.
  //
  // Vẫn TRUNG THỰC: màn hình nói "GHI ĐÈ" kèm "máy chủ sẽ giành lại quyền ở chu
  // kỳ sau" — cả hai đều đúng ngay cả khi mã IR chưa học.
  overrideLocal = true;

  const uint16_t n = IrStore::loadAlias(c.mode, aliasTemp(c.mode, c.setpoint),
                                        irBuf, IrIo::RAW_MAX);
  if (n == 0) {
    Serial.printf("[panel] %s %d: chua co ma trong NVS\n", c.mode, c.setpoint);
    Ui::reply("CHƯA HỌC MÃ — vào app để học");
    return;
  }

  IrIo::blast(irBuf, n);
  copyStr(actMode, sizeof(actMode), c.mode);
  actSetpoint = c.setpoint;
  Serial.printf("[panel] da phat %u moc -> %s %d\n", n, c.mode, c.setpoint);

  // Publish state KHÔNG kèm ack: không có req_id nào để khớp, nhưng
  // state_handler vẫn soi mode/setpoint vào redis_state_service nên app và web
  // thấy ngay trạng thái mới. Đây là tất cả những gì node làm được — đặt ghi đè
  // thật cần một topic mà backend chưa có (Interface/README.md §8.3).
  pending.reqId[0] = '\0';
  copyStr(pending.mode, sizeof(pending.mode), c.mode);
  pending.setpoint = c.setpoint;
  publishState();

  overrideLocal = true;
  Ui::reply("ĐÃ GỬI — máy chủ sẽ giành lại quyền");
}

/// Dựng ảnh chụp cho màn. Bitmask mã IR tính ở đây vì NVS thuộc quyền lõi 1.
static void pushUiModel() {
  Ui::Model m;
  m.wifiUp = (WiFi.status() == WL_CONNECTED);
  m.mqttUp = mqtt.connected();
  m.rssi   = m.wifiUp ? (int)WiFi.RSSI() : 0;
  m.channel = (uint8_t)WiFi.channel();
  strncpy(m.ip,   m.wifiUp ? WiFi.localIP().toString().c_str() : "", sizeof(m.ip) - 1);
  strncpy(m.ssid, WIFI_SSID, sizeof(m.ssid) - 1);
  strncpy(m.mac,  WiFi.macAddress().c_str(), sizeof(m.mac) - 1);

  Ui::readIndoor(m.tIn, m.hIn);     // để nguyên NAN nếu chưa có số đo hợp lệ

  m.tOut = lastSlaveT; m.hOut = lastSlaveH;
  // Cùng ngưỡng với SlaveWatch để màn hình và topic status không bao giờ nói
  // hai chuyện khác nhau về cùng một node.
  m.outOnline = lastSlaveMs && (millis() - lastSlaveMs < SlaveWatch::SLAVE_TIMEOUT_MS);
  m.outAgeSec = lastSlaveMs ? (millis() - lastSlaveMs) / 1000 : 0;
  m.espnowRx   = EspNowRelay::receivedCount();
  m.espnowDrop = EspNowRelay::droppedCount();

  copyStr(m.mode, sizeof(m.mode), actMode);
  m.setpoint      = actSetpoint;
  m.overrideLocal = overrideLocal;
  m.lastCmdSec    = lastCmdMs ? (millis() - lastCmdMs) / 1000 : 0;

  // Quét NVS đúng một lần rồi giữ lại — xem ghi chú ở `aliasDirty`.
  static uint16_t coolMask = 0;
  static bool     hasDry = false, hasFan = false, hasOff = false;
  if (aliasDirty) {
    coolMask = 0;
    for (uint8_t i = 0; i < 15; i++) {
      if (IrStore::hasAlias("COOL", 16 + i)) coolMask |= (uint16_t)(1u << i);
    }
    hasDry = IrStore::hasAlias("DRY", -1);
    hasFan = IrStore::hasAlias("FAN", -1);
    hasOff = IrStore::hasAlias("OFF", -1);
    aliasDirty = false;
  }
  m.coolMask = coolMask;
  m.hasDry   = hasDry;
  m.hasFan   = hasFan;
  m.hasOff   = hasOff;

  m.learning = IrIo::learning();
  copyStr(m.learnLabel, sizeof(m.learnLabel), learnLabel);
  m.learnRemainSec = IrIo::learnRemainingMs() / 1000;

  m.irCodeCount = IrStore::count();
  m.uptimeSec   = millis() / 1000;
  m.fw          = FW_VERSION;
  Ui::publish(m);
}

/// Đọc DHT22 theo nhịp rồi đặt số vào kho chung của giao diện.
///
/// NHƯỜNG SHT3x KHI CÓ MẶT: nếu cả hai cùng lắp mà cả hai cùng ghi, số trên màn
/// nhảy qua nhảy lại giữa hai con lệch nhau vài phần mười độ — nhìn như cảm biến
/// hỏng. SHT3x được ưu tiên vì mỗi lần đọc có CRC kiểm chứng.
static void readDht() {
  if (!DHT_PIN_OK) return;          // đã cảnh báo ở setup(), đừng phí công đọc
  if (millis() - lastDht < DHT_PERIOD_MS) return;
  lastDht = millis();
  if (BoardIo::sht3xPresent()) return;

  // Một lượt đọc lấy cả hai số — KHÔNG gọi getTemperature() rồi getHumidity()
  // như hai lệnh riêng: mỗi lệnh là một lần bắt tay 5ms với cảm biến, mà nhịp
  // tối thiểu của DHT22 là 2s nên lệnh thứ hai chỉ trả lại số cũ.
  const TempAndHumidity th = dht.getTempAndHumidity();

  // Sai checksum hoặc hết giờ chờ là chuyện thường với DHT22 (vài phần trăm số
  // lần đọc). Bỏ lượt này, 2.5s nữa đọc lại — ghi NaN vào kho chung sẽ làm màn
  // chớp "—" rồi lại hiện số, trông như cảm biến sắp hỏng.
  if (dht.getStatus() != DHTesp::ERROR_NONE) return;
  if (isnan(th.temperature) || isnan(th.humidity)) return;
  Ui::setIndoor(th.temperature, th.humidity);
}

void loop() {
  connectWifi();
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();
  readDht();

  // Rút lệnh người dùng bấm MỖI VÒNG (nút phải phản hồi ngay), nhưng ảnh chụp
  // trạng thái chỉ đẩy sang giao diện theo nhịp 200ms — đúng nhịp vẽ lại của màn
  // (Interface/README.md §7.4), mắt không phân biệt nhanh hơn. Đẩy mỗi vòng chỉ
  // tốn công vô ích: mỗi lần đẩy kéo theo cả loạt WiFi.RSSI()/millis()/memcpy.
  Ui::Command panelCmd;
  while (Ui::pollCommand(panelCmd)) runPanelCommand(panelCmd);

  static unsigned long lastUiPush = 0;
  if (millis() - lastUiPush >= 200) {
    lastUiPush = millis();
    pushUiModel();
  }

  // Thi hành lệnh Ở ĐÂY chứ không trong callback: xem ghi chú ở struct pending.
  if (pending.hasFrame) {
    pending.hasFrame = false;
    IrIo::blast(irBuf, pending.frameLen);
    strncpy(lastReqId, pending.reqId, sizeof(lastReqId) - 1);
    lastReqId[sizeof(lastReqId) - 1] = '\0';
    copyStr(actMode, sizeof(actMode), pending.mode);   // trạng thái THẬT đã bắn ra máy lạnh
    actSetpoint = pending.setpoint;
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

  // Đóng dấu nhịp NGAY, TRƯỚC khi biết có số đo hay không — chứ không chỉ khi
  // gửi thành công. Đặt sau lần đọc hỏng thì `lastPub` đứng yên ở 0, điều kiện
  // trên không bao giờ chặn, và cả khối này chạy MỖI VÒNG loop(): chưa cắm cảm
  // biến là log phun ra vài nghìn dòng mỗi phút (đo được 304 KB trong 40 s),
  // đủ để chôn mọi dòng log khác và ăn hẳn một phần lõi 1.
  lastPub = now;

  // Lấy số ĐÃ ĐO SẴN — không đọc cảm biến tại đây. readDht() ở đầu loop() lo
  // nhịp 2.5s của DHT22, còn SHT3x thì tác vụ UI đo. Chờ một lượt đo ở đây sẽ
  // chặn cả mqtt.loop() lẫn việc rút hàng đợi ESP-NOW.
  float t, h;
  if (!Ui::readIndoor(t, h)) {
    // Hai nguyên nhân khác hẳn nhau -> hai lời nhắc khác hẳn nhau. Nhắc "kiểm
    // tra trở kéo" khi thật ra chân không lái được ngõ ra là đẩy người đọc log
    // đi sai hướng — đúng cái mà dòng cảnh báo ở setup() sinh ra để chặn.
    if (!DHT_PIN_OK)
      Serial.printf("Chua co so do nhiet/am — DHT22 dang o GPIO%d (chan CHI VAO), "
                    "xem canh bao luc khoi dong\n", DHT_PIN);
    else
      Serial.printf("Chua co so do nhiet/am — kiem tra DHT22 tren GPIO%d "
                    "(co tro keo 4.7k len 3.3V chua?) hoac day I2C cua SHT3x tren J1\n",
                    DHT_PIN);
    return;
  }

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
