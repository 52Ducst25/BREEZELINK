#pragma once
#include <Arduino.h>

// ============================================================================
//  The QR Box Advance's discrete peripherals: backlight, buzzer, DS1307 clock.
// ----------------------------------------------------------------------------
//  Grouped in one place because all three are board peripherals with nothing to do
//  with air conditioning logic -- splitting them out leaves ui.cpp holding only
//  drawing and touch.
//
//  The DS1307 shares the I2C bus with the touch controller, so Touch::begin() has
//  to run FIRST (it is what calls Wire.begin and pins the bus at 100kHz).
// ============================================================================
namespace BoardIo {

// --- Backlight: a low-side Q5 BSS138, HIGH = lit ---
void backlightBegin(uint8_t pin);
/// 0..100. The practical floor is 10%: 0% makes the screen look exactly like a
/// fault, and the user will assume the node is dead and go unplug it.
void backlightSet(uint8_t percent);
uint8_t backlightGet();

// --- MLT-8530 buzzer via Q7 ---
void buzzerBegin(uint8_t pin);
void buzzerEnable(bool on);
bool buzzerEnabled();
/// A NON-BLOCKING beep: it sets a timer and returns immediately. buzzerTick()
/// switches it off.
void beep(uint16_t ms = 40, uint16_t freq = 2700);
void buzzerTick();

// --- DS1307 (I2C 0x68) ---
struct Clock { uint8_t hh, mm, ss; };

/// Read the time. Returns false if the chip does not answer OR if the oscillator is
/// halted (the CH bit) -- i.e. the time has never been set. Do not guess: the UI
/// showing "--:--" is better than showing 00:00 as though it were the real time.
///
bool clockRead(Clock &out);

/// Set the DS1307's time (24-hour mode, clearing the CH bit so the oscillator
/// runs).
///
/// It MUST be called from the UI task -- the DS1307 sits on the I2C bus that task
/// owns.
bool clockWrite(uint8_t hh, uint8_t mm, uint8_t ss);

// --- Time sync over NTP ---
//
//  IT IS BACK, BUT AUTOMATIC. An earlier version had a "SYNC TIME" button in
//  Settings, removed in 42332bc because making people press it by hand is a chore
//  everyone forgets. Removing the time-setting path entirely exposed a worse
//  consequence: the DS1307 has its own backup battery, so a WRONG value is
//  preserved forever, and with the CH bit already cleared clockRead() still reports
//  it as valid -- the screen displays a wrong time with complete confidence and no
//  path anywhere in the firmware can correct it.
//
//  So this time there is no button: the node syncs itself whenever it has a
//  network, and the DS1307 falls back to its proper role of holding the time
//  through a network or power outage.
void ntpBegin();

/// Query the system clock; if SNTP has answered, write it down to the DS1307.
/// Returns false while SNTP has no result yet -- it does NOT wait, just call it
/// again on the next tick.
/// Also must be called from the UI task (it writes I2C).
bool ntpPoll();

} // namespace BoardIo
