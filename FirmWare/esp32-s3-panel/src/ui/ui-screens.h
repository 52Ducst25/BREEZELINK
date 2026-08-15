#pragma once
#include <lvgl.h>
#include "ui.h"

// ============================================================================
//  Building and updating the LVGL widget tree for the 4 screens + the learn
//  overlay.
// ----------------------------------------------------------------------------
//  SPLIT OUT OF ui.cpp so each file does one job: ui.cpp handles the bridge between
//  LVGL and the hardware (display, touch, task, command queue), while this file
//  handles the CONTENT being displayed.
//
//  IT MAY ONLY BE CALLED FROM THE UI TASK. LVGL is not thread-safe -- loop()
//  touching it corrupts the widget tree at random, which is very hard to track
//  down.
//
//  Build ONCE and then only change the content: LVGL knows which regions are dirty
//  and redraws only those. This is what the TFT_eSPI version had to do by hand, by
//  keeping a "what was drawn last time" copy of every field.
// ============================================================================
namespace Screens {

/// The user pressed SEND / AUTO. ui.cpp forwards it into the queue for loop().
using CommandFn = void (*)(const Ui::Command &cmd);

/// The user pressed something in the SETTINGS screen. ui.cpp executes it
/// (backlight, NTP, reset) because that is hardware owned by the UI task.
///
/// BUZZER_ON / BUZZER_OFF ARE GONE. The "ÂM THANH" row has been removed from the
/// Settings screen: the ESP32-S3 panel board HAS NO BUZZER (board-pins.h:
/// `BUZZER_PIN = PIN_NONE` -- its four free pins all went to IR and the UART to the
/// UNO Q). A toggle for hardware that does not exist is the worst kind of control:
/// it presses, it changes colour, and it does nothing -- the user will go looking
/// for a fault in the speaker.
///
/// The click sound KEEPS its wiring (Theme::setPressSound -> BoardIo::beep):
/// beep() stays silent when the pin is 255, and the old QR Box board, which shares
/// this source, still has a real buzzer.
enum Setting : uint8_t { BRIGHT_DOWN, BRIGHT_UP, REBOOT };
using SettingFn = void (*)(Setting s);

/// Build everything. Called once from Ui::begin(), after Theme::init().
void build(CommandFn onCmd, SettingFn onSetting);

/// Feed new data in. Called at the draw interval (200 ms) from the UI task.
void update(const Ui::Model &m);

/// A short notification line at the bottom of the content area. [isError] colours
/// it red.
void toast(const char *msg, bool isError = false);

/// Call every UI task iteration so the toast dismisses itself after a few seconds.
void tickToast(uint32_t nowMs);

/// The status bar clock, WITH SECONDS. [valid]=false shows "--:--:--" -- if the
/// DS1307 has never been set, say so plainly rather than showing 00:00:00 as though
/// it were the real time.
///
/// The seconds are more useful than they look: they are the only sign of LIFE on
/// screen when every reading is static -- watching the seconds tick tells you the UI
/// task is still running rather than the screen having frozen on an old frame.
void setClock(bool valid, uint8_t hh, uint8_t mm, uint8_t ss);

/// Reflect the hardware state onto the SETTINGS screen.
void setBrightness(uint8_t percent);

/// Add a line to the command log (newest first, keeping the last 8).
///
/// [clockValid]/[hh]/[mm] are the DS1307 time read at arrival. If the time has
/// never been set, clockValid=false and the line shows a RELATIVE timestamp counted
/// from [nowSec] (uptime in seconds) -- "3 minutes ago". It does not fabricate
/// 00:00: the same rule as the status bar clock, better to say "I do not know the
/// time" than to give a wrong number.
void addLog(const Ui::CmdLog &e, bool clockValid, uint8_t hh, uint8_t mm, uint32_t nowSec);

} // namespace Screens
