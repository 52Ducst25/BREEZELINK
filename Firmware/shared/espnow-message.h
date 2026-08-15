#pragma once
#include <stdint.h>
#include <string.h>

// ============================================================================
//  ESP-NOW packet: SLAVE node -> MASTER node (gateway)
// ----------------------------------------------------------------------------
//  Shared by EVERY sensor node in a household: the 4 room-corner nodes
//  (ESP32-C3) and the outdoor node (ESP32 DevKit). All three chip families are
//  little-endian, so floats are byte-for-byte compatible.
//
//  The layout is FIXED (packed) because two different sides have to read back
//  the same bytes.
//
//  The packet is SELF-DESCRIBING: the slave includes its own device_uuid, so the
//  master can simply publish to bl/{org}/{uuid}/telemetry WITHOUT any MAC->uuid
//  mapping table. Adding a new slave node only means flashing that node; the
//  master needs no change at all.
//
//  THIS IS THE MOST VALUABLE PROPERTY OF THIS LAYOUT, and it is why the 4
//  room-corner nodes use ESP-NOW rather than BLE: a classic BLE advertising
//  packet is only 31 bytes and cannot carry a 32-character uuid, so the receiver
//  would be forced to keep an id->uuid lookup table and be reflashed every time a
//  node is added or removed. Not so here.
//
//  Known trade-off: any device in radio range can declare whatever uuid it likes.
//  Acceptable for one household's local network; to tighten it, enable ESP-NOW
//  encryption (PMK/LMK) or have the master filter by a MAC whitelist.
// ============================================================================

#define AC_ESPNOW_MAGIC   0xAC  // quick filter for junk/foreign frames on the same channel

// Version 2 appends `node_kind` + `corner` at the END of the struct.
//
// BACKWARD COMPATIBILITY IS DELIBERATE HERE, NOT INCIDENTAL: the outdoor node
// already installed in the field runs v1 and its firmware HAS NO OTA (audit §3) --
// upgrading it to v2 means physically going there with a USB-TTL. So the master
// accepts both: a 43-byte packet (v1) is understood as an outdoor node, because
// every v1 node that ever existed was an outdoor node. See acEspNowParse().
#define AC_ESPNOW_VERSION 2
#define AC_ESPNOW_V1_SIZE 43

/// Node kind -- determines where the master files this reading for display, and
/// (indirectly) whether the backend feeds it into the indoor median or the
/// outdoor running mean. The master does NOT need the kind in order to relay; it
/// only needs it to draw the screen.
enum AcNodeKind : uint8_t {
  AC_NODE_OUTDOOR = 0,
  AC_NODE_ROOM    = 1,
};

/// `corner` is only a DISPLAY LABEL ("corner 1".."corner 4") for the local
/// screen -- device_uuid is the real identity.
///
/// So TWO BOARDS SHARING A CORNER NUMBER IS HARMLESS: both still have their own
/// topic, both still feed the median, the screen just shows duplicate labels.
/// Quite unlike the BLE approach once considered, where a duplicate id meant the
/// receiver treated two boards as ONE and discarded the second board's readings
/// entirely -- the system running on three sensors while four lights still glow
/// on the wall.
#define AC_CORNER_NONE 0xFF

typedef struct __attribute__((packed)) {
  uint8_t  magic;             // = AC_ESPNOW_MAGIC
  uint8_t  version;           // = AC_ESPNOW_VERSION
  char     device_uuid[33];   // the SLAVE's uuid (32 chars + '\0')
  float    temp;              // degC -- NAN = node alive but the sensor is faulty
  float    humidity;          // %    -- NAN, as above
  uint8_t  node_kind;         // v2: AcNodeKind
  uint8_t  corner;            // v2: 0..3 for room nodes, AC_CORNER_NONE for outdoor
} AcEspNowPacket;             // 45 bytes -- comfortably under ESP-NOW's 250-byte limit

/// Unpack a received frame, accepting both v1 and v2.
///
/// Returns false if it is not one of this system's packets. A v1 packet (43
/// bytes) is filled in with node_kind = OUTDOOR and corner = AC_CORNER_NONE --
/// see the note on AC_ESPNOW_VERSION for why.
static inline bool acEspNowParse(const uint8_t *data, int len, AcEspNowPacket *out) {
  if (data == nullptr || len < AC_ESPNOW_V1_SIZE) return false;
  if (data[0] != AC_ESPNOW_MAGIC) return false;

  const uint8_t version = data[1];
  if (version == AC_ESPNOW_VERSION && len >= (int)sizeof(AcEspNowPacket)) {
    memcpy(out, data, sizeof(AcEspNowPacket));
    return true;
  }
  if (version == 1 && len >= AC_ESPNOW_V1_SIZE) {
    memset(out, 0, sizeof(*out));
    memcpy(out, data, AC_ESPNOW_V1_SIZE);
    out->node_kind = AC_NODE_OUTDOOR;
    out->corner    = AC_CORNER_NONE;
    return true;
  }
  return false;   // unknown version -> drop it, do not guess the layout
}

/// Fill in a packet to send. The caller is responsible for using NaN where there
/// is no reading -- this function does not guess on your behalf, and NaN is the
/// CORRECT answer for "the sensor is faulty" (quite unlike 0.0, which is a valid
/// temperature).
static inline void acEspNowFill(AcEspNowPacket *pkt, const char *deviceUuid,
                                float temp, float humidity,
                                uint8_t nodeKind, uint8_t corner) {
  memset(pkt, 0, sizeof(*pkt));
  pkt->magic     = AC_ESPNOW_MAGIC;
  pkt->version   = AC_ESPNOW_VERSION;
  pkt->temp      = temp;
  pkt->humidity  = humidity;
  pkt->node_kind = nodeKind;
  pkt->corner    = corner;
  strncpy(pkt->device_uuid, deviceUuid, sizeof(pkt->device_uuid) - 1);
  pkt->device_uuid[sizeof(pkt->device_uuid) - 1] = '\0';
}
