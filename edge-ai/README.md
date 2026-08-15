# Edge AI — Arduino UNO Q

A Python service running on the **Linux half** of the Arduino UNO Q (Debian on a Qualcomm
Dragonwing QRB2210). The MCU half (STM32U585) takes no part in this flow — "edge AI" here
is a service on a small computer, not a sketch on a microcontroller.

## What it does

1. Connects to the gateway over **Bluetooth (GATT)** — the UNO Q is *central*, the gateway is
   *peripheral*.
2. Listens to the snapshot the gateway pushes every 5 seconds: the 4 room corners, outdoor,
   the AC state, and **how long the server has been silent**.
3. Keeps **per-corner** history (30 minutes), forecasts the temperature 15 minutes ahead, and
   detects outlier corners.
4. Computes the setpoint using **the backend's own algorithm** (`src/app/comfort/`).
5. Normally: **advice only** (`kind=ADVICE`) — the gateway logs it and does NOT fire IR.
6. When the gateway reports the cloud has been silent longer than `EDGE_TAKEOVER_AFTER_SEC`:
   it sends `kind=COMMAND` → the gateway fires IR.

## Why Bluetooth and not MQTT

**A fallback layer has to survive exactly the failure it was built for.** The first version of
this service talked to the system over MQTT — meaning that when the network went down, exactly
when it was needed most, it also lost its path to the gateway and could not help at all. BLE is
a direct link between two devices in the same room: no router, no internet, no broker.

The change also erased an entire class of bug. The MQTT version subscribed to the same topic it
published on, so its own takeover command echoed back and was read as "the cloud is alive
again" → it released control one tick after taking it, forever, every 30 seconds. Now the
**gateway** counts the silence, and it only counts commands from the **server** — there is no
echo left to misread.

A worthwhile side effect: this service needs **no MQTT credentials at all**.

## Why it does not take control immediately

If the cloud and the UNO Q both issue commands, the AC receives two contradictory orders a
minute apart. The symptom — a setpoint that jumps on its own — looks exactly like an algorithm
bug, and it sends whoever is debugging into the wrong half of the system.

So the rule is **deliberately asymmetric**:

| | condition |
|---|---|
| **Take control** | the gateway reports the cloud has been silent for **a long time** (default 300s ≈ 20 telemetry ticks) |
| **Release control** | on the very first snapshot reporting the cloud has spoken again |

And one more boundary, at the protocol level: **`kind=ADVICE` is the default on every path**.
The gateway only fires IR when it receives `kind=COMMAND`, so every branch that does not meet
the conditions falls back safely to "advice only" instead of relying on someone remembering to
block it.

Taking control late costs a few minutes without adaptation. Releasing control late means both
sides fight over the compressor. Those two costs are not equal.

## Why it does not reimplement the comfort algorithm

`src/app/comfort/` is **pure-function** code — no DB, no Redis, no MQTT — so it imports and runs
unchanged on the UNO Q ([`comfort_bridge.py`](edge_ai/comfort_bridge.py)). Rewriting it here
would create **two answers** to "what temperature should this house be", drifting apart with
every backend change, and the symptom of that drift is an air conditioner running at the wrong
temperature in someone's home.

The single exception is the 11 configuration constants: importing them would pull in SQLAlchemy
and the whole ORM — far too heavy for a device out in the field. They are copied into
`controller._FALLBACK_CFG` with a comment pointing at the source, and **any household that has
tuned the algorithm on the web UI must have its real config pasted into `EDGE_COMFORT_CONFIG`** —
otherwise the edge computes something different from the cloud and neither side reports an error.

## Why only two dependencies

`bleak` and `pydantic`. This is software running on a device in a customer's home: every extra
package is one more thing that can break on upgrade and one more thing to patch when a CVE lands.

`pydantic` is not a choice but a consequence: `comfort_engine.compute()` returns
`app.schemas.comfort.ComfortResult`, and that class is a pydantic model. It lives on the backend
side, so it is very easy to miss when counting this directory's dependencies — the first version
only declared `bleak`, and on a dev machine it never showed up because the backend had already
installed pydantic. On a clean UNO Q the service dies on the very first import.

The BLE packet layout is parsed with the standard library's `struct`. Forecasting uses a
hand-written least-squares linear regression — room temperature over 15–30 minutes is close to
linear, and a deep learning model would need labelled data the project does not have, a training
pipeline nobody maintains, and would be far harder to explain when a customer asks "why did it
start the compressor".

`predictor.py` deliberately keeps a narrow interface so its internals can later be swapped for a
heavier model without touching `controller.py`.

## Why it does NOT run inside Arduino App Lab

Tried and failed — written down so the next person does not lose time retrying.

App Lab packages the Python side into a **container** (startup log: `Container
breezelink-edge-ai-main-1 Started`). Measured from inside that container:

```
/.dockerenv                  True
/run/dbus/system_bus_socket  missing
bluetoothctl                 missing
/sys/class/bluetooth         hci0        <- the adapter IS there
```

`bleak` talks to BlueZ **over D-Bus**, and the system D-Bus socket is not mounted into the
container. So it dies at initialisation with an error that never mentions Bluetooth at all:
`[Errno 2] No such file or directory` — the thing not found is a socket, not a device. App Lab's
Brick list has no Bluetooth entry either, so there is no way to request that access from inside.

Conclusion: keeping BLE means running directly on the board's OS — see below.

## Installation — systemd on the board

Requires a working Bluetooth adapter (the UNO Q has one) and BlueZ. This is the only way to get
BLE working, for the reason in the section above.

Install an SSH key once, then run a single command from the dev machine:

```bash
AC_ORG_ID=<org-id> bash edge-ai/deploy/deploy-to-unoq.sh
```

The script checks that the target machine actually has `hci0` (so it does not install onto the
wrong machine in the house), packages a **slice** of the backend, creates a venv, verifies the
imports, then prints the `sudo` step to paste into the board's terminal. Defaults are
`AC_UNOQ_HOST=192.168.1.7`, `AC_UNOQ_USER=arduino`.

**Do not copy the whole `src/` tree onto the board** — an earlier version did, and it broke:

```
File ".../src/app/models/__init__.py", line 7
  from app.models.app_release import AppRelease
ModuleNotFoundError: No module named 'sqlalchemy'
```

`comfort_bridge` only needs `app.models.enums.AcMode`, but Python runs the package's
`__init__.py` before loading the submodule, and the real one imports all 12 ORM models.
Installing SQLAlchemy on the board just to get one enum is the wrong direction — so the script
carries a slice and replaces `__init__.py` with an empty one. The slice is defined by
`_BACKEND_FILES` in `deploy/build-edge-payload.py`; change the backend and forget that list and
the build still succeeds, only blowing up at import time on the board, so the script aborts if a
file is missing.

The service runs as the `arduino` user rather than a dedicated one: BlueZ's D-Bus policy grants
access **per user**, and `arduino` is already in the `bluetooth` group. A brand-new user would be
refused by `org.bluez`, and the symptom is a BLE scan that stays empty forever rather than a
clear permission error.

## Running it on a dev machine

```bash
cd edge-ai
pip install -e .
cp .env.example .env    # fill it in
python -m edge_ai.main
```

## Verifying correct operation

| Action | Expected result |
|---|---|
| Start up with the cloud running | `Đã nối gateway … (MTU 247)`, then one `t_in=… máy chủ cầm lái` line every 30s. NO `ĐÃ RA LỆNH` |
| Look at the gateway screen, Info page | The footer shows `UNO Q đã nối` |
| Stop the cloud worker | After ~300s: `GIÀNH LÁI`, then `ĐÃ RA LỆNH (edge cầm lái)`; the gateway screen log records `edge takeover` |
| Start the cloud worker again | `NHẢ LÁI` on the very next tick, commands stop |
| Unplug all 4 corner nodes | `Gateway báo chưa có góc phòng nào còn tươi — không tính, không ra lệnh` |
| Shine a desk lamp on one corner | `Bất thường ở góc 3 (outlier): lệch +3.2°C…` |
| Press THỦ CÔNG on the gateway screen | The edge stops issuing commands even while holding control |

## Easy things to get wrong

- **A wrong `EDGE_ORG_ID` half-breaks the system silently.** This value is hashed into the
  `link_key`; if it is wrong the service still connects to the gateway and still **receives**
  readings, but every command it sends is silently refused by the gateway. The only sign is in
  the gateway log: `[unoq] tu choi goi sai link_key`.
- **MTU.** A snapshot is 39 bytes while the BLE default MTU only allows 20. Both sides check and
  complain loudly, but if you see `Bỏ ảnh chụp không hợp lệ: … cắt cụt` repeating, that is BlueZ
  failing to negotiate a larger MTU.
- **Changing the packet layout means changing BOTH sides** — `edge_ai/protocol.py` and
  `Firmware/shared/unoq-link-protocol.h`. The sizes are pinned by an `assert` at import time and
  a `static_assert` at compile time, so forgetting blows up immediately instead of quietly
  misreading fields.
- **If the UNO Q loses power, the fallback layer is gone.** It is an ADDITIONAL layer, not the
  only lifeline — the system behaves exactly as it did before it existed.

## Known limitations

- The edge only learns which temperatures the household has IR codes for by **observing the
  state the gateway reports**. A freshly installed household where the cloud has not yet issued
  a single COOL command will get advice only, no control — and it says so in the log.
- Commands issued by the edge **create no `commands` row** on the server (the server is
  disconnected — that is why the edge took over). The gateway still publishes `state` if MQTT is
  alive, so the web UI sees a new state but no command that produced it. The gateway screen log
  records `edge takeover` — that is the only place it is explained.
- **Not yet run on real hardware.** The whole decision path has been verified with smoke tests
  using byte-accurate packets, but a real BLE connection has not been tested.
