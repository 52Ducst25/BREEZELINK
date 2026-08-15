#pragma once
#include <Arduino.h>

// ============================================================================
//  Temperature/humidity sensor for a room-corner node (DHT22).
// ----------------------------------------------------------------------------
//  Split out of main.cpp not to wrap the DHT library, but to keep ONE rule in one
//  place: "a single failed read" and "the sensor is broken" are two different
//  things.
//
//  A DHT22 failing a few percent of its reads is NORMAL (bad checksum, timeout).
//  Reporting NaN on the very first failure makes the node broadcast a "no
//  reading" packet, the gateway drops it from the median, and 2.5 seconds later
//  there is a number again -- the room's temperature jumps because of a read
//  error that has nothing to do with the temperature.
//
//  So: keep the last valid reading across a few failures, and only admit the
//  sensor is broken and return NaN after enough CONSECUTIVE failures. At that
//  point NaN is the truth, and the gateway is right to drop this node from the
//  median.
// ============================================================================
namespace RoomSensor {

/// Read interval (ms). The DHT22 datasheet specifies a MINIMUM of 2s between
/// reads -- below that the chip returns the previous value instead of measuring
/// again. 2.5s leaves margin.
static const unsigned long READ_PERIOD_MS = 2500;

/// How many CONSECUTIVE failures before admitting the sensor is broken.
/// 6 x 2.5s = 15s, exactly one send interval -- long enough for a burst of noise
/// to pass, not long enough to hide a wire that has just come loose.
static const uint8_t FAIL_LIMIT = 6;

/// Bring up the sensor on [pin].
void begin(uint8_t pin);

/// Call every loop(); returns early if it is not yet time. Returns true if a read
/// round just completed (whether it succeeded or failed) -- use it to know when to
/// refresh the packet.
bool poll();

/// The current reading. Returns false (leaving the arguments untouched) when the
/// sensor is currently considered broken or has never read successfully. NEVER
/// returns 0.0 in place of "no reading".
bool read(float &tempC, float &humidity);

/// The current consecutive-failure count -- for printing to the log while
/// debugging an installation.
uint8_t consecutiveFailures();

} // namespace RoomSensor
