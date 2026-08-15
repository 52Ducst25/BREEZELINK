#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

// ============================================================================
//  UART link between the GATEWAY (indoor) and the ARDUINO UNO Q (edge AI)
// ----------------------------------------------------------------------------
//  THIS USED TO BE BLUETOOTH. Dropped because of a measurement, not a
//  preference:
//
//    Gateway, BLE on:    0.31 ESP-NOW packets/second
//    Gateway, BLE off:   0.80 packets/second   <- exactly 4 nodes x 5 seconds
//
//  Turning on Bluetooth FORCES the chip to enable WiFi modem sleep -- it aborts
//  rather than merely degrading (`Should enable WiFi modem sleep when both WiFi
//  and Bluetooth are enabled`). With the radio asleep, an ESP-NOW frame arriving
//  at that moment is lost, nothing buffers it, and since broadcast has no ACK the
//  node still reports "sent". The outdoor node dropped ~50% of its packets and
//  flickered between ONLINE and OFFLINE; with BLE off everything arrives, zero
//  drops.
//
//  In other words BLE ate ~60% of the receive capacity of the very gateway it
//  was serving. UART does not touch the radio, so all of it comes back. The
//  price is one wire, and the two boards have to sit next to each other.
//
//  IT ALSO REMOVES A PILE OF COMPLEXITY: no scanning, no pairing, no MTU
//  negotiation (a 39-byte snapshot was once suspected of being truncated because
//  BlueZ reported MTU 23), no ~100KB of NimBLE flash, nobody competing for the
//  2.4GHz antenna.
//
//  THE PACKET LAYOUT IS UNCHANGED from the BLE version -- it is the expensive
//  part and it has nothing to do with the transport. Only a CRC was added to the
//  command packet (see AcUnoQCommandHeader).
//
//  FRAME SYNC: packets are fixed size, start with `magic` and end with a CRC. The
//  receiver waits for the magic byte then collects the full length; on a mismatch
//  it slides one byte and resyncs. That way line noise, or plugging the cable in
//  mid-stream, both recover by themselves.
//
//  BINARY, NOT JSON: the firmware has no JSON parser to spare for this link
//  (ArduinoJson is reserved for MQTT with its 12KB buffer), and a packed struct
//  reads back the same bytes on both sides with nothing further to agree on.
//
//  THE PYTHON SIDE reads this very file through `struct` -- see
//  edge-ai/edge_ai/protocol.py, where the format string is duplicated with a
//  comment pointing back here. Changing the layout means changing BOTH, and
//  `version` below is what catches you when you forget.
// ============================================================================

#define AC_UNOQ_MAGIC   0xAC
// 2 = the UART version (the command packet gained a crc8). Version 1 was BLE and
// is NOT compatible: a v1 command packet is 12 bytes, v2 is 13. The receiver
// checks `version`, so a version mismatch is rejected outright rather than
// misreading fields and issuing a wrong command.
#define AC_UNOQ_VERSION 2

/// How many room corners the gateway can report in one snapshot. 4 is what is
/// actually installed; 8 would bloat the snapshot for nothing, while leaving it
/// at 4 with 5 installed makes the fifth corner vanish silently from the UNO Q's
/// view -- so this number matches RoomRegistry's slot count exactly.
#define AC_UNOQ_MAX_ROOMS 4

// The BLE version's service/characteristic UUIDs are gone -- UART has no such
// concept. They used to live here; see the git history if you ever go back to
// Bluetooth.

// --- Value encoding convention -----------------------------------------------
//  Temperature x100 into an int16 (range -327.68..327.67 degC), humidity x100
//  into a uint16.
//  INT16_MIN / 0xFFFF mean "NO READING" -- not 0. Both 0 degC and 0 %RH are valid
//  values, and reading 0 as "missing" is the fastest way to turn a broken sensor
//  into a room at zero degrees.
#define AC_UNOQ_T_INVALID ((int16_t)0x8000)
#define AC_UNOQ_H_INVALID ((uint16_t)0xFFFF)
/// The server has never issued a command at all (quite different from "it just
/// spoke").
#define AC_UNOQ_SILENCE_NEVER ((uint16_t)0xFFFF)

/// Air conditioner mode on the wire. Matches app.models.enums.AcMode.
enum AcUnoQMode : uint8_t {
  AC_UNOQ_MODE_OFF     = 0,
  AC_UNOQ_MODE_COOL    = 1,
  AC_UNOQ_MODE_DRY     = 2,
  AC_UNOQ_MODE_FAN     = 3,
  AC_UNOQ_MODE_UNKNOWN = 0xFF,
};

// Status flags inside a snapshot.
#define AC_UNOQ_FLAG_WIFI_UP     0x01
#define AC_UNOQ_FLAG_MQTT_UP     0x02
#define AC_UNOQ_FLAG_OVERRIDE    0x04  ///< the user currently holds control (panel/app)
#define AC_UNOQ_FLAG_OUT_ONLINE  0x08  ///< the outdoor node still has a heartbeat

/// Snapshot, gateway -> UNO Q. 39 bytes.
///
/// Carries BOTH the per-corner values AND the median, even though the UNO Q could
/// recompute it: the median is the number the gateway is CURRENTLY showing on the
/// wall, and if the UNO Q computes a different one that divergence has to be
/// visible rather than silently resolved in favour of one side.
typedef struct __attribute__((packed)) {
  uint8_t  magic;          // = AC_UNOQ_MAGIC
  uint8_t  version;        // = AC_UNOQ_VERSION
  uint8_t  room_count;     // corners WITH a valid reading, i.e. feeding the median
  uint8_t  flags;          // AC_UNOQ_FLAG_*

  int16_t  t_in_c100;      // median of the fresh corners
  uint16_t h_in_x100;
  int16_t  t_out_c100;
  uint16_t h_out_x100;

  int16_t  room_t_c100[AC_UNOQ_MAX_ROOMS];
  uint16_t room_h_x100[AC_UNOQ_MAX_ROOMS];
  uint8_t  room_corner[AC_UNOQ_MAX_ROOMS];   // corner label; AC_CORNER_NONE = empty slot

  /// Seconds since the last command the server sent down. THIS IS THE MOST
  /// IMPORTANT FIELD in the whole packet: it is the only thing telling the UNO Q
  /// whether the cloud is alive. The gateway knows this more reliably than the
  /// UNO Q could, because it is the one holding the MQTT session.
  uint16_t cloud_silence_sec;

  uint8_t  ac_mode;        // AcUnoQMode -- the REAL state transmitted to the unit
  int8_t   ac_setpoint;    // degC, -1 = unknown
  uint16_t uptime_min;
  uint8_t  crc8;
} AcUnoQSnapshot;

/// Command/advice, UNO Q -> gateway. 13 bytes.
typedef struct __attribute__((packed)) {
  uint8_t  magic;
  uint8_t  version;
  uint8_t  kind;       // AC_UNOQ_KIND_*
  uint8_t  mode;       // AcUnoQMode
  int8_t   setpoint;   // degC, -1 if the mode does not need one
  uint8_t  reserved;   // padding for alignment; must be 0
  uint16_t seq;        // incremented per command -- the gateway drops duplicates
  uint32_t link_key;   // = acEspNowSiteKey(ORG_ID), see the note below
  uint8_t  crc8;       // Dallas/Maxim over the preceding 12 bytes
} AcUnoQCommandHeader;

/// WHY THE COMMAND PACKET HAS A CRC WHEN THE BLE VERSION DID NOT:
///
/// The BLE version relied on Bluetooth's link layer for integrity -- reasonably
/// so, since BLE checks a CRC24 on every packet and discards corrupt ones before
/// they reach the application layer.
/// UART HAS NOTHING LIKE THAT: a noisy wire, hot-plugging the cable, a baud rate
/// mismatch -- all of them push garbage bytes straight up.
///
/// For a snapshot, one flipped bit only corrupts one reading for one tick. For a
/// COMMAND it changes `setpoint` or `mode` and goes straight out to the air
/// conditioner, with nobody in between to notice. So the command packet has to
/// prove its own integrity.

/// `kind` SEPARATES ADVICE FROM COMMAND, and this is the most important boundary
/// in the protocol: normally the UNO Q only sends ADVICE (the gateway logs it and
/// shows it on screen but does NOT fire IR), and only sends COMMAND once the
/// cloud has been silent long enough. Merging the two would send every
/// experimental calculation on the UNO Q straight to the compressor.
#define AC_UNOQ_KIND_ADVICE  0
#define AC_UNOQ_KIND_COMMAND 1

/// `link_key` is hashed from ORG_ID, the same trick as the site_key once
/// considered for the BLE sensor packets.
///
/// THIS IS NOT AUTHENTICATION. It stops: another household's UNO Q accidentally
/// connecting to your gateway in an apartment block, and BLE toys writing junk
/// into the characteristic. It does NOT stop a deliberate attacker -- the key
/// sits in config.h and travels in the clear.
///
/// How to harden it properly if needed: enable NimBLE bonding + a static passkey
/// (`NimBLEDevice::setSecurityAuth(true, true, true)` + `setSecurityPasskey`),
/// then pair once during installation. Not done, because it adds an installation
/// step that can go wrong, and the threat here (someone within 10m who wants to
/// adjust your air conditioning) does not justify it.
static inline uint32_t acUnoQLinkKey(const char *orgId) {
  uint32_t hash = 2166136261u;   // FNV-1a 32-bit
  for (const char *p = orgId; p && *p; p++) {
    hash ^= (uint8_t)*p;
    hash *= 16777619u;
  }
  return hash;
}

/// CRC8 Dallas/Maxim (reflected polynomial 0x8C).
///
/// NEEDED even though BLE already had a link-layer CRC24: BLE's CRC protects the
/// transmission, it says nothing about whether this packet has the layout you are
/// expecting. This is the last guard before some pair of bytes gets read out as a
/// room temperature.
static inline uint8_t acUnoQCrc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < len; i++) {
    uint8_t inbyte = data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      const uint8_t mix = (uint8_t)((crc ^ inbyte) & 0x01);
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      inbyte >>= 1;
    }
  }
  return crc;
}

static inline void acUnoQSealSnapshot(AcUnoQSnapshot *s) {
  s->crc8 = acUnoQCrc8((const uint8_t *)s, (uint8_t)(sizeof(*s) - 1));
}

static inline void acUnoQSealCommand(AcUnoQCommandHeader *c) {
  c->crc8 = acUnoQCrc8((const uint8_t *)c, (uint8_t)(sizeof(*c) - 1));
}

/// Is the command packet intact. The SENDER seals, the RECEIVER checks -- these
/// two have to travel together; forget one side and every command is rejected (or
/// worse: every command is accepted, corrupt ones included).
static inline bool acUnoQCheckCommand(const AcUnoQCommandHeader *c) {
  return c->crc8 == acUnoQCrc8((const uint8_t *)c, (uint8_t)(sizeof(*c) - 1));
}

static inline bool acUnoQCheckSnapshot(const AcUnoQSnapshot *s) {
  return s->crc8 == acUnoQCrc8((const uint8_t *)s, (uint8_t)(sizeof(*s) - 1));
}

// --- value encoding / decoding -----------------------------------------------

static inline int16_t acUnoQEncodeTemp(float celsius) {
  if (!(celsius > -300.0f && celsius < 300.0f)) return AC_UNOQ_T_INVALID;
  return (int16_t)(celsius * 100.0f);
}

static inline uint16_t acUnoQEncodeRh(float percent) {
  if (!(percent >= 0.0f && percent <= 100.0f)) return AC_UNOQ_H_INVALID;
  return (uint16_t)(percent * 100.0f);
}

/// The minimum MTU for a snapshot to fit in ONE notify packet (ATT costs 3 bytes
/// of header). Below this threshold the notify is SILENTLY TRUNCATED -- no error,
/// no warning, the last few corners simply vanish. The gateway has to check this
/// and complain loudly.
#define AC_UNOQ_MIN_MTU (sizeof(AcUnoQSnapshot) + 3)

// ---------------------------------------------------------------------------
//  SIZE PINNING -- this is what catches a two-sided sync error AT COMPILE TIME.
//
//  This layout has a Python twin in edge-ai/edge_ai/protocol.py. Add a field here
//  and forget the other side and the packet still "decodes successfully" -- only
//  every field after the insertion point is shifted, and the room temperature
//  reads out as garbage. The CRC does not save you: it is computed over exactly
//  the number of bytes the sender thought was right.
//
//  The two numbers below must match the SNAPSHOT_SIZE and COMMAND_SIZE that
//  protocol.py prints. Change the layout -> fix all three places, and this
//  static_assert line is what stops you forgetting.
// ---------------------------------------------------------------------------
#ifdef __cplusplus
static_assert(sizeof(AcUnoQSnapshot) == 39,
              "AcUnoQSnapshot size changed - update edge-ai/edge_ai/protocol.py to match");
static_assert(sizeof(AcUnoQCommandHeader) == 13,
              "AcUnoQCommandHeader size changed - update edge-ai/edge_ai/protocol.py to match");
#endif
