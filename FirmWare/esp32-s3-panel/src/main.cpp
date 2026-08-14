// ============================================================================
//  BreezeLink — QR Box Advance Touch · GATEWAY TRONG NHÀ + IR blaster
// ----------------------------------------------------------------------------
//  Bo này KHÔNG CÒN ĐO NHIỆT ĐỘ. Bốn node ESP32-C3 đặt ở bốn góc phòng làm việc
//  đó (FirmWare/esp32-room/), còn bo này làm năm việc của một cầu nối:
//    1. Nhận ESP-NOW từ 4 node góc phòng + node ngoài trời -> trung chuyển lên
//       MQTT hộ TỪNG node (mỗi node một topic riêng, theo uuid nó tự khai)
//    2. Nhận lệnh từ cloud -> phát hồng ngoại điều khiển máy lạnh + học remote
//    3. Hiển thị + cho điều khiển tại chỗ trên màn cảm ứng 2.8" (tác vụ lõi 0)
//    4. Đẩy ảnh chụp sang Arduino UNO Q qua Bluetooth (GATT, vai peripheral)
//    5. Nhận đề xuất/lệnh ngược từ UNO Q — và chỉ thi hành khi đó là LỆNH
//
//  VÌ SAO BỎ DHT22 KHỎI BO NÀY: một cảm biến treo trên tường không nói được
//  nhiệt độ của phòng, nó nói nhiệt độ của CÁI TƯỜNG ĐÓ. Bốn góc chênh nhau
//  3-4°C là chuyện thường (nắng cửa sổ, miệng gió, sau tủ). Nay số "trong nhà"
//  là TRUNG VỊ của các góc còn tươi — xem room-registry.h cho lý do chọn trung
//  vị thay vì trung bình cộng.
//
//  HỆ QUẢ QUAN TRỌNG: bo này không publish `telemetry` nữa. Nó không có số đo
//  nào của riêng mình, và gửi số mượn của node khác dưới tên mình là bịa. MAC
//  của nó đi kèm gói `state` (state_handler.py đọc) để trang nạp firmware vẫn
//  hiện được.
//
//  BA RADIO TRÊN MỘT ĂNG-TEN: WiFi/MQTT + ESP-NOW + BLE cùng dùng một khối
//  2.4GHz. Bộ đồng tồn tại của IDF chia thời gian giúp. BLE ở đây NHẸ hơn hẳn
//  phương án từng cân nhắc (quét advert của 4 node phòng): gateway chỉ quảng bá
//  và giữ MỘT kết nối GATT với UNO Q, không quét gì cả — mà quét mới là thứ ăn
//  sóng liên tục. Số đo phòng đi ESP-NOW, vốn đã chia sẻ radio WiFi sẵn có.
//
//  6 topic đang dùng, khớp CHÍNH XÁC backend (src/app/utils/mqtt_naming.py):
//    telemetry  node -> cloud   {ts,t,h,rssi,mac,fw}      (telemetry_handler.py)
//                               bo này CHỈ gửi hộ node khác, không gửi của mình
//    status     node -> cloud   "online"/"offline" retain (status_handler.py)
//    cmd        cloud -> node   lệnh IR HOẶC lệnh học     (command_publisher.py)
//    state      node -> cloud   {ack,mode,setpoint,mac} retain (state_handler.py)
//    learn      node -> cloud   {raw_timing,mode/action,temp} (learn_handler.py)
//    override   node -> cloud   {mode,setpoint} | {clear}  (override_handler.py)
//                               KHÔNG retain — xem buildTopics()
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "espnow-message.h"
#include "espnow-relay.h"
#include "room-registry.h"
#include "slave-watch.h"
#include "unoq-link.h"
#include "ir-io.h"
#include "ir-store.h"
#include "ac-actions.h"
// Máy tạo độ ẩm: panel tự lái bằng độ ẩm trung vị của 4 góc phòng, KHÔNG qua máy
// chủ. Backend chỉ tham gia ở khâu học mã (hai nút rời HUMID_ON/HUMID_OFF).
#include "humidifier-control.h"
// Nhật ký từng gói vào/ra. TẮT trừ khi biên dịch với -D GATEWAY_TRACE=1, và khi
// tắt thì mọi lời gọi dưới đây bị trình biên dịch xoá sạch (thân hàm rỗng).
#include "serial-trace.h"
// Bo QR Box Advance Touch Screen: màn 2.8" chạy trên TÁC VỤ RIÊNG Ở LÕI 0.
// Thiết kế giao diện + lý do phải tách lõi: ../../Interface/README.md và ui.h.
// loop() dưới đây không vẽ một pixel nào — nó chỉ đổ số liệu sang và rút lệnh về.
#include "ui/ui.h"

// KHÔNG CÓ BẢNG TRA NODE GÓC PHÒNG Ở ĐÂY, và đó là lý do chọn ESP-NOW.
//
// Gói ESP-NOW chở được 250 byte nên mỗi node góc phòng mang thẳng device_uuid 32
// ký tự của chính nó (shared/espnow-message.h). Gateway cứ thế publish vào topic
// của node đó — thêm/bớt một góc thì chỉ nạp firmware cho góc mới, gateway không
// phải sửa và không phải nạp lại.
//
// Phương án BLE từng cân nhắc thì ngược lại: advertising cổ điển chỉ có 31 byte,
// chở không nổi uuid, nên sẽ buộc file này giữ một mảng ROOM_NODE_UUIDS mà lệch
// một ô là số đo của góc A nộp lên cloud dưới tên góc B — biểu đồ vẫn có số,
// không lỗi ở đâu cả, hai góc bị hoán tên vĩnh viễn.

// Broker EMQX tự host trên VPS chạy plaintext 1883 (MQTT_TLS=false trong
// docker-compose), nên dùng WiFiClient thường — KHÔNG phải WiFiClientSecure.
static WiFiClient   net;
static PubSubClient mqtt(net);

/// PubSubClient mặc định chỉ có bộ đệm 256 byte và ÂM THẦM VỨT mọi gói lớn hơn.
/// Lệnh IR mang `ir_raw` vài trăm số -> vài KB JSON, và gói learn node gửi lên
/// cũng vậy. Không nới chỗ này thì mọi lệnh có ir_raw biến mất không dấu vết:
/// log không báo gì, máy lạnh không nhúc nhích. 12KB dư cho 600 mốc.
static const uint16_t MQTT_BUFFER_BYTES = 12288;

static String tTelemetry, tStatus, tCmd, tState, tLearn, tOverride;
static void buildTopics() {
  String base = String("bl/") + ORG_ID + "/" + DEVICE_UUID + "/";
  tTelemetry = base + "telemetry";
  tStatus    = base + "status";
  tCmd       = base + "cmd";
  tState     = base + "state";
  tLearn     = base + "learn";
  // `override` là đường để MÀN NÀY xin máy chủ nhường quyền — trước đây không có
  // nên ghi đè tại chỗ chỉ sống được tới chu kỳ comfort kế tiếp. Xem
  // ../Interface/README.md §8.3 và app/utils/mqtt_naming.py.
  //
  // PHẢI LÀ TOPIC RIÊNG, KHÔNG ĐƯỢC NHỒI VÀO `state`: publishState() gửi RETAIN,
  // nên một cờ ghi đè nằm trong đó sẽ được broker phát lại mỗi lần node nối lại
  // và tự bật ghi đè vĩnh viễn — máy chủ thôi tính comfort mãi mà không ai bấm gì.
  tOverride  = base + "override";
}

/// Bộ đệm khung IR dùng chung cho cả phát lẫn học. Một node chỉ làm một việc
/// tại một thời điểm nên không cần hai bộ đệm 1.2KB.
static uint16_t irBuf[IrIo::RAW_MAX];

// --- Nhãn đang học -----------------------------------------------------------
// "COOL 25" -> label="COOL", temp=25   |   "FAN_SPEED" -> label="FAN_SPEED", temp=-1
static char learnLabel[24] = "";
static int  learnTemp = -1;

// --- Dấu vết quyền điều khiển ------------------------------------------------
// Ai đang cầm lái: máy chủ, hay một người (ở màn này HOẶC trong app).
//
// `overrideLocal` GIỜ CÓ HIỆU LỰC THẬT, không còn là cờ trang trí: bấm THỦ CÔNG
// publish lên topic `override` và `override_handler.py` đặt cổng ghi đè trong
// Redis, nên vòng lặp comfort thực sự nhường quyền (Interface/README.md §8.3 —
// khoảng trống đó đã lấp).
//
// VÀ NÓ PHẢI PHẢN ÁNH CẢ GHI ĐÈ ĐẶT TỪ APP, không chỉ từ màn này. Trước đây mọi
// gói `cmd` đều kéo cờ này về false, kể cả gói do chính app phát khi người dùng
// vừa ghi đè trong app — nên đứng ở tường thì thấy huy hiệu TU DONG trong lúc
// máy chủ đã nhường quyền cho app xong. Màn khẳng định sai về việc ai đang cầm
// lái. Nay cờ bám theo `reason` của gói, xem cuối takeCommand().
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

/// Mức quạt panel VỪA BẮN THÀNH CÔNG. 0xFF = phiên này chưa bắn mức nào.
///
/// Chỉ đặt SAU khi IrIo::blast() đã chạy, không phải lúc nhận lệnh: đây là con số
/// duy nhất màn hình dựa vào để nói "quạt đang ở mức nào", và nói mức mà panel
/// không hề phát được là bịa. Nó cũng KHÔNG phải trạng thái thật của máy — ai
/// cầm remote thật bấm một cái là nó sai, và panel không có cách nào biết (xem
/// Ui::Model::fanLast).
static uint8_t  lastFanIdx = 0xFF;

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
  // ir_code_id cần xin server gửi lại mảng, rỗng = không xin gì. Phải ĐẶT HÀNG
  // như hasFrame/needAck chứ không publish thẳng trong callback — cùng đúng lý
  // do ghi ở đầu struct này. 37 byte đủ cho UUID 36 ký tự + '\0'.
  char     needRawId[40];
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
/// Master ĐỨNG TÊN slave báo trạng thái: slave không có kết nối MQTT nên broker
/// không thể sinh Last Will cho nó. Retained để web/app mở lên là thấy ngay.
static void publishSlaveStatus(const char *uuid, bool online) {
  String topic = String("bl/") + ORG_ID + "/" + uuid + "/status";
  mqtt.publish(topic.c_str(), online ? "online" : "offline", true);
  Serial.printf("[slave] %s -> %s\n", uuid, online ? "ONLINE" : "OFFLINE (mat nhip tim)");
}

// ---------------------------------------------------------------------------
//  ESP-NOW: chuyển tiếp số đo của node góc phòng + node ngoài trời
// ---------------------------------------------------------------------------
/// Đẩy số đo của MỘT node slave lên topic của chính nó.
///
/// Dùng chung cho cả node góc phòng lẫn node ngoài trời: gói ESP-NOW tự mang
/// uuid nên gateway không cần biết đó là loại node nào để trung chuyển. Loại
/// node chỉ quyết định gateway XẾP số đó vào đâu để hiển thị (xem onSlavePacket).
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
  const size_t n = serializeJson(doc, buf);
  const String topic = String("bl/") + ORG_ID + "/" + uuid + "/telemetry";
  const bool ok = mqtt.publish(topic.c_str(), (const uint8_t *)buf, n, false);
  Serial.printf("[relay] %s t=%.1f h=%.0f -> %s\n", uuid, t, h, ok ? "da chuyen" : "LOI");
  SerialTrace::mqttOut(topic.c_str(), (const uint8_t *)buf, n, ok);
}

/// Một gói vừa tới từ bất kỳ node slave nào — góc phòng hoặc ngoài trời.
static void onSlavePacket(const AcEspNowPacket &pkt, const uint8_t mac[6]) {
  const char *uuid = pkt.device_uuid;
  const bool isRoom = (pkt.node_kind == AC_NODE_ROOM);

  // TRƯỚC MỌI THỨ KHÁC: ghi lại gói đúng như nó tới. Các nhánh dưới đây lọc NaN,
  // chặn theo nhịp, bỏ qua bảng đầy — nên đặt trace sau chúng là mất đúng những
  // gói cần nhìn nhất khi đi tìm mất sóng.
  SerialTrace::packetIn(pkt, mac);

  // MỌI gói đều tính là nhịp tim -> phát hiện mất kết nối nhanh. Kể cả gói
  // KHÔNG có số đo (NaN, do cảm biến slave lỗi): node vẫn sống, chỉ cảm biến
  // hỏng — hai chuyện khác nhau, không được gộp thành "mất kết nối".
  SlaveWatch::heard(uuid, publishSlaveStatus);
  if (SlaveWatch::dueForStatusRefresh(uuid)) publishSlaveStatus(uuid, true);

  // Ghi vào bảng hiển thị TRƯỚC khi lọc NaN: bảng cần biết góc này còn sống, và
  // NaN trong đó là câu trả lời đúng cho "sống nhưng cảm biến hỏng".
  if (isRoom) {
    if (!RoomRegistry::update(pkt)) {
      static bool warned = false;
      if (!warned) {
        warned = true;
        Serial.printf("[room] bang day (%u o) — goc thu %u tro di khong hien tren man, "
                      "nhung VAN duoc chuyen tiep len cloud\n",
                      RoomRegistry::MAX_ROOMS, RoomRegistry::MAX_ROOMS + 1);
      }
    }
  } else {
    lastSlaveT = pkt.temp;
    lastSlaveH = pkt.humidity;
    lastSlaveMs = millis();
  }

  if (isnan(pkt.temp) || isnan(pkt.humidity)) {
    Serial.printf("[%s] %s con song nhung cam bien loi (NaN)\n",
                  isRoom ? "room" : "slave", uuid);
    return;
  }
  if (SlaveWatch::dueForRelay(uuid)) publishSlaveTelemetry(uuid, mac, pkt.temp, pkt.humidity);
}

// ---------------------------------------------------------------------------
//  HỌC remote
// ---------------------------------------------------------------------------
static bool isAcMode(const char *s) {
  return strcmp(s, "COOL") == 0 || strcmp(s, "DRY") == 0 ||
         strcmp(s, "FAN")  == 0 || strcmp(s, "OFF") == 0;
}

/// Nút rời này có phải thứ PANEL tự bắn được không (mức quạt, máy tạo độ ẩm)?
///
/// TẬP CON CÓ CHỦ ĐÍCH của ir_action_service.KNOWN_ACTIONS — xem ac-actions.h.
/// Chỉ những nút có mặt trên màn mới đáng giữ một bản trong NVS: mỗi khung chiếm
/// ~600 byte, và SLEEP/ECO/SWING/TIMER... không có nút nào trên panel để bấm nên
/// bản sao đó sẽ không bao giờ được phát. Chúng vẫn học và bắn bình thường TỪ
/// APP, đường đó không đi qua NVS của node.
static bool isPanelAction(const char *s) {
  for (uint8_t i = 0; i < AcActions::FAN_COUNT; i++) {
    if (strcmp(s, AcActions::fanWire(i)) == 0) return true;
  }
  return strcmp(s, AcActions::humidWire(true)) == 0 ||
         strcmp(s, AcActions::humidWire(false)) == 0;
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

    // GIỮ LUÔN MỘT BẢN Ở NODE, đừng chỉ đẩy lên cloud. Không có dòng này thì
    // người dùng vừa dạy mã ngay trên panel mà chính panel vẫn báo "CHƯA HỌC MÃ"
    // và nút chế độ vẫn mờ — phải chờ vòng lặp comfort trên server tình cờ gửi
    // xuống một lệnh cho đúng tổ hợp đó thì mới bấm được. Trên bảng điều khiển
    // treo tường, khoảng chờ không giải thích được đó bị đọc là hỏng.
    //
    // Làm TRƯỚC khi publish, có chủ đích: mất mạng vẫn học và điều khiển tại chỗ
    // được. Backend sẽ nhận mã ở lần học sau, còn máy lạnh thì chạy ngay.
    const int t = aliasTemp(learnLabel, learnTemp);
    if (IrStore::saveLearned(learnLabel, t, raw, len)) {
      aliasDirty = true;      // bảng "đã có mã" đổi -> màn phải tính lại
      Serial.printf("[learn] da luu vao NVS: %s%s%d — panel dung duoc ngay\n",
                    learnLabel, t >= 0 ? " " : "", t >= 0 ? t : 0);
    } else {
      Serial.println("[learn] KHONG luu duoc vao NVS — panel se van bao chua hoc ma");
    }
  } else {
    doc["action"] = learnLabel;

    // GIỮ LUÔN MỘT BẢN Ở NODE cho những nút panel tự bắn được — cùng lý do và
    // cùng khuôn với nhánh chế độ ở trên, và ở đây còn cấp thiết hơn: mã nút rời
    // KHÔNG CÓ ir_code_id, nên đường "backend gửi lệnh kèm ir_raw rồi node lưu
    // lại" không tồn tại cho chúng. Không lưu ở đây thì cách duy nhất còn lại để
    // panel có mã là người dùng bấm XIN MÃ, mà họ không có lý do gì để nghĩ tới
    // việc đó ngay sau khi vừa học xong.
    //
    // temp = -1: nút rời không có nhiệt độ, đúng quy ước của IrStore::saveAlias.
    if (isPanelAction(learnLabel)) {
      if (IrStore::saveLearned(learnLabel, -1, raw, len)) {
        aliasDirty = true;
        Serial.printf("[learn] da luu nut roi %s vao NVS — panel dung duoc ngay\n",
                      learnLabel);
      } else {
        Serial.printf("[learn] KHONG luu duoc %s vao NVS — panel se van bao chua hoc ma\n",
                      learnLabel);
      }
    }
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

/// Gói "chỉ lưu, không phát" — máy chủ đẩy lại kho mã sau khi người dùng bấm
/// XIN MÃ trên panel (ir_service.push_all_codes).
///
/// PHẢI TÁCH KHỎI takeCommand(): một gói cmd bình thường mang `ir_raw` nghĩa là
/// "bắn khung này ra máy lạnh ngay". Đi qua đường đó thì một lượt đồng bộ ~18 mã
/// biến thành 18 lần bấm remote liên tiếp — chế độ và nhiệt độ nhảy loạn rồi
/// dừng ở đúng hàng cuối cùng trong danh sách.
///
/// Cũng KHÔNG ack: không có req_id, và không có hàng `commands` nào bên server
/// chờ được đánh dấu.
static void storeCode(JsonDocument &doc) {
  const char *codeId = doc["ir_code_id"];
  const char *action = doc["action"];
  const char *mode   = doc["mode"] | "";
  const int   setp   = doc["setpoint"] | -1;
  JsonArray   irRaw  = doc["ir_raw"];

  if (irRaw.isNull()) {
    Serial.println("[sync] goi store_only khong co ir_raw — bo qua");
    return;
  }
  if (irRaw.size() > IrIo::RAW_MAX) {
    Serial.printf("[sync] ma dai %u moc > gioi han %u — bo qua\n",
                  (unsigned)irRaw.size(), IrIo::RAW_MAX);
    return;
  }

  uint16_t n = 0;
  for (JsonVariant v : irRaw) irBuf[n++] = (uint16_t)v.as<uint32_t>();

  // --- NÚT RỜI (mức quạt, máy tạo độ ẩm) ---
  //
  // KHÁC MA TRẬN (chế độ, nhiệt độ) ở đúng một chỗ, và chỗ đó quyết định cả
  // nhánh này: bảng `ir_action_codes` khoá theo (org, action) chứ không sinh
  // UUID, nên gói không mang `ir_code_id`. Không có id thì không có gì để đối
  // chiếu, và cũng không có đường `need_raw` để xin lại — đây là lần DUY NHẤT
  // panel nhận được mã này ngoài lúc chính nó học.
  //
  // Dùng saveLearned() (id tạm "local-FAN_60") thay vì save()+saveAlias(): id
  // tạm chính là thứ IrStore sinh ra để phục vụ mã không có UUID của backend.
  if (action != nullptr && action[0] != '\0') {
    if (!isPanelAction(action)) {
      // Backend đẩy cả kho nút rời; panel chỉ giữ những nút nó có chỗ để bấm.
      // Bỏ qua trong im lặng là ĐÚNG ở đây — không phải lỗi, và in một dòng cho
      // mỗi nút bị bỏ sẽ làm log resync dài gấp đôi vì một chuyện bình thường.
      return;
    }
    if (!IrStore::saveLearned(action, -1, irBuf, n)) {
      Serial.printf("[sync] khong luu duoc nut roi %s — NVS day?\n", action);
      Ui::reply("NVS ĐẦY — KHÔNG LƯU ĐƯỢC MÃ");
      return;
    }
    aliasDirty = true;
    Serial.printf("[sync] da nhan nut roi %s (%u moc)\n", action, n);
    return;
  }

  // --- ma trận (chế độ, nhiệt độ) ---
  if (codeId == nullptr || mode[0] == '\0') {
    Serial.println("[sync] goi store_only thieu truong — bo qua");
    return;
  }

  if (!IrStore::save(codeId, irBuf, n)) {
    Serial.printf("[sync] khong luu duoc %s — NVS day?\n", codeId);
    Ui::reply("NVS ĐẦY — KHÔNG LƯU ĐƯỢC MÃ");
    return;
  }
  IrStore::saveAlias(mode, aliasTemp(mode, setp), codeId);
  aliasDirty = true;
  Serial.printf("[sync] da nhan %s %d (%u moc)\n", mode, setp, n);
}

static void takeCommand(JsonDocument &doc) {
  // Trước MỌI thứ khác: gói đồng bộ không phải là lệnh điều khiển, nó không đi
  // qua pending/ack/chống-trùng và tuyệt đối không được bắn IR.
  if (doc["store_only"] | false) {
    storeCode(doc);
    return;
  }

  copyStr(pending.reqId, sizeof(pending.reqId), doc["req_id"]);
  copyStr(pending.mode,  sizeof(pending.mode),  doc["mode"]);
  pending.setpoint = doc["setpoint"] | -1;
  pending.hasFrame = false;
  pending.frameLen = 0;
  pending.needAck  = false;
  pending.needRawId[0] = '\0';

  const char *reason = doc["reason"] | "";

  // Dựng sẵn dòng nhật ký cho màn hình. `reason` tới trong MỌI lệnh nhưng trước
  // đây chỉ được in ra serial rồi vứt — nghĩa là muốn biết máy chủ vừa ra lệnh
  // gì phải cắm USB-TTL vào bo treo trên tường. Bốn nhánh dưới điền nốt `result`
  // rồi gửi sang tác vụ UI (chỉ đẩy hàng đợi, an toàn trong callback này).
  Ui::CmdLog logEntry{};
  copyStr(logEntry.mode,   sizeof(logEntry.mode),   pending.mode);
  copyStr(logEntry.reason, sizeof(logEntry.reason), reason);
  logEntry.setpoint = pending.setpoint;

  // MQTT QoS1 cho phép broker gửi LẠI cùng một lệnh nếu ack chưa kịp về. Phát
  // lại khung IR = bấm remote hai lần; với các nút xoay vòng (tốc độ quạt, đảo
  // gió) lần hai sẽ nhảy sang nấc khác, tức là lặp lại KHÔNG vô hại. Chặn theo
  // req_id, nhưng vẫn ack lại vì rất có thể chính cái ack cũ đã rơi.
  if (pending.reqId[0] && strcmp(pending.reqId, lastReqId) == 0) {
    Serial.printf("[cmd] %s da thi hanh roi — bo qua ban lap, ack lai\n", pending.reqId);
    pending.needAck = true;
    logEntry.result = Ui::CmdLog::DUPLICATE;
    Ui::logCommand(logEntry);
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
      // Tự xin lại thay vì chờ người vào xoá Redis bằng tay. loop() mới publish
      // (callback không được đụng mqtt.publish — xem đầu struct pending).
      copyStr(pending.needRawId, sizeof(pending.needRawId), codeId);
      Serial.println("      -> dang xin server gui lai mang thoi gian");
      logEntry.result = Ui::CmdLog::NEED_RAW;
      Ui::logCommand(logEntry);
      return;
    }
  } else {
    // Chưa học mã cho (mode, setpoint) này — command_publisher đã ghi warning
    // "No learned IR code" ở phía server rồi.
    Serial.printf("[cmd] %s %s %d: khong co ir_raw lan ir_code_id — chua hoc ma nay\n",
                  pending.reqId, pending.mode, pending.setpoint);
    logEntry.result = Ui::CmdLog::NO_CODE;
    Ui::logCommand(logEntry);
    return;
  }

  Serial.printf("[cmd] %s -> %s %d (%s) · %u moc, cho phat\n",
                pending.reqId, pending.mode, pending.setpoint, reason, pending.frameLen);
  pending.hasFrame = true;
  pending.needAck  = true;
  // Ghi SENT ngay ở đây dù việc bắn nằm ở loop(): hasFrame=true là cam kết chắc
  // chắn — loop() không có nhánh nào bỏ qua nó. Chờ tới sau khi bắn mới ghi thì
  // phải chuyền logEntry qua struct pending, thêm một trạng thái nữa chỉ để nói
  // lại đúng điều đã biết.
  logEntry.result = Ui::CmdLog::SENT;
  Ui::logCommand(logEntry);
  // Máy chủ vừa ra lệnh -> nó đã giành lại quyền, huy hiệu trên màn trở về
  // "TU DONG". Đây chính là điều giao diện đã cảnh báo lúc người dùng ghi đè.
  lastCmdMs = millis();

  // Ai đang cầm lái, đọc từ `reason` chứ không mặc định là máy chủ.
  //
  //   "auto:COOL@25"    máy chủ tự quyết        -> máy chủ cầm lái
  //   "manual override" người dùng ghi đè (app) -> NGƯỜI cầm lái
  //   "action:FAN_SPEED" bấm nút rời trong app  -> KHÔNG ĐỔI (xem dưới)
  //
  // Nút rời cố tình không đụng cờ này: nó không mang (mode, setpoint) và không
  // đặt cổng ghi đè nào phía máy chủ, nên suy ra quyền điều khiển từ nó là bịa.
  // Bấm "tốc độ quạt" trong lúc máy chủ đang tự chạy thì máy chủ VẪN đang tự
  // chạy — cờ giữ nguyên là câu trả lời đúng, không phải là bỏ sót nhánh.
  if (strncmp(reason, "auto:", 5) == 0)              overrideLocal = false;
  else if (strcmp(reason, "manual override") == 0)   overrideLocal = true;
}

static void onMessage(char *topic, byte *payload, unsigned int len) {
  // Ghi TRƯỚC khi bóc JSON: gói hỏng khuôn cũng là gói đã tới, mà nhánh lỗi bên
  // dưới chỉ in tên lỗi chứ không in nội dung — đúng lúc cần nhìn nội dung nhất.
  SerialTrace::mqttIn(topic, payload, len);

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
  // MAC ĐI KÈM Ở ĐÂY VÌ KHÔNG CÒN CHỖ NÀO KHÁC. Mọi node khác khai MAC trong gói
  // telemetry, nhưng gateway không còn cảm biến nên không publish telemetry nữa.
  // Thiếu dòng này thì trang "Nạp firmware" hiện "—" ở đúng cái node mà người đi
  // lắp đang đứng trước mặt. state_handler.py đọc trường này.
  doc["mac"] = WiFi.macAddress();
  char buf[160];
  size_t n = serializeJson(doc, buf);
  bool ok = mqtt.publish(tState.c_str(), (const uint8_t *)buf, n, true);
  SerialTrace::mqttOut(tState.c_str(), (const uint8_t *)buf, n, ok);
  Serial.printf("[state] ack=%s mode=%s setpoint=%d -> %s\n",
                pending.reqId, pending.mode, pending.setpoint, ok ? "da gui" : "GUI LOI");
}

/// Xin server gửi lại mảng thời gian của một ir_code_id mà node không còn giữ.
///
/// VÌ SAO CẦN: backend chỉ đính `ir_raw` cho LẦN ĐẦU mỗi mã, sau đó tin rằng node
/// còn giữ trong NVS (`command_publisher._resolve_ir_raw` + `bl:ircache:{id}`).
/// Hai bên lệch nhau bất cứ khi nào NVS mất mà DB thì không: `erase_flash`, thay
/// bo dùng lại DEVICE_UUID, hoặc người dùng vừa bấm XOÁ trong màn Cài đặt. Trước
/// đây nhánh đó chỉ in log rồi đứng im — máy lạnh câm mà log server sạch sẽ, kiểu
/// hỏng tốn nhiều thời gian nhất để tìm.
///
/// KHÔNG RETAIN. Đây là một yêu cầu xảy ra một lần, không phải trạng thái. Retain
/// thì broker phát lại nó sau mỗi lần node nối lại, và backend sẽ dọn cache rồi
/// gửi lại mảng vài KB một cách vô cớ, mãi mãi.
static void publishNeedRaw(const char *codeId) {
  JsonDocument doc;
  doc["need_raw"] = codeId;
  // Kèm req_id để bên server đối chiếu được đúng lệnh nào đã trượt. Lệnh đó CỐ Ý
  // không được ack (xem onCmdPacket) nên `commands.ack_ts` vẫn rỗng — đây là thứ
  // giải thích vì sao.
  if (pending.reqId[0]) doc["req_id"] = pending.reqId;
  char buf[96];
  size_t n = serializeJson(doc, buf);
  bool ok = mqtt.publish(tState.c_str(), (const uint8_t *)buf, n, false);
  Serial.printf("[cmd] xin lai ma %s -> %s\n", codeId, ok ? "da gui" : "GUI LOI");
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

/// Số lần thử WiFi lúc KHỞI ĐỘNG trước khi chạy tiếp mà không có mạng.
///
/// CÓ GIỚI HẠN, KHÔNG CHỜ MÃI. Bản trước chờ vô hạn ở đây với lý do "không có
/// mạng thì node chẳng làm được gì" — lý do đó SAI, và nó là lỗi nghiêm trọng
/// nhất từng có trong file này (xem serviceNetwork bên dưới). Không có mạng thì
/// node vẫn thu được ESP-NOW, vẫn nói chuyện được với UNO Q qua UART, và vẫn bắn
/// được hồng ngoại — tức là vẫn điều khiển được máy lạnh. Đó chính xác là thứ
/// lớp edge sinh ra để làm.
static const uint8_t WIFI_BOOT_ATTEMPTS = 3;

static void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);

  for (uint8_t attempt = 1; attempt <= WIFI_BOOT_ATTEMPTS; attempt++) {
    Serial.printf("WiFi -> \"%s\" (lan %u/%u) ", WIFI_SSID, attempt, WIFI_BOOT_ATTEMPTS);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    const uint32_t deadline = millis() + WIFI_ATTEMPT_MS;
    while (WiFi.status() != WL_CONNECTED && (int32_t)(millis() - deadline) < 0) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) break;
    // Quét CHỈ ĐƯỢC PHÉP Ở ĐÂY, lúc ESP-NOW chưa dựng. scanNetworks() nhảy khắp
    // các kênh và bỏ radio lại ở kênh cuối — gọi nó sau khi ESP-NOW đã chạy là
    // kéo gateway ra khỏi kênh của các node, và không một dòng log nào báo.
    if (attempt < WIFI_BOOT_ATTEMPTS) wifiDiagnose();
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("\nKHONG VAO DUOC WiFi sau %u lan — CHAY TIEP KHONG CO MANG.\n"
                  "  ESP-NOW, UART toi UNO Q va hong ngoai van hoat dong; chi mat\n"
                  "  duong len cloud. serviceNetwork() se thu lai nen trong loop().\n",
                  WIFI_BOOT_ATTEMPTS);
    return;
  }

  // TẮT tiết kiệm điện WiFi. BẮT BUỘC cho node master, và giờ làm được vì đường
  // tới UNO Q đã chuyển sang UART — không còn Bluetooth để ràng buộc.
  //
  // ESP32 mặc định bật modem sleep khi đã vào mạng: radio ngủ giữa các beacon.
  // Lưu lượng WiFi thường không sao vì router ĐỆM HỘ trong lúc ngủ, nhưng gói
  // ESP-NOW từ slave thì KHÔNG ai đệm — đến đúng lúc radio ngủ là mất luôn, mà
  // broadcast không có ACK nên slave vẫn tưởng gửi thành công.
  //
  // ĐÃ ĐO CÁI GIÁ ĐÓ, đừng bật lại vì bất kỳ lý do gì:
  //     modem sleep BẬT (thời BLE):  0,31 gói/giây
  //     modem sleep TẮT:             0,80 gói/giây  ← đúng 4 node × 5 giây
  // Node ngoài trời khi đó rơi ~50% và nhấp nháy ONLINE/OFFLINE liên tục.
  //
  // Hệ quả: KHÔNG bao giờ bật lại Bluetooth trên bo này. Chip từ chối chạy WiFi +
  // BT với modem sleep tắt — nó abort() chứ không chạy kém đi
  // (`Should enable WiFi modem sleep when both WiFi and Bluetooth are enabled`),
  // và bo sẽ lặp khởi động lại vô hạn mỗi ~6 giây.
  WiFi.setSleep(false);

  Serial.printf(" OK  IP=%s  RSSI=%d dBm\n", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
}

/// MỘT lần thử nối MQTT. Trả true nếu vào được.
static bool mqttTryConnect() {
  String cid = String("breezelink_") + DEVICE_UUID;  // = mqtt_naming.client_id()
  // LWT (will): topic=status, qos=1, retain=true, payload="offline".
  if (mqtt.connect(cid.c_str(), MQTT_USERNAME, MQTT_PASSWORD,
                   tStatus.c_str(), 1, true, "offline")) {
    Serial.println("MQTT ... connected");
    mqtt.publish(tStatus.c_str(), "online", true);  // retained -> web thấy "Trực tuyến"
    // QoS1: lệnh điều khiển máy lạnh không được phép rơi âm thầm.
    mqtt.subscribe(tCmd.c_str(), 1);
    return true;
  }
  // rc=-2 mạng lỗi; rc=4 sai user/pass; rc=5 chưa được cấp quyền trên broker.
  Serial.printf("MQTT ... that bai rc=%d\n", mqtt.state());
  return false;
}

/// Bao lâu thử nối lại một lần khi đang mất mạng (ms).
static const uint32_t NET_RETRY_MS = 15000UL;

/// Giữ WiFi/MQTT sống mà KHÔNG CHẶN loop(). Gọi mỗi vòng.
///
/// ĐÂY LÀ CHỖ SỬA LỖI NGHIÊM TRỌNG NHẤT CỦA FILE NÀY.
///
/// Bản trước gọi thẳng connectWifi() + connectMqtt() trong loop(), mà cả hai đều
/// là vòng lặp CHỜ VÔ HẠN. Hệ quả: mất WiFi là loop() không bao giờ quay lại —
///
///   ESP-NOW ngừng thu   -> 4 góc phòng và node ngoài trời biến mất
///   UART ngừng đẩy      -> UNO Q không còn ảnh chụp nào để tính
///   lệnh UNO Q không ai đọc -> edge không lái được máy lạnh
///   IR ngừng thi hành   -> kể cả lệnh đã nằm trong hàng đợi
///
/// Tức là mất mạng làm CHẾT luôn lớp dự phòng sinh ra để chịu đựng đúng sự cố
/// đó. Cả kiến trúc edge-ai dựa trên giả định "gateway vẫn chạy khi mất cloud",
/// và giả định đó sai ngay trong vòng lặp chính.
///
/// Nay: thử lại theo nhịp, không chờ, và TUYỆT ĐỐI KHÔNG QUÉT. scanNetworks()
/// nhảy khắp các kênh và bỏ radio lại ở kênh cuối — gọi nó khi ESP-NOW đang chạy
/// là tự cắt đứt đường thu của chính mình, âm thầm.
static void serviceNetwork() {
  static uint32_t lastTry = 0;
  const uint32_t now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastTry >= NET_RETRY_MS) {
      lastTry = now;
      Serial.println("[net] mat WiFi — thu noi lai (khong chan vong lap)");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);   // đặt lệnh rồi đi tiếp, không chờ
    }
    return;   // chưa có WiFi thì cũng chưa thể có MQTT
  }

  if (!mqtt.connected()) {
    if (now - lastTry >= NET_RETRY_MS) {
      lastTry = now;
      mqttTryConnect();   // MỘT lần; hỏng thì vòng sau thử tiếp
    }
    return;
  }

  // --- Nhịp "gateway còn sống" ---------------------------------------------
  //  TRƯỚC ĐÂY `status` CHỈ GỬI MỘT LẦN, trong mqttTryConnect(). Backend nay coi
  //  một node là trực tuyến khi `last_seen_at` còn tươi (services/device_presence.py),
  //  mà cột đó CHỈ được ghi khi có bản tin trên topic `status`. Nên một gateway
  //  chạy hoàn hảo vẫn bị báo ngoại tuyến sau PRESENCE_TTL — đã dính thật: bo
  //  chạy 9 phút, WiFi và MQTT đều nối, mà web hiện "Ngoại tuyến".
  //
  //  Gateway PHẢI TỰ NÓI VỀ MÌNH chứ không suy ra từ telemetry của slave: suy
  //  gián tiếp sẽ biến quan hệ hiện diện thành vòng tròn (slave sống nhờ gateway
  //  publish hộ, gateway lại sống nhờ slave gửi về), và khi ESP-NOW chết hẳn thì
  //  gateway khoẻ mạnh vẫn bị báo offline.
  //
  //  DÙNG CHUNG STATUS_REFRESH_MS với slave để MỘT hằng số chi phối ngưỡng bên
  //  backend — đổi nhịp ở đây thì phải đổi PRESENCE_TTL, và chú thích ở cả hai
  //  đầu đều trỏ về nhau.
  //
  //  Phép trừ không dấu nên millis() tràn vẫn đúng — cùng khuôn với slave-watch.cpp.
  static uint32_t lastOwnStatusMs = 0;
  if (now - lastOwnStatusMs >= SlaveWatch::STATUS_REFRESH_MS) {
    lastOwnStatusMs = now;
    mqtt.publish(tStatus.c_str(), "online", true);
  }

  mqtt.loop();
}

// ---------------------------------------------------------------------------
//  Máy tạo độ ẩm
// ---------------------------------------------------------------------------
/// Bắn khung IR cho hướng [on]. Trả false khi CHƯA HỌC MÃ cho hướng đó —
/// HumidifierControl dựa vào giá trị trả về để không tin nhầm là máy đã đổi.
///
/// REMOTE BẬP BÊNH ĐƯỢC XỬ LÝ Ở ĐÂY, và chỉ ở đây. Rất nhiều máy tạo ẩm chỉ có
/// MỘT nút nguồn: bấm lúc đang tắt thì bật, bấm lúc đang chạy thì tắt. Hộ như vậy
/// chỉ học được ô BẬT, nên chiều TẮT rơi về đúng khung đó.
///
/// BẢN Ở BO esp32-humidity DÙNG MỘT CỜ BIÊN DỊCH (`DIFFUSER_IR_TOGGLE`) cho việc
/// này, và cờ đó là thứ chỉ sai được ngoài hiện trường: khai 0 trong khi cả hai ô
/// đều học nút bập bênh thì mã "TẮT" thật ra cũng đảo trạng thái, và bo BẬT máy
/// đúng lúc nó tưởng mình đang tắt. Ở đây không có gì để khai — "ô TẮT có mã hay
/// không" là một sự kiện đọc được từ NVS, và màn MÁY TẠO ẨM nói thẳng ra nó.
///
/// Định nghĩa TRƯỚC setup() vì setup() truyền nó cho HumidifierControl::begin().
static bool humidifierEmit(bool on) {
  uint16_t n = IrStore::loadAlias(AcActions::humidWire(on), -1, irBuf, IrIo::RAW_MAX);
  bool viaToggle = false;
  if (n == 0 && !on) {
    n = IrStore::loadAlias(AcActions::humidWire(true), -1, irBuf, IrIo::RAW_MAX);
    viaToggle = (n > 0);
  }
  if (n == 0) return false;

  IrIo::blast(irBuf, n);
  Serial.printf("[am] da phat %u moc -> %s%s\n", n, on ? "BAT" : "TAT",
                viaToggle ? " (dung lai o BAT: remote bap benh)" : "");
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n== BreezeLink · QR Box Advance Touch · GATEWAY TRONG NHA (BLE + ESP-NOW + IR) ==");

  // Dựng màn TRƯỚC WiFi, có chủ đích: tác vụ giao diện chạy ở lõi 0 nên nó vẫn
  // vẽ bình thường suốt lúc connectWifi()/connectMqtt() đang chặn lõi 1 hàng
  // chục giây. Người đi lắp nhìn thấy "MAT KET NOI" nhấp nháy — tức là node
  // sống và đang dò mạng — thay vì một màn đen không nói gì.
  //
  // Ui::begin() cũng là nơi kéo EN_LEVEL_SHIFT lên HIGH, nên nó phải chạy TRƯỚC
  // IrIo::begin() nếu chân IR đi qua bộ dịch mức.
  Ui::begin();
  IrIo::begin(IR_TX_PIN, IR_RX_PIN);
  if (!IrStore::begin()) {
    // Không chặn khởi động: node vẫn chạy được, chỉ là mọi lệnh phải kèm ir_raw.
    Serial.println("NVS loi — se khong cache duoc ma IR, moi lenh deu phai co ir_raw");
  }
  // SAU IrStore::begin(): bộ điều khiển máy tạo ẩm nạp lại niềm tin trạng thái từ
  // NVS, và emitter của nó tra kho mã. Trước đó thì cả hai đều rỗng.
  HumidifierControl::begin(humidifierEmit);
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
  // MỘT lần lúc khởi động. Hỏng thì KHÔNG chặn — serviceNetwork() trong loop()
  // sẽ thử lại nền, và mọi thứ không cần mạng (ESP-NOW, UART, IR) chạy ngay.
  if (WiFi.status() == WL_CONNECTED) mqttTryConnect();

  // ESP-NOW khởi tạo SAU khi WiFi đã kết nối: nó dùng đúng kênh WiFi đang bám,
  // nên phải để WiFi chốt kênh trước thì slave (đang dò kênh router) mới gặp.
  if (EspNowRelay::begin()) {
    Serial.printf("ESP-NOW san sang · MAC master = %s · kenh %d\n",
                  WiFi.macAddress().c_str(), WiFi.channel());
  } else {
    Serial.println("ESP-NOW KHOI TAO LOI — se khong nhan duoc so lieu tu node ngoai troi");
  }

  // UART tới UNO Q. Thứ tự không còn quan trọng như thời BLE (nó không đụng
  // radio), nhưng giữ ở đây để log khởi động đọc theo đúng chiều dữ liệu chảy.
  //
  // ĐƯỜNG NÀY CHỈ ĐỂ NÓI CHUYỆN VỚI ARDUINO UNO Q, không liên quan gì tới cảm
  // biến — số đo phòng đi ESP-NOW. Nên hỏng nó thì mất lớp edge AI, KHÔNG mất
  // nhiệt độ trong nhà; đó là lý do dòng dưới không chặn khởi động.
  UnoQLink::begin(ORG_ID);
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
  Serial.println("Bo nay KHONG co cam bien nhiet/am — so \"trong nha\" la trung vi "
                 "cua cac node goc phong (xem room-registry.h)");
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
  if (c.kind == Ui::Command::FAN_SET) {
    // KHÔNG đụng overrideLocal, và KHÔNG publish `override`: mức quạt không mang
    // (chế độ, nhiệt độ) nên nó không tranh quyền với vòng lặp comfort. Cùng luật
    // đã áp cho gói `action:` đến từ app — xem cuối takeCommand().
    //
    // Cũng KHÔNG đi qua máy chủ: mã đã nằm trong NVS, nên bấm ở đây vẫn chạy khi
    // mất mạng. Đó là cả lý do panel giữ một bản.
    if (c.arg >= AcActions::FAN_COUNT) return;
    const char *wire = AcActions::fanWire(c.arg);
    const uint16_t n = IrStore::loadAlias(wire, -1, irBuf, IrIo::RAW_MAX);
    if (n == 0) {
      Serial.printf("[panel] quat %s: chua co ma trong NVS\n", wire);
      Ui::reply("CHƯA HỌC MÃ — vào app để học");
      return;
    }
    IrIo::blast(irBuf, n);
    lastFanIdx = c.arg;
    Serial.printf("[panel] da phat %u moc -> quat %s\n", n, wire);

    // Ghi vào nhật ký lệnh như mọi lệnh khác. `mode` giữ nguyên chế độ máy lạnh
    // đang chạy: mức quạt KHÔNG đổi chế độ, nên ghi "FAN_60" vào ô chế độ sẽ
    // dựng lên một trạng thái máy lạnh chưa từng có.
    Ui::CmdLog e{};
    copyStr(e.mode, sizeof(e.mode), actMode[0] ? actMode : "--");
    snprintf(e.reason, sizeof(e.reason), "panel:%s", wire);
    e.setpoint = actSetpoint;
    e.result   = Ui::CmdLog::SENT;
    Ui::logCommand(e);
    return;
  }

  if (c.kind == Ui::Command::HUMID_SET) {
    const uint32_t now = millis();
    if (c.arg == Ui::HUMID_AUTO_ARG) {
      HumidifierControl::backToAuto(now);
      Ui::reply("MÁY TẠO ẨM: TỰ ĐỘNG");
      return;
    }
    const bool want = (c.arg == Ui::HUMID_ON_ARG);
    HumidifierControl::manualSet(want, now);
    // Nói ra KẾT CỤC THẬT, không nói lại cái nút vừa bấm: manualSet() có thể
    // không bắn được vì chưa học mã, và khi đó máy không đổi gì cả. Báo "ĐÃ BẬT"
    // trong ca đó là màn khẳng định sai — đúng thứ luật NAN-chứ-không-0 của ui.h
    // ngăn cấm, chỉ ở tầng khác.
    const HumidifierControl::Status st = HumidifierControl::status(now);
    if (st.on == want) Ui::reply(want ? "MÁY TẠO ẨM: ĐÃ BẬT" : "MÁY TẠO ẨM: ĐÃ TẮT");
    else               Ui::reply("CHƯA HỌC MÃ — vào app để học");
    return;
  }

  if (c.kind == Ui::Command::AUTO) {
    overrideLocal = false;
    // Gỡ cổng ghi đè trên máy chủ, không chỉ đổi huy hiệu trên màn. Thiếu dòng
    // này thì THỦ CÔNG là đường một chiều: bấm một lần là hộ đó nằm ngoài vòng
    // comfort suốt `override_hours` (mặc định 2h) và panel không có cách nào gỡ.
    //
    // Gỡ ghi đè là việc CỦA CẢ HỘ chứ không riêng màn này, nên vẫn gửi dù cờ cục
    // bộ đang false — ghi đè có thể do app đặt, và người đứng ở tường bấm TỰ ĐỘNG
    // là đang nói "thôi, để máy chủ lo". Trả về sớm vì cờ false sẽ bỏ rơi đúng
    // trường hợp đó.
    const bool ok = mqtt.connected() &&
                    mqtt.publish(tOverride.c_str(), "{\"clear\":true}", false);
    Serial.printf("[panel] tra quyen ve cho may chu -> %s\n",
                  ok ? "da gui" : "GUI LOI (may chu se tu het han)");
    // KHÔNG gọi Ui::reply() ở đây: onAuto() đã hiện đúng toast đó ngay lúc bấm.
    // Gọi lại là vẽ lại cùng một câu -> nháy một cái vô nghĩa. Và không báo lỗi
    // ra màn khi gửi trượt: huy hiệu ĐÃ về TU DONG, đúng ở phía node, còn cổng
    // ghi đè phía máy chủ thì tự hết hạn — chậm, nhưng không sai.
    return;
  }

  if (c.kind == Ui::Command::RESYNC) {
    if (!mqtt.connected()) {
      Ui::reply("MẤT KẾT NỐI MÁY CHỦ");
      return;
    }
    // Cùng topic `state` với need_raw: đây vẫn là node nói về kho mã của chính
    // nó, chỉ khác ở chỗ xin CẢ KHO thay vì một mã. Không retain — yêu cầu xảy
    // ra một lần, retain thì mỗi lần node nối lại broker sẽ phát lại và máy chủ
    // đẩy nguyên kho mã xuống một cách vô cớ.
    const bool ok = mqtt.publish(tState.c_str(), "{\"resync\":true}", false);
    Serial.printf("[panel] xin lai toan bo kho ma -> %s\n", ok ? "da gui" : "GUI LOI");
    Ui::reply(ok ? "ĐANG XIN MÃ TỪ MÁY CHỦ..." : "GỬI YÊU CẦU THẤT BẠI");
    return;
  }

  if (c.kind == Ui::Command::DEL_CODE) {
    // KHÔNG đụng overrideLocal: xoá một mã không phải là ra lệnh cho máy lạnh,
    // nên nó không được đổi chuyện ai đang cầm lái.
    const bool gone = IrStore::removeAlias(c.mode, c.setpoint);
    if (gone) {
      aliasDirty = true;   // bảng "đã có mã" đổi -> màn phải tính lại
      Serial.printf("[panel] da xoa ma %s %d khoi NVS\n", c.mode, c.setpoint);
      // Nói rõ đây là việc LÙI ĐƯỢC. Mã vẫn nằm trong Postgres; lệnh kế tiếp cho
      // tổ hợp này rơi vào nhánh "có id, NVS rỗng" ở onCmdPacket() và nhánh đó
      // xin server gửi lại mảng. Không nói thì người dùng tưởng vừa xoá vĩnh
      // viễn và đi học lại bằng tay — thừa công.
      Ui::reply("ĐÃ XOÁ — sẽ tự nạp lại từ máy chủ");
    } else {
      Ui::reply("TỔ HỢP NÀY CHƯA CÓ MÃ");
    }
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
  // Vẫn TRUNG THỰC: huy hiệu "GHI ĐÈ" nói về Ý ĐỊNH, và ý định đó có thật ngay cả
  // khi mã IR chưa học. Còn ghi đè có SỐNG SÓT hay không thì hai câu toast ở cuối
  // hàm phân biệt rõ — chưa có mã thì không xin cổng ghi đè, nên máy chủ vẫn
  // giành lại, và màn nói đúng câu đó.
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
  // thấy ngay trạng thái mới (giờ kèm cả một nhịp realtime — state_handler đã
  // gọi live_events.publish_change, app không phải chờ tick telemetry nữa).
  pending.reqId[0] = '\0';
  copyStr(pending.mode, sizeof(pending.mode), c.mode);
  pending.setpoint = c.setpoint;
  publishState();

  // Rồi XIN ĐẶT CỔNG GHI ĐÈ — nửa còn lại, và là nửa làm cho ghi đè sống sót.
  //
  // ĐẶT SAU nhánh "chưa có mã" ở trên, CỐ Ý: chặn vòng lặp comfort trong lúc
  // node không bắn nổi khung nào là để máy lạnh nằm im ở trạng thái cũ suốt
  // `override_hours`. Chưa phát được thì thà để máy chủ tiếp tục thử — nó có thể
  // chọn một nhiệt độ khác mà node CÓ mã. "Giành quyền" chỉ có nghĩa khi kèm
  // được năng lực thi hành.
  //
  // Không retain (tham số cuối = false): đây là một Ý ĐỊNH xảy ra một lần, không
  // phải trạng thái. Retain thì mỗi lần node nối lại broker phát lại và ghi đè tự
  // bật lại vĩnh viễn — cùng lý do đã ghi ở buildTopics() và ở nhánh resync.
  JsonDocument ov;
  ov["mode"]     = c.mode;
  ov["setpoint"] = c.setpoint;
  char ovBuf[64];
  const size_t ovLen = serializeJson(ov, ovBuf);
  const bool ovOk = mqtt.connected() &&
                    mqtt.publish(tOverride.c_str(), (const uint8_t *)ovBuf, ovLen, false);
  Serial.printf("[panel] xin dat ghi de %s %d -> %s\n",
                c.mode, c.setpoint, ovOk ? "da gui" : "GUI LOI");

  // Nói đúng chuyện vừa xảy ra, hai câu khác nhau cho hai kết cục khác nhau.
  // Câu cũ ("máy chủ sẽ giành lại quyền") giờ SAI khi gửi được — cổng ghi đè đã
  // đặt xong thì máy chủ không giành lại nữa cho tới khi hết hạn hoặc bấm TỰ ĐỘNG.
  Ui::reply(ovOk ? "ĐÃ GỬI"
                 : "ĐÃ PHÁT");  
}

// ---------------------------------------------------------------------------
//  Arduino UNO Q (edge AI)
// ---------------------------------------------------------------------------
/// Đổi chuỗi chế độ của dự án ("COOL"...) sang mã 1 byte trên dây, và ngược lại.
/// Hai hàm nhỏ này là RANH GIỚI DUY NHẤT giữa hai cách biểu diễn — để rải
/// strcmp("COOL") khắp nơi là kiểu chỗ mà một lỗi gõ trở thành một chế độ sai.
static uint8_t modeToWire(const char *mode) {
  if (mode == nullptr || mode[0] == '\0') return AC_UNOQ_MODE_UNKNOWN;
  if (strcmp(mode, "OFF")  == 0) return AC_UNOQ_MODE_OFF;
  if (strcmp(mode, "COOL") == 0) return AC_UNOQ_MODE_COOL;
  if (strcmp(mode, "DRY")  == 0) return AC_UNOQ_MODE_DRY;
  if (strcmp(mode, "FAN")  == 0) return AC_UNOQ_MODE_FAN;
  return AC_UNOQ_MODE_UNKNOWN;
}

static const char *modeFromWire(uint8_t wire) {
  switch (wire) {
    case AC_UNOQ_MODE_OFF:  return "OFF";
    case AC_UNOQ_MODE_COOL: return "COOL";
    case AC_UNOQ_MODE_DRY:  return "DRY";
    case AC_UNOQ_MODE_FAN:  return "FAN";
    default:                return nullptr;   // nullptr = KHÔNG thi hành
  }
}

/// Nhịp đẩy ảnh chụp sang UNO Q (ms). Thưa hơn nhịp vẽ màn (200ms) rất nhiều:
/// UNO Q tính lại mỗi 30 giây, đẩy dày hơn chỉ tốn sóng BLE đang chia với WiFi.
static const uint32_t UNOQ_PUSH_MS = 5000;

/// Đóng gói mọi thứ UNO Q cần để tự quyết định, rồi notify.
///
/// `cloud_silence_sec` là trường quan trọng nhất: nó là thứ DUY NHẤT cho UNO Q
/// biết máy chủ còn sống hay không, và gateway biết chắc chắn hơn UNO Q vì chính
/// nó giữ phiên MQTT. Không có nó thì lớp dự phòng phải tự đoán, và đoán sai
/// theo chiều nào cũng tệ: giành lái sớm là hai bên tranh máy nén, giành muộn là
/// nhà nóng suốt thời gian mất mạng.
static void pushUnoQSnapshot() {
  static uint32_t lastPush = 0;
  if (millis() - lastPush < UNOQ_PUSH_MS) return;
  lastPush = millis();

  AcUnoQSnapshot snap{};
  snap.magic   = AC_UNOQ_MAGIC;
  snap.version = AC_UNOQ_VERSION;

  float tin = NAN, hin = NAN;
  uint8_t voting = 0;
  RoomRegistry::median(tin, hin, &voting);   // thất bại -> giữ NaN, mã hoá thành INVALID
  snap.room_count = voting;
  snap.t_in_c100  = acUnoQEncodeTemp(tin);
  snap.h_in_x100  = acUnoQEncodeRh(hin);
  snap.t_out_c100 = acUnoQEncodeTemp(lastSlaveT);
  snap.h_out_x100 = acUnoQEncodeRh(lastSlaveH);

  for (uint8_t i = 0; i < AC_UNOQ_MAX_ROOMS; i++) {
    const RoomRegistry::Room *r = RoomRegistry::at(i);
    const bool live = r != nullptr && RoomRegistry::online(i);
    snap.room_t_c100[i]  = live ? acUnoQEncodeTemp(r->t) : AC_UNOQ_T_INVALID;
    snap.room_h_x100[i]  = live ? acUnoQEncodeRh(r->h)   : AC_UNOQ_H_INVALID;
    snap.room_corner[i]  = r ? r->corner : AC_CORNER_NONE;
  }

  const bool outOnline =
      lastSlaveMs && (millis() - lastSlaveMs < SlaveWatch::SLAVE_TIMEOUT_MS);
  snap.flags = (uint8_t)((WiFi.status() == WL_CONNECTED ? AC_UNOQ_FLAG_WIFI_UP : 0) |
                         (mqtt.connected() ? AC_UNOQ_FLAG_MQTT_UP : 0) |
                         (overrideLocal ? AC_UNOQ_FLAG_OVERRIDE : 0) |
                         (outOnline ? AC_UNOQ_FLAG_OUT_ONLINE : 0));

  // lastCmdMs = 0 nghĩa là CHƯA TỪNG nghe máy chủ ra lệnh — khác hẳn "vừa nghe
  // xong". Gửi 0 cho cả hai ca sẽ khiến UNO Q tưởng cloud vừa nói và không bao
  // giờ giành lái ở một hộ mà cloud chưa từng hoạt động.
  const uint32_t silence = lastCmdMs ? (millis() - lastCmdMs) / 1000UL : 0xFFFFFFFFUL;
  snap.cloud_silence_sec =
      silence >= AC_UNOQ_SILENCE_NEVER ? AC_UNOQ_SILENCE_NEVER : (uint16_t)silence;

  snap.ac_mode     = modeToWire(actMode);
  snap.ac_setpoint = (int8_t)(actSetpoint >= 0 ? actSetpoint : -1);
  snap.uptime_min  = (uint16_t)(millis() / 60000UL);
  acUnoQSealSnapshot(&snap);

  UnoQLink::publish(snap);
  SerialTrace::snapshotOut(snap, UnoQLink::connected());
}

/// Thi hành (hoặc chỉ ghi nhận) thứ UNO Q vừa gửi sang.
static void runUnoQIncoming() {
  UnoQLink::Incoming in;
  if (!UnoQLink::poll(in)) return;

  const char *mode = modeFromWire(in.mode);
  if (mode == nullptr) {
    Serial.printf("[unoq] che do la (%u) — bo qua\n", in.mode);
    return;
  }

  if (!in.isCommand) {
    // ĐỀ XUẤT: ghi lại, KHÔNG bắn IR. Đây là ranh giới giữ cho mọi phép thử trên
    // UNO Q khỏi chạy thẳng vào máy nén — xem unoq-link.h §2.
    Serial.printf("[unoq] de xuat %s %d (chi ghi nhan, khong phat)\n", mode, in.setpoint);
    Ui::CmdLog entry{};
    copyStr(entry.mode,   sizeof(entry.mode),   mode);
    copyStr(entry.reason, sizeof(entry.reason), "edge advice");
    entry.setpoint = in.setpoint;
    entry.result   = Ui::CmdLog::NO_CODE;   // "không phát" — đúng chuyện đã xảy ra
    Ui::logCommand(entry);
    return;
  }

  // LỆNH THẬT — UNO Q đang cầm lái vì máy chủ im lặng.
  //
  // CỐ Ý KHÔNG ĐI QUA runPanelCommand(), dù đường đó đã sẵn và làm gần đúng
  // việc cần: nó đặt `overrideLocal` và xin máy chủ mở cổng GHI ĐÈ. Ghi đè là
  // để người dùng giành quyền KHỎI máy chủ; còn UNO Q thì đang ĐỨNG THAY máy
  // chủ. Đi nhầm đường đó thì lúc mạng về, máy chủ bị khoá ngoài suốt
  // `override_hours` (mặc định 2 giờ) bởi chính lớp dự phòng vừa cứu nó — và
  // trên màn hiện "GHI ĐÈ" trong khi không một ai bấm gì.
  Serial.printf("[unoq] LENH %s %d (edge dang cam lai)\n", mode, in.setpoint);

  const uint16_t n = IrStore::loadAlias(mode, aliasTemp(mode, in.setpoint),
                                        irBuf, IrIo::RAW_MAX);
  Ui::CmdLog entry{};
  copyStr(entry.mode,   sizeof(entry.mode),   mode);
  copyStr(entry.reason, sizeof(entry.reason), "edge takeover");
  entry.setpoint = in.setpoint;

  if (n == 0) {
    Serial.printf("[unoq] %s %d: chua hoc ma nay — khong phat\n", mode, in.setpoint);
    entry.result = Ui::CmdLog::NO_CODE;
    Ui::logCommand(entry);
    return;
  }

  IrIo::blast(irBuf, n);
  copyStr(actMode, sizeof(actMode), mode);   // trạng thái THẬT đã bắn ra máy lạnh
  actSetpoint = in.setpoint;
  entry.result = Ui::CmdLog::SENT;
  Ui::logCommand(entry);

  // Báo trạng thái mới lên cloud nếu còn đường — mất mạng thì thôi, chính vì
  // mất mạng nên UNO Q mới đang cầm lái. Không kèm `ack`: không có req_id nào
  // của máy chủ để khớp.
  pending.reqId[0] = '\0';
  copyStr(pending.mode, sizeof(pending.mode), mode);
  pending.setpoint = in.setpoint;
  if (mqtt.connected()) publishState();
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

  // Từng góc phòng, để màn nói được góc nào đang lệch — chứ không chỉ một con số
  // trung vị đã che mất chuyện đó. Đây chính là thứ biện minh cho bốn cảm biến.
  //
  // Số ô lấy từ SỐ GÓC ĐÃ NGHE THẤY, không từ một hằng số khai trước: gateway
  // không giữ danh sách node phòng nào cả (gói ESP-NOW tự mang uuid), nên lắp
  // thêm một góc là nó tự hiện lên màn mà không phải nạp lại firmware.
  m.roomSlots = RoomRegistry::knownCount() < Ui::Model::MAX_ROOMS
                    ? RoomRegistry::knownCount() : Ui::Model::MAX_ROOMS;
  for (uint8_t i = 0; i < m.roomSlots; i++) {
    const RoomRegistry::Room *r = RoomRegistry::at(i);
    m.roomOnline[i] = RoomRegistry::online(i);
    m.roomT[i] = (r && m.roomOnline[i]) ? r->t : NAN;
    m.roomH[i] = (r && m.roomOnline[i]) ? r->h : NAN;
    m.roomCorner[i] = r ? r->corner : AC_CORNER_NONE;
    m.roomAgeSec[i] = RoomRegistry::ageSec(i);
  }
  m.roomOnlineCount = RoomRegistry::onlineCount();
  // Không góc nào có số -> median() để nguyên m.tIn/m.hIn ở NAN mặc định của
  // Model, và màn hiện skeleton. KHÔNG được thay bằng 0.0 hay số cũ (ui.h §Model).
  RoomRegistry::median(m.tIn, m.hIn, &m.roomVoting);   // roomVoting = số góc CÓ SỐ ĐO
  m.unoqUp  = UnoQLink::connected();
  m.unoqRx  = UnoQLink::rxCount();

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
  static uint8_t  fanMask = 0;
  static bool     humidHasOn = false, humidHasOff = false;
  if (aliasDirty) {
    coolMask = 0;
    for (uint8_t i = 0; i < 15; i++) {
      if (IrStore::hasAlias("COOL", 16 + i)) coolMask |= (uint16_t)(1u << i);
    }
    hasDry = IrStore::hasAlias("DRY", -1);
    hasFan = IrStore::hasAlias("FAN", -1);
    hasOff = IrStore::hasAlias("OFF", -1);

    // Nút rời. CÙNG bộ đếm `aliasDirty` với ma trận trên, không thêm cờ thứ hai:
    // mọi đường ghi mã (học tại chỗ, backend gửi lệnh, resync) đều đã bật cờ đó,
    // nên tách ra chỉ tạo thêm một chỗ để quên bật.
    fanMask = 0;
    for (uint8_t i = 0; i < AcActions::FAN_COUNT; i++) {
      if (IrStore::hasAlias(AcActions::fanWire(i), -1)) fanMask |= (uint8_t)(1u << i);
    }
    humidHasOn  = IrStore::hasAlias(AcActions::humidWire(true), -1);
    humidHasOff = IrStore::hasAlias(AcActions::humidWire(false), -1);

    aliasDirty = false;
  }
  m.coolMask = coolMask;
  m.hasDry   = hasDry;
  m.hasFan   = hasFan;
  m.hasOff   = hasOff;
  m.fanMask  = fanMask;
  m.fanLast  = lastFanIdx;

  {
    const HumidifierControl::Status st = HumidifierControl::status(millis());
    m.humidOn       = st.on;
    m.humidOverride = st.overriding;
    m.humidRh       = st.rh;
    m.humidNote     = HumidifierControl::reasonText(st.reason);
    m.humidOverrideLeftSec = st.overrideLeftSec;
    m.humidHasOn    = humidHasOn;
    m.humidHasOff   = humidHasOff;
  }

  m.learning = IrIo::learning();
  copyStr(m.learnLabel, sizeof(m.learnLabel), learnLabel);
  m.learnRemainSec = IrIo::learnRemainingMs() / 1000;

  m.irCodeCount = IrStore::count();
  m.uptimeSec   = millis() / 1000;
  m.fw          = FW_VERSION;
  Ui::publish(m);
}

void loop() {
  // KHÔNG BAO GIỜ gọi connectWifi()/mqttTryConnect() thẳng ở đây — cả hai đều
  // chờ. serviceNetwork() thử lại theo nhịp rồi trả về ngay, để phần dưới luôn
  // chạy được kể cả khi mất mạng hoàn toàn. Xem chú thích của nó cho lý do.
  serviceNetwork();

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
  if (pending.needRawId[0]) {
    publishNeedRaw(pending.needRawId);
    // Xoá SAU khi gửi, và xoá dù gửi lỗi: mất mạng thì lệnh kế tiếp cho cùng tổ
    // hợp lại rơi vào đúng nhánh đó và đặt hàng lại. Giữ lại để thử gửi mãi thì
    // biến một yêu cầu một lần thành vòng lặp gửi mỗi vòng loop().
    pending.needRawId[0] = '\0';
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

  // Rút hàng đợi ESP-NOW: cả 4 góc phòng lẫn node ngoài trời đi chung đường này.
  // Callback chỉ chép gói, việc publish nằm ở đây — xem espnow-relay.h.
  EspNowRelay::poll(onSlavePacket);
  // Một hàm dọn hạn cho CẢ node ngoài trời lẫn các góc phòng: SlaveWatch khoá
  // theo device_uuid nên nó không cần biết node nào là loại gì.
  SlaveWatch::checkTimeouts(publishSlaveStatus);

  // Đề xuất/lệnh từ Arduino UNO Q. Rút ở đây chứ không thi hành trong callback
  // BLE — cùng luật đã áp cho callback MQTT và ESP-NOW (xem unoq-link.h §1).
  runUnoQIncoming();
  pushUnoQSnapshot();

  // --- Máy tạo độ ẩm: một lượt đo + quyết định ------------------------------
  //  NHỊP GIỮ Ở ĐÂY, không giấu trong module: EMA_ALPHA của nó gắn chặt với nhịp
  //  gọi (0.2 ở 5 giây = hằng số thời gian ~25 giây), nên cái nhịp đó phải nằm ở
  //  chỗ đọc được. Xem HumidifierControl::tick().
  //
  //  ĐỘ ẨM LÀ TRUNG VỊ CÁC GÓC, cùng con số mà màn hình và cloud dùng — không
  //  phải một phép đo riêng. Trung vị thất bại (chưa góc nào có số) thì `h` giữ
  //  nguyên NAN, và tick() hiểu đó là "chưa có số đo" rồi cắt máy sau
  //  SENSOR_STALE_SEC. Đúng nguyên tắc "nghi ngờ thì TẮT".
  {
    static uint32_t lastHumidMs = 0;
    const uint32_t nowMs = millis();
    if (nowMs - lastHumidMs >= HumidifierControl::TICK_MS) {
      lastHumidMs = nowMs;
      float ht = NAN, hh = NAN;
      uint8_t hv = 0;
      RoomRegistry::median(ht, hh, &hv);
      HumidifierControl::tick(hh, nowMs);
    }
  }

  // KHÔNG CÒN KHỐI PUBLISH TELEMETRY CỦA CHÍNH BO NÀY.
  //
  // Bo không có cảm biến nữa, và gửi số mượn của node góc phòng dưới tên gateway
  // là bịa ra một phép đo chưa từng xảy ra — đúng thứ luật "NAN chứ không 0" của
  // ui.h ngăn cấm, chỉ ở tầng khác. Trạng thái sống/chết của gateway đã có topic
  // `status` (retained + Last Will) lo, còn MAC đi kèm gói `state`.
  //
  // Số đo trong nhà lên cloud qua topic CỦA TỪNG GÓC, do publishRoomTelemetry()
  // gửi hộ; backend tự lấy trung vị (services/redis_room_state_service.py).

  unsigned long now = millis();
  if (lastPub != 0 && now - lastPub < TELEMETRY_MS) return;  // chưa tới nhịp tổng kết
  lastPub = now;

  float tin = NAN, hin = NAN;
  uint8_t voting = 0;
  const bool haveIndoor = RoomRegistry::median(tin, hin, &voting);
  if (haveIndoor) {
    Serial.printf("[trong nha] trung vi %.1f°C %.0f%% tu %u/%u goc · espnow nhan=%lu bo=%lu"
                  " · unoq=%s\n",
                  tin, hin, voting, RoomRegistry::knownCount(),
                  (unsigned long)EspNowRelay::receivedCount(),
                  (unsigned long)EspNowRelay::droppedCount(),
                  UnoQLink::connected() ? "da noi" : "chua noi");
  } else {
    // Ba ca hỏng khác hẳn nhau, và hai bộ đếm là thứ phân biệt được chúng:
    //   nhan=0            -> không nghe thấy ESP-NOW nào: sai WIFI_SSID ở node
    //                        phòng (nó bám nhầm kênh), hoặc node chưa bật
    //   nhan>0, 0 goc     -> nghe thấy nhưng toàn gói của node ngoài trời
    //   co goc, voting=0  -> các góc còn sống nhưng cảm biến đều hỏng (NaN)
    Serial.printf("[trong nha] CHUA CO SO DO — espnow nhan=%lu bo=%lu · "
                  "%u goc da biet, %u con song. Xem 3 ca trong main.cpp.\n",
                  (unsigned long)EspNowRelay::receivedCount(),
                  (unsigned long)EspNowRelay::droppedCount(),
                  RoomRegistry::knownCount(), RoomRegistry::onlineCount());
  }

  // Bảng Δ trung bình theo từng node. Bộ đếm tổng ở trên nói "có mất gói không";
  // bảng này nói "node NÀO đang mất" — hai câu hỏi khác nhau, và câu thứ hai mới
  // dẫn tới chỗ cần sửa.
  SerialTrace::summary();
}
