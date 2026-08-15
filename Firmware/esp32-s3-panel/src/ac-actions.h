#pragma once
#include <Arduino.h>

// ============================================================================
//  The DISCRETE BUTTONS the panel knows how to transmit -- a table shared by
//  loop() and the UI.
// ----------------------------------------------------------------------------
//  A "discrete button" = one learned IR frame, replayed verbatim, that is NOT
//  part of the (mode, temperature) matrix. The backend keeps them in the
//  `ir_action_codes` table, keyed by (org, action) -- see
//  src/app/services/ir_action_service.py.
//
//  WHY THIS FILE EXISTS instead of each side declaring its own:
//  the wire name ("FAN_60") is the LOOKUP KEY in NVS, while the Vietnamese label
//  ("60%") is what appears on screen. With the two tables in two files, inserting
//  one extra level in the middle on one side shifts every index -- and it fails in
//  the worst possible way: the user presses "60%", the unit runs at 80%, and
//  nothing anywhere reports an error.
//
//  THIS TABLE MUST MATCH `ir_action_service.KNOWN_ACTIONS`. When the backend adds
//  a new button the panel does NOT have to follow -- it can only transmit the
//  buttons listed here, and the rest still work normally from the app. In other
//  words: this file is the SUBSET the panel supports, not a full copy.
//
//  AND IT IS DELIBERATELY A SUBSET. Each IR frame takes ~600 bytes of NVS (~25
//  slots); copying all 19 buttons onto a board that has no control for most of
//  them means paying flash for something it can never transmit. SLEEP / ECO /
//  SWING / TIMER... can still be learned and transmitted from the app as before.
// ============================================================================
namespace AcActions {

/// How many fan levels the panel offers. 5 percentage levels + AUTO + the
/// remote's cycle button.
constexpr uint8_t FAN_COUNT = 7;

/// The wire names, matching the backend's `FAN_LEVELS` + `FAN_SPEED`. These are
/// also the aliases in IrStore (the NVS key is "a" + the name; the longest,
/// "aFAN_SPEED", is 10 characters, under NVS's limit of 15).
inline const char *fanWire(uint8_t i) {
  static const char *const k[FAN_COUNT] = {
      "FAN_20", "FAN_40", "FAN_60", "FAN_80", "FAN_100", "FAN_AUTO", "FAN_SPEED"};
  return i < FAN_COUNT ? k[i] : "";
}

/// The labels shown on screen. IN VIETNAMESE like every other panel label -- the
/// control screen already calls COOL "LẠNH", so showing "FAN_60" right underneath
/// it would force the user to learn two naming systems for the same appliance.
///
/// "NÚT VÒNG" describes what FAN_SPEED actually does: it steps to the NEXT level
/// in the unit's own cycle, so the panel does NOT know which level the unit ends
/// up on. Calling it a "level" like the six above would be a lie.
inline const char *fanLabel(uint8_t i) {
  static const char *const k[FAN_COUNT] = {
      "20%", "40%", "60%", "80%", "100%", "TỰ ĐỘNG", "NÚT VÒNG"};
  return i < FAN_COUNT ? k[i] : "";
}

/// Humidifier -- two code slots, learned from the app.
///
/// A household whose humidifier remote has only ONE power button (a toggle) can
/// learn the same frame into both slots, or only learn the ON slot -- main.cpp
/// detects that and reuses the ON slot for the off direction too. See
/// humidifier-control.h.
inline const char *humidWire(bool on) { return on ? "HUMID_ON" : "HUMID_OFF"; }

}  // namespace AcActions
