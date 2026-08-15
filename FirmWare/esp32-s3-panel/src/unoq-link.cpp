#include "unoq-link.h"

#include <string.h>

// The UART pins to the UNO Q. Declared in platformio.ini because they differ
// between the QR Box board (classic ESP32) and the ESP32-S3 board -- alongside the
// other pin flags.
#ifndef UNOQ_TX_PIN
#define UNOQ_TX_PIN 17
#endif
#ifndef UNOQ_RX_PIN
#define UNOQ_RX_PIN 18
#endif

// 115200 is more than enough: a 39-byte snapshot every 5 seconds = 62 bytes/second.
// Do not raise it -- the wire between the two boards can be tens of centimetres of
// unshielded cable, and a higher rate trades bit error rate for nothing at all at
// this throughput.
#ifndef UNOQ_BAUD
#define UNOQ_BAUD 115200
#endif

// UART1, NOT Serial (UART0). UART0 is the debug console -- sharing one port for both
// the log and binary data turns the log into garbage and the packets into log. There
// is already a lesson of the same kind at IR_TX_PIN: picking the wrong pin fails
// silently.
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

/// How long without a valid packet before the UNO Q counts as silent (ms).
///
/// The UNO Q sends every 30 seconds (its own computation cadence), so 90s tolerates
/// two consecutive misses. Setting it tighter makes the log flicker
/// "connected/disconnected" with nothing actually wrong -- the same trap already hit
/// with SlaveWatch when a 15s heartbeat met a 20s threshold.
const uint32_t SILENT_AFTER_MS = 90000UL;

/// Buffer accumulating bytes until a full command frame is present.
uint8_t g_buf[sizeof(AcUnoQCommandHeader)];
uint8_t g_len = 0;

/// A packet has just been decoded and is waiting for poll() to collect it.
bool     g_hasIncoming = false;
Incoming g_incoming;

/// Decode a frame once enough bytes are present. Returns false if it is invalid
/// (the caller slides the buffer itself).
bool decodeFrame(const uint8_t *raw) {
  AcUnoQCommandHeader hdr;
  memcpy(&hdr, raw, sizeof(hdr));

  if (hdr.magic != AC_UNOQ_MAGIC || hdr.version != AC_UNOQ_VERSION) return false;
  if (!acUnoQCheckCommand(&hdr)) return false;

  // A WRONG KEY MEANS DISCARD, not warn-and-do-it-anyway. Another household's UNO Q
  // board (or a miswired cable) must not be allowed to drive this house's air
  // conditioner.
  if (hdr.link_key != g_linkKey) {
    Serial.printf("[unoq] rejected packet with wrong link_key (%08X, expected %08X)\n",
                  (unsigned)hdr.link_key, (unsigned)g_linkKey);
    return false;
  }

  // A duplicate seq means the UNO Q resent because it thought the previous packet
  // was lost. Executing it twice is pressing the remote twice; with a cycle button
  // the second press steps to a different level.
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
  Serial.printf("[unoq] UART ready - TX=GPIO%d RX=GPIO%d @%d - link_key=%08X\n",
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

    // FRAME SYNC VIA THE MAGIC BYTE, WITH NO SEPARATE DELIMITER. Packets are fixed
    // size, start with the magic byte and end with a CRC -- so it is enough to wait
    // for the magic, collect the full length, and on a mismatch slide one byte and
    // resync. That way line noise or plugging the cable in mid-stream both recover
    // by themselves, with nobody having to reset anything.
    if (g_len == 0 && b != AC_UNOQ_MAGIC) continue;

    g_buf[g_len++] = b;
    if (g_len < sizeof(g_buf)) continue;

    if (decodeFrame(g_buf)) {
      g_len = 0;
      g_rx++;
      g_lastHeardMs = millis();
    } else {
      g_rejected++;
      // Slide ONE byte and keep looking for the magic, rather than clearing the
      // buffer: the real magic byte may be inside the bytes just collected (for
      // example half an old packet stuck to half a new one). Clearing would also
      // discard a good frame sitting immediately after a corrupt one.
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
