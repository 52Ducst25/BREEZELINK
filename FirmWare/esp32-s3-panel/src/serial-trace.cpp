#include "serial-trace.h"

#if defined(GATEWAY_TRACE) && GATEWAY_TRACE

#include <string.h>

namespace SerialTrace {
namespace {

/// Bao nhiêu node theo dõi cùng lúc. Bằng SlaveWatch::MAX_SLAVES — cùng một tập
/// node, và để lệch nhau thì bảng này sẽ im lặng bỏ qua đúng node mà bên kia
/// đang theo dõi.
const uint8_t MAX_NODES = 8;

struct NodeStat {
  char     uuid[33];
  uint32_t count;
  uint32_t lastMs;
  uint32_t sumGapMs;   // để tính Δ trung bình; bỏ qua gói đầu (không có Δ)
};

NodeStat g_nodes[MAX_NODES];
uint8_t  g_used = 0;

NodeStat *slotFor(const char *uuid) {
  for (uint8_t i = 0; i < g_used; i++) {
    if (strncmp(g_nodes[i].uuid, uuid, sizeof(g_nodes[i].uuid) - 1) == 0) return &g_nodes[i];
  }
  if (g_used >= MAX_NODES) return nullptr;
  NodeStat *s = &g_nodes[g_used++];
  strncpy(s->uuid, uuid, sizeof(s->uuid) - 1);
  s->uuid[sizeof(s->uuid) - 1] = '\0';
  s->count = 0;
  s->lastMs = 0;
  s->sumGapMs = 0;
  return s;
}

/// 6 ký tự cuối của uuid — đủ để phân biệt bằng mắt mà không chiếm nửa dòng.
/// In cả 32 ký tự thì mỗi dòng chỉ còn chỗ cho một con số.
const char *shortUuid(const char *uuid) {
  const size_t n = strlen(uuid);
  return n > 6 ? uuid + n - 6 : uuid;
}

void printValue(float v, const char *unit) {
  if (isnan(v)) Serial.printf(" %-6s", "--");
  else          Serial.printf(" %5.1f%s", v, unit);
}

} // namespace

void packetIn(const AcEspNowPacket &pkt, const uint8_t mac[6]) {
  NodeStat *s = slotFor(pkt.device_uuid);
  const uint32_t now = millis();

  Serial.printf("[rx] %02X:%02X:%02X:%02X:%02X:%02X ", mac[0], mac[1], mac[2],
                mac[3], mac[4], mac[5]);

  if (pkt.node_kind == AC_NODE_ROOM) {
    if (pkt.corner == AC_CORNER_NONE) Serial.print("room  goc?  ");
    else                              Serial.printf("room  goc%-2u ", pkt.corner);
  } else {
    Serial.print("ngoai troi  ");
  }
  Serial.printf("…%s v%u", shortUuid(pkt.device_uuid), pkt.version);

  printValue(pkt.temp, "C");
  printValue(pkt.humidity, "%");

  if (s == nullptr) {
    Serial.println("  (bang day, khong theo doi duoc)");
    return;
  }

  s->count++;
  if (s->lastMs != 0) {
    const uint32_t gap = now - s->lastMs;
    s->sumGapMs += gap;
    // Δ là thứ đáng nhìn nhất dòng này: node phát mỗi 5s mà Δ=15.0s là rơi 2 gói.
    Serial.printf("  #%-5lu Δ%.1fs", (unsigned long)s->count, gap / 1000.0f);
  } else {
    Serial.printf("  #%-5lu (goi dau)", (unsigned long)s->count);
  }
  s->lastMs = now;
  Serial.println();
}

void mqttOut(const char *topic, const uint8_t *payload, size_t len, bool ok) {
  // Chỉ in phần đuôi topic: tiền tố `bl/<org>/<uuid>/` dài 70 ký tự và giống hệt
  // nhau ở mọi dòng, nên nó chỉ đẩy phần đáng đọc ra khỏi màn hình.
  const char *tail = strrchr(topic, '/');
  Serial.printf("[tx mqtt] %-10s %3uB %s  ", tail ? tail + 1 : topic,
                (unsigned)len, ok ? "OK " : "LOI");
  for (size_t i = 0; i < len && i < 120; i++) Serial.write((char)payload[i]);
  if (len > 120) Serial.print(" …");
  Serial.println();
}

void mqttIn(const char *topic, const uint8_t *payload, size_t len) {
  const char *tail = strrchr(topic, '/');
  Serial.printf("[rx mqtt] %-10s %4uB  ", tail ? tail + 1 : topic, (unsigned)len);
  // CẮT NGẮN CÓ CHỦ ĐÍCH: lệnh mang `ir_raw` vài trăm mốc thời gian (~vài KB).
  // In đủ thì đúng cái dòng cần đọc bị đẩy khỏi màn hình.
  for (size_t i = 0; i < len && i < 160; i++) Serial.write((char)payload[i]);
  if (len > 160) Serial.printf(" … (+%u byte)", (unsigned)(len - 160));
  Serial.println();
}

void snapshotOut(const AcUnoQSnapshot &snap, bool linkUp) {
  // Không có ai nối thì notify rơi vào hư không — nói ra, đừng để người đọc tưởng
  // ảnh chụp đã sang tới UNO Q.
  Serial.printf("[tx unoq] %s  phong=%u", linkUp ? "da noi " : "CHUA NOI", snap.room_count);
  if (snap.t_in_c100 == AC_UNOQ_T_INVALID) Serial.print("  t_in=--");
  else Serial.printf("  t_in=%.1fC", snap.t_in_c100 / 100.0f);
  if (snap.t_out_c100 == AC_UNOQ_T_INVALID) Serial.print("  t_out=--");
  else Serial.printf("  t_out=%.1fC", snap.t_out_c100 / 100.0f);
  Serial.printf("  co=0x%02X  im lang=%us  may lanh=%u/%d\n", snap.flags,
                snap.cloud_silence_sec, snap.ac_mode, snap.ac_setpoint);
}

void summary() {
  Serial.println("[trace] node                 goi     Δ trung binh   lan cuoi");
  const uint32_t now = millis();
  for (uint8_t i = 0; i < g_used; i++) {
    const NodeStat &s = g_nodes[i];
    const float avg = s.count > 1 ? (s.sumGapMs / 1000.0f) / (s.count - 1) : 0.0f;
    Serial.printf("        …%-6s %10lu   %8.1fs   %6.1fs truoc\n", shortUuid(s.uuid),
                  (unsigned long)s.count, avg, (now - s.lastMs) / 1000.0f);
  }
}

} // namespace SerialTrace

#endif
