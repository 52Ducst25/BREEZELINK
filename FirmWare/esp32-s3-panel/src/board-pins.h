#pragma once
// ============================================================================
//  BreezeLink - PER-BOARD PINOUT. Selected with a -D flag in platformio.ini.
// ----------------------------------------------------------------------------
//  WHY THIS IS SEPARATE FROM config.h: config.h holds SECRETS (WiFi password,
//  MQTT token) and IDENTITY (device_uuid) -- things tied to ONE HOUSEHOLD, not to
//  a board. Two different panel boards run against the same `devices` row on the
//  web UI, so they must SHARE a config.h, while their pins are completely
//  different. Mixing the two kinds into one file means that every board change
//  requires editing the file containing the passwords -- and that file is
//  gitignored, so a bad edit cannot be reviewed and git cannot recover it.
//
//  ADDING A NEW BOARD: add an #elif branch below, do NOT touch config.h.
//
//  THE IR PINS ARE NOT HERE. `IR_TX_PIN`/`IR_RX_PIN` have to live in build_flags
//  alongside the TFT_* flags, because both groups are read while compiling the
//  LIBRARIES, not only while compiling src/.
// ============================================================================

/// That hardware is not present on this board. 255 rather than -1: every pin API
/// in this project takes a uint8_t, so -1 silently becomes 255 in one place and
/// 0xFF in another.
#define PIN_NONE 255

#if defined(BOARD_S3_PANEL)
// ============================================================================
//  2.8" ESP32-S3 panel board  (ILI9341V display + FT6336G touch + I2S mic/speaker
//  + SD card)
// ----------------------------------------------------------------------------
//  REPLACES the QR Box Advance. The QR Box board has a broken programming circuit
//  (esptool detects the chip but every write aborts partway), and this board comes
//  with capacitive touch and a display of the same 2.8" 240x320 size, so the LVGL
//  interface ports across unchanged.
//
//  WHAT TO KNOW BEFORE SOLDERING: the board is nearly out of pins. Everything is
//  spoken for except:
//    IO2, IO3, IO14, IO21   -- the "Expand pin" row in the board documentation
//    IO43, IO44             -- UART0, free ONLY because the console goes over
//                              native USB
//  Already spoken for: display (10/11/12/13/45/46), touch (15/16/17/18), SD card
//  (38/39/40/41/47/48), I2S audio (1/4/5/6/7/8), RGB LED (42), battery monitor
//  (9), BOOT button (0).
//
//  Allocated as follows:
//    IO2  IR transmit    IO43/44  UART to the UNO Q (see platformio.ini: the
//    IO3  IR receive              direction must NOT be swapped, and the native
//                                 USB precondition)
//    IO14 IO21  RESERVED for the PZEM in a later phase -- Modbus-RTU over UART,
//               2 pins.
//
//  So there is STILL NO PIN LEFT FOR A BUZZER (see BUZZER_PIN).
// ============================================================================

//  I2C bus: only the FT6336G touch controller (0x38). NO DS1307, NO SHT3x.
//  Since this bus now has just one chip and that chip tolerates 400kHz, the 100kHz
//  ceiling in touch.cpp is redundant -- but leave it, it costs nothing measurable
//  (5 bytes read every 5ms).
#define I2C_SDA_PIN        16
#define I2C_SCL_PIN        15
#define TOUCH_RST_PIN      18
#define TOUCH_INT_PIN      17

//  Backlight: HIGH = lit, PWM-capable.
//
//  IO45 IS A STRAPPING PIN (VDD_SPI) -- IT MUST BE LOW AT RESET; if it is high the
//  chip selects the 1.8V flash level and the board WILL NOT BOOT. It is safe here
//  because LEDC only attaches to the pin after boot completes. BUT: NEVER SOLDER A
//  PULL-UP onto this pin, and do not let any firmware drive it high and then do a
//  soft reset.
#define LCD_BACKLIGHT_PIN  45

//  NO BUZZER ON THIS BOARD.
//
//  Not an oversight: the four free pins all went to IR + the UNO Q UART (see the
//  top of this block), and the buzzer is the most expendable of the lot -- all it
//  signals is "touch registered", which the screen can do with a toast.
//
//  The board DOES have a speaker over I2S (IO1/4/5/6/7/8) so a beep is still
//  feasible later, it just has to play a PCM sample over I2S rather than PWM on a
//  pin -- separate work, not bundled into this board migration.
#define BUZZER_PIN         PIN_NONE

//  NO TXS0104 (level shifter). This board brings 3.3V straight out to the header
//  with no buffer IC at all -- so the UART link to the UNO Q depends on no enable
//  pin whatsoever. This is a GOOD THING: the QR Box board's biggest trap
//  (forgetting to raise OE leaves the UART completely mute with no error at all)
//  disappears.
#define EN_LEVEL_SHIFT_PIN PIN_NONE

//  NO DS1307. The clock comes from NTP into the chip's system clock.
//  A CONSEQUENCE YOU MUST KNOW: a power cut loses the time (there is no backup
//  battery), so after every boot the status bar shows "--:--" until the network is
//  up and SNTP answers -- usually a few seconds. That is correct behaviour, not a
//  bug.
#define HAS_RTC_DS1307     0

//  1 or 3 = landscape 320x240 (the native panel is 240x320 portrait, rotated in
//  software).
//  NOT YET MEASURED ON REAL HARDWARE -- if the image is correct but upside down by
//  180 degrees, change it to 3.
#define TFT_ROTATION       1

//  Rotate/flip the touch coordinates into the TFT's frame.
//  NOT YET MEASURED ON REAL HARDWARE. How to adjust: touch the TOP-LEFT corner of
//  the screen and watch the log.
//    - touching left/right moves the cursor up/down  -> change TOUCH_SWAP_XY
//    - touching left lands on the right               -> change TOUCH_INVERT_X
//    - touching the top lands at the bottom           -> change TOUCH_INVERT_Y
//  The three flags are independent; change one at a time, never two at once.
#define TOUCH_SWAP_XY      1
#define TOUCH_INVERT_X     0
#define TOUCH_INVERT_Y     1

#else
// ============================================================================
//  QR Box Advance Touch Screen board  (ESP32-WROOM-32E-N8 + YT280S030/ST7789
//  display)
// ----------------------------------------------------------------------------
//  The default when no board flag is declared -- preserves the old behaviour.
//  How the pinout was reverse-engineered from the schematic:
//  ../../Interface/README.md
// ============================================================================

//  Shared I2C bus: the display's touch controller (0x38/0x5D/0x15) + DS1307
//  (0x68) + SHT3x (0x44).
//  DO NOT raise it to 400kHz -- the DS1307 only tolerates 100kHz, see
//  Interface/README.md §2.1.
#define I2C_SCL_PIN        4
#define I2C_SDA_PIN        16
#define TOUCH_RST_PIN      25
#define TOUCH_INT_PIN      33

//  Backlight via Q5 (a low-side BSS138): HIGH = lit, PWM-capable.
#define LCD_BACKLIGHT_PIN  27
#define BUZZER_PIN         13

//  OE of the TXS0104 (the level shifter for UART_1 out to port P3).
//
//  THE UART LINK TO THE UNO Q (GPIO2/15) GOES THROUGH THIS IC -- so this pin is a
//  precondition for it working at all. With OE low the outputs float and the UART
//  is completely mute with no error at all. (IR does not: both the GPIO5
//  transmitter and the GPIO17 receiver are 3.3V direct.)
//
//  It MUST be set inside setup() rather than pulled up with an external resistor:
//  GPIO12 = MTDI, and HIGH at reset makes the ROM select the 1.8V flash level so
//  the board does not boot. On this board R7 10k pulls GPIO12 DOWN to GND -- which
//  is correct (cross-checked against the schematic).
#define EN_LEVEL_SHIFT_PIN 12

//  The DS1307 has its own backup battery -> it keeps time across a power cut.
#define HAS_RTC_DS1307     1

//  1 or 3 = landscape 320x240 (the native panel is 240x320 portrait, rotated in
//  software).
//  This board uses 1 -- measured on real hardware: 3 gives a CORRECT image but
//  UPSIDE DOWN by 180 degrees.
#define TFT_ROTATION       1

//  Rotate/flip the touch coordinates into the TFT's frame. Every batch of modules
//  laminates the touch panel with a different axis orientation and the schematic
//  says nothing about it -- test by touching and adjust these three flags.
#define TOUCH_SWAP_XY      1
#define TOUCH_INVERT_X     0
#define TOUCH_INVERT_Y     1

#endif
