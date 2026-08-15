# BreezeLink — Node firmware

Six devices for **one household**, three firmware directories:

| Directory | Board | Node type | Shown in app/web as |
|---|---|---|---|
| `esp32-s3-panel/` | **2.8" ESP32‑S3 board** (ILI9341V + FT6336G) | **Gateway / wall panel** — receives ESP‑NOW, transmits IR, touch screen, UART to the UNO Q, **no sensor** | "Gateway trong nhà" |
| `esp32-room/` | 4× **ESP32‑C3‑DevKitM‑1** + DHT22 | **Room sensor** — ESP‑NOW slave | "Cảm biến trong phòng" |
| `esp32-outdoor/` | **ESP32‑C3‑DevKitM‑1** + DHT22 | **Outdoor** — ESP‑NOW slave | "Nút ngoài trời" |
| `shared/` | — | Shared definitions: ESP‑NOW packets, slave radio, the UART protocol to the UNO Q. **All three nodes above use `-I../shared`** | — |
| (not here) | Arduino UNO Q | Edge AI — connects to the gateway over **UART**, see [`../edge-ai/`](../edge-ai/) | — |

> **The panel source lives in `esp32-s3-panel/src/` — exactly ONE copy.** Anything that
> differs between boards goes in `esp32-s3-panel/src/board-pins.h` and is selected with
> `-D BOARD_*`; **do not sprinkle `#ifdef` through the code**.
>
> The 2026-08-15 cleanup removed three directories: `esp32-qrbox/` (the old QR Box board,
> broken programming circuit) and `esp32-s3-gateway/` (a headless debug board) — both were
> only build configurations pointing at `esp32-s3-panel/src/` — plus `esp32-humidity/` (the
> humidifier test board, whose logic has been ported into the panel). To look them up:
> `git log --diff-filter=D -- Firmware/esp32-qrbox`.
>
> Pinout and bring-up order: [`esp32-s3-panel/README.md`](esp32-s3-panel/README.md).

> **THE GATEWAY NO LONGER MEASURES TEMPERATURE.** Both the DHT22 and the SHT3x have been
> removed from its firmware: a wall-mounted sensor only measures *that wall*, and four room
> corners differing by 3–4 °C is normal. The "indoor" number is now the **median** of the
> fresh corners (`esp32-s3-panel/src/room-registry.h`).

> **BOTH NODES ARE NOW ESP32.** They used to be different chip families (indoor ESP32‑S3,
> outdoor ESP8266), so each side used a completely different ESP‑NOW API — changing the
> protocol meant changing it twice in two different styles, exactly the kind of work where
> one half gets forgotten. Now they share one API (IDF's `esp_now.h`), one toolchain, one
> flashing procedure.
>
> The two old directories `esp32s3-indoor-master/` (an experimental version without IR) and
> `esp8266-outdoor/` have **been removed** — check the git history if you need them.

---

## 1. Wiring the temperature/humidity sensor

**The two nodes use two different sensor types** — not by whim, but because of the boards:

### Outdoor node (ESP32‑C3‑DevKitM‑1) — DHT22

Moved from an ESP32 DevKit V1 to the C3 on 2026-08-15 — **the same board as the 4 room
nodes**, so all five sensor nodes now share one chip family, one toolchain, one set of
lessons learned.

DHT comes in 2 forms: a **3-pin module** (pull-up already fitted — wire it directly) or a
**bare 4-pin sensor** (add a 4.7k–10kΩ resistor between DATA and 3V3).

```
   DHT22/DHT11        ESP32-C3-DevKitM-1
  ┌───────────┐
  │  +  / VCC │──────── 3V3
  │  S  / DATA│──────── GPIO4
  │  -  / GND │──────── GND
  └───────────┘
```

⚠️ Power the DHT from **3V3**, not 5V (the DATA pin goes into a 3.3V GPIO).
To change the pin: edit `DHT_PIN` in `src/config.h` — but on the C3 you must avoid
GPIO2/8/9 (strapping, GPIO9 is the BOOT button), GPIO18/19 (USB), GPIO11‑17 (flash),
GPIO20/21 (UART0). `DHT_TYPE` is now a DHTesp type (`DHTesp::DHT22`), not an Adafruit
macro.

> **Leave the transmit power at the DEFAULT 8 dBm on this board.** The old board could run
> 19.5 dBm because the AMS1117 could supply the current peaks; a USB-powered C3 at that
> level produces distorted output and the gateway receives 0 packets — and since broadcast
> has no ACK, the node still reports "sent". The full reasoning is at the end of
> `esp32-outdoor/platformio.ini`.

### Room-corner node (ESP32‑C3‑DevKitM‑1) — DHT22 on GPIO4

Four identical boards differing only in `DEVICE_UUID`. DATA → GPIO4, **4.7k pull-up to
3.3V**, powered from **3.3V not 5V**. The pins to avoid on the C3 and why:
[`esp32-room/README.md`](esp32-room/README.md).

### Gateway (QR Box Advance) — **NO sensor at all**

This board once had an optional SHT3x on I²C 0x44; that has been removed from the firmware
entirely. Wiring one in now does nothing — no code reads it, and if it did it would only add
a second source of numbers, making the screen flip back and forth between the wall's
temperature and the room's.

---

## 2. Filling in the configuration (`src/config.h` on each node)

Take the values from **admin web → Khách hàng → open each node → the "Nạp firmware"
section**:

| Field in config.h | Where on the panel | Note |
|---|---|---|
| `ORG_ID` | ORG_ID field | **the same** on both nodes |
| `DEVICE_UUID` | DEVICE_UUID field | **different** per node |
| `MQTT_USERNAME` | = DEVICE_UUID | |
| `MQTT_PASSWORD` | MQTT_PASSWORD field | **different** per node |
| `MQTT_HOST` | MQTT_HOST field | **the same** — see the warning below |
| `MQTT_PORT` | 1883 | fixed (plaintext, NO TLS) |

> ⚠️ **The web panel prints `MQTT_HOST = "emqx"`** — that is the internal Docker service
> name, which an ESP cannot resolve. You must fill in the server's **public IP/domain**.

WiFi (`WIFI_SSID`/`WIFI_PASSWORD`): the network at the node's installation site.

The outdoor node running the `esp32-espnow` env **does not join WiFi** — it only *scans* to
learn which channel the router is broadcasting on and then locks onto it (ESP‑NOW requires
both sides on the same channel). With that env only `WIFI_SSID` matters, and the entire MQTT
block is ignored.

---

## 3. Build & flash (PlatformIO)

```bash
# The OFFICIAL wall panel (2.8" ESP32-S3 board) — plug straight into the USB-C port
# READ esp32-s3-panel/README.md §4 BEFORE SOLDERING: 3 things to measure on real hardware.
cd esp32-s3-panel
cp src/config.h.example src/config.h      # fill in as per §2 — SHARED by every panel board
pio run -e esp32s3-panel -t upload --upload-port COMx
pio device monitor -p COMx -b 115200      # watch the log

# The 4 room-corner nodes — flash them ONE AT A TIME, changing DEVICE_UUID in between
cd esp32-room
cp src/config.h.example src/config.h      # WIFI_SSID (channel discovery only) + DEVICE_UUID
pio run -e esp32c3-room -t upload --upload-port COMz

# Outdoor node (ESP32-C3-DevKitM-1)
cd esp32-outdoor
cp src/config.h.example src/config.h
pio run -e esp32-espnow -t upload --upload-port COMy   # DEFAULT: ESP-NOW slave
pio device monitor -p COMy -b 115200
```

> ⚠️ **The QR Box Advance board needs its own 9–24 VDC supply on P2/P4.** Port P3 only has
> TX/RX/GND for debugging — powering it from a USB‑TTL alone leaves the display + ESP32
> short of current, the board browns out and **resets continuously** (the log emits
> `rst:0x3 (SW_RESET)` every few tens of ms and never manages to print a single line of
> firmware output).

The outdoor node has a **fallback env** that connects straight to WiFi + MQTT, for use when
ESP‑NOW misbehaves — it does not depend on the indoor node, so it isolates faults very
quickly:

```bash
pio run -e esp32-wifi -t upload --upload-port COMy
```

A healthy run looks like:
```
WiFi -> "SSID_NAME" .... OK  IP=192.168.x.x
MQTT ... connected
[telemetry] t=30.0°C h=60% -> sent
```

---

## 4. Verifying the link

- **Web:** the node shows **"Trực tuyến"**; the *Nhiệt độ/Độ ẩm mới nhất* section and the
  chart update.
- **App:** the device card shows *"Nhiệt độ trong nhà"* (ESP32) and *"Nhiệt độ ngoài trời"*
  (ESP8266).

---

## 5. Common problems

| Log | Cause | Fix |
|---|---|---|
| `MQTT ... failed rc=4` | Wrong username/password | Re-copy DEVICE_UUID/MQTT_PASSWORD from the panel |
| `MQTT ... failed rc=5` | The broker has not authorised the device | The device's user/token pair needs loading into EMQX (seed/sync auth) — see *Open questions* |
| `rc=-2` repeating forever | Network/host | Check that MQTT_HOST is the public IP (not `emqx`) and that the WiFi has internet |
| `Sensor read error (NaN)` *(outdoor node)* | Wrong pin/supply/DHT type | Re-check VCC 3V3 + DATA on the right pin + (4-pin type) the 10k pull-up + the correct `DHT_TYPE` |
| `No SHT3x reading yet` *(indoor node)* | The SHT3x is not wired to the I²C bus, or the address is wrong | Wire it to J1 (§1); the SHT3x must be at 0x44 |
| `rst:0x3 (SW_RESET)` every few tens of ms | The QR Box board is missing its main supply | Feed 9–24 VDC into P2/P4 — a USB‑TTL on P3 cannot power the board (§3) |

---

## 6. Gateway — the wall panel (IR + touch screen)

The code is in `esp32-s3-panel/src/`, flashed onto the `esp32-s3-panel/` board (official) or
the old QR Box board.

This board combines **5 roles** — but **measuring temperature is no longer one of them**:
receiving ESP‑NOW from the 4 room-corner nodes and the outdoor node · relaying each node's
data up to MQTT on its behalf · driving the air conditioner over infrared · displaying and
controlling locally on the 2.8" screen · talking to the Arduino UNO Q over UART. All five are
*relaying* and *executing*; none of them produces a measurement.

UI design, how the pinout was reverse-engineered from the schematic, and why the two cores
must be separated: [`Interface/README.md`](Interface/README.md).

### 6.1 Why the gateway connects to MQTT directly instead of being another slave

1. **The target of a command is a lookup, not an inference.** `get_gateway_device()` picks
   `role=master` first (and **refuses** `room` nodes — they have no MQTT session at all),
   only then falling back to the oldest `node_type=indoor`. So **this board must be given the
   master role on the web UI**; without that, a household with two indoor rows will drive the
   wrong node, and the symptom is a command that publishes successfully while the air
   conditioner does not move.
2. **IR commands are too large to travel over ESP‑NOW.** `command_publisher.py` includes
   `ir_raw` — an array of several hundred µs timings, a few KB. ESP‑NOW is limited to
   **250 bytes per frame**, so relaying through the master would mean writing our own
   fragmentation + reassembly + missing-fragment protocol. Connecting to MQTT directly means
   the broker has already solved it.

→ This node **carries the old ESP32‑S3 node's own `DEVICE_UUID`**. Do not create a new device
on the web UI.

### 6.2 Wiring

The receiver and transmitter are two separate 3-pin modules. The QR Box Advance board has **no
free GPIO left**, so the two IR pins have to come from two different places — this is a
hardware constraint, not a preference:

```
   IR Transmitter (emitter LED)   QR Box Advance
   VCC ──────────────────────── 3V3
   DAT ──────────────────────── GPIO17   ← TX pin of the A7680C 4G module
   GND ──────────────────────── GND

   IR Receiver
   VCC ──────────────────────── 5V       ← available on header P3
   OUT ──────────────────────── GPIO15   ← P3, through the TXS0104 level shifter
   GND ──────────────────────── GND
```

- **IR transmit → GPIO17** is only usable if the **A7680C 4G module is NOT soldered** to the
  board (this project does not use 4G). This pin **does not reach a header** — you have to
  solder a wire directly to the module's pad. It runs at 3.3V directly: the UART_2 lines do
  **not** pass through the TXS0104.
- **IR receive → GPIO15** goes through the TXS0104, so the receiver can run at 5V. That level
  shifter only conducts while `EN_LEVEL_SHIFT` (GPIO12) is driven HIGH — `Ui::begin()` does
  that *inside `setup()`*, and it **must never be pulled up with an external resistor**:
  GPIO12 is MTDI, and HIGH at reset makes the ROM select the 1.8V flash level so **the board
  will not boot**.

These two pins are declared in `platformio.ini`'s `build_flags` (alongside the `TFT_*` flags),
**not** in `src/config.h`.

**Where to mount the board:** the transmitter LED on this module is driven straight from the
data pin with no amplifying transistor → **its range is only ~2‑5 m and it needs line of sight
to the indoor unit's receiver**. Going further requires adding your own transistor + power
LED. Point the receiver towards wherever the user stands to press the remote.

### 6.3 Flashing

```bash
cd esp32-s3-panel
pio run -e esp32s3-panel -t upload --upload-port COMx
pio device monitor -p COMx -b 115200
```

A healthy run:
```
== BreezeLink · QR Box Advance Touch · INDOOR (indoor + master + IR) ==
LCD: ST7789 320x240 (rotation 1) · touch: GT911
WiFi -> "SSID_NAME" .... OK  IP=192.168.x.x
MQTT ... connected
ESP-NOW ready · master MAC = XX:XX:... · channel 6
IR: tx GPIO17 · rx GPIO15
[telemetry] t=30.0°C h=60% -> sent · espnow rx=0 dropped=0 · channel=6
```

### 6.4 Learning the remote (required before auto-control can work)

On the web/app press **"Học nút này"** for each entry. The backend sends `{"learn":"COOL 25"}`
to the `cmd` topic, the node enables the receiver for 30s, you press the corresponding button
on the real remote, the node uploads the waveform to the `learn` topic and the backend stores
it in `ir_codes`.

You need the full set (per `ir_service._REQUIRED_*`): **COOL 24, 25, 26, 27, 28** + **DRY**,
**FAN**, **OFF**. If any entry is missing, `comfort_engine` raises `NoIrCodesError` and
**auto-control is completely locked out**. The discrete buttons (`FAN_SPEED`, `SLEEP`,
`SWING_V`…) are optional.

The log while learning:
```
[learn] "COOL 25" — point the remote at the receiver and press the button (max 30s)
[learn] "COOL 25" 227 marks (1348 bytes) -> uploaded to cloud
```

### 6.5 Receiving commands

```
[cmd] c-1a2b3c4d -> COOL 26 (auto:COOL@26) · 227 marks, ready to transmit
[cmd] saved code 7f3e…-… to NVS (227 marks)
[ir] transmitted 227 marks to the air conditioner
[state] ack=c-1a2b3c4d mode=COOL setpoint=26 -> sent
```

The backend only includes `ir_raw` on the **first** use of each `ir_code_id`; after that it
trusts that the node has kept the code in NVS and sends only the id. Codes in NVS **survive
power loss and even `pio run -t upload`** (NVS lives in its own partition).

### 6.6 Problems specific to this node

| Log | Cause | Fix |
|---|---|---|
| `ir_code_id=… not in NVS and the server sent no ir_raw` | **Cache mismatch**: the node was `erase_flash`ed or swapped so it lost NVS, but Redis on the server still remembers "that node has this code" | Clear the org's IR cache in Redis (the `redis_ir_cache` key) so the server resends `ir_raw`. The node **deliberately does not ack** in this case, so that `commands.acked_at` on the web UI does not falsely report execution |
| `[learn] timed out waiting for "…"` | Flat remote battery, not aimed at the receiver, or too far away | Press it within 1 m of the receiver, aimed straight at it |
| `[ir] ignoring noise (3 marks)` | A fluorescent lamp or another remote leaked in | Normal — the node is still waiting, just press the remote |
| `[cmd] … this code has not been learned` | No code learned for that (mode, temperature) | See §6.4 and learn the full set |
| The AC does not react even though the log says `transmitted` | Out of range / the emitter LED is aimed wrong | See the range note in §6.2 |
| `Could not allocate the MQTT buffer` | Out of heap at startup | Rare; if it happens, reduce `MQTT_BUFFER_BYTES` and `IrIo::RAW_MAX` |
| IR learning always times out even with a good remote | `EN_LEVEL_SHIFT` never went HIGH → the TXS0104 is blocking the receiver | See §6.2; check whether `Ui::begin()` is running (does the screen light up?) |

> `espnow-relay.*` and `slave-watch.*` **used to be** copies shared with
> `esp32s3-indoor-master/`. That directory is gone, so `esp32-s3-panel/src/` is now the
> **only** place holding them — fix once and you are done, no more remembering to sync two
> places. The packet layout is still shared with the outdoor node via
> `shared/espnow-message.h`.

---

## Open questions
- **EMQX auth for new devices:** the backend stores a per-device `mqtt_token`; we still need
  to confirm whether loading the pair (username=DEVICE_UUID, password=token) into the EMQX
  built-in DB already happens automatically when a device is created on the web UI. If not,
  `rc=5` will appear and it has to be seeded by hand.
