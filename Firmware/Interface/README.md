# Screen interface for the INDOOR node — `QR_Box_Advance_TouchScreen` board

Touch screen design for the indoor node, replacing the current bare ESP32 DevKit V1 board
(`../esp32-qrbox/`). Before this version the indoor node had **no local display**: everything
was only visible through the app/web, so during a network outage the user could be standing
right next to the unit with no way to tell whether the node was even alive.

This document is the **source of truth for the interface**. The code lives in
`../esp32-s3-panel/src/ui/`, built with the `qrbox-touch` env.

- `Lopaka/` — a Lopaka sketch from a different project, kept as an **API reference**
  (`tft.drawRoundRect`, `tft.setFreeFont`, `tft.pushImage`…). Not used directly: it draws its
  background from a hard-coded 320×240 image (537 KB of source for **one** screen), its text is
  English, and its layout is for a "GreenSystems" menu, not air conditioning. See §4 for why the
  embedded-background approach was dropped.

---

## 1. The board

`Research/Schematic/Touch_Screen_Extend_2026-03-11_13-31.PDF` — *QR Box Advance Touch Screen*,
rev 1.2, 9 pages.

| Block | Component | Note |
|---|---|---|
| MCU | **ESP32‑WROOM‑32E‑N8** | 8 MB flash, **no PSRAM** |
| Display | **YT280S030 2.8″ 240×320**, 4-wire SPI | J2 18-pin, **MISO not connected** · controller **ST7789V** — see the warning in §2.2 |
| Touch | Capacitive, **I²C** (J1 6-pin: SCL/SDA/INT/RST) | the controller is on the display module |
| RTC | **DS1307Z** + VBAT battery + 32.768 kHz crystal | shares the I²C bus with the touch controller |
| Buzzer | MLT‑8530 via Q7 BSS138 | GPIO13 |
| 4G | **A7680C** (UART2) | *not used by this project* → see §3 |
| Isolated I/O | 2 opto inputs + 2 pulse inputs + 1 opto output | *not used by this project* |
| Power | 9–24 VDC → TPS5430 → 5 V → 2× TLV75733 → 3.3 V | +5 V/1.2 A, plenty for the IR module |
| Supervision | TPS3823‑33 external watchdog | resets automatically if the MCU hangs |

Input power is **9–24 VDC** (P2/P4, XH2.54 jack) — quite different from a USB-powered DevKit
board. Apart from the UART0 debug port (P3) the board **has no USB port**: flash the firmware with
a USB‑TTL plugged into P3, and the AUTO BOOT circuit (Q1/Q2 + DTR/RTS) puts it into programming
mode just like a DevKit.

---

## 2. Pin table — reverse-engineered from the schematic

The U1 module pin → GPIO mapping is taken from the netlist on page 5 of the schematic.

| GPIO | Net on the board | Role | Note |
|---|---|---|---|
| 0 | `ESP_BOOT` | strapping, auto‑boot circuit | |
| 1 / 3 | `UART_0_TX/RX` | debug serial → P3 | the firmware log comes out here |
| 2 | `UART_1_RX` | UART1 through the TXS0104 → P3 | strapping · 5 V at the outer end · **unused** |
| 4 | `TOUCH_SCREEN_SCL` | **I²C SCL** (touch + DS1307) | R2 10k pull-up |
| 5 | `UART_2_TX` → A7680C RX | strapping · R6 100k pull-up | **IR receive** (§3.1) — usable because the 4G module is not fitted |
| 12 | `EN_LEVEL_SHIFT` | TXS0104 OE | **MTDI strapping — must be LOW at boot** |
| 13 | `BUZZER` | buzzer | |
| 14 | `SIM_PWD_CNT` | 4G module power control (Q4) | R11 10k pull-**down** |
| 15 | `UART_1_TX` | UART1 through the TXS0104 → P3 | strapping · 5 V at the outer end · **unused** |
| 16 | `TOUCH_SCREEN_SDA` | **I²C SDA** | R3 10k pull-up |
| 17 | `UART_2_RX` ← A7680C TX | R5 100k pull-up | **IR transmit** (§3.1) — usable because the 4G module is not fitted |
| 18 | `LCD_MOSI` | **TFT MOSI** | |
| 19 | `LCD_CS` | **TFT CS** | |
| 21 | `LCD_A0` | **TFT DC** | |
| 22 | `LCD_SCK` | **TFT SCLK** | |
| 23 | `LCD_RESET` | **TFT RST** (shared with the display module) | |
| 25 | `TOUCH_SCREEN_RST` | touch controller reset | |
| 26 | `SIGNAL_OUT_MCU` | TLP291 opto output | |
| 27 | `LCD_BACKLIGHT` | **backlight**, Q5 BSS138 low-side | HIGH = lit, PWM-capable |
| 32 | `DEVICE_PULSE_MCU` | opto input | |
| 33 | `TOUCH_INT` | touch interrupt | |
| 34 | `OTA` | OTA button/jumper | **input only** |
| 35 | `BILL_PULSE_MCU` | opto input | **input only** |
| 36 | `SIGNAL_IN_1` | opto input | **input only** |
| 39 | `SIGNAL_IN_2` | opto input | **input only** |

> **There is not a single free GPIO left.** Every pin brought out of the module is already
> spoken for. This is the constraint that governs the whole of §3 — this board was designed for
> a QR payment terminal, not for an air conditioning node.

### 2.1 Two traps in this I²C bus

1. **The DS1307 only tolerates 100 kHz.** The GT911 touch controller prefers 400 kHz, but the two
   share a bus, so **`Wire.setClock(100000)` has to be pinned**. Run it at 400 kHz and the clock
   reads back garbage times *without reporting an error* — the BCD checksum still validates.
2. **Address 0x38 is already taken** if the touch module uses an FT6236. Do not add an AHT20
   (also 0x38) to this bus — see §3.
   *(The board actually measured is a **GT911** at 0x5D/0x14 — but we still keep the SHT3x at
   0x44, because every batch of touch modules ships a different controller and it is not worth
   gambling on 0x38.)*

### 2.2 The display controller is an ST7789V, NOT the ILI9341 the schematic claims

The schematic (page 6) lists the part as `ILI9341SP4`, but the YT280S030 module is sold by its
manufacturer with **two controller options — ILI9341 *or* ST7789V**
([eya-display.com/yt280s030](https://www.eya-display.com/yt280s030/)) and this board has the
**ST7789V** variant fitted. The firmware must declare `-D ST7789_DRIVER=1`.

Declaring `ILI9341_DRIVER` by mistake is **very easy to misdiagnose**, because the display does not
die outright: the two controllers share many commands, so `fillScreen`/`fillRect` still produce the
right colours and only text and drawn shapes come out noisy/torn. That symptom looks exactly like
SPI noise or a multi-core race, and it has already cost several hours of searching in the wrong
direction.

**How to tell them apart in a single flash:** enable `-D LCD_SELFTEST=1`. It draws reference shapes
with simple commands and then freezes, *inside `setup()` on core 1, before the core-0 UI task
starts*. Clean reference shapes followed by a broken UI frame drawn immediately afterwards rules out
both multi-core issues and signal noise, leaving only the wrong driver.

---

## 3. Three conflicts with the current `esp32-qrbox` firmware

The indoor firmware needs 3 pins that this board has already used up:

| Function | Old pin | The conflict on the new board |
|---|---|---|
| DHT | GPIO4 | = `TOUCH_SCREEN_SCL` |
| IR receive | GPIO27 | = `LCD_BACKLIGHT` |
| IR transmit | GPIO26 | = `SIGNAL_OUT_MCU` (an opto — it cannot produce a 38 kHz carrier) |

### 3.1 The chosen solution

```
IR transmit -> GPIO5  = UART_2_TX   (the A7680C's RX pad), 3.3V direct
IR receive  -> GPIO17 = UART_2_RX   (the A7680C's TX pad), 3.3V direct
DHT22       -> GPIO2  = UART_1_RX,  out to port P3 THROUGH the TXS0104 (5V)

GPIO15 (UART_1_TX, out to P3 through the TXS0104) LEFT FREE
```

**IR transmit — GPIO5** (`UART_2_TX`, the A7680C's RX pad). 3.3 V direct, **not through the
TXS0104**, so it does not depend on `EN_LEVEL_SHIFT`. R6 100 kΩ pull-up is already fitted. It does
not reach a header — a wire has to be soldered to the pad.

At 3.3 V the transmitter module **should have its own transistor**; the LED-plus-resistor type
(KY-005 style) still works but at low current, so the range is poor.

> **GPIO5 is a strapping pin but harmless here.** Together with MTDO it selects the *SDIO slave
> timing* — a mode this design never uses, so its level at reset has no effect on booting. Quite
> unlike GPIO12/MTDI (which selects the flash voltage; get it wrong and the board is dead) or
> GPIO2 (which selects programming mode).

#### Why the IR transmitter must stay off the TXS0104 — tested on hardware

Going through the level shifter offered two attractive things: the pin **comes out directly on
header P3** (no soldering to a pad) and the output is **5 V**, so the range would be longer. Wired
to GPIO15, **the transmitter module's LED lit up continuously**.

In its steady state the TXS0104 only *holds* the level through a ~40 kΩ resistor — its one-shot
circuit only drives hard for a few tens of ns during a transition. It **cannot pull the module's
input low**, and it **cannot supply the current** for an infrared LED or an NPN base. A double
failure: IR is mute, while the LED runs on DC continuously → it gets hot and degrades quickly.

This is a **limitation of the IC, not of the pin**, so GPIO2 (the other half of the same `UART_1`
pair) behaves the same way — do not retry that route. GPIO5 drives a **real push-pull** output so
it avoids both problems; the price is a 3.3 V level and hence a shorter range.

**IR receive — GPIO17** (`UART_2_RX`, the A7680C's TX pad). 3.3 V direct, **not through the
TXS0104**. R5 100 kΩ pull-up is already fitted. It does not reach a header — a wire has to be
soldered to the pad.

- **Power the receiver from 3.3 V, not 5 V.** No ESP32 pin tolerates overvoltage (absolute maximum
  VDD+0.3 ≈ 3.6 V).
- GPIO17 **carries no strapping role at all** — the cleanest of the remaining pins.

**MANDATORY PRECONDITION: the A7680C module must not be soldered to the board.** Both IR pins
borrow its pads. Fit it and both pins have two owners, leaving IR completely mute.

**DHT22 — GPIO2** (`UART_1_RX` → P3 through the TXS0104). This is the **only thing still going
through the level shifter**, so `EN_LEVEL_SHIFT` (GPIO12) has to be HIGH before temperature/humidity
can be read at all. `OE` low → the output floats → permanent `NaN`.

**Verified working on exactly this pin:** `[telemetry] t=30.1°C h=49% -> sent`, reading 30.1 then
29.9 in succession — numbers drifting naturally rather than jumping about. This was **not obvious**:
the DHT22 uses a bidirectional one-wire protocol (the MCU pulls low for ~1 ms to start, then
**releases** so the sensor can drive) while the TXS0104 infers direction with a one-shot circuit —
a mid-transaction direction reversal is exactly where this class of IC tends to fail. What failure
would look like: intermittent reads with interleaved `NaN` (the checksum catches the bad ones, so
you get **no** wrong numbers).

- **The 4.7 kΩ pull-up must go on the P3 SIDE, not soldered to the chip pin.** Both GPIO2 and
  GPIO15 (left free next to it) are strapping pins, and they fail in opposite directions:
  - **GPIO2** selects programming mode — at reset it must be **low or floating**. Pulling it high
    at the chip is **losing the USB-TTL programming route entirely**, and the board has no other.
  - **GPIO15** is **MTDO** — pulling it **low** at reset **disables the entire boot log** on U0TXD,
    a symptom that looks exactly like a dead board.

  Placing it on the P3 side makes both safe: at reset the R7 10 kΩ holds GPIO12 low, so the TXS0104
  is in Hi-Z and completely isolates any external resistor from the chip pins.
- The R5/R6 100 kΩ resistors on the A7680C pads are **far too weak** for the DHT22's ~1 µs edges →
  either pin needs an additional 4.7 kΩ, otherwise every read comes back `NaN`.
- The firmware reads the DHT22 **in `loop()` (core 1)**, not in the UI task: decoding requires
  interrupts to be off for ~5 ms, and core 0 is where the WiFi stack runs. The read interval is
  2.5 s (the datasheet requires a minimum of 2 s).

> **Do not put `DHT_PIN` in the GPIO34…39 range.** Those are **input-only pins** — they have no
> output driver — so the MCU cannot pull the line low and the sensor **never replies**. The danger
> is that it **fails silently**: `pinMode(36, OUTPUT)` is valid code, the compiler does not warn,
> the ESP32 quietly does nothing, and the only symptom is permanent `NaN` — which looks exactly
> like a broken wire or a dead sensor. The firmware has a guard that prints a warning at boot and
> disables reading altogether (`DHT_PIN_OK` in `main.cpp`).

#### Why the receiver does NOT use GPIO36/39 (`SENSOR_VP`/`VN`) — tried and measured on hardware

On the schematic those two pins look ideal: they are the **collectors of the TLP291 optos** (U7/U9)
in the `PULSE OUT CONFIG` block, and they **already have 10 kΩ pull-ups to 3.3 V** (R22/R27), so
nothing extra would need fitting. Measured in practice it fails on all three counts — the receiver
outputs **2.08 V idle, 4.85 V when signalling**:

| Measured | Why it fails |
|---|---|
| 4.85 V when signalling | Exceeds the pin's absolute maximum (~3.6 V) → the ESD diode conducts back into the 3.3 V rail on every signal, damaging the pin over time |
| 2.08 V idle | **Falls between** the two logic thresholds (low < ~0.83 V, high > ~2.48 V) → the chip cannot read it as 0 or 1, the input oscillates and draws shoot-through current in the buffer stage |
| Idle low, high when signalling | **The wrong polarity.** A TSOP/VS1838B must idle HIGH and pull LOW on a burst — `IRremoteESP8266` assumes exactly that, and reversed polarity learns garbage codes |

If you ever come back to those two pins, three things to remember:

- Connect on the **MCU side (`SIGNAL_IN_MCU_x`)**, not to `SIGNAL_IN_x` — the far end is the opto's
  LED behind a 750 Ω resistor: the signal is **inverted** and the TLP291 **smears the edges** by
  tens of µs (it is a general-purpose opto, dependent on CTR), completely ruining mark/space timing.
- The opto sits **in parallel** on the same node as the receiver. Normally its LED has no current →
  the phototransistor is open → the node is free for the receiver to drive. But feed a pulse into
  `SIGNAL_IN_x` on the header and the opto conducts and **clamps the node low**, overriding
  everything.
- **ESP32 erratum:** GPIO36/39 get a ~80 ns glitch to low every time the SAR ADC block powers
  up/down, and `IRrecv` captures using an **edge interrupt** (`attachInterrupt CHANGE`), so that
  glitch lands straight in the received frame → **do not call `analogRead()` on any ADC1 pin.**

`EN_LEVEL_SHIFT` (GPIO12) is **MTDI**: HIGH at reset makes the ROM select the 1.8 V flash level and
the board **will not boot** — never pull it up with an external resistor, it has to be set inside
`setup()`. On this board **R7 10 kΩ pulls GPIO12 down to GND**, which is correct (cross-checked
against schematic page 5).

**The SHT3x is still in the firmware.** It is probed on I²C 0x44 at startup and **preferred if
present** (every read is CRC-verified, making it more trustworthy than a DHT22); with nothing wired
it falls back to the DHT22 automatically. The two never write readings at the same time — if they
did, the number on screen would flip back and forth between two sources a few tenths of a degree
apart, looking exactly like a failing sensor.

| Sensor | Address | Verdict |
|---|---|---|
| **SHT30 / SHT31 / SHT35** | **0x44** | ✅ the best option — conflicts with nothing, more accurate than a DHT22 |
| SHT40 | 0x44 | ✅ equivalent |
| AHT20 / AHT21 | 0x38 | ❌ **collides with the FT6236 touch controller** |
| DS1307 (already fitted) | 0x68 | — |
| Touch | 0x38 / 0x5D / 0x15 | — |

Where to wire the SHT3x: the I²C bus has no header, so tap it from **J1** (pin 2 = SCL, pin 3 = SDA,
pin 1 = 3V3, pin 6 = GND) or from the DS1307 pins.

### 3.2 The new pinout for the indoor node on this board

```
IR transmit -> GPIO5   (the A7680C's RX pad — solder directly, 3.3V)  ┐ do NOT fit
IR receive  -> GPIO17  (the A7680C's TX pad — solder directly, 3.3V)  ┘ the 4G module
              (power the receiver at 3.3V, NOT 5V — no pin tolerates overvoltage)
DHT22       -> GPIO2   (UART_1_RX -> P3 through the TXS0104, 5V; 4.7k pull-up ON THE P3 SIDE)
GPIO15      -> LEFT FREE (UART_1_TX -> P3 through the TXS0104)

PULL-UPS FOR LINES GOING THROUGH P3 GO ON THE P3 SIDE, never soldered to the chip pin:
  GPIO2  strapping, selects programming mode — pulling it HIGH at the chip loses USB-TTL flashing
  GPIO15 strapping MTDO                     — pulling it LOW at the chip kills the whole boot log

Level-shifter EN -> GPIO12 (set HIGH inside setup(), NEVER pulled up with an external resistor)
                  — ONLY the DHT22 goes through it now; without it temperature/humidity is NaN
                    forever. IR does not need it.
Display     -> ST7789V, MOSI 18 / SCK 22 / CS 19 / A0(DC) 21 / RST 23, landscape (rotation 1)
Temp/humid  -> DHT22 on GPIO2; or SHT3x on I²C 0x44 (SCL GPIO4 / SDA GPIO16) if fitted, preferred
```

---

## 4. Design system

### 4.1 Principles

This interface follows **exactly the palette and geometry of the Flutter app and the admin web**
("Titanium Command": dark carbon background, square borders, **45° chamfered corners**, engineering
blue `#0055FF`) — see `app-flutter/lib/theme/ac_colors.dart`. The user looks at the panel on the
wall and at the app on their phone within the same minute; if the two look like different design
systems they read as two different products.

Three content rules, inherited verbatim from the app:

1. **Never invent a number.** With no reading available, show `—`, not `0.0`. This is the bug the
   predecessor app made and that `ComfortPreview` was designed to block at the type level — the
   display must not reintroduce it.
2. **Thermal colour expresses MEANING, not SOURCE.** `thermalCold/Neutral/Warm/Hot` are applied by
   *value*, never by "this is an indoor or outdoor sensor".
3. **No dead keys.** A button with no learned IR code is shown dimmed with the reason stated, never
   silently ignored.

### 4.2 Colours — RGB565 converted from `AcColors`

| Token | App hex | RGB565 |
|---|---|---|
| `carbon` (background) | `#0A0E14` | `0x0862` |
| `carbonUp` | `#121924` | `0x10C4` |
| `carbonPanel` | `#141C28` | `0x10E5` |
| `carbonLine` | `#2A3B4C` | `0x29C9` |
| `carbonLineBright` | `#3E5468` | `0x3AAD` |
| `ice` (accent) | `#0055FF` | `0x02BF` |
| `iceText` | `#4D8DFF` | `0x4C7F` |
| `white` | `#E7F1F8` | `0xE79F` |
| `whiteDim` | `#8DA2B5` | `0x8D16` |
| `success` | `#22C55E` | `0x262B` |
| `error` | `#FF4D4D` | `0xFA69` |
| `warning` | `#F5A623` | `0xF524` |
| `thermalCold` | `#3AA0FF` | `0x3D1F` |

### 4.3 Text — ACCENTED Vietnamese via VLW fonts

TFT_eSPI's GFX fonts (`FreeSansBold12pt7b`…) are indexed by **a single byte** and only cover ASCII
`0x20`–`0x7E`. Writing `LÀM LẠNH` with a GFX font produces boxes or dropped diacritics. The display
therefore uses **VLW fonts** (smooth fonts, indexed by Unicode code point) — an earlier revision of
this document settled on "write without diacritics, it is cheaper"; that has now been reversed.

The fonts are **embedded directly into flash** as `PROGMEM` arrays rather than kept in SPIFFS:
`tft.loadFont(const uint8_t*)` can read from flash, so there is no SPIFFS partition to carve out and
nothing to remember about running `pio run -t uploadfs` on every flash — exactly the kind of extra
step an installer will forget, and forgetting it gives a blank white screen with no error.

Regenerating the fonts: `python tools/make_vlw.py` (requires Pillow). The script reads Windows'
Arial TTF, rasterises each glyph and writes `src/ui/fonts/*.h`. It only needs rerunning when the
text sizes or the character set change.

> **THE BIGGEST TRAP:** once `loadFont()` has been called, both `setFreeFont()` **and**
> `setTextFont()` are **ignored** — you cannot mix VLW with GFX fonts in the same frame. So the
> entire type scale has to be rebuilt in VLW, and every string sent to the display must be UTF-8
> (`drawString` decodes it itself via `decodeUTF8`).

| Role | VLW font | Source | Character set |
|---|---|---|---|
| Large numbers (temperature, setpoint) | `VietFontBig` 34px | Arial Bold | digits + `°C` only (18 glyphs) |
| Headings, button labels | `VietFontLabel` 17px | Arial Bold | the full 230 glyphs |
| Secondary labels, status bar | `VietFontSmall` 13px | Arial | the full 230 glyphs |

The large-number font **deliberately has no Vietnamese glyphs** — it only ever shows temperatures,
so cutting it to 18 glyphs makes it ~15× smaller. All three sizes together: **~70 KB of flash**.

The 230-glyph set = printable ASCII + `°` + **134 precomposed Vietnamese characters**. Deliberately
NOT trimmed to "the characters the UI currently uses": adding a new line of text later with a
missing glyph makes the text **vanish from the screen while the build stays green** — a silent
failure that is very hard to track down.

Changing text size has a real cost (one `malloc` plus re-reading the 230-glyph table from flash), so
`Theme::useFont()` skips the call when the size has not changed. Because the UI only redraws fields
whose **value changed**, most frames never switch fonts at all.

### 4.4 Why we do NOT embed a background image like the Lopaka version

`Lopaka/Main.cpp` embeds a `uint16_t[76800]` array = **150 KB of flash for one screen**. Four
screens plus the learn screen would be ~750 KB, and every screen change would have to push 150 KB
over SPI (~40 ms at 40 MHz) → a clearly visible flash.

Replaced by **flat panels drawn geometrically**: `chamferRect()` (45° chamfered corners) matches the
brand geometry, costs 0 bytes of flash, and allows **redrawing individual cells** instead of the
whole screen. See §7.

---

## 5. The screen set

```
                          ┌──────────────┐
                 ┌────────┤ STATUS BAR (always visible, y 0..21) │
                 │        └──────────────┘
   ┌────────┬───┴────┬────────┬────────┬────────┐
   │TRANG CHU│DIEU KHIEN│MAY TAO AM│THONG TIN│CAI DAT│  ← nav y 206..239, 5 × 64
   └────────┴────────┴────────┴────────┴────────┘
                 │
                 └── LEARN REMOTE: full-screen overlay, opens itself when the server
                     sends {"learn":"COOL 25"}, closes on success/timeout
```

**FIVE tabs of 64 px each** (previously four tabs × 80). The fifth is `MAY TAO AM` — see §5.3. It
gets its own page rather than being squeezed into `DIEU KHIEN`: that page already has a
`THU CONG`/`TU DONG` pair for the air conditioner, and a second identical pair for a different
appliance on the same screen makes "which machine is this button talking about?" unanswerable by
looking.

Shared grid: a 320×240 screen, 6 px padding, a 22-high status bar, a 34-high nav bar, content area
`y = 24…203`.

### 5.1 TRANG CHU — the default

```
┌────────────────────────────────────────────────┐ 0
│ ▣ AIRCON            ◇ ◇ ((( )))          04:20 │ status bar
├────────────────────────────────────────────────┤ 22
│ ┌─────────────────────┐  ┌─────────────────────┐│
│ │ TRONG NHA           │  │ NGOAI TROI       ● ││ ← ● green/grey = slave alive/dead
│ │                     │  │                     ││
│ │   28.4 °C           │  │   33.1 °C           ││ FreeSansBold24pt
│ │                     │  │                     ││
│ │ DO AM      62 %     │  │ DO AM      70 %     ││
│ └─────────────────────┘  └─────────────────────┘│
│  6,26,150,96              164,26,150,96         │
│ ┌────────────────────────────────────────────┐  │
│ │ ❄ LAM LANH        26 °C        [TU DONG]   │  │ 6,128,308,74
│ │ lenh cuoi 2 phut truoc                     │  │
│ └────────────────────────────────────────────┘  │
├────────────────────────────────────────────────┤ 205
│ TRANG CHU │DIEU KHIEN│THONG TIN │  CAI DAT     │
└────────────────────────────────────────────────┘ 239
```

- Temperature numbers are coloured by the **thermal scale** (`thermalCold…Hot`), not by
  indoor/outdoor.
- `NGOAI TROI` shows `—` and a grey dot when `SlaveWatch` reports a missed heartbeat — the same
  information the web UI is showing, rather than a frozen stale number.
- The badge in the corner of the AC block: `TU DONG` (blue `ice`) or `GHI DE` (orange `warning`) —
  see §8.

### 5.2 DIEU KHIEN

```
├────────────────────────────────────────────────┤ 24 (y values below are relative to this)
│ ┌──────┐ ┌────────────────────┐ ┌──────┐       │
│ │  −   │ │       26 °C        │ │  +   │       │ 8,2,68,68 · 84,2,152,68 · 244,2,68,68
│ └──────┘ └────────────────────┘ └──────┘       │
│ ┌──────┐┌──────┐┌──────┐┌──────┐               │
│ │ LANH ││ KHO  ││ QUAT ││ TAT  │               │ y=74 h=38 w=74 @ x 6/84/162/240
│ └──────┘└──────┘└──────┘└──────┘               │
│ ┌──────────────┐  ┌──────────────┐             │
│ │  THU CONG    │  │   TU DONG    │             │ 6,116,150,36 · 164,116,150,36
│ └──────────────┘  └──────────────┘             │
│  QUAT                    60%      [  CHON  ]   │ y=156 h=24, button 244,156,68,24
```

± changes are **coalesced for 500 ms and then sent automatically** (matching the app's
`_tempDebounce`), while a mode change is sent immediately. Pressing `TU DONG` hands control back to
the server's comfort loop.

**WHILE IN AUTOMATIC MODE, ± AND THE 4 MODE BUTTONS ARE LOCKED** (dimmed, ignoring touch). They used
to remain pressable: the number on screen changed, but nothing was sent anywhere and the next
comfort cycle pulled it back — the user saw the machine obey for a few seconds and then change its
mind, and read that as a broken board. The line under the temperature states the required order:
`TU DONG — BAM THU CONG DE CHINH`. The `THU CONG`/`TU DONG` buttons and the `QUAT` row are **not**
locked.

A mode button with no IR code in NVS → filled `carbonUp` with `whiteDim` text, and touching it
raises the toast `CHUA HOC MA — vao app de hoc`. Never silently ignored.

The **`QUAT` row** opens a fan-speed overlay (7 rows: 20/40/60/80/100 % · `TU DONG` · `NUT VONG`).
Each level is **a discrete button code learned from the app** (`ir_action_codes`); the panel looks it
up in NVS and transmits directly — so it works with no network, unlike the app which has to go
through the server. A level with no learned code is dimmed, not hidden.

> A fan level is **NOT** the `QUAT` mode in the row above. The `QUAT` mode = the unit blows without
> cooling, and it belongs to the (mode, temperature) matrix. A fan level = airflow speed, settable in
> any mode. Vietnamese unfortunately uses the same word for both, so the two places that show them
> are deliberately given **completely different shapes**.

The number to the right of `QUAT` is **the level the panel last transmitted**, not the unit's true
state — anyone pressing the real remote makes it wrong and the panel has no way of knowing. The same
rule (and the same reason) as `_fanWire` in `override_panel.dart`.

### 5.3 MAY TAO AM

```
├────────────────────────────────────────────────┤ 24
│ ┌────────────────────────────────────────────┐ │
│ │ MAY TAO DO AM                   [TU DONG]  │ │ 6,2,308,88
│ │ DANG CHAY                           52 %   │ │ ← 52 % is the SMOOTHED value (EMA)
│ │ phòng khô hơn ngưỡng bật                   │ │
│ └────────────────────────────────────────────┘ │
│ ┌────────────────────────────────────────────┐ │
│ │ MA IR                                      │ │ 6,94,308,38
│ │ DA CO MA BAT VA TAT                        │ │
│ └────────────────────────────────────────────┘ │
│ ┌────────┐ ┌────────┐ ┌──────────┐             │
│ │  BAT   │ │  TAT   │ │ TU DONG  │             │ y=136 h=42
│ └────────┘ └────────┘ └──────────┘             │
```

The panel **drives the humidifier itself**: it measures using the **median humidity of the 4 room
corners**, decides using a port of `esp32-humidity/src/diffuser-control.cpp` (see
`src/humidifier-control.h`), and transmits through the panel's own IR LED. **Not via the server** —
the backend is only involved in learning the codes.

- The % figure is the **smoothed humidity** (EMA α=0.2 at a 5 s cadence), not the raw number on the
  home page. It is what explains why the unit switched on or off.
- The line under the state is the **reason**, one sentence — `DEADBAND`, `DWELL_HOLD`, `LOCKOUT`,
  `SENSOR_LOST`, `NO_CODE`… While overridden it also says "back to automatic in N minutes": an
  override expires after 2 hours, and without saying so the unit restarts by itself and whoever
  pressed `TAT` concludes the button does not work.
- The `MA IR` card has **three** outcomes, not two: `DA CO MA BAT VA TAT` ·
  `CHI CO MA BAT — DUNG NHU REMOTE BAP BENH` · `CHUA HOC MA`. The middle case is the most easily
  misunderstood, which is why it gets a name of its own.
- `BAT`/`TAT` are dimmed until the `HUMID_ON` code has been learned; `TU DONG` is **never** dimmed —
  the user must always be able to escape an override.

### 5.4 THONG TIN — local diagnostics

Eight label/value lines, `y = 30` stepping 21 px, labels left at x=14, values right at x=306:

`WIFI` · `IP` · `SONG` (RSSI dBm) · `MQTT` · `ESP-NOW` (received/dropped) · `NGOAI TROI` (seconds
since the last packet) · `MA IR` (number of codes in NVS) · `FW / UPTIME`.
Footer line: `MAC xx:xx:… · KENH n`.

This screen exists so an installer can answer "why does the web not see this node?" without plugging
in a laptop — exactly the lines you would otherwise have to read with `pio device monitor`.

### 5.5 CAI DAT

Four full-width rows, `x=6 w=308 h=40`, `y = 4 / 48 / 92 / 136` (coordinates within the content
area). That fits 180 px exactly, so this page **does not scroll**.

| Row | Control | Note |
|---|---|---|
| `DO SANG` | `−` `70%` `+` | LEDC PWM, 10 % steps, 10 % floor (0 % looks like a broken screen). Hold to repeat |
| `KHOI DONG LAI` | ↻ | `ESP.restart()`, with a confirmation step |
| `MA IR DA HOC` | `XEM` | an overlay of 18 combinations, with per-row `XIN MA` and `XOA` |
| `NHAT KY LENH` | `XEM` | the last 8 commands from the backend plus their results |

> The `AM BAO` row has **been removed**. The ESP32-S3 panel board has no buzzer (`BUZZER_PIN =
> PIN_NONE` — its four free pins all went to IR and the UART to the UNO Q). A toggle for hardware
> that does not exist is the worst kind of control: it presses, it changes colour, and it does
> nothing. The click-sound path (`Theme::setPressSound`) is still wired up, so the old QR Box board —
> which shares this source and DOES have a real buzzer — still beeps.

> The `DONG BO GIO` row has **been removed** along with the entire NTP path
> (`ntpBegin`/`ntpPoll`/`clockWrite`). The node now only **reads** the DS1307 to show the time in the
> status bar. Consequence: the clock has to be set with another tool — the chip has its own backup
> battery, so setting it once is enough. If it has never been set, the status bar shows `--:--`
> forever.

There is no "change WiFi" entry — the WiFi configuration lives in `config.h`, and adding a password
entry screen with an on-screen keyboard is a project of its own.

### 5.6 LEARN REMOTE — overlay

Opens itself when `IrIo::learning()` is true (the server sent `{"learn":"COOL 25"}`), not on a user
press. Box `16,30,288,168`:

```
┌──────────────────────────────────────┐
│           DANG HOC REMOTE            │
│                                      │
│              COOL 25                 │  FreeSansBold24pt
│                                      │
│  Huong remote vao mat thu, bam nut   │
│  ████████████████░░░░░░░░░░    18s   │  countdown bar
└──────────────────────────────────────┘
```

Success → 2 beeps + `DA HOC XONG` for 1.5 s. Timeout → `KHONG BAT DUOC TIN HIEU` plus hints (remote
battery / aim / distance) — the same content as the serial log.

---

## 6. Touch map

| Region | Rect | Action |
|---|---|---|
| Nav tabs | `(80·i, 206, 80, 34)`, i=0..3 | switch screen |
| Home · AC block | `6,128,308,74` | jump to DIEU KHIEN |
| Control · `−` / `+` | `8,28,68,76` / `244,28,68,76` | setpoint ∓1, clamped to `[16,30]` |
| Control · mode | `(6+78·i, 110, 74, 44)` | select LANH/KHO/QUAT/TAT |
| Control · `GUI` | `6,160,150,42` | transmit IR + publish `state` |
| Control · `TU DONG` | `164,160,150,42` | drop the local override |
| Settings | the 4 rows in §5.4 | |

The smallest touch target is 44×44 px — on a 2.8″ screen (≈0.18 mm/px) that is about 8 mm, just
enough for a fingertip. **Debouncing**: falling edge only, 250 ms lockout; capacitive screens very
easily generate a burst of touches, and 5 accidental `+` presses is a 5 °C error.

---

## 7. Concurrency — two tasks, two cores

### 7.1 Why not draw inside `loop()`

The node's `loop()` **stalls for seconds at a time as a matter of routine**, not as a rare event:

| Blocking site | For how long |
|---|---|
| `connectWifi()` — `while` + `delay(500)` | until it joins the network |
| `connectMqtt()` — `delay(2000)` on every `rc != 0` | forever while the broker is down |
| `IrIo::blast()` | 50–250 ms per command |
| DHT failure (old board) | `delay(3000)` |

Drawing inside `loop()` means that **exactly when the network drops — exactly when the user most
needs to look at the screen — the screen freezes as if the node had died**. For a wall-mounted
control panel that is the worst failure mode: it asserts something false.

### 7.2 Core 0 for the UI, core 1 for IR — mandatory

Not for smoothness. `IrIo::blast()` times itself with `delayMicroseconds()` to build the 38 kHz
carrier (a 26 µs period). Another task **on the same core** being scheduled in between stretches the
marks and spaces by tens of µs → a malformed IR frame → **the air conditioner sits there doing
nothing while the log still says `sent`**.

Arduino runs `loop()` on core 1 (`ARDUINO_RUNNING_CORE`), so the UI has to go on core 0. Use
`xTaskCreatePinnedToCore(..., UI_CORE=0)` — **not** `xTaskCreate`, because leaving the choice to the
scheduler means it will eventually put it on core 1.

```
  core 1 — loopTask (priority 1)        core 0 — the "ui" task (priority 2)
  ┌────────────────────────┐           ┌──────────────────────────────┐
  │ WiFi · MQTT · ESP-NOW  │  publish  │ display SPI                  │
  │ IR transmit/learn      │ ────────► │ I2C: touch + DS1307 + SHT3x  │
  │ DHT22 (GPIO17)         │           │ LEDC: backlight + buzzer     │
  │ NVS (IrStore)          │ ◄──────── │                              │
  └────────────────────────┘ pollCommand└──────────────────────────────┘
                             / reply
```

### 7.3 Three paths across the boundary — and there is no fourth

| Path | Kind | Who waits for whom |
|---|---|---|
| `Ui::publish(Model)` | mutex + `memcpy` | **nobody waits**: `xSemaphoreTake(mx, 0)` — if it cannot take the lock it drops this snapshot, and the next one arrives a few ms later |
| `Ui::pollCommand()` | 4-element queue | the UI places an order, `loop()` picks it up and executes |
| `Ui::reply()` | 4-element queue | `loop()` returns the result → toast |
| `Ui::readIndoor()` | spinlock, 2 floats | the UI reads the SHT3x (it owns the I2C bus), `loop()` takes the numbers to send |
| `Ui::setIndoor()` | the same spinlock | **the other direction**: `loop()` reads the DHT22 and stores the numbers. Reversed because the DHT library disables interrupts for ~5 ms per read, and core 0 is where the WiFi stack runs. Only one of the two sources writes — the SHT3x wins if detected |

Hardware ownership is split cleanly, with **no resource touched by both sides**. This is exactly the
pattern already used for the MQTT callback in `main.cpp` ("the callback only unpacks the message and
places an order; `loop()` transmits the IR and sends the ack") — now applied to a second core.

The practical consequence: pressing `GUI` on the screen does **not** transmit IR from the UI task. It
pushes a `Command` into the queue; `loop()` looks up `IrStore::loadAlias()`, transmits the IR on core
1, then calls `Ui::reply()` with the result. The user sees the toast `DANG GUI...` change to
`DA GUI...` — the actual sequence of events, not a fake animation.

### 7.4 What to draw, and when

Redrawing the whole screen is 320×240×2 bytes over 40 MHz SPI ≈ **40 ms**. Two tiers:

1. **Screen change / overlay close** → draw the background and static frame once.
2. **Value change** → each field keeps a copy of "what was drawn last time", and only the fields that
   differ are repainted. Cadence 200 ms (the eye cannot distinguish faster).

Flicker is avoided **without sprites**: `tft.setTextPadding(w)` erases the old text during the same
pass that draws the new text, so there is never a moment where the cell is blank on screen. Far
cheaper than buffering a 152×76 `TFT_eSprite`, which would cost ~23 KB of RAM that a WROOM‑32E‑N8
has no PSRAM to make up for.

The touch scan runs at **15 ms** (~66 Hz), decoupled from the 200 ms draw cadence: buttons respond
instantly while the screen still avoids redundant redraws.

### 7.5 Things only possible because the tasks are split

- **Button flash on press** (`pressFlash`, 90 ms) — a `vTaskDelay` on core 0 does not disturb the
  MQTT/IR work running on core 1.
- **Auto-dim** after 60 s without a touch (down to 15 %). The wake-up touch does **not** count as a
  button press: the user touches a dark screen to *look* at it, not to change the temperature.
- **Sensor reads every 2–2.5 s** (SHT3x at 2 s in the UI task, DHT22 at 2.5 s in `loop()` — the DHT22
  datasheet requires a minimum of 2 s between reads) so the screen reflects the room almost
  instantly, while telemetry to the cloud keeps its own `TELEMETRY_MS` cadence. A failed DHT22 read
  (bad checksum) **skips the round rather than storing `NaN`** — storing it would make the screen
  blink to `—` and back to a number, looking like a sensor about to fail.

Budget: 8 KB UI task stack + 12 KB MQTT buffer + 1.2 KB `irBuf`.

---

## 8. Local override — and the gap on the backend

### 8.1 The problem

The node **has no (mode, setpoint) → IR code lookup table**. `IrStore` is keyed by `ir_code_id` (a
UUID generated by the server); the server sends `ir_raw` together with `mode` + `setpoint` +
`ir_code_id`, but the node only stores it by id. So when the user presses `26 °C` on the screen, the
node **does not know which frame to transmit**.

### 8.2 The fix: an alias index in NVS

Add `IrStore::saveAlias(mode, temp, irCodeId)` / `loadAlias(...)`: every time the server sends a
command, the node additionally stores the key `aCOOL26` → `ir_code_id`. The display can then look it
up in reverse.

The consequence — **and the interface has to be honest about it**: the panel can only control the
(mode, temperature) combinations **the server has sent at least once**. On a freshly flashed board
every mode button is dimmed until the comfort loop has run a few cycles. This is why §5.2 requires a
"dimmed + explained" state.

### 8.3 The gap: the server will take control back

`mqtt_naming.py` only has 5 topics (`telemetry` `cmd` `state` `status` `learn`) — **there is no path
for the node to request an override**. The real override lives in Redis
(`redis_override_service`) and can only be set through the REST API.

So an override from the screen is currently **local**:

1. Transmit IR immediately → the air conditioner obeys at once.
2. Publish `state` **without an `ack`** → `state_handler` writes the mode/setpoint into
   `redis_state_service`, and the app and web see the new state.
3. But `comfort_engine` **does not stop**: on the next cycle it decides for itself again and sends a
   `cmd` that overrides it.

The interface states that plainly rather than hiding it: the `GHI DE` badge carries the line
`may chu se gianh lai quyen o chu ky sau`, and the badge returns to `TU DONG` the moment the next
`cmd` arrives.

**To make panel overrides real**, the backend needs one of two things:
- a new topic `bl/{org}/{uuid}/override` (node → cloud) that the worker forwards straight into
  `redis_override_service`; or
- the node calling the REST `POST /control/override` — which would require embedding a JWT in the
  firmware, considerably heavier.

Recommendation: **the new topic**. That is backend work, outside the scope of this revision, and it
is recorded here so it does not get forgotten.

---

## 9. Build

```bash
# The source (config.h included) now lives in Firmware/esp32-s3-panel/src/ — the
# esp32-qrbox/ directory only holds the build configuration for the QR Box board.
cd Firmware/esp32-s3-panel
cp src/config.h.example src/config.h      # fill in as per README §2
cd ../esp32-qrbox
pio run -e qrbox-touch -t upload --upload-port COMx   # USB-TTL plugged into P3
pio device monitor -p COMx -b 115200
```

The `qrbox-touch` env presets: the TFT/I²C/IR pins, `HAS_DISPLAY`, and all of TFT_eSPI's
`USER_SETUP_LOADED` flags (do not edit `User_Setup.h` inside the library — that file lives in
`.pio/` and `pio pkg update` wipes it).

Three envs share **one** source tree, for the reason recorded at the top of `platformio.ini`: the
only difference between the boards is the pinout.

### 9.1 If the screen is blank / striped

| Symptom | Most likely cause |
|---|---|
| Pure white, nothing shown | Backlight is fine but SPI is dead → check `TFT_MOSI=18` `TFT_SCLK=22` (not the default VSPI pins) |
| **Background/colour blocks correct but TEXT and shapes noisy/torn** | **Wrong driver** — it must be `ST7789_DRIVER`, not `ILI9341_DRIVER` (§2.2). This is NOT SPI noise: lowering the clock will not help at any frequency |
| Stripes/noise during fast drawing | 40 MHz through the GPIO matrix is at the limit → drop `SPI_FREQUENCY` to 27 MHz |
| Image correct but upside down by 180° | Switch `TFT_ROTATION` between 1 and 3 (both are 320×240 landscape) |
| **`rst:0x3 (SW_RESET)` loop every ~28 ms, unable to print a single log line** | NOT dead hardware. The partition table exceeds the 4 MB mark — use `huge_app.csv`, not `default_8MB.csv` (see `platformio.ini`) |
| Completely dark | The backlight PWM was never set, or `DO SANG` = 0 |
| Touch axes off | Change `TOUCH_SWAP_XY` / `TOUCH_INVERT_X` / `TOUCH_INVERT_Y` in `config.h` |
| Touch not detected | `Touch::chip()` prints at boot; `NONE` = wrong address or RST (GPIO25) never released |
| Nonsensical clock values | The I²C bus is running above 100 kHz — the DS1307 cannot cope (§2.1) |
| **`Interrupt wdt timeout on CPU1` right after setup() finishes** | Wrong DHT library: `adafruit/DHT sensor library` runs all 80 `expectPulse()` rounds **without an early exit** on timeout → interrupts off for >1 s on core 1. You must use `beegee-tokyo/DHT sensor library for ESPx` (it times each edge with `micros()`, capped at 90 µs, and exits immediately). It only crashes **when the sensor is not connected**, which makes it very easy to miss on the bench |
| **The LED on the IR transmitter module stays lit** | `IR_TX_PIN` is routed through the TXS0104 (the `UART_1` pair = GPIO2/GPIO15). That IC only *holds* the level through a ~40 kΩ resistor, so it cannot pull the module's input low → IR is mute while the LED runs on DC, getting hot and degrading fast. Use **GPIO5** (3.3 V direct, real push-pull) — §3.1 |
| The AC does not react but the log prints `sent` | The transmitter circuit cannot supply enough current at 3.3 V (the module should have its own transistor), or the emitter is aimed wrong / too far. IR on the `UART_2` pair does not depend on `EN_LEVEL_SHIFT`, so that pin is **not** the suspect |
| Temperature/humidity permanently `NaN` even with the DHT22 wired | `EN_LEVEL_SHIFT` (GPIO12) never went HIGH → the TXS0104 leaves its output floating. This is the **only** thing still routed through the level shifter (§3.1) |
| Temperature/humidity always `—`, the log mentions the DHT22 every 15 s | Missing the 4.7 kΩ pull-up to 3.3 V — the fitted R5/R6 100 kΩ are far too weak for ~1 µs edges (§3.1) |
| **Serial spews `lv_draw_letter: glyph dsc. not found for U+xxxx` and that text is blank on screen** | The string uses a character **not in the font**. Two sources seen so far: (a) a TEXT label assigned to `fontHero()`/`fontBig()` — those two fonts **only contain digits** (§4.3), use `fontTitle()`/`fontLabel()` instead; (b) punctuation **outside the generated ranges** in `tools/make_lvgl_fonts.ps1`. The ranges currently present: `·` `–` `—` `•` `…` `°`. Adding a new character to a string means opening the script, adding the range and regenerating the font. Look up the U+xxxx code in the message to find out which character it is |
| **Entering learn mode a second time leaves the receiver dead silent** | `enableIRIn()` was called on top of itself → `timer_isr_callback_add: register interrupt service failed` + `addApbChangeCallback: duplicate`, and the sampling timer stops running until a restart — while the log still prints `[learn] point the remote at the receiver`. Already fixed: `IrIo::learnStart()` now calls `disableIRIn()` first if a learn is still in progress |
| **`INVALID SENSOR PIN` at boot, the DHT22 never reads** | `DHT_PIN` is in the GPIO34…39 range — **input-only pins**, which cannot pull the line low, so the DHT22 never replies. The compiler does NOT catch this (§3.1). Move to a pin that can drive an output, e.g. GPIO2 |

---

## 10. Remaining work

- [x] ~~**DHT22 on GPIO2 (through the TXS0104) works**~~ — measured for real:
      `[telemetry] t=30.1°C h=49% -> sent`. The 4.7 kΩ pull-up goes **on the P3 side**, not soldered
      to the chip pin (GPIO2 is the programming-mode strapping pin — pulling it high at the chip
      loses the USB-TTL programming route entirely)
- [ ] **The remaining wiring** (§3.1/§3.2 — *the firmware is done, this is hardware work*):
      - solder the IR receiver to the A7680C's TX pad (GPIO17), **powering the receiver at 3.3 V,
        not 5 V**. No pull-up needed — R5 100 kΩ is already fitted
      - solder the IR transmitter to the A7680C's RX pad (GPIO5), 3.3 V direct. The transmitter
        module **should have its own transistor** — at 3.3 V the LED-plus-resistor type still works
        but at low current, so the range is poor
      - *(optional)* an SHT3x on J1 (pin 1 = 3V3, 2 = SCL, 3 = SDA, 6 = GND) if you want to replace
        the DHT22 with a more accurate sensor — the firmware prefers it automatically
- [ ] Adjust `TOUCH_SWAP_XY` / `TOUCH_INVERT_*` after testing touch on real hardware (§9.1)
- [ ] The `override` topic on the backend (§8.3)
- [x] ~~Accented Vietnamese VLW fonts~~ — done, see §4.3 and `tools/make_vlw.py`
- [ ] Re-measure SW1/SW2 on the board: the schematic says "INTERNAL BUTTON" but the traces go into
      the `PULSE OUT CONFIG` block, so they may not be user buttons at all
