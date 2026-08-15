#pragma once
#include <Arduino.h>
#include "espnow-message.h"

// ============================================================================
//  Receiving ESP-NOW packets from the SLAVE nodes (the MASTER role).
// ----------------------------------------------------------------------------
//  The ESP-NOW callback runs inside the WiFi task and must NOT publish to MQTT
//  right there (PubSubClient is not thread-safe and the callback has to return
//  very quickly). So the callback only copies the packet into a ring buffer, and
//  loop() is what takes it out and sends it.
//
//  It also carries the sender's MAC: ESP-NOW reports the source MAC, which lets
//  the master report a slave's MAC to the cloud without the slave having to
//  declare it itself.
// ============================================================================
namespace EspNowRelay {

/// Called from loop() for each received packet, in arrival order.
///
/// Passes the whole struct rather than pre-unpacking each field: the packet now
/// also carries node_kind and corner, and changing the function signature every
/// time a field is added forces every caller to be updated -- exactly the thing
/// that gets forgotten while adding a new node type.
typedef void (*Handler)(const AcEspNowPacket &pkt, const uint8_t mac[6]);

/// Initialise ESP-NOW. Call AFTER WiFi has connected: ESP-NOW uses whatever
/// channel WiFi is on, so WiFi has to settle the channel first.
bool begin();

/// Drain every packet waiting in the queue. Call every loop().
void poll(Handler handler);

uint32_t receivedCount();
uint32_t droppedCount();

} // namespace EspNowRelay
