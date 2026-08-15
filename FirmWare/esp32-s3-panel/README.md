# Wall panel — 2.8" ESP32-S3 board

**BreezeLink's official panel.** Replaces the QR Box Advance board.

| | |
|---|---|
| Chip | ESP32-S3 |
| Display | 2.8" IPS 240×320, **ILI9341V**, 4-wire SPI |
| Touch | **FT6336G** capacitive, I²C |
| Also on board | I2S mic + speaker, microSD, RGB LED, battery monitor |
| Flash | `pio run -e esp32s3-panel -t upload --upload-port COMx` |

```
pio run -e esp32s3-panel -t upload --upload-port COM5   # flash
pio device monitor -p COM5 -b 115200                    # watch the log
```

---

## 1. The panel source lives here — one copy only

`src/` in this directory is the real thing, `ui/` included.

There used to be two more envs sharing exactly this `src/` — `esp32-qrbox` (the old QR Box board)
and `esp32-s3-gateway` (a headless debug board) — **both removed from the repo on 2026-08-15**. To
look them up: `git log --diff-filter=D -- FirmWare/esp32-qrbox`.

Copying the source into another directory "to keep things tidy" creates **two panels**. Command
execution logic, `req_id` deduplication, re-requesting IR codes, the advice/command boundary with
the UNO Q — all subtle places, each of which cost something to get right once. Two copies diverge
on the very first edit, and the symptom is that this board works and that one does not, with no way
to tell which is the real version.

Per-board differences live in [`src/board-pins.h`](src/board-pins.h), selected with the
`-D BOARD_S3_PANEL` flag. **Do not sprinkle `#ifdef` through the code.** This rule stands even
though there is only one board now — it is what makes adding a second board later not require
copying the source.

Configuration (WiFi, MQTT token, `DEVICE_UUID`) is in [`src/config.h`](src/config.h.example),
**gitignored** because it holds real passwords; the committed version is `src/config.h.example`.
That file ends with `#include "board-pins.h"` — without that line the build breaks at
`I2C_SDA_PIN was not declared`.

---

## 2. Pinout

The board is nearly out of pins. The table below is **everything** still free and how it is
allocated:

| Pin | Used for | Note |
|---|---|---|
| IO2 | IR **transmit** | |
| IO3 | IR **receive** | strapping, see §2.2 |
| IO43 | UART **TX** → UNO Q | see §2.1 |
| IO44 | UART **RX** ← UNO Q | |
| IO14, IO21 | *reserved* | for the **PZEM** (Modbus-RTU = 2 pins) |

Already spoken for, do not touch: display (10/11/12/13/45/46) · touch (15/16/17/18) · SD card
(38/39/40/41/47/48) · I2S audio (1/4/5/6/7/8) · RGB LED (42) · battery monitor (9) · BOOT button (0).

### Wires that must be soldered

```
   Panel board                     External device
  ┌──────────┐
  │ IO2      │───────────────────  IR transmitter module (3.3V supply)
  │ IO3      │───────────────────  TSOP receiver         (3.3V supply)
  │ IO43  TX │──────────────────>  RX on the Arduino UNO Q
  │ IO44  RX │<──────────────────  TX on the Arduino UNO Q
  │ GND      │───────────────────  COMMON GND  ← missing this gives you garbage bytes
  └──────────┘
```

**Crossing over is mandatory** on the UART pair: TX on this board goes to RX on the other. Wiring
TX-TX gives complete silence and damages nothing — which makes it very easy to waste time looking
for the problem in software.

**Power the receiver from 3.3V, not 5V.** The absolute maximum on the pin is VDD+0.3 (~3.6V). At
3.3V the transmitter module should have its own transistor; the LED-plus-resistor type (KY-005
style) still works but at low current, so the range is poor.

### 2.1 Why UART0's pins can be borrowed

Only because the USB-C port is the S3's **native USB** — `Serial` (the console) goes over USB, not
over UART0, so IO43/44 really are free. If the board turned out to have a CH34x bridge chip, the
console would be on exactly those two pins, and wiring the UNO Q in would cost you both the console
and the UNO Q link. **Check before soldering** — §4 step 1.

What you gain: IO14 + IO21 are freed, exactly the number of pins the PZEM will need later. Without
this, the PZEM stage would run out of pins and something else would have to be removed.

**The direction cannot be swapped.** The board documentation says `RXD0(IO43) / TXD0(IO44)` — the
**opposite** of the ESP32-S3 datasheet (default IOMUX: IO43 = U0TXD, IO44 = U0RXD). Trust the
datasheet, because it determines which pin the ROM bootloader drives at startup. Get it backwards
and on every reset the ROM drives IO43 as an output while the UNO Q is also driving an output into
it — two outputs fighting on one wire.

On every board reset the UNO Q will receive a handful of stray bytes (the ROM and the second-stage
bootloader print their log on IO43 before `setup()` runs). **This is normal** — the framing filters
them out with a `0xAC` magic byte + CRC8 + byte-sliding resync. Once `Serial1.begin()` attaches
UART1 to these pins through the GPIO matrix, UART0 no longer reaches the pins at all.

The price: while the UNO Q wire is attached you **cannot flash over UART0** — flash over USB-C.

### 2.2 Two strapping pins to remember

**IO45 (backlight)** — selects the VDD_SPI voltage level and **must be low at reset**. High makes
the chip select 1.8V for the flash and the board will not boot. It is safe because LEDC only
attaches to the pin after boot completes, but: **never solder a pull-up onto this pin.**

**IO3 (IR receive)** — selects the JTAG signal source, but only takes effect once the
`JTAG_SEL_ENABLE` eFuse is burned, which it is not by default. IR **receive** (not transmit) is
deliberately placed here: a TSOP receiver holds the pin high from the moment it is powered, so the
level at reset is deterministic. The transmitter side floats until `setup()` runs — and a floating
strapping pin is something you should not have.

---

## 3. Differences from the QR Box board

| | QR Box | This board |
|---|---|---|
| Display controller | ST7789 | **ILI9341** |
| Buzzer | yes (GPIO13) | **no** |
| Clock | DS1307 (battery-backed) | **no** — taken from NTP |
| Level shifter | TXS0104 (OE must be driven) | **no** — 3.3V direct |
| Display SPI | 27 MHz | 40 MHz (FSPI IOMUX pins) |

**No buzzer** because there are no pins left, and all it signalled was "touch registered" — an
on-screen toast does that job. The board has an I2S speaker, so a beep is still feasible later, it
just has to play a PCM sample over I2S rather than PWM on a pin.

> Which is why the **`ÂM THANH` row in the Settings screen has been removed entirely**. A toggle
> for hardware that does not exist is the worst kind of control: it presses, it changes colour, and
> it does nothing — the user will go looking for a fault in the speaker. The sound path
> (`Theme::setPressSound` → `BoardIo::beep`) is KEPT AS IS: `beep()` stays silent when the pin is
> 255, and the QR Box board, which shares this source, still has a real buzzer.

**No RTC**, so a power cut loses the time: the status bar shows `--:--` for a few seconds until SNTP
answers. In exchange it avoids the worse DS1307 case — a backup battery preserving a **wrong** time
forever while `clockRead()` still declares it valid, which is exactly what happened on the old board.

**Learned IR codes survive** a board swap: the partition table is unchanged so NVS is still in the
same place. (You still have to relearn if you replace the chip, since NVS lives in the chip's flash.)

---

## 4. Bring-up — in this order

The three items below **have not been measured on real hardware**; the current configuration is
inferred from documentation.

### Step 1 — native USB or bridge chip? *(do this BEFORE soldering)*

```bash
esptool.py --port COM5 chip_id
```

- If you see `USB mode: USB-Serial/JTAG` → **native USB**, the current configuration is right, go
  ahead and solder.
- If not → the board uses a bridge chip. You must do **both**:
  1. remove `-D ARDUINO_USB_MODE=1` and `-D ARDUINO_USB_CDC_ON_BOOT=1`
  2. move the UNO Q UART back to `UNOQ_TX_PIN=14 / UNOQ_RX_PIN=21` — and lose the PZEM pins

Guess wrong and flash anyway and the serial monitor is **as silent as a dead board** while the flash
itself reports success. This project's ESP32-C3 node fell into exactly this trap in the other
direction.

### Step 2 — how many MB of flash?

```bash
esptool.py --port COM5 flash_id
```

`platformio.ini` declares **4MB, deliberately**: under-declaring only wastes the surplus flash, but
over-declaring makes the bootloader find partitions outside the flash range it knows about and
**give up in silence** — endless `rst` loops, not one line of log, very easily misdiagnosed as dead
hardware. The firmware currently occupies ~1.29 MB, so 4MB is ample.

To increase it, change **both** the `board_build.flash_size` and `board_upload.flash_size` keys —
esptool reads the second one to write the image header, and the first cannot change it.

### Step 3 — are the colours right?

`-D DISPLAY_SELFTEST=1` is currently on: at startup it shows 3 named colour swatches for 3 seconds.

- If you get **XANH DUONG / TRANG / XANH LA** → correct. **Delete that line.**
- If you get a negative image (black backgrounds turn white, blue turns orange) → add
  `-D DISPLAY_INVERT=1`.

Remember to remove it once settled: this is a wall-mounted panel, and 3 seconds of engineering test
pattern on every power-up looks like a fault screen, not a splash screen.

### Step 4 — touch axes

The three flags in [`board-pins.h`](src/board-pins.h) are still set as on the old board, unmeasured.
Touch the **top-left** corner and see where the cursor goes:

| Symptom | Flag to change |
|---|---|
| touching left/right moves the cursor up/down | `TOUCH_SWAP_XY` |
| touching left lands on the right | `TOUCH_INVERT_X` |
| touching the top lands at the bottom | `TOUCH_INVERT_Y` |

**Change one flag at a time.** The three are independent; changing two at once leaves you unable to
tell which one did anything.

If the image is correct but **upside down by 180°**, change `TFT_ROTATION` from 1 to 3.

### Three things TFT_eSPI charges you for on the ESP32-S3 — settled on real hardware (2026-08-14)

All three are already in `platformio.ini`; they are recorded here because **all three fail without
saying why**, and the first two put the board in an endless boot loop.

| Flag | Why it is mandatory |
|---|---|
| `-D USE_FSPI_PORT` | Without it, `Processors/TFT_eSPI_ESP32_S3.c` takes a **reference** to the global `SPI` (`SPIClass& spi = SPI`) instead of creating its own bus. `spi.begin()` cannot allocate the bus, `_spi` stays NULL, and the first `beginTransaction()` call writes to `0x10`. Crashes inside `tft.init()`. FSPI rather than HSPI: only FSPI has IOMUX for IO10..13, which is what keeps 40 MHz. |
| `-D TFT_USE_DMA=0` | The library's `dma_end_callback()` writes to `SPI_DMA_CONF_REG(spi_host)`, a register that **is not mapped as it is on the classic ESP32**. Crashes IN AN INTERRUPT on the first DMA transfer (`EXCVADDR: 0x30`). `initDMA()` still returns **true**, so the return value cannot detect it — it has to be blocked from the configuration. |
| `-D DISPLAY_INVERT=1` | **Opposite of the QR Box board.** TFT_eSPI sends `INVON` unconditionally for ST7789 but not for ILI9341V, so the same setting produces opposite results. |

The first two crashes are very easy to misdiagnose because the log **stops at a point that looks
perfectly normal**: the first right after `Cam ung: OK`, the second right after
`Bo dem ve: 2 x 48 dong ...` — that is, after the display has finished initialising. From the log it
looks like out-of-memory or a bad LVGL configuration; it is neither. Decoding the backtrace is the
fastest route:

```bash
~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line \
    -pfiaC -e .pio/build/esp32s3-panel/firmware.elf 0x42016ea8 0x42016f81 ...
```

The cost of `TFT_USE_DMA=0`: every flush is pushed by the CPU (`pushColors`) rather than DMA.
Acceptable — the draw interval is 200 ms and the dirty region is usually much smaller than a full
screen.

---

## 5. Common problems

| Symptom | Most likely cause |
|---|---|
| Flash succeeds, serial completely silent | Wrong USB mode — §4 step 1 |
| `rst:0x3 (SW_RESET)` loop, no log | Flash declared larger than it is, or `board_upload.offset_address` missing |
| Screen dark, log still running | Missing `-D BOARD_S3_PANEL=1` → compiled with the QR Box pinout |
| `fillScreen` gives the right colour, text is noisy/torn | Wrong display controller (ILI9341 ↔ ST7789) |
| Boot loop right after `Cam ung: OK` | Missing `-D USE_FSPI_PORT` — see §4 |
| Boot loop right after `Bo dem ve: ...` | Missing `-D TFT_USE_DMA=0` — see §4 |
| The whole UI is a negative (WHITE swatch comes out BLACK) | `DISPLAY_INVERT` is backwards |
| WHITE swatch stays white but blue comes out red/orange | R↔B order swapped, **not** colour inversion → `-D TFT_RGB_ORDER=TFT_RGB` |
| Touch axes off | §4 step 4 |
| The UNO Q receives no packets | TX-TX (not crossed over), or no common GND |
| The AC does not react, log still says "da phat" | The IR LED is not soldered, or the transmitter module lacks a transistor |

---

## 6. Related

- [`src/board-pins.h`](src/board-pins.h) — per-board pinout, selected with `-D BOARD_*` flags
- [`../shared/unoq-link-protocol.h`](../shared/unoq-link-protocol.h) — UART framing to the UNO Q
- [`../shared/espnow-message.h`](../shared/espnow-message.h) — packet layout for the 4 room corners + outdoor
- [`../Interface/README.md`](../Interface/README.md) — wireframes, coordinates, and the reasoning behind each layout decision
