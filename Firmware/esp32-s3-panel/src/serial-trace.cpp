#include "serial-trace.h"

#if defined(GATEWAY_TRACE) && GATEWAY_TRACE

#include <string.h>

namespace SerialTrace {
namespace {

/// How many nodes to track at once. Equal to SlaveWatch::MAX_SLAVES -- the same set
/// of nodes, and letting the two diverge would make this table silently skip the
/// very node the other side is tracking.
const uint8_t MAX_NODES = 8;

struct NodeStat {
  char     uuid[33];
  uint32_t count;
  uint32_t lastMs;
  uint32_t sumGapMs;   // for the average delta; the first packet is skipped (no delta)
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

/// The last 6 characters of the uuid -- enough to tell them apart by eye without
/// taking up half the line. Printing all 32 leaves room for one number per line.
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
    if (pkt.corner == AC_CORNER_NONE) Serial.print("room  corner? ");
    else                              Serial.printf("room  corner%-2u ", pkt.corner);
  } else {
    Serial.print("outdoor       ");
  }
  Serial.printf("...%s v%u", shortUuid(pkt.device_uuid), pkt.version);

  printValue(pkt.temp, "C");
  printValue(pkt.humidity, "%");

  if (s == nullptr) {
    Serial.println("  (table full, cannot track)");
    return;
  }

  s->count++;
  if (s->lastMs != 0) {
    const uint32_t gap = now - s->lastMs;
    s->sumGapMs += gap;
    // The delta is the most informative thing on this line: a node transmitting
    // every 5s with delta=15.0s has lost 2 packets.
    Serial.printf("  #%-5lu d%.1fs", (unsigned long)s->count, gap / 1000.0f);
  } else {
    Serial.printf("  #%-5lu (first packet)", (unsigned long)s->count);
  }
  s->lastMs = now;
  Serial.println();
}

void mqttOut(const char *topic, const uint8_t *payload, size_t len, bool ok) {
  // Print only the tail of the topic: the `bl/<org>/<uuid>/` prefix is 70 characters
  // long and identical on every line, so all it does is push the readable part off
  // the screen.
  const char *tail = strrchr(topic, '/');
  Serial.printf("[tx mqtt] %-10s %3uB %s  ", tail ? tail + 1 : topic,
                (unsigned)len, ok ? "OK " : "ERR");
  for (size_t i = 0; i < len && i < 120; i++) Serial.write((char)payload[i]);
  if (len > 120) Serial.print(" ...");
  Serial.println();
}

void mqttIn(const char *topic, const uint8_t *payload, size_t len) {
  const char *tail = strrchr(topic, '/');
  Serial.printf("[rx mqtt] %-10s %4uB  ", tail ? tail + 1 : topic, (unsigned)len);
  // TRUNCATED DELIBERATELY: a command carries an `ir_raw` of several hundred
  // timings (~a few KB). Printing it in full pushes the very line you need to read
  // off the screen.
  for (size_t i = 0; i < len && i < 160; i++) Serial.write((char)payload[i]);
  if (len > 160) Serial.printf(" ... (+%u bytes)", (unsigned)(len - 160));
  Serial.println();
}

void snapshotOut(const AcUnoQSnapshot &snap, bool linkUp) {
  // With nobody connected the notify goes nowhere -- say so, rather than letting the
  // reader assume the snapshot reached the UNO Q.
  Serial.printf("[tx unoq] %s  rooms=%u", linkUp ? "connected   " : "NOT CONNECTED", snap.room_count);
  if (snap.t_in_c100 == AC_UNOQ_T_INVALID) Serial.print("  t_in=--");
  else Serial.printf("  t_in=%.1fC", snap.t_in_c100 / 100.0f);
  if (snap.t_out_c100 == AC_UNOQ_T_INVALID) Serial.print("  t_out=--");
  else Serial.printf("  t_out=%.1fC", snap.t_out_c100 / 100.0f);
  Serial.printf("  flags=0x%02X  silence=%us  ac=%u/%d\n", snap.flags,
                snap.cloud_silence_sec, snap.ac_mode, snap.ac_setpoint);
}

void summary() {
  Serial.println("[trace] node               packets   avg delta      last seen");
  const uint32_t now = millis();
  for (uint8_t i = 0; i < g_used; i++) {
    const NodeStat &s = g_nodes[i];
    const float avg = s.count > 1 ? (s.sumGapMs / 1000.0f) / (s.count - 1) : 0.0f;
    Serial.printf("        ...%-6s %10lu   %8.1fs   %6.1fs ago\n", shortUuid(s.uuid),
                  (unsigned long)s.count, avg, (now - s.lastMs) / 1000.0f);
  }
}

} // namespace SerialTrace

#endif
