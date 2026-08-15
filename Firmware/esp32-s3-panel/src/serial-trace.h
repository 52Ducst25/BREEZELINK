#pragma once
#include <Arduino.h>

#include "espnow-message.h"
#include "unoq-link-protocol.h"

// ============================================================================
//  Per-packet IN/OUT trace log for the gateway -- for debugging only.
// ----------------------------------------------------------------------------
//  ENABLE WITH `-D GATEWAY_TRACE=1`. OFF BY DEFAULT, and when off every function
//  below is an empty inline body -- the compiler removes the calls entirely, at a
//  cost of zero bytes of flash. That is what makes leaving the calls in main.cpp
//  harmless.
//
//  WHY NOT JUST LEAVE IT ON: the wall-mounted QR Box board has a screen to look at,
//  and one log line per PACKET (5 nodes x 5 seconds) would drown out the lines that
//  really matter -- `[cmd]`, `[learn]`, the NVS-full warning. A dense log destroys
//  the important log rather than clarifying it.
//
//  THE MOST VALUABLE THING HERE IS THE `delta` COLUMN: the time since THAT SAME
//  node's PREVIOUS packet. A node transmitting every 5s with delta=15.0s means two
//  lost packets -- one line tells you, with no timestamp subtraction. ESP-NOW
//  broadcast has no ACK, so this is the only way to see packet loss from the
//  receiving side.
// ============================================================================
namespace SerialTrace {

#if defined(GATEWAY_TRACE) && GATEWAY_TRACE

/// An ESP-NOW packet just arrived (printed with delta and that node's own sequence
/// number).
void packetIn(const AcEspNowPacket &pkt, const uint8_t mac[6]);

/// Just published to MQTT. [ok] is PubSubClient's return value -- false means the
/// packet did NOT leave the board (usually because it exceeded the buffer),
/// something very easily mistaken for a network fault.
void mqttOut(const char *topic, const uint8_t *payload, size_t len, bool ok);

/// Just received an MQTT packet. The payload is truncated when printed: a command
/// carries an `ir_raw` of several hundred timings, and printing it in full scrolls
/// the whole screen away.
void mqttIn(const char *topic, const uint8_t *payload, size_t len);

/// Just pushed a snapshot to the Arduino UNO Q.
void snapshotOut(const AcUnoQSnapshot &snap, bool linkUp);

/// Summary table: one line per node, with its packet count and average delta.
void summary();

#else   // ----- OFF: empty bodies, the compiler removes every call -----

inline void packetIn(const AcEspNowPacket &, const uint8_t[6]) {}
inline void mqttOut(const char *, const uint8_t *, size_t, bool) {}
inline void mqttIn(const char *, const uint8_t *, size_t) {}
inline void snapshotOut(const AcUnoQSnapshot &, bool) {}
inline void summary() {}

#endif

} // namespace SerialTrace
