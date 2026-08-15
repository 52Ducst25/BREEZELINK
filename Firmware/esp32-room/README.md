# Room-corner sensor node — ESP32-C3-DevKitM-1 + DHT22

Four identical boards, one in each corner of a room. Each board reads a DHT22 and
**broadcasts over ESP-NOW** to the gateway mounted near the air conditioner. No WiFi, no
MQTT, no secrets in `config.h`.

## Why four sensors

A single sensor on a wall does not tell you the temperature of the room — it tells you the
temperature of **that wall**. A corner in direct sun, a corner under the AC outlet and a
corner behind a cabinet routinely differ by 3–4 °C.

Both the gateway and the backend take the **median** of the fresh corners (not the mean), so
one misbehaving corner cannot drag the setpoint away. A mean can — permanently — and the
only symptom is "it just feels wrong in here".

## Why ESP-NOW and not Bluetooth

An ESP-NOW frame carries 250 bytes, so it can carry the node's own **32-character
`device_uuid`** directly. The gateway simply publishes to that node's topic — adding or
removing a corner only means flashing a new board; the gateway needs no change and no
reflash.

A classic BLE advertising packet is only 31 bytes, not enough for the uuid. Going that route
would force the gateway to keep an ordered uuid array and be reflashed every time a node
changes; one slot out of place and corner A's readings land in the cloud under corner B's
name — the charts still show numbers and nothing anywhere reports an error.

Bluetooth in this system is reserved for the **gateway ↔ Arduino UNO Q** link, where both
sides have a real GATT connection (bidirectional, negotiable MTU) and no 31-byte ceiling.

## Flashing

All four boards run **the same firmware** and differ in exactly two values (`DEVICE_UUID`,
`ROOM_CORNER`). So they are **four envs**, not four directories:

```bash
cd Firmware/esp32-room
cp src/config.h.example src/config.h     # WIFI_SSID + FW_VERSION, shared by all 4 boards
cp nodes.ini.example nodes.ini           # per-board identity

pio run -e ss1 -t upload --upload-port COM30
pio run -e ss4 -t upload --upload-port COM33
pio device monitor -e ss1 --port COM30

pio run                                   # build all 4 — check whether a change broke any board
```

Each env has its own build directory (`.pio/build/ss1…`), so flashing one board does not
rebuild the others.

**Why not four separate directories:** splitting them means four copies of `main.cpp` +
`room-sensor.cpp`. One bug fix has to be applied four times, and on the fourth someone will
forget — then one corner of the room runs an old build and nobody notices, because it
**still reports perfectly normal readings**.

**Why identity lives outside `config.h`:** there is only one of that file, but the four
boards differ. Flashing the third board would mean editing the file, flashing, editing again,
flashing the fourth — and there is no way to look at the directory and tell which board's
identity it currently holds.

Get `DEVICE_UUID` from the admin web UI → **Khách hàng** → open the *Cảm biến phòng* node →
**Nạp firmware**. Each corner is its own device row, so **every board needs a different
uuid** — duplicate uuids mean two boards overwrite the same device row and the charts jump
around for no visible reason.

## The three biggest time sinks

- **`WIFI_SSID` must match the gateway exactly and must be a 2.4 GHz network.** This is
  silent-failure number one. The node **does not join** WiFi — it only *scans* for this exact
  name to learn which channel the router is on, because ESP-NOW requires every party to be on
  the same channel. One wrong character (or a 5 GHz network name) and the node stays on
  default channel 1, the frames go nowhere, and since broadcast has **no ACK** not a single
  log line anywhere reports a problem.

  *Check:* the boot log prints the network name it is looking for and the channel it locked
  onto.

- **`ROOM_CORNER` is only a display label.** The real identity is `DEVICE_UUID`. Two boards
  sharing a corner number is **harmless**: both still have their own topic, both still feed
  the median, the screen just shows duplicate labels. Setting it correctly means a technician
  can read the screen and immediately see which corner is drifting.

- **4.7k pull-up to 3.3V on the DHT22 data line.** Without it, reads succeed intermittently
  (the checksum catches the bad ones, so you get **no wrong numbers**, just interleaved NaN) —
  which looks exactly like a loose wire. And **power the DHT22 from 3.3V, not 5V**: the
  ESP32-C3 pins do not tolerate overvoltage.

## Pinout

| C3 pin | Connects to | Note |
|---|---|---|
| GPIO4 | DHT22 DATA | + 4.7k pull-up to 3.3V |
| 3V3 | DHT22 VCC | do **not** use 5V |
| GND | DHT22 GND | |

The pins you **must avoid** on the C3 and why: see the comment block at the top of
[platformio.ini](platformio.ini).

## Packet layout

45 bytes, defined in [`../shared/espnow-message.h`](../shared/espnow-message.h) — shared with
the outdoor node and the gateway. The radio part (channel scan, channel lock, broadcast) lives
in [`../shared/espnow-slave-radio.h`](../shared/espnow-slave-radio.h) and is also shared with
the outdoor node: the "scanNetworks leaves the radio on the last channel" trap has already
cost us once, and there should not be two copies of it.

## Power

This version runs on **5V USB power**. Battery operation would need deep sleep — ESP-NOW suits
that well because there is no WiFi/DHCP/TCP handshake to redo after each wake, unlike a node
that actually joins WiFi. Not implemented yet.
