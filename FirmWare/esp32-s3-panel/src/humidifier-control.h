#pragma once
#include <Arduino.h>

// ============================================================================
//  Humidifier: the ON / OFF decision, running directly on the panel.
// ----------------------------------------------------------------------------
//  ORIGIN: a port of the `esp32-humidity` test board (removed from the repo in the
//  2026-08-15 cleanup -- that board only existed to prove the logic before it went
//  onto real hardware, and the production version is now this very file). To look
//  at the original:
//      git log --diff-filter=D -- FirmWare/esp32-humidity/src/diffuser-control.cpp
//
//  The six priority branches, three layers of oscillation control, and the "when in
//  doubt, turn it OFF" principle are unchanged. THREE THINGS DIFFER from the
//  original, all three because the panel has something the test board did not:
//
//   1. HUMIDITY IS THE MEDIAN OF FOUR ROOM CORNERS, not a DHT mounted on the board.
//      The test board measured with its own sensor, so it had to disable WiFi to
//      avoid its own self-heating skewing the reading. The panel has no sensor at
//      all -- the number arriving here is built by RoomRegistry::median() from the
//      corner nodes, far away from the panel -- so that constraint disappears.
//
//   2. THE IR CODES ARE LEARNED FROM THE APP and live in the panel's shared store
//      under the two aliases "HUMID_ON" / "HUMID_OFF" (see
//      ir_action_service.KNOWN_ACTIONS). There is no separate two-slot store any
//      more.
//
//   3. A TOGGLE REMOTE IS DETECTED AUTOMATICALLY, with no DIFFUSER_IR_TOGGLE
//      compile flag. A household whose remote has only one power button can learn
//      the same frame into both slots, or learn only the ON slot -- the Emitter over
//      in main.cpp handles that. This module does not need to know, and that is the
//      valuable part: a misconfigured compile flag only reveals itself out in the
//      field, whereas here "the OFF slot has not been learned" is a state you can
//      read straight off the screen.
//
//  PRIORITY ORDER (upper overrides lower) -- the easiest part to get wrong, kept
//  identical to the original:
//     1. Running too long   -> CUT, overriding even a manual override
//     2. Manual override    -> respect the user's intent
//     3. No reading         -> CUT
//     4. Refill lockout     -> stay OFF
//     5. Hysteresis (deadband) -> whatever it wants
//     6. Dwell              -> may it change right now -- BLOCKS THE OFF DIRECTION
//                              ONLY, see DWELL_SEC for the asymmetry
// ============================================================================
namespace HumidifierControl {

// --- Thresholds and timings ---------------------------------------------------
//  The numbers (and their reasoning) were copied verbatim from the test board
//  before it was removed from the repo. This is the only remaining copy, so
//  changing it here changes it for the product -- there is no second copy left to
//  drift from.
//
//  NOT PUT IN config.h: that file is gitignored because it holds passwords, so any
//  default placed there would vanish from the repo and the next person would have
//  no idea where the numbers came from.

/// Drier than this and it turns ON (%RH).
constexpr float ON_BELOW_RH = 45.0f;

/// Wetter than this and it turns OFF (%RH). In between: no change.
///
/// = HUMID_LOW_KNEE from src/app/comfort/setpoint_calculator.py. Anchoring to it
/// is deliberate: the humidifier must NEVER push the room past the point where the
/// comfort algorithm itself starts treating it as uncomfortable -- otherwise two
/// controllers in one house are fighting each other, and the occupant only notices
/// that "it feels a bit stuffy".
constexpr float OFF_ABOVE_RH = 60.0f;

/// The update() call interval (ms). EMA_ALPHA below is calibrated to exactly this
/// interval.
constexpr uint32_t TICK_MS = 5000UL;

/// Input smoothing. 0.5 at a 5-second interval -> a time constant of ~7 seconds.
///
/// RAISED FROM 0.2 (tau ~22 seconds) BECAUSE THE UNIT TURNED ON TOO SLOWLY after
/// the room dried out. For a 50%->40% step, the threshold of 45 sits exactly in the
/// middle, so the filtered value needs `ln0.5/ln(1-alpha)` ticks to cross it: at 0.2
/// that is ~16 seconds, at 0.5 it is **one tick = 5 seconds**.
///
/// SAFE BECAUSE THE INPUT HAS ALREADY BEEN FILTERED ONCE: the number arriving here
/// is the MEDIAN of four room corners, i.e. already denoised SPATIALLY. The EMA
/// only has to denoise that median TEMPORALLY, and a median does not jump
/// abruptly. The real oscillation guard is still the 15-point DEADBAND in §2, not
/// the EMA.
constexpr float EMA_ALPHA = 0.5f;

/// Minimum time to hold a state before it may be turned OFF (seconds).
///
/// APPLIES ONLY TO THE OFF DIRECTION -- turning on happens immediately, with no
/// wait. The asymmetry is deliberate, the same pattern as the edge AI layer's
/// "take control slowly, release control quickly":
///
///   LATE ON   -> the room stays dry and the occupant notices at once. This was the
///                actual reported bug: a 300-second dwell blocked even the first
///                switch-on.
///   LATE OFF  -> the unit sprays for a few more minutes. Almost harmless.
///
/// And the dwell's original justification only holds for the OFF direction: "water
/// vapour needs a few minutes to reach the sensor" is something that happens AFTER
/// SWITCHING ON -- it stops us concluding too early that the last command had no
/// effect. It says nothing about whether to switch on when the room really is dry.
///
/// NO RISK OF OSCILLATION from dropping the dwell on the on direction: once on, it
/// has to exceed 60%RH to switch off (a 15-point deadband), so there is no path to
/// rapid cycling.
constexpr uint32_t DWELL_SEC = 300UL;

/// If no IR code has been learned, retry this often (seconds).
///
/// Separate from DWELL_SEC because the dwell no longer blocks the on direction:
/// without its own counter, every 5-second tick would attempt a transmission and
/// the log would spew continuously until somebody went into the app and learned the
/// code.
constexpr uint32_t NO_CODE_RETRY_SEC = 300UL;

/// Cut off after running continuously for this long (seconds). In a very dry room,
/// or with a door left open, the loop never reaches the off threshold and the unit
/// runs until the tank is empty.
constexpr uint32_t MAX_RUN_SEC = 4UL * 3600UL;

/// Lockout after the cut-off above (seconds). REMOVING IT DEFEATS MAX_RUN_SEC
/// ENTIRELY: cut without locking out and the next cycle still sees a dry room and
/// switches straight back on.
constexpr uint32_t REFILL_LOCKOUT_SEC = 30UL * 60UL;

/// Cut off after losing readings for longer than this (seconds). Running blind is
/// worse than not running.
constexpr uint32_t SENSOR_STALE_SEC = 120UL;

/// A manual override expires by itself after this long (seconds). The same
/// AUTO/OVERRIDE semantics as the air conditioner: the user always beats the
/// machine, but NOT permanently -- an override held forever means that three months
/// later nobody remembers why the unit stopped running by itself.
constexpr uint32_t OVERRIDE_HOLD_SEC = 2UL * 3600UL;

static_assert(OFF_ABOVE_RH > ON_BELOW_RH,
              "The OFF threshold must be GREATER THAN the ON threshold - equal values remove "
              "the hysteresis band entirely and the unit will cycle around the switching point.");
static_assert(DWELL_SEC * 1000UL > TICK_MS,
              "DWELL_SEC must be longer than one tick, otherwise the dwell does nothing.");
static_assert(MAX_RUN_SEC > DWELL_SEC, "MAX_RUN_SEC must be longer than DWELL_SEC.");

/// Why the current state is what it is. It exists so the screen can answer "why is
/// the unit not running?" in ONE line -- without it the user has to infer it from a
/// handful of disconnected numbers, and they will infer it wrongly.
enum class Reason : uint8_t {
  BOOT,         ///< just started, no decision made yet
  AUTO_DRY,     ///< automatic: the room is drier than the ON threshold
  AUTO_WET,     ///< automatic: the room is wetter than the OFF threshold
  DEADBAND,     ///< between the two thresholds -- unchanged, exactly as designed
  DWELL_HOLD,   ///< it wants to change but the minimum hold time has not elapsed
  MANUAL,       ///< the user is overriding
  MAX_RUN,      ///< cut off for running continuously too long
  LOCKOUT,      ///< in the refill lockout following the cut-off above
  SENSOR_LOST,  ///< no readings for too long
  NO_CODE,      ///< it wants to change but NO IR CODE has been learned for it
};

struct Status {
  bool     on;                ///< our belief: is the unit running?
  bool     overriding;        ///< currently in manual OVERRIDE?
  Reason   reason;
  float    rh;                ///< the SMOOTHED humidity, NAN when there is no reading
  uint32_t stateAgeSec;       ///< how long the current state has been held
  uint32_t overrideLeftSec;   ///< how long until it returns to AUTO (0 = not overriding)
  uint32_t lockoutLeftSec;    ///< how long until the refill lockout ends (0 = not locked)
  uint32_t dwellLeftSec;      ///< how long until a change is ALLOWED (0 = can change now)
};

/// The caller supplies the way to transmit IR.
///
/// RETURN FALSE WHEN IT CANNOT TRANSMIT (no code learned). The controller will then
/// NOT update its belief about the state -- because the real unit did not change
/// either. This is a very easy thing to get wrong: treating it as changed makes the
/// panel believe the unit is running, so it never retries, and the room stays dry
/// while the log says "turned on".
typedef bool (*Emitter)(bool on);

/// Call once in setup(), AFTER IrStore::begin().
void begin(Emitter emit);

/// One measure-and-decide round. [rhRaw] is the RAW humidity (the median of the
/// corners); NAN = no reading yet.
///
/// IT MUST BE CALLED AT EXACTLY TICK_MS -- this function DELIBERATELY does not rate
/// limit itself. EMA_ALPHA is tightly coupled to the call interval (0.2 at 5 seconds
/// = a time constant of ~25 seconds), so rate limiting internally would hide the
/// fact that the caller is calling at the wrong rate: the filter would silently
/// change its time constant with no symptom at all. Leaving the caller to keep the
/// interval puts that interval somewhere visible, right there in loop().
void tick(float rhRaw, uint32_t nowMs);

/// Manual on/off -- enters OVERRIDE, expiring by itself after OVERRIDE_HOLD_SEC.
/// It ALSO clears the refill lockout: "I have just refilled it, run again" is the
/// main use case.
void manualSet(bool on, uint32_t nowMs);

/// Leave OVERRIDE and return to AUTO immediately.
void backToAuto(uint32_t nowMs);

Status status(uint32_t nowMs);

/// A short description of [r] -- ACCENTED Vietnamese (it appears on the LVGL
/// screen, and the ui/fonts cover the full range). The serial log uses
/// reasonAscii() instead -- see the note in the .cpp.
const char *reasonText(Reason r);

}  // namespace HumidifierControl
