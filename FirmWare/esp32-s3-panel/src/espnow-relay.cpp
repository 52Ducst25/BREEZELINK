#include "espnow-relay.h"
#include <WiFi.h>
#include <esp_now.h>
#include <string.h>

namespace EspNowRelay {

// Ring buffer: the callback (WiFi task) writes at head, loop() reads at tail.
// One writer + one reader, so no lock is needed.
//
// 32 SLOTS, NOT 8. The old figure of 8 was based on "each slave sends every 15s" --
// but that is the PUSH-TO-CLOUD cadence (SlaveWatch::RELAY_INTERVAL_MS), not a
// node's transmit cadence. A room-corner node broadcasts every 5 seconds and there
// are four of them, plus the outdoor node: roughly 1 packet/second. Eight slots
// fill up in 8 seconds.
//
// And loop() SOMETIMES cannot drain the queue that fast: every mqtt.* call blocks,
// and on a half-dead socket (WiFi still associated but the link is gone) a single
// publish can take several seconds. The timeout has been clamped in setup() so the
// blocking window is far shorter than it used to be, but it cannot be zero -- so
// the queue has to absorb it. 32 slots is about half a minute of data, at a cost of
// ~1.2 KB of RAM.
static const uint8_t QUEUE_SIZE = 32;

struct Slot {
  AcEspNowPacket pkt;
  uint8_t        mac[6];
};

static Slot             queue[QUEUE_SIZE];
static volatile uint8_t head = 0, tail = 0;
static volatile uint32_t nRecv = 0, nDrop = 0;

static void enqueue(const uint8_t *mac, const uint8_t *data, int len) {
  // Drop malformed packets immediately: another system's ESP-NOW traffic can be on
  // the same channel, and parsing it blindly would produce garbage numbers that go
  // straight up to the cloud.
  //
  // acEspNowParse() accepts BOTH v1 (43 bytes) and v2 (45 bytes) -- the outdoor
  // node already installed in the field still runs v1 and its firmware has no OTA,
  // so the gateway has to understand it. A v1 packet is filled in with
  // node_kind = OUTDOOR; see espnow-message.h.
  AcEspNowPacket parsed;
  if (!acEspNowParse(data, len, &parsed)) {
    // SPEAK UP, DO NOT STAY SILENT. An earlier version just `return`ed, so a
    // malformed packet went into no counter at all -- and "received 0, dropped 0"
    // looks exactly like the case where NO PACKET ARRIVED AT ALL. Those two cases
    // need investigating in completely different places: one is radio/channel, the
    // other is a packet layout mismatch between two firmwares. Time has already
    // been lost on exactly this once.
    //
    // Print with decreasing frequency: the first 3 packets, then every 50th, so a
    // neighbouring node broadcasting ESP-NOW continuously does not drown the log.
    static uint32_t nBad = 0;
    if (++nBad <= 3 || nBad % 50 == 0) {
      Serial.printf("[espnow] dropped FOREIGN packet: %d bytes", len);
      if (mac) Serial.printf(" from %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
                             mac[3], mac[4], mac[5]);
      if (len >= 2) Serial.printf(" - magic=0x%02X version=%u", data[0], data[1]);
      Serial.printf(" (expected %u or %u bytes, magic=0x%02X)\n",
                    (unsigned)AC_ESPNOW_V1_SIZE, (unsigned)sizeof(AcEspNowPacket),
                    AC_ESPNOW_MAGIC);
    }
    return;
  }

  uint8_t next = (uint8_t)((head + 1) % QUEUE_SIZE);
  if (next == tail) {           // full -- better to drop the new packet than overwrite an unsent one
    nDrop++;
    return;
  }
  queue[head].pkt = parsed;
  if (mac) memcpy(queue[head].mac, mac, 6);
  else     memset(queue[head].mac, 0, 6);
  head = next;
  nRecv++;
}

// The callback signature changed between Arduino-ESP32 core 2.x and 3.x -- keep
// both so a core upgrade does not break the build.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  enqueue(info ? info->src_addr : nullptr, data, len);
}
#else
static void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  enqueue(mac, data, len);
}
#endif

bool begin() {
  if (esp_now_init() != ESP_OK) return false;
  // TRIED AND IT IS NOT THE ANSWER: registering a broadcast peer on the receiving
  // side does not make the callback fire. Receiving broadcasts genuinely does not
  // need a peer -- recorded here so nobody tries the same thing again.
  return esp_now_register_recv_cb(onRecv) == ESP_OK;
}

void poll(Handler handler) {
  while (tail != head) {
    Slot s = queue[tail];
    tail = (uint8_t)((tail + 1) % QUEUE_SIZE);
    s.pkt.device_uuid[sizeof(s.pkt.device_uuid) - 1] = '\0';  // guard against a packet missing its '\0'
    handler(s.pkt, s.mac);
  }
}

uint32_t receivedCount() { return nRecv; }
uint32_t droppedCount()  { return nDrop; }

} // namespace EspNowRelay
