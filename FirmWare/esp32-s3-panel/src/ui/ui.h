#pragma once
#include <Arduino.h>

// ============================================================================
//  The indoor node's 2.8" touch screen UI -- runs in ITS OWN TASK.
// ----------------------------------------------------------------------------
//  Full design (wireframes, coordinates, touch map, the reasoning behind each
//  decision): ../../Interface/README.md
//
//  WHY IT HAS TO BE TWO TASKS ON TWO CORES RATHER THAN "CALLED FROM loop()":
//
//  1. loop() STALLS FOR SECONDS, NOT RARELY BUT ROUTINELY:
//       connectWifi()  a while loop + delay(500) until it joins the network
//       connectMqtt()  delay(2000) on every rc != 0
//       IrIo::blast()  blocks for 50-250ms
//     Drawing inside loop() means that exactly when the network drops -- exactly
//     when the user most needs to look at the screen -- the screen freezes as if
//     the node had died. That is the worst failure mode for a wall-mounted control
//     panel: it asserts something false.
//
//  2. CORE 0 FOR THE UI, CORE 1 FOR IR -- MANDATORY, NOT FOR SMOOTHNESS:
//     IrIo::blast() times itself with delayMicroseconds to build the 38kHz carrier
//     (a 26us period). Another task on the same core being scheduled in between
//     stretches the marks and spaces by tens of us -> a malformed IR frame -> the
//     air conditioner sits there doing nothing while the log still says "sent".
//     Arduino runs loop() on core 1, so the UI has to go on core 0.
//
//  3. THE TWO TASKS MUST NOT TOUCH EACH OTHER'S PROPERTY:
//     PubSubClient is not thread-safe, and IR has to be on core 1. So the UI task
//     does NOT publish and does NOT transmit IR itself -- it queues an order and
//     loop() collects and executes it. This is exactly the pattern already used for
//     the MQTT callback in main.cpp ("the callback only unpacks the message and
//     places an order; loop() transmits the IR and sends the ack").
//
//  Hardware ownership, split cleanly:
//     UI task -> the display SPI, the I2C bus (touch + DS1307), LEDC (backlight/buzzer)
//     loop()  -> WiFi, MQTT, ESP-NOW, IR, NVS
// ============================================================================
namespace Ui {

/// A snapshot of node state to draw. loop() rebuilds it every iteration and calls
/// publish(); the UI task copies it out under a mutex and diffs it against the last
/// drawn version so only changed cells are repainted.
///
/// Every reading uses NaN for "missing", NOT 0. The predecessor app defaulted
/// missing values to 0.0 and then displayed a fabricated setpoint -- the same rule
/// blocks it here.
struct Model {
  /// How many room-corner sensor slots the UI pre-allocates. 8 = the BLE packet
  /// layout's AC_BLE_MAX_NODES, so no valid node falls off the screen. The real
  /// installation is currently 4; the surplus slots are simply not drawn (see
  /// `roomSlots`).
  static const uint8_t MAX_ROOMS = 8;

  // --- connectivity ---
  bool     wifiUp   = false;
  int      rssi     = 0;
  bool     mqttUp   = false;
  char     ip[16]   = "";
  char     ssid[33] = "";
  char     mac[18]  = "";
  uint8_t  channel  = 0;
  /// Is the panel pinning the ESP-NOW channel ITSELF (true = WiFi is down and it is
  /// holding the remembered channel). Distinguishes "the router decides the channel"
  /// from "the panel remembered the channel" -- see espnow-channel.h.
  bool     channelPinned = false;

  // --- indoor readings: the MEDIAN of the room-corner nodes ---
  //
  // NO LONGER THIS BOARD'S OWN NUMBERS. The gateway board has no sensor; the two
  // values below are built by RoomRegistry::median() on core 1 and copied across.
  // NaN = no corner has a reading yet, and the screen must show a skeleton rather
  // than a zero.
  float    tIn = NAN, hIn = NAN;

  // --- each room corner individually (BLE mesh) ---
  //
  // Present here, not just the median, BECAUSE THAT IS WHY THERE ARE FOUR SENSORS:
  // the median hides exactly the most valuable information -- which corner is
  // drifting. Someone standing at the panel needs to see "the window corner is 29°,
  // the other three are 25°" to know to draw the curtains, not an unexplained 25.5°.
  uint8_t  roomSlots       = 0;      ///< corners HEARD FROM -- only this many are drawn
  uint8_t  roomOnlineCount = 0;      ///< corners still being heard (broken sensors included)
  uint8_t  roomVoting      = 0;      ///< corners actually feeding the median
  float    roomT[MAX_ROOMS]  = {NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN};
  float    roomH[MAX_ROOMS]  = {NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN};
  bool     roomOnline[MAX_ROOMS] = {false};
  uint8_t  roomCorner[MAX_ROOMS] = {0};   ///< the corner label the node declares; 0xFF = none
  uint32_t roomAgeSec[MAX_ROOMS] = {0};

  // --- Arduino UNO Q (edge AI, over UART) ---
  bool     unoqUp = false;   ///< has a valid packet been heard recently
  uint32_t unoqRx = 0;       ///< advice/commands received from it
  uint32_t unoqRejected = 0; ///< packets dropped (bad magic/CRC/link_key/duplicate)

  /// The UNO Q's LATEST advice -- the content of the EDGE AI tab.
  ///
  /// A LIMITATION TO KNOW BEFORE READING THAT SCREEN: the wire packet only carries
  /// (kind, mode, setpoint, seq) -- see AcUnoQCommandHeader. The UNO Q also computes
  /// a 15-minute forecast and counts anomalies, but THOSE DO NOT TRAVEL OVER THE
  /// WIRE; they stay in its own log. So this tab can say WHAT and WHEN, but not WHY.
  /// Do not invent a "reason" line inferred from mode/setpoint -- that is guessing,
  /// and a confident explanation that is wrong is worse than no explanation.
  char     unoqMode[8] = "";       ///< "COOL"/"DRY"/"FAN"/"OFF"; empty = nothing yet
  int      unoqSetpoint = -1;      ///< -1 = this mode does not use a temperature
  bool     unoqWasCommand = false; ///< true = a COMMAND (IR was transmitted), false = advice only
  uint32_t unoqAgeSec = 0;         ///< seconds since the latest advice
  bool     unoqEverAdvised = false;///< distinguishes "NEVER" from "0 seconds ago"

  // --- outdoor readings (over ESP-NOW) ---
  float    tOut = NAN, hOut = NAN;
  bool     outOnline  = false;
  uint32_t outAgeSec  = 0;      // seconds since the last ESP-NOW packet
  uint32_t espnowRx   = 0;
  uint32_t espnowDrop = 0;

  // --- air conditioner ---
  char     mode[8]  = "";       // "COOL"/"DRY"/"FAN"/"OFF", empty = unknown
  int      setpoint = -1;       // -1 = unknown
  bool     overrideLocal = false;   // an override set from this very screen (README §8.3)
  uint32_t lastCmdSec = 0;      // seconds since the server's last command
  /// Has the server EVER issued a command. This cannot be folded into
  /// lastCmdSec=0: "the server has never been heard" and "the server just issued a
  /// command" are opposite situations that both produce a zero. The same reasoning
  /// recorded at cloud_silence_sec in pushUnoQSnapshot() -- the EDGE AI tab relies
  /// on exactly this distinction to explain why the UNO Q is still only advising
  /// rather than taking control.
  bool     cloudEverCommanded = false;

  /// Which combinations already have an IR code in NVS. loop() computes it and puts
  /// it here rather than letting the UI task ask IrStore: NVS is owned by core 1,
  /// and copying a 16-bit bitmask across is far cheaper than building a lock around
  /// the whole store.
  /// bit i = a code exists for COOL (16 + i), i = 0..14.
  uint16_t coolMask = 0;
  bool     hasDry = false, hasFan = false, hasOff = false;

  // --- fan speed: DISCRETE BUTTON codes, learned from the app ---
  //
  // NOT the FAN mode above. The two are completely different and very easy to
  // confuse:
  //   FAN mode   -> the unit blows without cooling; part of the (mode, temperature)
  //                 matrix, a single fixed code.
  //   fan level  -> airflow speed, settable in EVERY mode; each level is its own
  //                 discrete code in the backend's `ir_action_codes` table.
  // bit i = a code exists for AcActions::fanWire(i).
  uint8_t  fanMask = 0;
  /// The fan level the panel JUST TRANSMITTED. 0xFF = none this session.
  ///
  /// NOT the unit's real state, and the screen must not present it as such -- the
  /// same rule recorded in the app (override_panel.dart `_fanWire`). A fan command
  /// is a one-way IR frame; one press of the actual remote makes this number wrong
  /// immediately and the panel has no way of knowing.
  uint8_t  fanLast = 0xFF;

  // --- humidifier (driven by the panel, not through the server) ---
  bool     humidOn        = false;   ///< our belief: the unit is running
  bool     humidOverride  = false;   ///< a manual OVERRIDE is active (not yet expired)
  bool     humidHasOn     = false;   ///< the ON slot has a learned code
  bool     humidHasOff    = false;   ///< the OFF slot has a learned code
  float    humidRh        = NAN;     ///< the smoothed humidity it is working from
  uint32_t humidOverrideLeftSec = 0;
  /// Why it is in that state, as one readable sentence.
  ///
  /// A pointer rather than an array: HumidifierControl::reasonText() returns a
  /// constant string living in flash, so it lives forever and memcpy'ing the Model
  /// to another core is still safe -- the same pattern as `fw` below. A 40-byte
  /// array just to copy an immutable string would be wasteful, and this Model is
  /// memcpy'd 5 times a second.
  const char *humidNote = "";

  // --- learning the remote ---
  bool     learning = false;
  char     learnLabel[24] = "";
  uint32_t learnRemainSec = 0;

  // --- miscellaneous ---
  uint16_t irCodeCount = 0;
  uint32_t uptimeSec   = 0;
  const char *fw       = "";
};

/// What the user has just pressed. The UI task pushes it into a queue and loop()
/// collects and executes it.
///
/// DEL_CODE travels in THIS queue rather than through SettingFn, even though its
/// button lives in the Settings screen: SettingFn is called DIRECTLY by the UI task
/// on core 0 (it is for hardware core 0 owns -- backlight, buzzer), while deleting a
/// code writes NVS, and NVS belongs to loop() on core 1 (see the ownership table at
/// the top of this file). Calling IrStore from core 0 means two cores opening the
/// same Preferences namespace -- failing at random.
struct Command {
  /// RESYNC:    ask the server to resend the whole IR code store. Carries no
  ///            mode/setpoint.
  /// FAN_SET:   transmit a learned fan level. See `arg`.
  /// HUMID_SET: change the humidifier mode. See `arg`.
  enum Kind : uint8_t { MANUAL, AUTO, DEL_CODE, RESYNC, FAN_SET, HUMID_SET } kind;
  char mode[8];
  /// MANUAL: the setpoint. DEL_CODE: the temperature of the combination to delete,
  /// -1 for a fixed code (DRY/FAN/OFF) -- the same convention as
  /// IrStore::removeAlias().
  int  setpoint;

  /// FAN_SET:   the fan level index, 0..AcActions::FAN_COUNT-1.
  /// HUMID_SET: HUMID_OFF_ARG / HUMID_ON_ARG / HUMID_AUTO_ARG below.
  ///
  /// ITS OWN FIELD rather than being stuffed into `mode`: `mode` only holds 7
  /// characters, while discrete button labels run to 9 ("FAN_SPEED", "HUMID_OFF").
  /// Stuffing it in makes snprintf silently truncate to "FAN_SPE" -- an NVS lookup
  /// key that does not exist, so the button becomes a dead key with no error to
  /// read.
  uint8_t arg;
};

/// The values of `Command::arg` when kind = HUMID_SET.
enum : uint8_t { HUMID_OFF_ARG = 0, HUMID_ON_ARG = 1, HUMID_AUTO_ARG = 2 };

/// One log entry: a command just received from the backend and what the node did
/// with it.
///
/// It carries NO timestamp. The time is kept by the DS1307, and the DS1307 sits on
/// the I2C bus owned by the UI task (see the ownership table at the top of this
/// file) -- loop() reading it here would mean two cores talking on one bus. So the
/// UI task stamps it itself when it drains the queue: that is one draw interval
/// (200ms) later than the packet's arrival, negligible against the minute-level
/// resolution the screen displays.
struct CmdLog {
  /// What the node did with this command. This is the part the serial log always
  /// had and the screen did not -- and it is the question an installer actually
  /// needs answered when the air conditioner does not react: did the command NOT
  /// arrive, or did it arrive and the node could not transmit it?
  enum Result : uint8_t {
    SENT,      // an IR frame existed and has been (or is being) transmitted
    NO_CODE,   // the backend sent neither ir_raw nor ir_code_id -- this combination is unlearned
    NEED_RAW,  // an ir_code_id arrived but NVS is empty -> asking the server to resend
    DUPLICATE, // a req_id already executed (a QoS1 broker replay) -> skipped, ack only
  };

  char    mode[8];
  int     setpoint;
  char    reason[24];   // "auto:COOL@25" / "manual override" / "action:FAN_SPEED"
  Result  result;
};

/// Write one log entry. Called from loop() (including inside the MQTT callback --
/// it only pushes into a queue, touching neither LVGL nor publishing anything).
void logCommand(const CmdLog &e);

/// Bring up the display + touch + backlight + buzzer, then create the UI task on
/// core 0.
/// Call in setup(), BEFORE connectWifi() so the installer sees the screen light up
/// immediately -- and so the screen can keep drawing while WiFi probes for the
/// network.
/// Returns false if no touch controller was detected (the screen still displays, it
/// just cannot be pressed).
bool begin();

/// Called every loop(): copy the snapshot into the shared area. Cheap (one memcpy
/// under a mutex) and draws nothing -- drawing is the UI task's job.
void publish(const Model &m);

/// Called every loop(): collect whatever the user has just pressed. Returns false
/// if there is nothing.
bool pollCommand(Command &out);

/// Called after execution completes: the result line appears as a toast on screen.
/// nullptr = success, use the default wording.
void reply(const char *msg);

// readIndoor()/setIndoor() ARE GONE. This board used to carry its own sensor (a
// DHT22 on core 1, or an SHT3x read by the UI task over I2C), so it needed a shared
// locked store for the two cores to hand numbers to each other. The only source is
// now the room-corner nodes, and core 1 has already computed the median before
// calling publish() -- the numbers travel inside the Model like every other field,
// with no separate channel needed.

} // namespace Ui
