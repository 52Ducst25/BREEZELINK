// ============================================================================
//  BreezeLink — bản cài đặt KHÔNG MÀN HÌNH của giao diện gateway.
// ----------------------------------------------------------------------------
//  Bo ESP32-S3 không có màn cảm ứng của QR Box, nhưng ../esp32-indoor/src/main.cpp
//  được biên dịch nguyên vẹn (xem platformio.ini) nên nó vẫn gọi đủ 5 hàm của
//  namespace Ui. File này là bản thay thế: thay vì vẽ lên màn 2.8", nó in ra
//  serial.
//
//  VÌ SAO KHÔNG PHẢI 5 HÀM RỖNG:
//  panel treo tường tồn tại để trả lời một câu hỏi — "hệ này đang thấy gì?".
//  Bo S3 được lắp lên đúng vì câu hỏi đó đang không ai trả lời được (gateway thật
//  không nạp được). Năm hàm rỗng sẽ biên dịch qua và chạy được, nhưng lấy đi
//  đúng thứ khiến bo này đáng lắp. Nên `publish()` in một bảng tóm tắt định kỳ —
//  cùng nội dung mà màn hình sẽ hiện, xếp theo chiều dọc.
//
//  KHÔNG CÓ TÁC VỤ RIÊNG, KHÔNG CÓ MUTEX. Bản thật tách hẳn sang lõi 0 vì LVGL
//  vẽ chậm và IR phải giữ nhịp 38kHz ở lõi 1 (xem ui.h). Ở đây "vẽ" là vài dòng
//  printf mỗi 10 giây, rẻ hơn nhiều bậc so với một lần bắn IR — tách lõi chỉ tổ
//  thêm một nguồn sai khác giữa hai bo mà không đổi lấy được gì.
//
//  CHỮ IN RA CỐ Ý KHÔNG DẤU, theo đúng lệ của mọi log serial trong dự án: nhiều
//  cửa sổ serial monitor không dựng nổi UTF-8 và biến chữ có dấu thành rác, mà
//  log là thứ người ta đọc đúng lúc đang bí.
// ============================================================================
#include <Arduino.h>

#include "ui/ui.h"

#if defined(GATEWAY_ESPNOW_BEACON) && GATEWAY_ESPNOW_BEACON
// ---------------------------------------------------------------------------
//  ĐÈN HIỆU ESP-NOW — CHỈ ĐỂ TRUY LỖI, bật bằng -D GATEWAY_ESPNOW_BEACON=1
// ---------------------------------------------------------------------------
//  Gateway bình thường KHÔNG phát ESP-NOW, nó chỉ nghe. Nhưng khi gateway nghe
//  0 gói thì có hai khả năng không phân biệt được từ phía nó: node không bức xạ,
//  hay gateway không thu. Cho gateway PHÁT rồi để node góc nghe là đảo chiều
//  đường truyền — nghe được nghĩa là sóng giữa hai bo thông, và lỗi nằm ở chiều
//  phát của node.
//
//  Nằm trong file này chứ không trong main.cpp dùng chung: đây là đồ nghề của
//  riêng bo S3, không được lẫn vào firmware bo treo tường.
#include <WiFi.h>
#include <esp_now.h>

#include "espnow-message.h"

static void espNowBeaconTick() {
  static uint32_t last = 0;
  static int8_t   peerState = -1;   // -1 chưa thử, 0 lỗi, 1 sẵn sàng
  if (millis() - last < 2000) return;
  last = millis();

  uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if (peerState < 0) {
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, bcast, 6);
    p.channel = 0;            // 0 = dùng kênh hiện tại của giao diện
    p.encrypt = false;
    p.ifidx   = WIFI_IF_STA;
    const esp_err_t e = esp_now_add_peer(&p);
    peerState = (e == ESP_OK) ? 1 : 0;
    Serial.printf("[beacon] them peer quang ba: %s\n", esp_err_to_name(e));
  }
  if (peerState != 1) return;

  AcEspNowPacket pkt;
  acEspNowFill(&pkt, "0123456789abcdef0123456789abcdef", 11.1f, 22.2f, AC_NODE_ROOM, 9);
  const esp_err_t e = esp_now_send(bcast, (const uint8_t *)&pkt, sizeof(pkt));
  Serial.printf("[beacon] phat %u byte tren kenh %d -> %s\n",
                (unsigned)sizeof(pkt), WiFi.channel(), esp_err_to_name(e));
}
#endif

namespace Ui {

/// Nhịp in bảng tóm tắt (ms).
///
/// Màn thật vẽ lại mỗi 200ms vì mắt người nhìn liên tục; serial thì ngược lại —
/// in dày là cuộn trôi mất những dòng đáng đọc ([cmd], [relay], [unoq]), vốn mới
/// là thứ nói cho ta biết luồng dữ liệu có chạy hay không.
static const uint32_t STATUS_PERIOD_MS = 10000UL;

static const char *onOff(bool v, const char *yes, const char *no) { return v ? yes : no; }

/// In một số đo, hoặc "--" nếu chưa có. NAN phải hiện là KHÔNG CÓ SỐ, không phải
/// 0.0 — cùng luật đã áp cho màn hình và cho app (xem Ui::Model).
static void printReading(float t, float h) {
  if (isnan(t) || isnan(h)) Serial.print("  --        ");
  else                      Serial.printf("  %5.1fC %3.0f%%", t, h);
}

bool begin() {
  Serial.println("[ui] ban KHONG MAN HINH — bang tom tat in ra serial moi 10s");
  return true;   // không có chip cảm ứng để dò, và không có gì hỏng được
}

void publish(const Model &m) {
#if defined(GATEWAY_ESPNOW_BEACON) && GATEWAY_ESPNOW_BEACON
  // Bám vào publish() vì nó được main.cpp gọi mỗi vòng loop(), tức đúng tác vụ
  // sở hữu WiFi/ESP-NOW. Tự đếm nhịp 2s bên trong nên không phụ thuộc nhịp in.
  espNowBeaconTick();
#endif
  static uint32_t lastPrint = 0;
  // In ngay lần đầu (lastPrint == 0) để người vừa nạp xong thấy được cái gì đó
  // trước khi phải chờ hết một chu kỳ.
  if (lastPrint != 0 && millis() - lastPrint < STATUS_PERIOD_MS) return;
  lastPrint = millis();

  Serial.println();
  Serial.printf("=== GATEWAY (S3, khong man) · up %lum%02lus · fw %s ===\n",
                (unsigned long)(m.uptimeSec / 60), (unsigned long)(m.uptimeSec % 60),
                m.fw ? m.fw : "?");

  Serial.printf("  WiFi %s", onOff(m.wifiUp, "NOI", "MAT"));
  if (m.wifiUp) {
    Serial.printf("  \"%s\" kenh %u  %d dBm  ip=%s  mac=%s",
                  m.ssid, m.channel, m.rssi, m.ip, m.mac);
  }
  Serial.printf("  |  MQTT %s\n", onOff(m.mqttUp, "NOI", "MAT"));

  // --- Số đo trong nhà: đây là thứ cả hệ xoay quanh ---------------------------
  // roomSlots = số góc ĐÃ TỪNG nghe thấy; roomOnlineCount = còn tươi;
  // roomVoting = thật sự có số đo và được tính vào trung vị. Ba con số khác nhau,
  // và phân biệt được chúng chính là cách biết node nào chết với node nào sống mà
  // cảm biến hỏng.
  if (m.roomVoting > 0) {
    Serial.printf("  Phong: %u goc biet, %u tuoi, %u phieu -> TRUNG VI %.1fC %.0f%%\n",
                  m.roomSlots, m.roomOnlineCount, m.roomVoting, m.tIn, m.hIn);
  } else {
    Serial.printf("  Phong: %u goc biet, %u tuoi, 0 phieu -> CHUA CO SO DO TRONG NHA\n",
                  m.roomSlots, m.roomOnlineCount);
  }
  for (uint8_t i = 0; i < m.roomSlots && i < Model::MAX_ROOMS; i++) {
    // corner 0xFF = node không khai nhãn góc (gói ESP-NOW v1 hoặc node cũ).
    if (m.roomCorner[i] == 0xFF) Serial.printf("    goc ?  ");
    else                         Serial.printf("    goc %u  ", m.roomCorner[i]);
    printReading(m.roomT[i], m.roomH[i]);
    Serial.printf("  %lus%s\n", (unsigned long)m.roomAgeSec[i],
                  m.roomOnline[i] ? "" : "  <== MAT KET NOI");
  }

  Serial.print("  Ngoai troi");
  printReading(m.tOut, m.hOut);
  Serial.printf("  %lus%s\n", (unsigned long)m.outAgeSec,
                m.outOnline ? "" : "  <== MAT KET NOI");

  // --- Máy lạnh + ai đang cầm lái ---------------------------------------------
  Serial.printf("  May lanh %s", m.mode[0] ? m.mode : "chua biet");
  if (m.setpoint >= 0) Serial.printf(" %d", m.setpoint);
  Serial.printf("  ·  %s", m.overrideLocal ? "GHI DE (nguoi dung cam lai)" : "tu dong");
  if (m.lastCmdSec) Serial.printf("  ·  lenh may chu cuoi: %lus truoc", (unsigned long)m.lastCmdSec);
  else              Serial.print("  ·  may chu CHUA TUNG ra lenh");
  Serial.println();

  if (m.learning) {
    Serial.printf("  DANG HOC \"%s\" — con %lus, huong remote vao mat thu (GPIO%d)\n",
                  m.learnLabel, (unsigned long)m.learnRemainSec, IR_RX_PIN);
  }

  Serial.printf("  UNO Q %s (da nhan %lu)  |  ESP-NOW nhan %lu, rot %lu  |  ma IR trong NVS: %u\n",
                onOff(m.unoqUp, "da noi", "chua noi"), (unsigned long)m.unoqRx,
                (unsigned long)m.espnowRx, (unsigned long)m.espnowDrop, m.irCodeCount);
}

/// Bo này không có nút bấm nào -> không bao giờ có lệnh tại chỗ.
///
/// Hệ quả cần biết khi đọc log: sẽ KHÔNG bao giờ thấy "manual override" hay
/// "resync" phát ra từ bo này, và cờ GHI DE ở trên chỉ có thể bật do app hoặc
/// web đặt. Đó là hành vi đúng, không phải thiếu sót.
bool pollCommand(Command &out) {
  (void)out;
  return false;
}

void reply(const char *msg) {
  Serial.printf("[ui] %s\n", msg ? msg : "xong");
}

void logCommand(const CmdLog &e) {
  const char *result = "?";
  switch (e.result) {
    case CmdLog::SENT:      result = "DA PHAT";                       break;
    case CmdLog::NO_CODE:   result = "CHUA HOC MA — khong phat";      break;
    case CmdLog::NEED_RAW:  result = "NVS rong — dang xin lai ma";    break;
    case CmdLog::DUPLICATE: result = "TRUNG req_id — bo, chi ack";    break;
  }
  Serial.printf("[log] %s", e.mode[0] ? e.mode : "?");
  if (e.setpoint >= 0) Serial.printf(" %d", e.setpoint);
  Serial.printf("  (%s)  -> %s\n", e.reason[0] ? e.reason : "-", result);
}

} // namespace Ui
