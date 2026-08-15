#pragma once
#include <Arduino.h>

// ============================================================================
//  I2C capacitive touch on the 2.8" display module (J1 on the QR Box Advance).
// ----------------------------------------------------------------------------
//  WHY THE CONTROLLER IS AUTO-DETECTED: the schematic only draws as far as the J1
//  connector (SCL/SDA/INT/RST) -- the touch controller lives on the display module
//  and is NOT in the drawing at all. Different module batches fit an FT6236 /
//  GT911 / CST816S depending on supply, and all three are identical 4-wire I2C.
//  Hard-coding one type means a change of batch produces a screen with
//  "dead touch" for no visible reason; probing by address at startup is far cheaper
//  than debugging that.
//
//  THE BUS IS SHARED WITH THE DS1307 (0x68) -- see board-io.h. The DS1307 only
//  tolerates 100kHz so the whole bus is pinned at 100kHz; the GT911 runs slower but
//  still correctly.
// ============================================================================
namespace Touch {

enum Chip : uint8_t { NONE = 0, FT6236, GT911, CST816 };

/// Initialise Wire (if not already) + release RST + probe the addresses. Returns
/// false if no controller was found -- the UI still draws normally, it just cannot
/// be pressed.
bool begin(uint8_t sdaPin, uint8_t sclPin, uint8_t rstPin, uint8_t intPin);

Chip chip();
const char *chipName();

/// Coordinates ALREADY rotated into the TFT's 320x240 landscape frame.
/// Returns true while a finger is touching. Non-blocking, no delays.
bool read(int16_t &x, int16_t &y);

} // namespace Touch
