#include "humidifier-control.h"

#include <Preferences.h>
#include <math.h>

namespace HumidifierControl {
namespace {

Emitter g_emit = nullptr;

bool   g_on = false;
Reason g_reason = Reason::BOOT;

float    g_rh = NAN;           ///< the smoothed humidity
uint32_t g_lastGoodMs = 0;     ///< last time there was a valid reading (0 = never)

uint32_t g_lastSwitchMs = 0;   ///< last time the state CHANGED SUCCESSFULLY
uint32_t g_lastTryMs = 0;      ///< last IR transmit ATTEMPT, including failures
uint32_t g_onSinceMs = 0;      ///< last time the unit was switched on

bool     g_override = false;
uint32_t g_overrideStartMs = 0;

bool     g_lockout = false;
uint32_t g_lockoutStartMs = 0;

// Our belief about the state, preserved across power cuts.
//
// REQUIRED BECAUSE OF TOGGLE REMOTES: for a household whose humidifier has only one
// power button, the panel cannot measure whether the unit is on or off -- it only
// REMEMBERS what it last transmitted. Forgetting that belief on every power cut
// would give every subsequent command a 50% chance of going the wrong way. With a
// remote that has two separate buttons this field is nearly harmless:
// retransmitting changes nothing, so a wrong belief corrects itself on the next
// decision.
//
// ITS OWN NAMESPACE, not sharing IrStore's "aircon-ir": the IR store is sometimes
// wipe()d to force the backend to resend everything, and taking the state belief
// down with it would be a side effect nobody could predict from that function's
// name.
Preferences g_nvs;
bool        g_nvsReady = false;
const char *const NVS_NS  = "bl-humid";
const char *const NVS_KEY = "on";

void rememberOn(bool on) {
  if (g_nvsReady) g_nvs.putBool(NVS_KEY, on);
}

/// The same content as reasonText() but in English, for the serial log.
///
/// TWO SETS OF STRINGS FOR ONE LIST IS DELIBERATE, not a failure to merge them. The
/// two readers are different and their rules point in opposite directions:
///   LVGL screen -> ACCENTED Vietnamese; the ui/fonts/ cover the full range, and
///                  every other label on the panel is accented, so an unaccented
///                  line here would look out of place.
///   serial      -> English, following the convention of every log in this project:
///                  many serial monitor windows cannot render UTF-8 and turn
///                  accented text into garbage -- and the log is what people read at
///                  exactly the moment they are stuck.
/// Merging them means sacrificing one of the two, and neither is worth sacrificing.
const char *reasonAscii(Reason r) {
  switch (r) {
    case Reason::AUTO_DRY:    return "room drier than the on threshold";
    case Reason::AUTO_WET:    return "room wetter than the off threshold";
    case Reason::DEADBAND:    return "inside the hysteresis band - unchanged";
    case Reason::DWELL_HOLD:  return "waiting out the minimum hold time";
    case Reason::MANUAL:      return "manually overridden";
    case Reason::MAX_RUN:     return "ran too long - cut off";
    case Reason::LOCKOUT:     return "refill lockout";
    case Reason::SENSOR_LOST: return "lost the humidity reading";
    case Reason::NO_CODE:     return "no code learned - use the app to learn it";
    case Reason::BOOT:        return "just started";
  }
  return "?";
}

/// Seconds elapsed since [sinceMs]. SIGNED arithmetic so it stays correct across
/// the millis() wrap at ~49 days. A wall-mounted panel runs continuously so that
/// point really does arrive; with unsigned subtraction, on day 49 every timer would
/// jump and the unit would be cut off for "running too long" seconds after being
/// switched on.
uint32_t elapsedSec(uint32_t sinceMs, uint32_t nowMs) {
  const int32_t d = (int32_t)(nowMs - sinceMs);
  return d > 0 ? (uint32_t)d / 1000u : 0u;
}

/// Change the REAL state: transmit IR first, only record it if the transmission
/// worked.
void applyState(bool want, Reason why, uint32_t nowMs) {
  if (g_emit == nullptr) return;

  // The ATTEMPT timestamp, written before we know the result: it is what throttles
  // the retry loop when no code has been learned. Do NOT use g_lastSwitchMs for
  // that any more -- the dwell now only blocks the off direction, so loading it here
  // would both fail to throttle the on-direction log spam and silently push out the
  // off-direction dwell deadline because of one failed transmission.
  g_lastTryMs = nowMs;

  if (!g_emit(want)) {
    // Could not transmit (no code learned). Do NOT change g_on -- the real unit did
    // not change either.
    g_reason = Reason::NO_CODE;
    return;
  }

  g_on = want;
  g_reason = why;
  g_lastSwitchMs = nowMs;
  if (want) g_onSinceMs = nowMs;
  rememberOn(want);

  Serial.printf("[humid] %s the humidifier - %s\n", want ? "ON" : "OFF", reasonAscii(why));
}

/// One decision round. Split out of tick() so that tick() only deals with the
/// cadence and the EMA.
void decide(uint32_t nowMs) {
  // --- 1) Running too long: CUT, overriding even a manual override -----------
  // At the top because this is the hardware-protection branch. Putting it below the
  // override branch would let a single button press keep the unit running
  // indefinitely -- and "pressed the button and forgot" is precisely the case
  // MAX_RUN exists to catch.
  if (g_on && elapsedSec(g_onSinceMs, nowMs) >= MAX_RUN_SEC) {
    g_override = false;
    applyState(false, Reason::MAX_RUN, nowMs);
    g_lockout = true;
    g_lockoutStartMs = nowMs;
    Serial.println("[humid] ran continuously for too long -> cut off and locked out. GO CHECK THE TANK.");
    return;
  }

  // --- 2) Manual override ---------------------------------------------------
  if (g_override) {
    if (elapsedSec(g_overrideStartMs, nowMs) >= OVERRIDE_HOLD_SEC) {
      g_override = false;
      Serial.println("[humid] OVERRIDE expired -> back to AUTO");
    } else {
      g_reason = Reason::MANUAL;
      return;
    }
  }

  // --- 3) No reading: CUT ---------------------------------------------------
  const bool sensorOk =
      !isnan(g_rh) && g_lastGoodMs != 0 &&
      elapsedSec(g_lastGoodMs, nowMs) < SENSOR_STALE_SEC;
  if (!sensorOk) {
    if (g_on) applyState(false, Reason::SENSOR_LOST, nowMs);
    else      g_reason = Reason::SENSOR_LOST;
    return;
  }

  // --- 4) Refill lockout ----------------------------------------------------
  if (g_lockout) {
    if (elapsedSec(g_lockoutStartMs, nowMs) < REFILL_LOCKOUT_SEC) {
      if (g_on) applyState(false, Reason::LOCKOUT, nowMs);
      else      g_reason = Reason::LOCKOUT;
      return;
    }
    g_lockout = false;
    Serial.println("[humid] refill lockout over -> automatic again");
  }

  // --- 5) Hysteresis (deadband) ---------------------------------------------
  bool   want = g_on;
  Reason why  = Reason::DEADBAND;
  if (g_rh < ON_BELOW_RH) {
    want = true;
    why  = Reason::AUTO_DRY;
  } else if (g_rh > OFF_ABOVE_RH) {
    want = false;
    why  = Reason::AUTO_WET;
  }

  if (want == g_on) {
    g_reason = why;
    return;
  }

  // --- 6) Dwell -- BLOCKS THE OFF DIRECTION ONLY ----------------------------
  //  Turning on happens immediately. The asymmetry is explained in full at
  //  DWELL_SEC in the .h; in short: turning on late leaves the room dry and the
  //  occupant notices, while turning off late only sprays for a few extra minutes.
  //  The 15-point deadband is the oscillation guard, so dropping the dwell on the
  //  on direction opens no path to rapid cycling.
  if (!want && elapsedSec(g_lastSwitchMs, nowMs) < DWELL_SEC) {
    g_reason = Reason::DWELL_HOLD;
    return;
  }

  // With no code learned, do not retry on every 5-second tick -- see
  // NO_CODE_RETRY_SEC.
  if (g_reason == Reason::NO_CODE &&
      elapsedSec(g_lastTryMs, nowMs) < NO_CODE_RETRY_SEC) {
    return;
  }

  applyState(want, why, nowMs);
}

}  // namespace

void begin(Emitter emit) {
  g_emit = emit;

  g_nvsReady = g_nvs.begin(NVS_NS, false /*read-write*/);
  // Reload the belief. Do NOT transmit IR here: we do not know the real unit's
  // state, and transmitting once "to be sure" with a toggle remote inverts the very
  // state we just restored.
  g_on = g_nvsReady && g_nvs.getBool(NVS_KEY, false);
  g_reason = Reason::BOOT;

  const uint32_t now = millis();
  // The dwell deadline starts at boot. IT NOW ONLY BLOCKS THE OFF DIRECTION, so it
  // no longer hard-locks the first 5 minutes as it used to: if the room is dry the
  // panel switches the unit on at the very first decision tick. That was exactly
  // the "slow response" bug that was reported.
  g_lastSwitchMs = now;
  g_lastTryMs    = now;
  // Count the runtime from boot rather than from the real switch-on -- we do not
  // know when that was, and UNDER-estimating it would make MAX_RUN cut off late.
  // Restarting from 0 only errs in the safe direction (cutting off later) when the
  // panel lost power together with the unit; acceptable, because MAX_RUN is the
  // second safety net, not the primary mechanism.
  g_onSinceMs = now;

  Serial.printf("[humid] started, believing the humidifier is %s%s\n",
                g_on ? "RUNNING" : "OFF",
                g_nvsReady ? "" : " (NVS error - the belief will not survive a power cut)");
}

void tick(float rhRaw, uint32_t nowMs) {
  // THE EMA ONLY RUNS ON VALID NUMBERS. Mixing in a NaN makes g_rh permanently NaN --
  // one corner dropping out for half a second would be enough to kill the filter for
  // good, and the symptom would be the unit shutting down with reason SENSOR_LOST
  // while the other three corners keep reporting perfectly normal numbers.
  if (!isnan(rhRaw)) {
    g_rh = isnan(g_rh) ? rhRaw : (EMA_ALPHA * rhRaw + (1.0f - EMA_ALPHA) * g_rh);
    g_lastGoodMs = nowMs;
  }

  decide(nowMs);
}

void manualSet(bool on, uint32_t nowMs) {
  // A manual press also clears the refill lockout: the main use case for this
  // button is precisely "I have just refilled it, run again".
  g_lockout = false;
  g_override = true;
  g_overrideStartMs = nowMs;

  if (on == g_on) {
    // The state is already what was asked for -- still enter OVERRIDE (to stop
    // automation changing it) but do NOT transmit IR. One redundant transmission
    // with a toggle remote inverts exactly what was just requested.
    g_reason = Reason::MANUAL;
    Serial.printf("[humid] OVERRIDE: holding %s\n", on ? "ON" : "OFF");
    return;
  }
  applyState(on, Reason::MANUAL, nowMs);
  // applyState() may fall into the NO_CODE branch and set a different reason. Only
  // overwrite the reason once the state really did change -- otherwise the screen
  // says "the user is OVERRIDING" when what the user needs to read is "NO CODE
  // LEARNED".
  if (g_on == on) g_reason = Reason::MANUAL;
}

void backToAuto(uint32_t nowMs) {
  g_override = false;
  // Do NOT reset to BOOT: the next decision round (within TICK_MS) will fill in the
  // real reason. Setting BOOT here makes the screen say "just started" on a panel
  // that has been running for days -- a false statement, even if only false for 5
  // seconds.
  decide(nowMs);
  Serial.println("[humid] back to AUTO");
}

Status status(uint32_t nowMs) {
  Status s;
  s.on          = g_on;
  s.overriding  = g_override;
  s.reason      = g_reason;
  s.rh          = g_rh;
  s.stateAgeSec = elapsedSec(g_lastSwitchMs, nowMs);

  s.overrideLeftSec = 0;
  if (g_override) {
    const uint32_t used = elapsedSec(g_overrideStartMs, nowMs);
    s.overrideLeftSec = used < OVERRIDE_HOLD_SEC ? OVERRIDE_HOLD_SEC - used : 0;
  }

  s.dwellLeftSec = s.stateAgeSec < DWELL_SEC ? DWELL_SEC - s.stateAgeSec : 0;

  s.lockoutLeftSec = 0;
  if (g_lockout) {
    const uint32_t used = elapsedSec(g_lockoutStartMs, nowMs);
    s.lockoutLeftSec = used < REFILL_LOCKOUT_SEC ? REFILL_LOCKOUT_SEC - used : 0;
  }
  return s;
}

const char *reasonText(Reason r) {
  switch (r) {
    case Reason::AUTO_DRY:    return "phòng khô hơn ngưỡng bật";
    case Reason::AUTO_WET:    return "phòng ẩm hơn ngưỡng tắt";
    case Reason::DEADBAND:    return "trong vùng trễ — giữ nguyên";
    case Reason::DWELL_HOLD:  return "chờ đủ thời gian giữ tối thiểu";
    case Reason::MANUAL:      return "đang ghi đè bằng tay";
    case Reason::MAX_RUN:     return "chạy quá lâu — đã cắt";
    case Reason::LOCKOUT:     return "khoá chờ đổ nước";
    case Reason::SENSOR_LOST: return "mất số đo độ ẩm";
    case Reason::NO_CODE:     return "chưa học mã — vào app để học";
    case Reason::BOOT:        return "vừa khởi động";
  }
  return "?";
}

}  // namespace HumidifierControl
