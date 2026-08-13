#include "unoq-link.h"

#include <string.h>

// Chân UART tới UNO Q. Khai trong platformio.ini vì chúng khác nhau giữa bo QR
// Box (ESP32 classic) và bo ESP32-S3 — cùng chỗ với các cờ chân khác.
#ifndef UNOQ_TX_PIN
#define UNOQ_TX_PIN 17
#endif
#ifndef UNOQ_RX_PIN
#define UNOQ_RX_PIN 18
#endif

// 115200 là dư: ảnh chụp 39 byte mỗi 5 giây = 62 byte/giây. Không nâng lên cao
// hơn — dây đi giữa hai bo có thể dài vài chục cm không bọc giáp, và tốc độ cao
// đổi lấy tỉ lệ lỗi bit chứ không đổi lấy gì cả ở lưu lượng này.
#ifndef UNOQ_BAUD
#define UNOQ_BAUD 115200
#endif

// UART1, KHÔNG PHẢI Serial (UART0). UART0 là console gỡ lỗi — dùng chung một
// cổng cho cả log lẫn dữ liệu nhị phân thì log biến thành rác và gói biến thành
// log. Đã có sẵn một bài học cùng loại ở IR_TX_PIN: chọn nhầm chân là hỏng câm.
#define UNOQ_SERIAL Serial1

namespace UnoQLink {
namespace {

uint32_t g_linkKey = 0;
bool     g_started = false;
uint32_t g_rx = 0;
uint32_t g_rejected = 0;
uint32_t g_lastHeardMs = 0;
uint16_t g_lastSeq = 0;
bool     g_haveSeq = false;

/// Bao lâu không nghe thấy gói hợp lệ thì coi là UNO Q đã im (ms).
///
/// UNO Q gửi mỗi 30 giây (nhịp tính của nó), nên 90s cho phép rơi hai nhịp liên
/// tiếp. Đặt sát hơn thì log nhấp nháy "đã nối/chưa nối" mà chẳng có gì hỏng —
/// cùng cái bẫy đã gặp với SlaveWatch khi nhịp tim 15s gặp ngưỡng 20s.
const uint32_t SILENT_AFTER_MS = 90000UL;

/// Đệm gom byte cho tới khi đủ một khung lệnh.
uint8_t g_buf[sizeof(AcUnoQCommandHeader)];
uint8_t g_len = 0;

/// Có gói vừa bóc xong chờ poll() rút.
bool     g_hasIncoming = false;
Incoming g_incoming;

/// Bóc một khung đã đủ byte. Trả false nếu không hợp lệ (bên gọi tự trượt đệm).
bool decodeFrame(const uint8_t *raw) {
  AcUnoQCommandHeader hdr;
  memcpy(&hdr, raw, sizeof(hdr));

  if (hdr.magic != AC_UNOQ_MAGIC || hdr.version != AC_UNOQ_VERSION) return false;
  if (!acUnoQCheckCommand(&hdr)) return false;

  // SAI KHOÁ LÀ VỨT, KHÔNG PHẢI CẢNH BÁO RỒI VẪN LÀM. Một bo UNO Q của hộ khác
  // (hoặc cắm nhầm dây) không được lái máy lạnh nhà này.
  if (hdr.link_key != g_linkKey) {
    Serial.printf("[unoq] tu choi goi sai link_key (%08X, cho %08X)\n",
                  (unsigned)hdr.link_key, (unsigned)g_linkKey);
    return false;
  }

  // Trùng seq = UNO Q gửi lại vì tưởng gói trước rơi. Thi hành hai lần là bấm
  // remote hai lần; với nút xoay vòng thì lần hai nhảy sang nấc khác.
  if (g_haveSeq && hdr.seq == g_lastSeq) return false;
  g_lastSeq = hdr.seq;
  g_haveSeq = true;

  g_incoming.isCommand = (hdr.kind == AC_UNOQ_KIND_COMMAND);
  g_incoming.mode      = hdr.mode;
  g_incoming.setpoint  = hdr.setpoint;
  g_incoming.seq       = hdr.seq;
  g_hasIncoming = true;
  return true;
}

} // namespace

bool begin(const char *orgId) {
  g_linkKey = acUnoQLinkKey(orgId);
  UNOQ_SERIAL.begin(UNOQ_BAUD, SERIAL_8N1, UNOQ_RX_PIN, UNOQ_TX_PIN);
  g_started = true;
  Serial.printf("[unoq] UART san sang · TX=GPIO%d RX=GPIO%d @%d · link_key=%08X\n",
                UNOQ_TX_PIN, UNOQ_RX_PIN, UNOQ_BAUD, (unsigned)g_linkKey);
  return true;
}

void publish(const AcUnoQSnapshot &snapshot) {
  if (!g_started) return;
  UNOQ_SERIAL.write((const uint8_t *)&snapshot, sizeof(snapshot));
}

bool poll(Incoming &out) {
  if (!g_started) return false;

  while (UNOQ_SERIAL.available() > 0) {
    const uint8_t b = (uint8_t)UNOQ_SERIAL.read();

    // ĐỒNG BỘ KHUNG BẰNG MAGIC, KHÔNG CẦN BYTE PHÂN CÁCH RIÊNG. Gói có kích
    // thước cố định, mở đầu bằng magic và kết bằng CRC — nên chỉ cần chờ thấy
    // magic rồi gom đủ byte, sai thì trượt một byte và tìm lại. Nhờ vậy nhiễu
    // trên dây hay cắm dây giữa chừng đều tự phục hồi, không cần ai reset.
    if (g_len == 0 && b != AC_UNOQ_MAGIC) continue;

    g_buf[g_len++] = b;
    if (g_len < sizeof(g_buf)) continue;

    if (decodeFrame(g_buf)) {
      g_len = 0;
      g_rx++;
      g_lastHeardMs = millis();
    } else {
      g_rejected++;
      // Trượt MỘT byte rồi tìm magic tiếp, không xoá sạch đệm: magic thật có thể
      // nằm ngay trong đám byte vừa gom (ví dụ nửa gói cũ dính nửa gói mới).
      // Xoá sạch thì mất luôn khung tốt đứng ngay sau khung hỏng.
      memmove(g_buf, g_buf + 1, sizeof(g_buf) - 1);
      g_len = sizeof(g_buf) - 1;
      while (g_len > 0 && g_buf[0] != AC_UNOQ_MAGIC) {
        memmove(g_buf, g_buf + 1, g_len - 1);
        g_len--;
      }
    }
  }

  if (!g_hasIncoming) return false;
  out = g_incoming;
  g_hasIncoming = false;
  return true;
}

bool connected() {
  return g_lastHeardMs != 0 && (millis() - g_lastHeardMs) < SILENT_AFTER_MS;
}

uint32_t rxCount()       { return g_rx; }
uint32_t rejectedCount() { return g_rejected; }

} // namespace UnoQLink
