# System architecture

Updated: 2026-08-11 · Scope: the whole system, after the move from 2 nodes to 6 devices per
household.

This document answers **why** each boundary sits where it sits. How to run the system and how
to flash the firmware is in [`../README.md`](../README.md); per-phase detail lives in
`plans/260811-1809-kien-truc-4-node-phong-espnow-uno-q/`.

---

## 1. Six devices in one household

| Device | `node_type` | Uplink | Has a sensor? | MQTT session? |
|---|---|---|---|---|
| 4× ESP32-C3-DevKitM-1 | `room` | ESP-NOW → gateway | DHT22 | no |
| 1× QR Box Advance (WROOM-32) | `indoor` | WiFi + MQTT | **no** | yes (master) |
| 1× ESP32 DevKit V1 | `outdoor` | ESP-NOW → gateway | DHT22 | no |
| 1× Arduino UNO Q | — | **Bluetooth GATT** → gateway | no | **no** |

**Only the gateway has an MQTT session.** Everything else — the four room corners, the outdoor
node, and the UNO Q — is mute to the cloud. The four sensor nodes are published on behalf of by
the gateway, so they need no credentials of their own, no access to the customer's WiFi, and
changing the WiFi password does not kill them. The UNO Q deliberately has no uplink to the cloud
at all: see §5.

---

## 2. Three decisions that shape everything else

### 2.1 The "indoor" temperature is the MEDIAN of several sensors

A single wall-mounted sensor measures **that wall**, not the room. Four corners differing by
3–4 °C is normal.

**Median, not mean:** a mean lets one stray corner (window sun, air outlet) drag the setpoint
away permanently, and the only symptom is "it just feels wrong in here" — no log, no alert, no way
to trace it. A median ignores a single outlier entirely as long as the other three corners agree.

This rule exists in **three places** and all three **must produce the same number**:

| Location | File |
|---|---|
| Backend (source of truth) | `src/app/comfort/room_aggregate.py` |
| Gateway (local screen + the packet sent to the UNO Q) | `Firmware/esp32-s3-panel/src/room-registry.cpp` |
| Edge AI | re-imports the backend file itself, via `edge-ai/edge_ai/comfort_bridge.py` |

The C++ version is a copy that **has to exist** (firmware cannot import Python) — change the rule
on one side and you must change both, otherwise the wall panel and the app report two different
temperatures for the same room and neither one is obviously wrong enough to fix.

**And there is a guard for exactly that:** the edge AI recomputes the median with the Python
version and compares it against the number the gateway sent. A discrepancy over 0.05 °C writes a
WARNING naming the two files that have drifted apart (`controller._MEDIAN_DRIFT_C`). Without that
guard this kind of drift has no symptom at all.

### 2.2 The comfort engine does NOT know how many sensors there are

`comfort_engine.compute()` still takes exactly one `(tin, hin)` pair, as it did in the two-node
era. Aggregation happens **before** it, in `telemetry_handler`, and is written to `state:indoor` —
the same Redis key the old version used.

As a result, going from 1 to 4 sensors changed **not a single line** inside `comfort/` beyond
adding one new pure-function module. The algorithm is the riskiest part of the project (audit §1:
no tests, hardware already in customers' homes) — keeping it frozen is deliberate.

### 2.3 The target of a control command is a LOOKUP, not an inference

It used to be possible to infer "which node receives the IR command" from whichever node had just
sent telemetry, because there was only one node indoors. Now most telemetry comes from nodes with
**no IR hardware**.

`telemetry_service.get_gateway_device()` is the single place that answers that question:
`role=master` first (and it **refuses** `room` nodes), only then falling back to the oldest
`node_type=indoor`. Picking wrong means the command publishes successfully, nobody executes it,
and there is no nack — that node does not even subscribe.

---

## 3. Three radio links, one antenna

The gateway runs WiFi (MQTT), ESP-NOW and BLE simultaneously on a single 2.4 GHz radio. The IDF
coexistence layer time-slices them, and the priority order is baked into the design:

- **The gateway does NOT scan for BLE.** It only advertises and holds **one** GATT connection to
  the UNO Q — scanning is the thing that eats airtime continuously. The NimBLE role is cut down to
  peripheral + broadcaster right in `platformio.ini`.
- **Sensor readings go over ESP-NOW**, which reuses the existing WiFi radio rather than opening
  another link.
- **MQTT gets priority** — it is the **only** path by which AC commands come down.

### Why every sensor uses ESP-NOW

An ESP-NOW frame carries 250 bytes, so each node carries its own 32-character `device_uuid`
directly. The consequence is that **the gateway keeps no lookup table**: adding or removing a
corner only means flashing a new board.

A classic BLE advertising packet is only 31 bytes — not enough for the uuid. Going that route
would force the gateway to keep an ordered `uuid[]` array and be reflashed every time a node
changes; one slot out of place and corner A's readings land in the cloud under corner B's name,
the charts still show numbers and nothing anywhere reports an error.

The cost of ESP-NOW: every party **must be on the same WiFi channel**, so each node has to scan
for the household SSID to learn which channel the router is on. That is silent-failure number one
for the whole system — see §7.

### Why Bluetooth is reserved for the UNO Q

This link is **bidirectional** (the UNO Q has to send commands back) and has to carry a snapshot
of all four corners. Advertising can do neither; GATT negotiates an MTU of several hundred bytes
and has a write-back channel built in.

Roles: **gateway = peripheral, UNO Q = central**. The UNO Q runs Debian + BlueZ, a full and easily
programmed central; making the ESP32 the peripheral is the lightest pattern for it. And reversing
the roles would force the gateway to *scan* every time the UNO Q restarts — exactly what was just
ruled out.

### The 39-byte ceiling and the MTU trap

A snapshot is 39 bytes, but the **BLE default MTU is 23** (i.e. 20 bytes of payload). A notify
larger than the MTU is **silently truncated** — no error on either side, the last few corners
simply vanish. So: the gateway requests MTU 247 at initialisation, re-checks it on connect and
prints a loud warning if it fell short; the Python side checks the length before decoding and says
outright "almost certainly the MTU is too small and the packet was truncated".

The packet size is pinned by a `static_assert` on the C side and an `assert` at import time on the
Python side. Add a field on one side and forget the other and the packet still "decodes
successfully", except every field after the insertion point is shifted — CRC does not save you,
because it is computed over exactly the number of bytes the sender *thought* was right.

---

## 4. One cycle of data flow

```
4 room-corner nodes ──NOW─┐
                          │
outdoor node ────────NOW──┴─► gateway ─MQTT──► telemetry_handler
                             ▲ │
              Bluetooth GATT │ ▼
                          Arduino UNO Q (edge AI)
                                                │
                             ┌──────────────────┼──────────────────┐
                        node_type=room     =outdoor            =indoor
                             │                  │           (legacy firmware)
                    set_room_state()     set_outdoor_state()       │
                             │                  │                  │
                    aggregate_rooms()           │                  │
                         (median)               │                  │
                             └────────► state:indoor ◄─────────────┘
                                                │
                                       comfort_engine.compute()
                                                │
                                    get_gateway_device()  ← lookup, not inference
                                                │
                                        command_publisher ─MQTT─► gateway ─IR─► air conditioner
```

Only ticks from the **outdoor** node are allowed to advance `tout_ema` (`is_outdoor_tick`). The
four room nodes tick many times more often; letting them drive the EMA would make the setpoint
track the cold air the unit is currently blowing rather than the weather.

---

## 5. Edge AI: who is in control

If the cloud and the UNO Q both issue commands, the AC receives two contradictory orders — and the
symptom (a setpoint that jumps on its own) looks exactly like an algorithm bug, sending whoever is
debugging into the wrong half of the system. Hence the **deliberately asymmetric** rule:

| | condition | why |
|---|---|---|
| Take control | the gateway reports the cloud silent for ≥ 300 s (20 telemetry ticks) | taking over late only costs a few minutes without adaptation |
| Release control | on the **very first** snapshot reporting the cloud has spoken | releasing late means both sides fight over the compressor |

**Advice ≠ command.** Every packet the UNO Q sends carries a `kind`, and the gateway **only fires
IR on `COMMAND`**. Normally it sends `ADVICE` — the gateway writes it to the on-screen log and does
nothing. Merging the two would mean every experiment on the UNO Q goes straight to the compressor.

**Who counts the silence.** The gateway, not the UNO Q — it holds the MQTT session so it knows more
reliably, and it only counts commands from the **server**. That erases an entire class of bug the
earlier MQTT version had: the service subscribed to the same topic it published on, so its own
takeover command echoed back, was read as "the cloud is alive again", and it released control one
tick after taking it — forever, every 30 seconds.

**UNO Q commands do not go through the override path.** The gateway already has
`runPanelCommand()`, which does almost the right thing, but that path sets the override flag and
asks the server to open an override window. Override exists so a *user* can take control **away
from** the server; the UNO Q is **standing in for** the server. Taking that path means that when
the network comes back, the server is locked out for `override_hours` by the very fallback layer
that just rescued it — and the screen shows "GHI ĐÈ" while nobody pressed anything.

**Why BLE and not MQTT.** A fallback layer has to survive exactly the failure it was built for.
Going through a broker means that when the network drops — exactly when it is needed most — it also
loses its path to the gateway. BLE is a direct link between two devices in the same room.

The edge **imports** the algorithm from `src/app/comfort/` rather than copying it: two copies drift
apart with every backend change, and the consequence of that drift is an air conditioner running at
the wrong temperature in someone's home. The single exception is the 11 configuration constants
(importing them would pull in all of SQLAlchemy).

---

## 6. Identity and security at the device layer

| Layer | Mechanism | Protects against | Does NOT protect against |
|---|---|---|---|
| ESP-NOW | `magic`/`version` + self-declared uuid | junk frames from other systems on the same channel | a device in radio range declaring a uuid |
| BLE (UNO Q) | `link_key` = FNV-1a(ORG_ID) + CRC8 + `seq` replay guard | another household's UNO Q in an apartment block; BLE toys writing at random | a deliberate attacker — the key is in config.h and travels in the clear |
| MQTT | per-device user/pass | devices with no credentials | publishing into another household's topic (needs broker ACLs — audit §6) |
| Backend | `get_device_for_topic` matches org | **a mistyped ORG_ID** in `config.h` | a deliberate attacker holding valid credentials |

Hardening BLE for real, when needed: enable NimBLE bonding + a static passkey and pair once during
installation. Not done, because it adds an installation step that can go wrong, and the threat here
(someone within 10 m who wants to adjust your air conditioning) does not justify it.

The reverse caveat: sensor nodes have **no** whitelist filtering, because the frame declares its own
uuid. A foreign board declaring a valid uuid will be relayed — but the backend rejects uuids that are
not in `devices`, so it only costs a log line.

---

## 7. The SILENT failure modes (nothing is ever printed)

This list is the most worthwhile thing in the document — every item already has a guard in the code,
and a comment at the site explains the guard.

| Symptom | Cause | Guard |
|---|---|---|
| One corner never comes up | `WIFI_SSID` mistyped / is a 5 GHz band -> node locks onto the wrong channel | the node prints the network name it is searching for at boot; broadcast has no ACK, so this is the only guard |
| Outdoor node goes quiet after a packet upgrade | the gateway only accepts v2, the old board still sends v1 | `acEspNowParse()` accepts both, and reads v1 as outdoor |
| The snapshot reaching the UNO Q is missing its last corners | MTU < 42, the notify is truncated | the gateway checks MTU on connect + Python checks the length before decoding |
| Fields in the snapshot are shifted, temperatures come out as garbage | a field was added on one side and forgotten on the other | `static_assert` on the C side + `assert` at import on the Python side |
| The server is locked out for 2 hours after the network returns | edge commands went through the panel's override path | `runUnoQIncoming()` has its own execution path |
| The same edge command executes twice | the UNO Q replays it after reconnecting | `seq`-based replay guard in `unoq-link.cpp` |
| The wall panel and the app report different temperatures | `room-registry.cpp` and `room_aggregate.py` have drifted apart | the edge cross-checks the median and WARNs if it differs by > 0.05 °C |
| The gateway MAC shows "—" on the firmware-flashing page | the gateway has stopped publishing telemetry | the MAC ships with the `state` packet |
| The comfort loop stalls and the log only says "indoor state unknown" | the gateway lost its DHT22 and the backend does not yet know about `room` | phase 01 — the `room` branch in `telemetry_handler` |

---

## 8. Outstanding debt (per audit `plans/reports/audit-260729-2217-*.md`)

Still open: no automated tests, no CI, no OTA for the firmware, MQTT running plaintext on 1883,
EMQX ACLs granted by hand, no telemetry retention. The new architecture **makes the retention item
worse**: a household now has 6 writing sources instead of 2.
