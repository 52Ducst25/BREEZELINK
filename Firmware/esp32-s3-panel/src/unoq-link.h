#pragma once
#include <Arduino.h>

#include "unoq-link-protocol.h"

// ============================================================================
//  The UART link to the Arduino UNO Q (edge AI).
// ----------------------------------------------------------------------------
//  Packet layout, link key, and the reasoning behind the role split:
//  ../../shared/unoq-link-protocol.h -- UNCHANGED by the move from Bluetooth to
//  UART. Only the transport changed.
//
//  WHY BLUETOOTH WAS DROPPED -- THIS IS A MEASUREMENT, NOT A PREFERENCE:
//
//    Gateway, BLE on:    0.31 ESP-NOW packets/second
//    Gateway, BLE off:   0.80 packets/second  <- exactly 4 nodes x 5 seconds
//
//  Turning on Bluetooth FORCES the chip to enable WiFi modem sleep -- it aborts
//  rather than merely degrading (`Should enable WiFi modem sleep when both WiFi and
//  Bluetooth are enabled`). With the radio asleep, an ESP-NOW frame arriving at that
//  moment is lost, nothing buffers it, and since broadcast has no ACK the node still
//  reports "sent". The outdoor node dropped ~50% of its packets and flickered
//  between ONLINE and OFFLINE constantly; with BLE off it arrives cleanly, zero
//  drops.
//
//  In other words: BLE ate ~60% of the receive capacity of the very gateway it was
//  serving. UART does not touch the radio, so all of it comes back. The price is one
//  wire.
//
//  IT ALSO REMOVES A PILE OF COMPLEXITY: no scanning, no pairing, no MTU
//  negotiation (a 39-byte snapshot was once suspected of being truncated because
//  BlueZ reported MTU 23), no ~100KB of NimBLE flash, and nobody competing for the
//  2.4GHz antenna.
//
//  THIS FILE'S THREE RULES ARE UNCHANGED FROM THE BLE VERSION:
//
//  1. PLACE ORDERS ONLY, NEVER EXECUTE. poll() hands the packet out to loop(); it
//     does not transmit IR and does not publish MQTT -- the same rule already
//     applied to the ESP-NOW and MQTT callbacks.
//
//  2. ADVICE IS NOT A COMMAND. The packet's `kind` says which, and this file does
//     NOT infer: when the UNO Q sends ADVICE the gateway only records it; only
//     COMMAND reaches the air conditioner. Merging the two would send every
//     experiment on the UNO Q straight to the compressor.
//
//  3. A WRONG link_key MEANS DISCARD. A packet not carrying the correct hash of
//     ORG_ID is dropped and counted in rejectedCount() -- so another household's UNO
//     Q board cannot drive the wrong air conditioner.
// ============================================================================
namespace UnoQLink {

/// A command/advice just received from the UNO Q, already validated for
/// magic/version/CRC/link_key and de-duplicated by `seq`. Retrieved with poll() in
/// loop().
struct Incoming {
  bool     isCommand;   ///< false = advice only, IR must NOT be transmitted
  uint8_t  mode;        ///< AcUnoQMode
  int8_t   setpoint;
  uint16_t seq;
};

/// Open the UART port and prepare the link key. [orgId] is hashed into the
/// link_key.
///
/// Can be called AT ANY POINT in setup() -- quite unlike the BLE version, which had
/// to come after WiFi because they competed for the radio. UART has nothing to do
/// with radio.
bool begin(const char *orgId);

/// Push one snapshot to the UNO Q. Cheap and non-blocking: 39 bytes at 115200 baud
/// takes ~3.4ms, and the driver's transmit buffer swallows it whole so the function
/// returns immediately.
void publish(const AcUnoQSnapshot &snapshot);

/// Retrieve a command/advice the UNO Q has just sent. Returns false if there is
/// none. Call every loop().
bool poll(Incoming &out);

/// Is the UNO Q talking to us.
///
/// UART HAS NO CONNECTION STATE the way BLE does -- a plugged-in cable is
/// "connected" even when the other end is dead. So here "connected" means A VALID
/// PACKET WAS HEARD RECENTLY. That is what someone reading the log actually wants
/// to know.
bool connected();

uint32_t rxCount();        ///< valid packets received
uint32_t rejectedCount();  ///< packets dropped (bad magic/version/CRC/link_key/duplicate)

} // namespace UnoQLink
