// ============================================================================
//  BreezeLink - QR Box Advance Touch - INDOOR GATEWAY + IR blaster
// ----------------------------------------------------------------------------
//  THIS BOARD NO LONGER MEASURES TEMPERATURE. Four ESP32-C3 nodes in the four
//  corners of the room do that (Firmware/esp32-room/), while this board does the
//  five jobs of a bridge:
//    1. Receive ESP-NOW from the 4 room-corner nodes + the outdoor node -> relay
//       to MQTT on EACH node's behalf (one topic per node, keyed by the uuid it
//       declares itself)
//    2. Receive commands from the cloud -> transmit infrared to the air
//       conditioner + learn the remote
//    3. Display and allow local control on the 2.8" touch screen (a core-0 task)
//    4. Push snapshots to the Arduino UNO Q over Bluetooth (GATT, peripheral role)
//    5. Receive advice/commands back from the UNO Q -- and only execute when it is
//       a COMMAND
//
//  WHY THE DHT22 WAS REMOVED FROM THIS BOARD: a sensor on a wall does not tell you
//  the temperature of the room, it tells you the temperature of THAT WALL. Four
//  corners differing by 3-4degC is routine (window sun, air outlet, behind a
//  cabinet). The "indoor" number is now the MEDIAN of the fresh corners -- see
//  room-registry.h for why the median rather than the mean.
//
//  AN IMPORTANT CONSEQUENCE: this board no longer publishes `telemetry`. It has no
//  reading of its own, and sending another node's borrowed numbers under its own
//  name would be fabrication. Its MAC ships with the `state` packet (read by
//  state_handler.py) so the firmware-flashing page can still show it.
//
//  THREE RADIOS ON ONE ANTENNA: WiFi/MQTT + ESP-NOW + BLE all share one 2.4GHz
//  block. IDF's coexistence layer time-slices them. BLE here is far LIGHTER than
//  the approach once considered (scanning for the 4 room nodes' advertisements):
//  the gateway only advertises and holds ONE GATT connection to the UNO Q, with no
//  scanning at all -- and scanning is the thing that eats airtime continuously.
//  Room readings go over ESP-NOW, which already shares the existing WiFi radio.
//
//  The 6 topics in use, matching the backend EXACTLY
//  (src/app/utils/mqtt_naming.py):
//    telemetry  node -> cloud   {ts,t,h,rssi,mac,fw}      (telemetry_handler.py)
//                               this board ONLY sends on other nodes' behalf,
//                               never its own
//    status     node -> cloud   "online"/"offline" retained (status_handler.py)
//    cmd        cloud -> node   an IR command OR a learn command
//                                                          (command_publisher.py)
//    state      node -> cloud   {ack,mode,setpoint,mac} retained (state_handler.py)
//    learn      node -> cloud   {raw_timing,mode/action,temp} (learn_handler.py)
//    override   node -> cloud   {mode,setpoint} | {clear}  (override_handler.py)
//                               NOT retained -- see buildTopics()
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "espnow-message.h"
#include "espnow-relay.h"
// Pin the ESP-NOW channel while WiFi is down. Without it, losing the network also
// means losing the 4 room corners' readings, because the panel's radio wanders off
// with the WiFi probe loop -- see espnow-channel.h.
#include "espnow-channel.h"
#include "room-registry.h"
#include "slave-watch.h"
#include "unoq-link.h"
#include "ir-io.h"
#include "ir-store.h"
#include "ac-actions.h"
// Humidifier: the panel drives it itself from the median humidity of the 4 room
// corners, WITHOUT going through the server. The backend is only involved in
// learning the codes (the two discrete buttons HUMID_ON/HUMID_OFF).
#include "humidifier-control.h"
// Per-packet IN/OUT trace log. OFF unless compiled with -D GATEWAY_TRACE=1, and
// when off the compiler removes every call below entirely (empty function bodies).
#include "serial-trace.h"
// The QR Box Advance Touch Screen board: the 2.8" display runs in ITS OWN TASK ON
// CORE 0. UI design and why the cores must be separated: ../../Interface/README.md
// and ui.h. The loop() below does not draw a single pixel -- it only feeds data
// across and collects commands back.
#include "ui/ui.h"

// THERE IS NO ROOM-NODE LOOKUP TABLE HERE, and that is the reason for choosing
// ESP-NOW.
//
// An ESP-NOW frame carries 250 bytes, so each room-corner node carries its own
// 32-character device_uuid directly (shared/espnow-message.h). The gateway simply
// publishes to that node's topic -- adding or removing a corner only means flashing
// the new corner; the gateway needs no change and no reflash.
//
// The BLE approach once considered is the opposite: a classic advertising packet
// is only 31 bytes and cannot carry the uuid, which would force this file to keep
// a ROOM_NODE_UUIDS array where one slot out of place puts corner A's readings in
// the cloud under corner B's name -- the charts still show numbers, nothing
// anywhere reports an error, and the two corners stay permanently swapped.

// The self-hosted EMQX broker on the VPS runs plaintext on 1883 (MQTT_TLS=false in
// docker-compose), so use a plain WiFiClient -- NOT WiFiClientSecure.
static WiFiClient   net;
static PubSubClient mqtt(net);

/// PubSubClient's default buffer is only 256 bytes and it SILENTLY DISCARDS
/// anything larger. An IR command carries an `ir_raw` of several hundred numbers ->
/// a few KB of JSON, and so does the learn packet the node uploads. Without
/// enlarging this, every command containing ir_raw vanishes without a trace: the
/// log says nothing and the air conditioner does not move. 12KB is ample for 600
/// transitions.
static const uint16_t MQTT_BUFFER_BYTES = 12288;

static String tTelemetry, tStatus, tCmd, tState, tLearn, tOverride;
static void buildTopics() {
  String base = String("bl/") + ORG_ID + "/" + DEVICE_UUID + "/";
  tTelemetry = base + "telemetry";
  tStatus    = base + "status";
  tCmd       = base + "cmd";
  tState     = base + "state";
  tLearn     = base + "learn";
  // `override` is the channel by which THIS SCREEN asks the server to hand over
  // control -- it did not exist before, so a local override only survived until the
  // next comfort cycle. See ../Interface/README.md §8.3 and
  // app/utils/mqtt_naming.py.
  //
  // IT MUST BE ITS OWN TOPIC AND MUST NOT BE STUFFED INTO `state`: publishState()
  // sends RETAINED, so an override flag inside it would be replayed by the broker
  // every time the node reconnects and would switch override on permanently -- the
  // server would stop computing comfort forever with nobody having pressed
  // anything.
  tOverride  = base + "override";
}

/// IR frame buffer shared by both transmit and learn. A node only does one thing
/// at a time, so two 1.2KB buffers are unnecessary.
static uint16_t irBuf[IrIo::RAW_MAX];

// --- The label currently being learned ---------------------------------------
// "COOL 25" -> label="COOL", temp=25   |   "FAN_SPEED" -> label="FAN_SPEED", temp=-1
static char learnLabel[24] = "";
static int  learnTemp = -1;

// --- Control-authority tracking ----------------------------------------------
// Who is in control: the server, or a person (on this screen OR in the app).
//
// `overrideLocal` NOW HAS REAL EFFECT and is no longer a decorative flag: pressing
// MANUAL publishes to the `override` topic and `override_handler.py` opens the
// override window in Redis, so the comfort loop genuinely hands over control
// (Interface/README.md §8.3 -- that gap has been filled).
//
// AND IT MUST ALSO REFLECT AN OVERRIDE SET FROM THE APP, not only from this
// screen. Every `cmd` packet used to pull this flag back to false, including
// packets the app itself triggered when the user had just overridden in the app --
// so standing at the wall you saw the AUTO badge while the server had already
// handed control to the app. The screen was asserting something false about who
// was in control. The flag now follows the packet's `reason`, see the end of
// takeCommand().
static uint32_t lastCmdMs = 0;
static bool     overrideLocal = false;

// The "which modes already have an IR code in NVS" table, used to dim unlearned
// buttons on screen.
// It MUST be cached: querying NVS directly inside pushUiModel() is 18 keys EVERY
// loop(), and every MISS makes the Preferences library print an ERROR line -- on a
// board that has learned nothing the serial log is drowned completely, at exactly
// the moment the log is most needed to debug an installation. The table only
// changes when a new code is learned, so recompute it then.
static bool     aliasDirty = true;   // true = rescan NVS on the next push

// The latest outdoor readings and air conditioner state -- FOR DISPLAY ONLY. Kept
// separately rather than borrowing `pending`: pending.mode is filled in as soon as
// a packet is unpacked, including commands that are then dropped for having no
// learned code, so displaying it would make the screen show off an air conditioner
// state that never happened.
static float    lastSlaveT = NAN, lastSlaveH = NAN;
static uint32_t lastSlaveMs = 0;
static char     actMode[8] = "";
static int      actSetpoint = -1;

/// The fan level the panel has JUST TRANSMITTED SUCCESSFULLY. 0xFF = no level has
/// been transmitted this session.
///
/// Set only AFTER IrIo::blast() has run, not on receiving the command: this is the
/// only number the screen relies on to say "what level is the fan at", and naming a
/// level the panel never managed to transmit is fabrication. It is also NOT the
/// unit's real state -- one press of the actual remote makes it wrong and the panel
/// has no way to know (see Ui::Model::fanLast).
static uint8_t  lastFanIdx = 0xFF;

/// Does the temperature take part in the IR code lookup key.
/// Only COOL has a (mode, temp) matrix; DRY/FAN/OFF are fixed codes -- matching
/// ir_service._REQUIRED_* ("COOL 24..28 + DRY + FAN + OFF").
static int aliasTemp(const char *mode, int setpoint) {
  return (strcmp(mode, "COOL") == 0) ? setpoint : -1;
}

// --- Commands awaiting execution ---------------------------------------------
// PubSubClient's callback runs RIGHT IN THE MIDDLE of the library reading a packet
// into its own internal buffer. Calling mqtt.publish() there overwrites the very
// buffer being read. So the callback only unpacks the message and places an order,
// while loop() is what transmits the IR and sends the ack.
static struct {
  bool     hasFrame;      // an IR frame is waiting to be transmitted
  uint16_t frameLen;
  bool     needAck;       // an ack is waiting to be sent (even if nothing was transmitted)
  // The ir_code_id whose array must be re-requested from the server; empty = no
  // request. It has to be QUEUED like hasFrame/needAck rather than published
  // directly in the callback -- for exactly the reason at the top of this struct.
  // 37 bytes is enough for a 36-character UUID + '\0'.
  char     needRawId[40];
  char     reqId[24];
  char     mode[8];
  int      setpoint;
} pending;

static String macToText(const uint8_t m[6]) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(buf);
}

/// Copy a string out of the JSON into our own buffer.
/// COPYING IS MANDATORY rather than keeping the pointer: ArduinoJson parses in
/// zero-copy mode (the strings point straight into PubSubClient's buffer), and that
/// buffer is reused by the very next packet -- keep the pointer and by the time
/// loop() sends the ack, req_id is garbage.
static void copyStr(char *dst, size_t dstSize, const char *src) {
  if (src == nullptr) { dst[0] = '\0'; return; }
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

// ---------------------------------------------------------------------------
//  ESP-NOW: relaying the outdoor node's readings
// ---------------------------------------------------------------------------
/// The master reports status ON THE SLAVE'S BEHALF: a slave has no MQTT connection
/// so the broker cannot produce a Last Will for it. Retained so the web/app show it
/// immediately on opening.
static void publishSlaveStatus(const char *uuid, bool online) {
  String topic = String("bl/") + ORG_ID + "/" + uuid + "/status";
  mqtt.publish(topic.c_str(), online ? "online" : "offline", true);
  Serial.printf("[slave] %s -> %s\n", uuid, online ? "ONLINE" : "OFFLINE (heartbeat lost)");
}

// ---------------------------------------------------------------------------
//  ESP-NOW: relaying the room-corner + outdoor nodes' readings
// ---------------------------------------------------------------------------
/// Push ONE slave node's readings to that node's own topic.
///
/// Shared by both the room-corner and the outdoor nodes: an ESP-NOW packet carries
/// its own uuid, so the gateway does not need to know which kind of node it is in
/// order to relay it. The node kind only decides where the gateway FILES the number
/// for display (see onSlavePacket).
static void publishSlaveTelemetry(const char *uuid, const uint8_t mac[6],
                                  float t, float h) {
  JsonDocument doc;
  doc["ts"]   = (uint32_t)(millis() / 1000);
  doc["t"]    = t;
  doc["h"]    = h;
  doc["rssi"] = 0;                    // slaves do not join WiFi, so there is no RSSI
  doc["fw"]   = FW_VERSION;
  doc["mac"]  = macToText(mac);
  doc["via"]  = "espnow";
  char buf[224];
  const size_t n = serializeJson(doc, buf);
  const String topic = String("bl/") + ORG_ID + "/" + uuid + "/telemetry";
  const bool ok = mqtt.publish(topic.c_str(), (const uint8_t *)buf, n, false);
  Serial.printf("[relay] %s t=%.1f h=%.0f -> %s\n", uuid, t, h, ok ? "relayed" : "ERROR");
  SerialTrace::mqttOut(topic.c_str(), (const uint8_t *)buf, n, ok);
}

/// A packet has just arrived from any slave node -- a room corner or outdoor.
static void onSlavePacket(const AcEspNowPacket &pkt, const uint8_t mac[6]) {
  const char *uuid = pkt.device_uuid;
  const bool isRoom = (pkt.node_kind == AC_NODE_ROOM);

  // BEFORE ANYTHING ELSE: record the packet exactly as it arrived. The branches
  // below filter NaN, rate limit, and skip when the table is full -- so putting the
  // trace after them loses precisely the packets you most need to see when chasing
  // a radio problem.
  SerialTrace::packetIn(pkt, mac);

  // EVERY packet counts as a heartbeat -> fast disconnection detection. Including
  // packets with NO reading (NaN, because the slave's sensor failed): the node is
  // still alive, only its sensor is broken -- two different things that must not be
  // merged into "disconnected".
  SlaveWatch::heard(uuid, publishSlaveStatus);
  if (SlaveWatch::dueForStatusRefresh(uuid)) publishSlaveStatus(uuid, true);

  // Write to the display table BEFORE filtering NaN: the table needs to know this
  // corner is alive, and a NaN in it is the correct answer to "alive but the sensor
  // is broken".
  if (isRoom) {
    if (!RoomRegistry::update(pkt)) {
      static bool warned = false;
      if (!warned) {
        warned = true;
        Serial.printf("[room] table full (%u slots) - corner %u onwards will not appear on "
                      "screen, but IS STILL relayed to the cloud\n",
                      RoomRegistry::MAX_ROOMS, RoomRegistry::MAX_ROOMS + 1);
      }
    }
  } else {
    lastSlaveT = pkt.temp;
    lastSlaveH = pkt.humidity;
    lastSlaveMs = millis();
  }

  if (isnan(pkt.temp) || isnan(pkt.humidity)) {
    Serial.printf("[%s] %s alive but its sensor is faulty (NaN)\n",
                  isRoom ? "room" : "slave", uuid);
    return;
  }
  if (SlaveWatch::dueForRelay(uuid)) publishSlaveTelemetry(uuid, mac, pkt.temp, pkt.humidity);
}

// ---------------------------------------------------------------------------
//  LEARNING the remote
// ---------------------------------------------------------------------------
static bool isAcMode(const char *s) {
  return strcmp(s, "COOL") == 0 || strcmp(s, "DRY") == 0 ||
         strcmp(s, "FAN")  == 0 || strcmp(s, "OFF") == 0;
}

/// Is this discrete button something THE PANEL can transmit itself (fan level,
/// humidifier)?
///
/// A DELIBERATE SUBSET of ir_action_service.KNOWN_ACTIONS -- see ac-actions.h. Only
/// buttons present on the screen are worth keeping a copy of in NVS: each frame
/// takes ~600 bytes, and SLEEP/ECO/SWING/TIMER... have no button on the panel to
/// press, so that copy would never be transmitted. They are still learned and
/// transmitted normally FROM THE APP, a path that does not go through the node's
/// NVS.
static bool isPanelAction(const char *s) {
  for (uint8_t i = 0; i < AcActions::FAN_COUNT; i++) {
    if (strcmp(s, AcActions::fanWire(i)) == 0) return true;
  }
  return strcmp(s, AcActions::humidWire(true)) == 0 ||
         strcmp(s, AcActions::humidWire(false)) == 0;
}

static void startLearn(const char *label) {
  const char *space = strchr(label, ' ');
  size_t nameLen = space ? (size_t)(space - label) : strlen(label);
  if (nameLen >= sizeof(learnLabel)) nameLen = sizeof(learnLabel) - 1;
  memcpy(learnLabel, label, nameLen);
  learnLabel[nameLen] = '\0';
  learnTemp = space ? atoi(space + 1) : -1;

  IrIo::learnStart(LEARN_TIMEOUT_MS);
  Serial.printf("[learn] \"%s\" - point the remote at the receiver and press the button (max %lus)\n",
                label, (unsigned long)(LEARN_TIMEOUT_MS / 1000));
}

static void publishLearned(const uint16_t *raw, uint16_t len) {
  JsonDocument doc;
  JsonArray arr = doc["raw_timing"].to<JsonArray>();
  for (uint16_t i = 0; i < len; i++) arr.add(raw[i]);

  // learn_handler.py reads the label from "action" OR "mode" and routes on that:
  // discrete buttons (FAN_SPEED, SLEEP, SWING_V...) go into the ir_action_codes
  // table, while COOL/DRY/FAN/OFF go into ir_codes' (mode, temp) matrix. Sending
  // the wrong key means the code is learned but stored in the wrong table, and the
  // comfort algorithm never sees it.
  //
  // Distinguished by the list of 4 modes rather than by copying the whole discrete
  // button list (ir_action_service.KNOWN_ACTIONS): the backend can add a new button
  // without the node needing a reflash, while the 4 modes are fixed by AcMode.
  if (isAcMode(learnLabel)) {
    doc["mode"] = learnLabel;
    // DRY/FAN/OFF have no temperature -> OMIT the "temp" key entirely. The backend
    // reads a missing temp as a fixed code (upsert_learned_code); sending -1 would
    // store it as a garbage temperature.
    if (learnTemp >= 0) doc["temp"] = learnTemp;

    // ALSO KEEP A COPY ON THE NODE, do not only upload it to the cloud. Without
    // this line, a user who has just taught a code on the panel sees that same
    // panel still report "CHƯA HỌC MÃ" with the mode button dimmed -- they have to
    // wait for the server's comfort loop to happen to send down a command for that
    // exact combination before it becomes pressable. On a wall-mounted control
    // panel, that unexplained wait reads as a fault.
    //
    // Done BEFORE publishing, deliberately: learning and local control keep working
    // with no network. The backend will receive the code on a later learn, while
    // the air conditioner works immediately.
    const int t = aliasTemp(learnLabel, learnTemp);
    if (IrStore::saveLearned(learnLabel, t, raw, len)) {
      aliasDirty = true;      // the "has a code" table changed -> the screen must recompute
      Serial.printf("[learn] saved to NVS: %s%s%d - the panel can use it right away\n",
                    learnLabel, t >= 0 ? " " : "", t >= 0 ? t : 0);
    } else {
      Serial.println("[learn] COULD NOT save to NVS - the panel will still report no code learned");
    }
  } else {
    doc["action"] = learnLabel;

    // ALSO KEEP A COPY ON THE NODE for the buttons the panel can transmit itself --
    // the same reason and the same pattern as the mode branch above, and here it
    // matters even more: discrete button codes have NO ir_code_id, so the "backend
    // sends a command with ir_raw and the node stores it" path does not exist for
    // them. Without storing here, the only remaining way for the panel to get the
    // code is the user pressing REQUEST CODES, and they have no reason to think of
    // that right after having just learned it.
    //
    // temp = -1: discrete buttons have no temperature, per IrStore::saveAlias's
    // convention.
    if (isPanelAction(learnLabel)) {
      if (IrStore::saveLearned(learnLabel, -1, raw, len)) {
        aliasDirty = true;
        Serial.printf("[learn] saved discrete button %s to NVS - the panel can use it right away\n",
                      learnLabel);
      } else {
        Serial.printf("[learn] COULD NOT save %s to NVS - the panel will still report no code learned\n",
                      learnLabel);
      }
    }
  }

  String out;
  serializeJson(doc, out);
  bool ok = mqtt.publish(tLearn.c_str(), out.c_str(), false);
  Serial.printf("[learn] \"%s\" %u transitions (%u bytes) -> %s\n",
                learnLabel, len, (unsigned)out.length(),
                ok ? "uploaded to cloud" : "SEND ERROR (payload larger than the MQTT buffer?)");
}

// ---------------------------------------------------------------------------
//  Receiving commands
// ---------------------------------------------------------------------------
static char lastReqId[24] = "";

/// A "store only, do not transmit" packet -- the server pushing the whole code
/// store back after the user pressed REQUEST CODES on the panel
/// (ir_service.push_all_codes).
///
/// IT MUST BE SEPARATE FROM takeCommand(): a normal cmd packet carrying `ir_raw`
/// means "transmit this frame at the air conditioner now". Going through that path
/// would turn one ~18-code sync into 18 consecutive remote presses -- mode and
/// temperature jumping wildly and settling on whichever row happened to be last in
/// the list.
///
/// It also does NOT ack: there is no req_id, and no `commands` row on the server
/// waiting to be marked.
static void storeCode(JsonDocument &doc) {
  const char *codeId = doc["ir_code_id"];
  const char *action = doc["action"];
  const char *mode   = doc["mode"] | "";
  const int   setp   = doc["setpoint"] | -1;
  JsonArray   irRaw  = doc["ir_raw"];

  if (irRaw.isNull()) {
    Serial.println("[sync] store_only packet has no ir_raw - discarded");
    return;
  }
  if (irRaw.size() > IrIo::RAW_MAX) {
    Serial.printf("[sync] code is %u transitions > the %u limit - discarded\n",
                  (unsigned)irRaw.size(), IrIo::RAW_MAX);
    return;
  }

  uint16_t n = 0;
  for (JsonVariant v : irRaw) irBuf[n++] = (uint16_t)v.as<uint32_t>();

  // --- DISCRETE BUTTONS (fan level, humidifier) ---
  //
  // They differ from the (mode, temperature) matrix in exactly one place, and that
  // place governs this whole branch: the `ir_action_codes` table is keyed by
  // (org, action) rather than generating a UUID, so the packet carries no
  // `ir_code_id`. With no id there is nothing to verify against, and there is no
  // `need_raw` path to re-request it -- this is the ONLY time the panel receives
  // this code apart from learning it itself.
  //
  // Use saveLearned() (with the temporary id "local-FAN_60") rather than
  // save()+saveAlias(): the temporary id is exactly what IrStore invented to serve
  // codes that have no backend UUID.
  if (action != nullptr && action[0] != '\0') {
    if (!isPanelAction(action)) {
      // The backend pushes the whole discrete-button store; the panel only keeps
      // the buttons it has somewhere to press. Skipping silently is CORRECT here --
      // it is not an error, and printing a line per skipped button would double the
      // length of the resync log over something entirely routine.
      return;
    }
    if (!IrStore::saveLearned(action, -1, irBuf, n)) {
      Serial.printf("[sync] could not save discrete button %s - NVS full?\n", action);
      Ui::reply("NVS ĐẦY — KHÔNG LƯU ĐƯỢC MÃ");
      return;
    }
    aliasDirty = true;
    Serial.printf("[sync] received discrete button %s (%u transitions)\n", action, n);
    return;
  }

  // --- the (mode, temperature) matrix ---
  if (codeId == nullptr || mode[0] == '\0') {
    Serial.println("[sync] store_only packet is missing fields - discarded");
    return;
  }

  if (!IrStore::save(codeId, irBuf, n)) {
    Serial.printf("[sync] could not save %s - NVS full?\n", codeId);
    Ui::reply("NVS ĐẦY — KHÔNG LƯU ĐƯỢC MÃ");
    return;
  }
  IrStore::saveAlias(mode, aliasTemp(mode, setp), codeId);
  aliasDirty = true;
  Serial.printf("[sync] received %s %d (%u transitions)\n", mode, setp, n);
}

static void takeCommand(JsonDocument &doc) {
  // Before ANYTHING else: a sync packet is not a control command, it does not go
  // through pending/ack/deduplication and must never transmit IR.
  if (doc["store_only"] | false) {
    storeCode(doc);
    return;
  }

  copyStr(pending.reqId, sizeof(pending.reqId), doc["req_id"]);
  copyStr(pending.mode,  sizeof(pending.mode),  doc["mode"]);
  pending.setpoint = doc["setpoint"] | -1;
  pending.hasFrame = false;
  pending.frameLen = 0;
  pending.needAck  = false;
  pending.needRawId[0] = '\0';

  const char *reason = doc["reason"] | "";

  // Prepare the on-screen log entry. `reason` arrives with EVERY command but used
  // to be printed to serial and thrown away -- meaning that finding out what the
  // server had just commanded required plugging a USB-TTL into a board mounted on
  // the wall. The four branches below fill in `result` and send it to the UI task
  // (queue push only, which is safe inside this callback).
  Ui::CmdLog logEntry{};
  copyStr(logEntry.mode,   sizeof(logEntry.mode),   pending.mode);
  copyStr(logEntry.reason, sizeof(logEntry.reason), reason);
  logEntry.setpoint = pending.setpoint;

  // MQTT QoS1 allows the broker to RESEND the same command if the ack did not
  // arrive in time. Retransmitting an IR frame = pressing the remote twice; with
  // cycle buttons (fan speed, swing) the second press steps to a different level,
  // so a repeat is NOT harmless. Deduplicate by req_id, but still re-ack, since it
  // may well be the old ack that was lost.
  if (pending.reqId[0] && strcmp(pending.reqId, lastReqId) == 0) {
    Serial.printf("[cmd] %s already executed - skipping the duplicate, re-acking\n", pending.reqId);
    pending.needAck = true;
    logEntry.result = Ui::CmdLog::DUPLICATE;
    Ui::logCommand(logEntry);
    return;
  }

  const char *codeId = doc["ir_code_id"];   // JSON null -> nullptr
  JsonArray irRaw = doc["ir_raw"];

  if (!irRaw.isNull()) {
    if (irRaw.size() > IrIo::RAW_MAX) {
      // Truncating and transmitting makes the air conditioner receive a COMPLETELY
      // DIFFERENT command, not an incomplete one. Better to do nothing and let the
      // log say so plainly.
      Serial.printf("[cmd] ir_raw is %u transitions > the %u limit - NOT transmitting (a truncated frame is a wrong command)\n",
                    (unsigned)irRaw.size(), IrIo::RAW_MAX);
      return;
    }
    for (JsonVariant v : irRaw) irBuf[pending.frameLen++] = (uint16_t)v.as<uint32_t>();

    // Only (mode,temp) codes have an ir_code_id to cache. Discrete buttons have no
    // id and the backend always includes ir_raw, so they do not need storing.
    if (codeId != nullptr && pending.frameLen > 0) {
      if (IrStore::save(codeId, irBuf, pending.frameLen)) {
        Serial.printf("[cmd] saved code %s to NVS (%u transitions)\n", codeId, pending.frameLen);
        // Also write the alias (mode, temp) -> id. This is the ONLY thing that lets
        // the touch screen look up an IR frame itself when the user presses locally:
        // the main store is keyed by the server's UUID and the node has no reverse
        // lookup. See ir-store.h.
        if (pending.mode[0]) {
          IrStore::saveAlias(pending.mode, aliasTemp(pending.mode, pending.setpoint), codeId);
          aliasDirty = true;   // the "has a code" table changed -> the screen must recompute (see pushUiModel)
        }
      }
    }
  } else if (codeId != nullptr) {
    pending.frameLen = IrStore::load(codeId, irBuf, IrIo::RAW_MAX);
    if (pending.frameLen == 0) {
      // The backend believed the node still held this code and deliberately did NOT
      // include ir_raw (command_publisher._resolve_ir_raw + redis_ir_cache). If the
      // node has just been erase_flash'ed or swapped, the two sides have diverged
      // with no channel to re-request it. DELIBERATELY no ack: a command that was
      // not executed must not be reported as done, so commands.acked_at on the web
      // reflects the truth.
      Serial.printf("[cmd] ir_code_id=%s is not in NVS and the server sent no ir_raw\n", codeId);
      // Re-request it ourselves rather than waiting for somebody to clear Redis by
      // hand. loop() does the publishing (the callback must not touch mqtt.publish
      // -- see the top of the pending struct).
      copyStr(pending.needRawId, sizeof(pending.needRawId), codeId);
      Serial.println("      -> asking the server to resend the timing array");
      logEntry.result = Ui::CmdLog::NEED_RAW;
      Ui::logCommand(logEntry);
      return;
    }
  } else {
    // No code learned for this (mode, setpoint) -- command_publisher has already
    // logged a "No learned IR code" warning on the server side.
    Serial.printf("[cmd] %s %s %d: neither ir_raw nor ir_code_id - this code has not been learned\n",
                  pending.reqId, pending.mode, pending.setpoint);
    logEntry.result = Ui::CmdLog::NO_CODE;
    Ui::logCommand(logEntry);
    return;
  }

  Serial.printf("[cmd] %s -> %s %d (%s) - %u transitions, ready to transmit\n",
                pending.reqId, pending.mode, pending.setpoint, reason, pending.frameLen);
  pending.hasFrame = true;
  pending.needAck  = true;
  // Record SENT right here even though the transmission happens in loop():
  // hasFrame=true is a firm commitment -- loop() has no branch that skips it.
  // Waiting until after transmission would mean threading logEntry through the
  // pending struct, adding another piece of state just to restate something already
  // known.
  logEntry.result = Ui::CmdLog::SENT;
  Ui::logCommand(logEntry);
  // The server has just issued a command -> it has taken control back, and the
  // on-screen badge returns to "TU DONG". This is exactly what the UI warned about
  // when the user overrode.
  lastCmdMs = millis();

  // Who is in control, read from `reason` rather than assumed to be the server.
  //
  //   "auto:COOL@25"     the server decided        -> the server is in control
  //   "manual override"  the user overrode (app)   -> a PERSON is in control
  //   "action:FAN_SPEED" a discrete button in the app -> UNCHANGED (see below)
  //
  // Discrete buttons deliberately do not touch this flag: they carry no
  // (mode, setpoint) and open no override window on the server, so inferring
  // control authority from them would be fabrication. Pressing "fan speed" while
  // the server is running automatically leaves the server STILL running
  // automatically -- leaving the flag alone is the correct answer, not a missing
  // branch.
  if (strncmp(reason, "auto:", 5) == 0)              overrideLocal = false;
  else if (strcmp(reason, "manual override") == 0)   overrideLocal = true;
}

static void onMessage(char *topic, byte *payload, unsigned int len) {
  // Record BEFORE parsing the JSON: a malformed packet is still a packet that
  // arrived, and the error branch below only prints the error name rather than the
  // content -- at exactly the moment the content is most worth seeing.
  SerialTrace::mqttIn(topic, payload, len);

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) {
    Serial.printf("[cmd] malformed JSON (%s) - discarded\n", err.c_str());
    return;
  }

  // The single cmd topic carries TWO completely different payload shapes:
  //   {"learn":"COOL 25"}                  -> enter learn mode (ir_service.py)
  //   {"req_id","mode","setpoint",...}     -> transmit a frame (command_publisher.py)
  const char *learn = doc["learn"];
  if (learn != nullptr) {
    startLearn(learn);
    return;
  }
  takeCommand(doc);
}

/// Ack + state sync. retain=true: state_handler matches the ack against the
/// commands row, and the web/app show the latest mode/setpoint immediately on
/// opening rather than waiting for the next command.
static void publishState() {
  JsonDocument doc;
  if (pending.reqId[0]) doc["ack"] = pending.reqId;
  if (pending.mode[0])  doc["mode"] = pending.mode;
  if (pending.setpoint >= 0) doc["setpoint"] = pending.setpoint;
  // THE MAC TRAVELS HERE BECAUSE THERE IS NOWHERE ELSE LEFT. Every other node
  // declares its MAC in the telemetry packet, but the gateway no longer has a
  // sensor so it no longer publishes telemetry. Without this line the "Nạp
  // firmware" page shows "—" for the very node the installer is standing in front
  // of. state_handler.py reads this field.
  doc["mac"] = WiFi.macAddress();
  char buf[160];
  size_t n = serializeJson(doc, buf);
  bool ok = mqtt.publish(tState.c_str(), (const uint8_t *)buf, n, true);
  SerialTrace::mqttOut(tState.c_str(), (const uint8_t *)buf, n, ok);
  Serial.printf("[state] ack=%s mode=%s setpoint=%d -> %s\n",
                pending.reqId, pending.mode, pending.setpoint, ok ? "sent" : "SEND ERROR");
}

/// Ask the server to resend the timing array for an ir_code_id the node no longer
/// holds.
///
/// WHY IT IS NEEDED: the backend only attaches `ir_raw` on the FIRST use of each
/// code, after which it trusts the node still holds it in NVS
/// (`command_publisher._resolve_ir_raw` + `bl:ircache:{id}`). The two sides diverge
/// whenever NVS is lost while the DB is not: `erase_flash`, a board swap reusing
/// the DEVICE_UUID, or the user having just pressed DELETE in the Settings screen.
/// This branch used to print a log line and then sit there -- the air conditioner
/// mute while the server log looked clean, the kind of failure that takes longest
/// to find.
///
/// NOT RETAINED. This is a one-off request, not a state. Retained, the broker would
/// replay it after every reconnect and the backend would clear its cache and resend
/// a several-KB array for no reason, forever.
static void publishNeedRaw(const char *codeId) {
  JsonDocument doc;
  doc["need_raw"] = codeId;
  // Include the req_id so the server can identify exactly which command failed.
  // That command is DELIBERATELY not acked (see onCmdPacket) so `commands.ack_ts`
  // stays empty -- this is what explains why.
  if (pending.reqId[0]) doc["req_id"] = pending.reqId;
  char buf[96];
  size_t n = serializeJson(doc, buf);
  bool ok = mqtt.publish(tState.c_str(), (const uint8_t *)buf, n, false);
  Serial.printf("[cmd] re-requesting code %s -> %s\n", codeId, ok ? "sent" : "SEND ERROR");
}

// ---------------------------------------------------------------------------
//  Connectivity
// ---------------------------------------------------------------------------
// Scan and print the networks that ARE VISIBLE when the configured one cannot be
// joined.
//
// The old loop just printed dots forever. A dot cannot distinguish three completely
// different causes whose remedies are completely different:
//   - SSID not found   -> wrong name, or the router is on 5 GHz (the ESP32 only
//                         does 2.4 GHz -- the most common case in a home install)
//   - found but weak   -> the node is in the wrong place
//   - found and strong -> wrong password
// An installer standing at the distribution board needs to know IMMEDIATELY whether
// to change the network name, move the node, or retype the password.
// Not put in config.h: this is a firmware constant, not something that changes per
// installation. 20 s is enough for a slow router to finish handshaking while still
// not making anyone wait too long to see the reason.
constexpr uint32_t WIFI_ATTEMPT_MS = 20000UL;

static void wifiDiagnose() {
  Serial.printf("\n  Could not join \"%s\". Scanning to see what is around:\n", WIFI_SSID);
  // Fully abort the connection attempt in progress: scanning mid-handshake gives
  // incomplete results.
  WiFi.disconnect(true);
  delay(100);

  const int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("  (no 2.4 GHz network found at all - check the antenna or where the node is mounted)");
    return;
  }
  bool found = false;
  for (int i = 0; i < n; i++) {
    const bool me = (WiFi.SSID(i) == String(WIFI_SSID));
    found = found || me;
    Serial.printf("  %-22s channel %2d  %4d dBm  %-11s%s\n",
                  WiFi.SSID(i).c_str(), WiFi.channel(i), (int)WiFi.RSSI(i),
                  WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "password",
                  me ? "  <== LOOKING FOR THIS ONE" : "");
  }
  if (!found) {
    Serial.printf("  => \"%s\" IS NOT in the list. The ESP32 can only see 2.4 GHz;\n"
                  "     if the router gives its 5 GHz band a separate name, put the\n"
                  "     2.4 GHz band's name into WIFI_SSID in src/config.h.\n", WIFI_SSID);
  } else {
    Serial.println("  => The network is visible but cannot be joined: almost certainly the WRONG PASSWORD.");
  }
  WiFi.scanDelete();
}

/// How many WiFi attempts to make AT BOOT before carrying on without a network.
///
/// IT IS BOUNDED, IT DOES NOT WAIT FOREVER. An earlier version waited indefinitely
/// here, reasoning that "without a network the node cannot do anything" -- that
/// reasoning is WRONG, and it was the most serious bug this file ever had (see
/// serviceNetwork below). Without a network the node can still receive ESP-NOW,
/// still talk to the UNO Q over UART, and still transmit infrared -- i.e. it can
/// still control the air conditioner. That is precisely what the edge layer exists
/// to do.
static const uint8_t WIFI_BOOT_ATTEMPTS = 3;

/// Three things that MUST happen every time WiFi comes up -- missing any one of
/// them fails silently.
///
/// CALLED FROM BOTH ENTRY POINTS (boot and background reconnect). This block used
/// to live entirely inside connectWifi(), meaning it only ran if the board joined
/// the network AT BOOT. A panel powered on before the router came up would then be
/// reconnected by `serviceNetwork()` using a bare `WiFi.begin()` that never touched
/// it -- so the board ran the whole session with modem sleep ON, losing ~60% of
/// ESP-NOW packets, while every status light stayed green.
static void onWifiUp() {
  // 1. DISABLE WiFi power saving. MANDATORY on the master node.
  //
  // By default the ESP32 enables modem sleep once joined: the radio sleeps between
  // beacons. Ordinary WiFi traffic is fine because the router BUFFERS for us while
  // we sleep, but an ESP-NOW packet from a slave has NOBODY buffering it -- arriving
  // while the radio is asleep it is simply lost, and since broadcast has no ACK the
  // slave still believes it sent successfully.
  //
  // THE COST HAS BEEN MEASURED; do not re-enable it for any reason:
  //     modem sleep ON (the BLE era):  0.31 packets/second
  //     modem sleep OFF:               0.80 packets/second  <- exactly 4 nodes x 5 seconds
  // The outdoor node was dropping ~50% and flickering ONLINE/OFFLINE constantly.
  //
  // A consequence: NEVER re-enable Bluetooth on this board. The chip refuses to run
  // WiFi + BT with modem sleep disabled -- it abort()s rather than merely degrading
  // (`Should enable WiFi modem sleep when both WiFi and Bluetooth are enabled`), and
  // the board goes into an endless boot loop every ~6 seconds.
  WiFi.setSleep(false);

  // 2. Remember the router's channel. This is the number the panel returns to when
  //    the network goes down, and the only thing that lets it meet the nodes again
  //    once the router is completely off.
  EspNowChannel::note((uint8_t)WiFi.channel());

  // 3. Stop pinning -- from now on the router holds the channel for us, and the
  //    nodes follow the router too.
  EspNowChannel::release();

  Serial.printf(" OK  IP=%s  RSSI=%d dBm  channel=%d\n",
                WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(), WiFi.channel());
}

static void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);

  // Do not write the WiFi configuration to NVS on every begin(): the retry loop can
  // run hundreds of times during an overnight outage, and flash has finite write
  // endurance.
  WiFi.persistent(false);

  // AUTO-RECONNECT: ON during boot, OFF immediately after the loop below.
  //
  // THE COST OF TURNING IT OFF TOO EARLY HAS BEEN MEASURED: disabled before the
  // loop, each 20-second attempt becomes 20 seconds of DOING NOTHING -- the stack
  // never retries after a failed handshake, so the board has to wait out the whole
  // attempt before calling begin() again. On real hardware at RSSI -36 dBm (very
  // strong), boot took until "attempt 3/3" ~= 40 seconds to join.
  //
  // Letting the stack handle it here is CORRECT: ESP-NOW is not up yet
  // (EspNowRelay::begin() comes later), so any channel hopping cuts off nobody's
  // reception -- which is exactly what stops being true once setup() finishes.
  WiFi.setAutoReconnect(true);

  for (uint8_t attempt = 1; attempt <= WIFI_BOOT_ATTEMPTS; attempt++) {
    Serial.printf("WiFi -> \"%s\" (attempt %u/%u) ", WIFI_SSID, attempt, WIFI_BOOT_ATTEMPTS);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    const uint32_t deadline = millis() + WIFI_ATTEMPT_MS;
    while (WiFi.status() != WL_CONNECTED && (int32_t)(millis() - deadline) < 0) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) break;
    // Scanning is ONLY PERMITTED HERE, while ESP-NOW is not up. scanNetworks() hops
    // across every channel and leaves the radio on the last one -- calling it after
    // ESP-NOW is running pulls the gateway off the nodes' channel, and not one log
    // line reports it.
    if (attempt < WIFI_BOOT_ATTEMPTS) wifiDiagnose();
  }

  // FROM HERE ON, THE RADIO BELONGS TO serviceNetwork(). Disable auto-reconnect.
  //
  // Not to save anything -- to TAKE OWNERSHIP OF RADIO SCHEDULING. The begin() the
  // stack calls itself after every drop declares NO channel, so it scans across all
  // of them: right after serviceNetwork() has pinned the radio to the nodes'
  // channel, a background auto-reconnect drags it away -- with none of our own log
  // lines reporting it, because we did not make the call. Disabled, every departure
  // from the channel is decided in exactly one place.
  WiFi.setAutoReconnect(false);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("\nCOULD NOT JOIN WiFi after %u attempts - CARRYING ON WITHOUT A NETWORK.\n"
                  "  ESP-NOW, the UART to the UNO Q and infrared all still work; only the\n"
                  "  path to the cloud is lost. serviceNetwork() will keep retrying in the\n"
                  "  background from loop().\n",
                  WIFI_BOOT_ATTEMPTS);
    // PARK THE RADIO BEFORE MOVING ON. The loop above (and wifiDiagnose() in
    // between) leaves the radio on whichever channel it last probed -- entirely at
    // random. Without this line, the EspNowRelay::begin() immediately below builds a
    // receiver on a meaningless channel, and the panel is deaf to all 5 nodes even
    // though ESP-NOW initialised "successfully".
    //
    // disconnect(false) FIRST: the last begin() is still probing, and while probing
    // the WiFi stack silently overwrites any channel we set. `false` keeps the
    // station interface UP.
    WiFi.disconnect(false);
    EspNowChannel::park();
    return;
  }

  onWifiUp();
}

/// ONE MQTT connection attempt. Returns true if it succeeded.
static bool mqttTryConnect() {
  String cid = String("breezelink_") + DEVICE_UUID;  // = mqtt_naming.client_id()
  // LWT (will): topic=status, qos=1, retain=true, payload="offline".
  if (mqtt.connect(cid.c_str(), MQTT_USERNAME, MQTT_PASSWORD,
                   tStatus.c_str(), 1, true, "offline")) {
    Serial.println("MQTT ... connected");
    mqtt.publish(tStatus.c_str(), "online", true);  // retained -> the web shows "Trực tuyến"
    // QoS1: an air conditioner control command must never be dropped silently.
    mqtt.subscribe(tCmd.c_str(), 1);
    return true;
  }
  // rc=-2 network error; rc=4 wrong user/pass; rc=5 not authorised on the broker.
  Serial.printf("MQTT ... failed rc=%d\n", mqtt.state());
  return false;
}

/// How long WiFi has to be down CONTINUOUSLY before it counts as really down (ms).
///
/// WITHOUT THIS, THE PANEL DISCONNECTS ITS OWN NETWORK -- a bug measured on real
/// hardware: RSSI -39 dBm (very strong) and the log still dropping/reconnecting
/// constantly.
///
/// Why: loop() runs hundreds of times a second, and `WiFi.status()` occasionally
/// returns a momentary NOT-CONNECTED value (a DHCP renewal, an internal event)
/// while the link is perfectly healthy. Reacting to ONE such reading means calling
/// `WiFi.begin()`, and begin() GENUINELY tears down a healthy link to rebuild it
/// from scratch. A blink turns into a real outage, and then repeats.
///
/// 5 seconds: longer than any blink, far shorter than the time it takes a user to
/// notice the network is gone.
static const uint32_t WIFI_DOWN_DEBOUNCE_MS = 5000UL;

/// Retry interval once it is really down (ms). FIXED, no longer backing off.
///
/// An earlier version backed off 30s -> 60s -> 300s to "disturb the radio less".
/// That was wrong here: what genuinely pulls the radio off the nodes' channel is
/// SCANNING, and an attempt on the remembered channel does not scan -- it goes
/// straight to the channel the nodes are transmitting on, at almost no cost.
/// Backing those cheap attempts off to 5 minutes buys exactly one thing: the router
/// comes back and the panel stays silent for another 5 minutes, and the user reads
/// that as "it cannot reconnect by itself".
///
/// Now: retry steadily every 30 seconds, and make the EXPENSIVE part (scanning) the
/// part that is made rare.
static const uint32_t WIFI_RETRY_MS = 30000UL;

/// How many attempts before allowing ONE full scan (in case the router really has
/// changed channel). 6 x 30s = one scan every ~3 minutes.
static const uint8_t WIFI_SCAN_EVERY = 6;

/// The same idea for MQTT. Kept separate because the two fail independently: good
/// WiFi with a dead broker is routine, and in that case there is no reason to touch
/// the radio at all.
static uint32_t mqttRetryDelayMs(uint8_t misses) {
  if (misses < 3) return 15000UL;
  if (misses < 8) return 60000UL;
  return 300000UL;
}

/// How long ONE WiFi attempt gets before giving up and returning to the parked
/// channel (ms).
static const uint32_t WIFI_ATTEMPT_WINDOW_MS = 12000UL;


/// Keep WiFi/MQTT alive WITHOUT BLOCKING loop(). Call every iteration.
///
/// THIS IS WHERE THIS FILE'S MOST SERIOUS BUG WAS FIXED.
///
/// An earlier version called connectWifi() + connectMqtt() directly from loop(),
/// and both are INFINITE WAIT loops. The consequence: losing WiFi meant loop()
/// never came back --
///
///   ESP-NOW stops receiving  -> the 4 room corners and the outdoor node vanish
///   UART stops pushing       -> the UNO Q has no snapshots left to compute from
///   nobody reads UNO Q commands -> the edge cannot drive the air conditioner
///   IR stops executing       -> including commands already in the queue
///
/// In other words, losing the network KILLED the very fallback layer built to
/// survive that failure. The whole edge-ai architecture rests on the assumption
/// "the gateway keeps running when the cloud is gone", and that assumption was
/// false right inside the main loop.
///
/// Now: retry on a schedule and never wait.
///
/// THE SCANNING RULE HAS CHANGED -- read carefully, the previous version said the
/// opposite.
///
/// Old: "NEVER SCAN", because scanNetworks() hops across every channel and leaves
/// the radio on the last one, i.e. cutting off our own ESP-NOW reception. The
/// diagnosis was right, the conclusion incomplete: it banned EXPLICIT scan calls,
/// while a `WiFi.begin()` WITH NO CHANNEL scans exactly the same way -- and the
/// retry loop called it 4 times a minute. That ban protected nothing at all; the
/// radio still wandered for the whole outage.
///
/// Now, three rules replace one ineffective ban:
///   1. Attempt with `WiFi.begin(ssid, pass, THE REMEMBERED CHANNEL)` -- given a
///      channel the station goes straight there without scanning, so the attempt
///      itself sits ON the nodes' channel.
///   2. Between attempts, park the radio on the remembered channel
///      (EspNowChannel::park()).
///   3. Scanning is allowed, but rarely and DELIBERATELY -- and every scan must
///      re-lock the channel immediately afterwards (EspNowChannel::rescan handles
///      this).
static void serviceNetwork() {
  static uint32_t lastWifiTry = 0, attemptStartMs = 0, lastRescanMs = 0, lastMqttTry = 0;
  static uint32_t downSinceMs = 0;   // 0 = the network is up
  static uint8_t  wifiMisses = 0, mqttMisses = 0;
  static bool     attempting = false;
  const uint32_t  now = millis();

  // ==========================================================================
  //  WiFi IS DOWN -- priority one is KEEPING THE RADIO ON THE RIGHT CHANNEL, not
  //  reconnecting quickly
  // ==========================================================================
  if (WiFi.status() != WL_CONNECTED) {
    if (downSinceMs == 0) downSinceMs = now;

    // WAIT AND SEE WHETHER IT IS REALLY DOWN before touching anything. See
    // WIFI_DOWN_DEBOUNCE_MS: reacting to one momentary reading means tearing down a
    // healthy link with our own hands. Skip the debounce WHILE attempting -- at that
    // point "not connected" is the expected state, not news.
    if (!attempting && now - downSinceMs < WIFI_DOWN_DEBOUNCE_MS) return;

    // --- inside an attempt ---
    if (attempting) {
      // WAIT OUT THE WHOLE WINDOW; DO NOT GIVE UP EARLY BASED ON WiFi.status().
      //
      // An earlier version had a "hopeless" branch that gave up as soon as the
      // status was WL_NO_SSID_AVAIL / WL_CONNECT_FAILED. It was reading STALE
      // STATE: right after WiFi.begin(), the WiFi task has not yet started the new
      // attempt so the status still holds the PREVIOUS result. The next loop
      // iteration (~1ms later) read that stale value, concluded "hopeless", and
      // cancelled the attempt it had just started before it could run.
      //
      // The consequence: every attempt after the first failure died within 1ms, the
      // miss counter shot up, and the panel could never reconnect -- exactly the
      // reported bug.
      if (now - attemptStartMs < WIFI_ATTEMPT_WINDOW_MS) return;

      attempting = false;
      if (wifiMisses < 255) wifiMisses++;
      lastWifiTry = now;

      // FULLY STOP probing BEFORE re-parking the channel -- this order exactly, it
      // cannot be swapped. While probing, the WiFi stack drives the channel itself
      // and any channel we set is silently overwritten. `false` = keep the station
      // interface UP, so ESP-NOW keeps receiving.
      WiFi.disconnect(false);
      EspNowChannel::park();
      return;
    }

    // --- parked on the channel, waiting for the next attempt ---
    //
    // Guard the channel every iteration. Placed HERE rather than in the branch
    // above: while `attempting`, the WiFi stack is deliberately hopping channels to
    // find the SSID, and pulling it back makes the two fight so no connection ever
    // completes.
    EspNowChannel::hold();

    // Re-scan for the router's channel periodically, at the same cadence as the
    // nodes. Only needed for the "router visible but cannot join" case (password
    // changed, MAC filtering): there the attempt loop below never succeeds, so there
    // is no other way to learn a new channel.
    if (now - lastRescanMs >= EspNowChannel::RESCAN_INTERVAL_MS) {
      lastRescanMs = now;
      EspNowChannel::rescan(WIFI_SSID);
      return;   // the scan just ate ~1 second; leave the connection attempt to the next round
    }

    if (now - lastWifiTry < WIFI_RETRY_MS) return;
    lastWifiTry    = now;
    attemptStartMs = now;
    attempting     = true;

    // Scanning is the EXPENSIVE part (it drags the radio off the nodes' channel) so
    // it is rare; attempting on the remembered channel is the CHEAP part, so it is
    // the default.
    if (wifiMisses == 0 || (wifiMisses % WIFI_SCAN_EVERY) != 0) {
      // DECLARE THE CHANNEL IN begin() -- this is the trick that makes almost this
      // whole problem disappear. Given a channel the station goes STRAIGHT there
      // with NO scan, so the reconnection attempt is itself sitting on the nodes'
      // channel: the panel probes for the network and receives ESP-NOW normally at
      // the same time, with no trade-off between the two.
      const uint8_t ch = EspNowChannel::last();
      Serial.printf("[net] WiFi down - retrying on channel %u without scanning (attempt %u)\n",
                    ch, (unsigned)(wifiMisses + 1));
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD, ch);
    } else {
      // Repeated failures on the old channel -> the router may really have changed
      // channel. Scan after all, and accept losing a few ESP-NOW packets in this
      // window.
      Serial.printf("[net] %u failures on channel %u - retrying WITH A SCAN\n",
                    (unsigned)wifiMisses, EspNowChannel::last());
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    return;   // without WiFi there cannot be MQTT either
  }

  // ==========================================================================
  //  WiFi IS UP
  // ==========================================================================
  // Clear the debounce timestamp IMMEDIATELY, before any branch: a blink that has
  // passed must leave no trace to accumulate into the next one.
  downSinceMs = 0;

  // Just came back after an outage: remember the channel, disable modem sleep, stop
  // pinning.
  if (attempting || EspNowChannel::pinned()) {
    attempting   = false;
    wifiMisses   = 0;
    lastRescanMs = now;   // restart the count here, so the next drop does not scan instantly
    onWifiUp();
  }

  if (!mqtt.connected()) {
    if (now - lastMqttTry >= mqttRetryDelayMs(mqttMisses)) {
      const bool ok = mqttTryConnect();   // ONE attempt; on failure the next round retries
      // TIMESTAMP AFTER THE FUNCTION RETURNS, not before -- and this is a real bug
      // that was fixed, not a matter of style. mqtt.connect() BLOCKS until the
      // socket timeout expires; timestamping before the call lets the blocking time
      // consume the whole interval, so on the next round `now - lastMqttTry` has
      // already passed the threshold and it retries IMMEDIATELY. The spaced-out
      // cadence turns into continuous blocking, and loop() never reaches the point
      // where it collects user commands -- the wall panel stops responding to
      // presses.
      lastMqttTry = millis();
      if (ok) mqttMisses = 0;
      else if (mqttMisses < 255) mqttMisses++;
    }
    return;
  }

  // --- The "gateway is alive" heartbeat -------------------------------------
  //  `status` USED TO BE SENT ONLY ONCE, in mqttTryConnect(). The backend now
  //  considers a node online while `last_seen_at` is fresh
  //  (services/device_presence.py), and that column is ONLY written when a message
  //  arrives on the `status` topic. So a perfectly healthy gateway was still
  //  reported offline after PRESENCE_TTL -- and this really happened: the board ran
  //  for 9 minutes with both WiFi and MQTT connected while the web showed
  //  "Ngoại tuyến".
  //
  //  The gateway MUST SPEAK FOR ITSELF rather than being inferred from a slave's
  //  telemetry: inferring it indirectly makes the presence relationship circular (a
  //  slave is alive because the gateway publishes for it, the gateway is alive
  //  because the slave sends to it), and once ESP-NOW dies completely a healthy
  //  gateway is still reported offline.
  //
  //  SHARES STATUS_REFRESH_MS with the slaves so that ONE constant governs the
  //  backend threshold -- change the cadence here and you must change PRESENCE_TTL,
  //  and the comments at both ends point at each other.
  //
  //  Unsigned subtraction, so a millis() wrap is still correct -- the same pattern
  //  as slave-watch.cpp.
  static uint32_t lastOwnStatusMs = 0;
  if (now - lastOwnStatusMs >= SlaveWatch::STATUS_REFRESH_MS) {
    lastOwnStatusMs = now;
    mqtt.publish(tStatus.c_str(), "online", true);
  }

  mqtt.loop();
}

// ---------------------------------------------------------------------------
//  Humidifier
// ---------------------------------------------------------------------------
/// Transmit the IR frame for direction [on]. Returns false when NO CODE HAS BEEN
/// LEARNED for that direction -- HumidifierControl relies on the return value so it
/// does not wrongly believe the unit has changed.
///
/// TOGGLE REMOTES ARE HANDLED HERE, and only here. Many humidifiers have only ONE
/// power button: pressed while off it turns on, pressed while running it turns off.
/// Such a household can only learn the ON slot, so the OFF direction falls back to
/// that same frame.
///
/// THE TEST BOARD USED A COMPILE FLAG (`DIFFUSER_IR_TOGGLE`) for this, and that
/// flag was the kind of thing that could only be wrong out in the field: declared 0
/// while both slots had learned the toggle button, the "OFF" code also inverted the
/// state, and the board turned the unit ON at the exact moment it believed it was
/// turning it off. Here there is nothing to declare -- "does the OFF slot have a
/// code" is a fact readable from NVS, and the humidifier screen states it plainly.
///
/// Defined BEFORE setup() because setup() passes it to
/// HumidifierControl::begin().
static bool humidifierEmit(bool on) {
  uint16_t n = IrStore::loadAlias(AcActions::humidWire(on), -1, irBuf, IrIo::RAW_MAX);
  bool viaToggle = false;
  if (n == 0 && !on) {
    n = IrStore::loadAlias(AcActions::humidWire(true), -1, irBuf, IrIo::RAW_MAX);
    viaToggle = (n > 0);
  }
  if (n == 0) return false;

  IrIo::blast(irBuf, n);
  Serial.printf("[humid] transmitted %u transitions -> %s%s\n", n, on ? "ON" : "OFF",
                viaToggle ? " (reused the ON slot: toggle remote)" : "");
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n== BreezeLink - QR Box Advance Touch - INDOOR GATEWAY (BLE + ESP-NOW + IR) ==");

  // Bring the display up BEFORE WiFi, deliberately: the UI task runs on core 0 so
  // it keeps drawing normally throughout the tens of seconds that
  // connectWifi()/connectMqtt() block core 1. The installer sees "MAT KET NOI"
  // blinking -- i.e. the node is alive and probing for the network -- rather than a
  // black screen saying nothing.
  //
  // Ui::begin() is also where EN_LEVEL_SHIFT is driven HIGH, so it has to run
  // BEFORE IrIo::begin() if the IR pins go through the level shifter.
  Ui::begin();
  IrIo::begin(IR_TX_PIN, IR_RX_PIN);
  if (!IrStore::begin()) {
    // Do not block startup: the node still works, it just means every command has
    // to include ir_raw.
    Serial.println("NVS error - IR codes cannot be cached, every command will need ir_raw");
  }
  // AFTER IrStore::begin(): the humidifier controller reloads its state belief from
  // NVS, and its emitter looks up the code store. Before that, both are empty.
  HumidifierControl::begin(humidifierEmit);
  buildTopics();

  // BEFORE connectWifi(): its failure branch calls EspNowChannel::park(), and park
  // needs to know the channel remembered from the previous run.
  EspNowChannel::begin();

  connectWifi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);

  // --- CLAMP THE TWO NETWORK TIMEOUTS. This is what decides whether the panel
  // responds to presses at all.
  //
  // Every mqtt.* call BLOCKS loop(), and loop() is the ONLY place that executes the
  // commands a user presses on screen (see runPanelCommand). At the defaults:
  //   - PubSubClient waits up to 15 seconds for CONNACK
  //   - WiFiClient waits up to 3 seconds for the TCP handshake, and up to ~10
  //     seconds writing into a dead socket
  // Together, a dead server makes the wall panel "not respond to presses" for tens
  // of seconds at a time, while the screen keeps drawing and beeping because the
  // core-0 UI task is unaffected -- looking exactly like a hardware fault.
  //
  // 3 seconds / 5 seconds is the "fail fast" threshold. Still ample for a healthy
  // broker on the LAN or across the Internet; it only cuts the pointless waiting
  // when the other end is dead.
  net.setTimeout(3);            // seconds -- TCP handshake
  mqtt.setSocketTimeout(5);     // seconds -- waiting for CONNACK and the rest of a packet
  if (!mqtt.setBufferSize(MQTT_BUFFER_BYTES)) {
    Serial.println("Could not allocate the MQTT buffer - commands with ir_raw will be dropped silently!");
  }
  // Keep PubSubClient's default 15s keepalive -> the broker declares the node dead
  // after ~22s. STABILITY comes first: dropping to 3-5s means one network blink is
  // enough for the broker to cut the session and the client to reconnect, flipping
  // the state online/offline constantly.
  mqtt.setKeepAlive(15);
  // ONE attempt at startup. On failure DO NOT block -- serviceNetwork() in loop()
  // retries in the background, and everything that does not need a network
  // (ESP-NOW, UART, IR) works immediately.
  if (WiFi.status() == WL_CONNECTED) mqttTryConnect();

  // ESP-NOW is initialised AFTER WiFi has connected: it uses whatever channel WiFi
  // is on, so WiFi has to settle the channel first for the slaves (which are
  // probing for the router's channel) to meet it.
  if (EspNowRelay::begin()) {
    Serial.printf("ESP-NOW ready - master MAC = %s - channel %d\n",
                  WiFi.macAddress().c_str(), WiFi.channel());
  } else {
    Serial.println("ESP-NOW INIT ERROR - no data will be received from the outdoor node");
  }

  // UART to the UNO Q. The ordering no longer matters as it did in the BLE era (it
  // does not touch the radio), but it is kept here so the boot log reads in the
  // direction the data flows.
  //
  // THIS LINK EXISTS ONLY TO TALK TO THE ARDUINO UNO Q, and has nothing to do with
  // sensors -- room readings come over ESP-NOW. So breaking it loses the edge AI
  // layer but does NOT lose the indoor temperature; that is why the line below does
  // not block startup.
  UnoQLink::begin(ORG_ID);
  // Derive the path rather than hard-coding it: the UART_1 pair (GPIO2/15) reaches
  // P3 through the TXS0104 and therefore depends on EN_LEVEL_SHIFT, while the rest
  // are 3.3V direct. An earlier version hard-coded "through the TXS0104" from when
  // the IR transmitter was still on GPIO15, and that log line kept asserting
  // something false after the pin had moved -- exactly the kind of confident lie
  // that costs the reader more time than printing nothing.
  auto irPath = [](int pin) {
    return (pin == 2 || pin == 15) ? "P3 via the TXS0104, needs EN_LEVEL_SHIFT" : "3.3V direct";
  };
  Serial.printf("IR: tx GPIO%d (%s) - rx GPIO%d (%s)\n",
                IR_TX_PIN, irPath(IR_TX_PIN), IR_RX_PIN, irPath(IR_RX_PIN));
  Serial.println("This board has NO temperature/humidity sensor - the \"indoor\" number is the "
                 "median of the room-corner nodes (see room-registry.h)");
}

static unsigned long lastPub = 0;

// --- The bridge to the UI task -----------------------------------------------
// Bidirectional, and neither direction blocks:
//   UI -> loop(): Ui::pollCommand()  (the user has just pressed SEND / AUTO)
//   loop() -> UI: Ui::publish()      (a state snapshot to draw)
// EXECUTING a command happens here rather than in the UI task, for exactly the two
// reasons recorded in ui.h: PubSubClient is not thread-safe, and 38kHz IR
// bit-banging has to run on core 1 so the scheduler cannot interrupt it.

/// The user pressed SEND on screen: look the code up by alias and transmit it right
/// here.
static void runPanelCommand(const Ui::Command &c) {
  if (c.kind == Ui::Command::FAN_SET) {
    // Do NOT touch overrideLocal, and do NOT publish `override`: a fan level does
    // not carry (mode, temperature) so it does not compete with the comfort loop.
    // The same rule already applied to `action:` packets coming from the app -- see
    // the end of takeCommand().
    //
    // It also does NOT go through the server: the code is already in NVS, so
    // pressing here still works with no network. That is the entire reason the
    // panel keeps a copy.
    if (c.arg >= AcActions::FAN_COUNT) return;
    const char *wire = AcActions::fanWire(c.arg);
    const uint16_t n = IrStore::loadAlias(wire, -1, irBuf, IrIo::RAW_MAX);
    if (n == 0) {
      Serial.printf("[panel] fan %s: no code in NVS\n", wire);
      Ui::reply("CHƯA HỌC MÃ — vào app để học");
      return;
    }
    IrIo::blast(irBuf, n);
    lastFanIdx = c.arg;
    Serial.printf("[panel] transmitted %u transitions -> fan %s\n", n, wire);

    // Record it in the command log like any other command. `mode` keeps the air
    // conditioner's current mode: a fan level does NOT change the mode, so writing
    // "FAN_60" into the mode field would construct an air conditioner state that
    // never existed.
    Ui::CmdLog e{};
    copyStr(e.mode, sizeof(e.mode), actMode[0] ? actMode : "--");
    snprintf(e.reason, sizeof(e.reason), "panel:%s", wire);
    e.setpoint = actSetpoint;
    e.result   = Ui::CmdLog::SENT;
    Ui::logCommand(e);
    return;
  }

  if (c.kind == Ui::Command::HUMID_SET) {
    const uint32_t now = millis();
    if (c.arg == Ui::HUMID_AUTO_ARG) {
      HumidifierControl::backToAuto(now);
      Ui::reply("MÁY TẠO ẨM: TỰ ĐỘNG");
      return;
    }
    const bool want = (c.arg == Ui::HUMID_ON_ARG);
    HumidifierControl::manualSet(want, now);
    // State the REAL OUTCOME rather than echoing the button just pressed:
    // manualSet() may not have been able to transmit because no code is learned,
    // in which case the unit changed nothing. Reporting "ĐÃ BẬT" in that case is
    // the screen asserting something false -- exactly what ui.h's
    // NaN-rather-than-zero rule forbids, just at a different layer.
    const HumidifierControl::Status st = HumidifierControl::status(now);
    if (st.on == want) Ui::reply(want ? "MÁY TẠO ẨM: ĐÃ BẬT" : "MÁY TẠO ẨM: ĐÃ TẮT");
    else               Ui::reply("CHƯA HỌC MÃ — vào app để học");
    return;
  }

  if (c.kind == Ui::Command::AUTO) {
    overrideLocal = false;
    // Clear the override window on the server, not just the badge on screen.
    // Without this line, MANUAL is a one-way street: one press and the household
    // sits outside the comfort loop for the whole `override_hours` (2h by default)
    // with no way for the panel to undo it.
    //
    // Clearing an override is a HOUSEHOLD-WIDE action rather than one belonging to
    // this screen, so it is sent even when the local flag is already false -- the
    // override may have been set from the app, and someone at the wall pressing
    // AUTO is saying "never mind, let the server handle it". Returning early on a
    // false flag would abandon exactly that case.
    const bool ok = mqtt.connected() &&
                    mqtt.publish(tOverride.c_str(), "{\"clear\":true}", false);
    Serial.printf("[panel] handing control back to the server -> %s\n",
                  ok ? "sent" : "SEND ERROR (the server's window will expire by itself)");
    // Do NOT call Ui::reply() here: onAuto() already showed that exact toast at the
    // moment of the press. Calling again redraws the same sentence -> a pointless
    // flicker. And do not report a send failure on screen either: the badge HAS
    // returned to TU DONG, which is correct on the node side, while the server's
    // override window expires by itself -- slow, but not wrong.
    return;
  }

  if (c.kind == Ui::Command::RESYNC) {
    if (!mqtt.connected()) {
      Ui::reply("MẤT KẾT NỐI MÁY CHỦ");
      return;
    }
    // The same `state` topic as need_raw: this is still the node talking about its
    // own code store, differing only in asking for THE WHOLE STORE rather than one
    // code. Not retained -- it is a one-off request, and retained the broker would
    // replay it on every reconnect and the server would push the entire store down
    // for no reason.
    const bool ok = mqtt.publish(tState.c_str(), "{\"resync\":true}", false);
    Serial.printf("[panel] requesting the whole code store -> %s\n", ok ? "sent" : "SEND ERROR");
    Ui::reply(ok ? "ĐANG XIN MÃ TỪ MÁY CHỦ..." : "GỬI YÊU CẦU THẤT BẠI");
    return;
  }

  if (c.kind == Ui::Command::DEL_CODE) {
    // Do NOT touch overrideLocal: deleting a code is not commanding the air
    // conditioner, so it must not change who is in control.
    const bool gone = IrStore::removeAlias(c.mode, c.setpoint);
    if (gone) {
      aliasDirty = true;   // the "has a code" table changed -> the screen must recompute
      Serial.printf("[panel] deleted code %s %d from NVS\n", c.mode, c.setpoint);
      // Say clearly that this is REVERSIBLE. The code is still in Postgres; the next
      // command for this combination lands in onCmdPacket()'s "has an id, NVS is
      // empty" branch, and that branch asks the server to resend the array. Without
      // saying so, the user assumes they have just deleted it permanently and goes
      // off to relearn it by hand -- wasted effort.
      Ui::reply("ĐÃ XOÁ — sẽ tự nạp lại từ máy chủ");
    } else {
      Ui::reply("TỔ HỢP NÀY CHƯA CÓ MÃ");
    }
    return;
  }

  // Record control authority BEFORE attempting to transmit the code, not after.
  //
  // This line used to sit at the end of the function, after the "no code learned"
  // early return. The consequence: on a node that had learned nothing, pressing
  // MANUAL could never change the state -- the AUTO button stayed lit forever and
  // the user concluded it was broken.
  //
  // Separate two genuinely different things:
  //   WHO IS IN CONTROL     -> changes the moment the user presses; this is their
  //                            INTENT and it is real whether or not an IR code
  //                            exists.
  //   COULD IT TRANSMIT     -> reported separately by the toast below.
  // Conflating the two is why a control that is both an action and an indicator
  // became unpressable.
  //
  // Still HONEST: the "GHI ĐÈ" badge speaks about INTENT, and that intent is real
  // even with no learned IR code. Whether the override SURVIVES is what the two
  // toasts at the end of the function distinguish -- with no code we do not request
  // an override window, so the server does take control back, and the screen says
  // exactly that.
  overrideLocal = true;

  const uint16_t n = IrStore::loadAlias(c.mode, aliasTemp(c.mode, c.setpoint),
                                        irBuf, IrIo::RAW_MAX);
  if (n == 0) {
    Serial.printf("[panel] %s %d: no code in NVS\n", c.mode, c.setpoint);
    Ui::reply("CHƯA HỌC MÃ — vào app để học");
    return;
  }

  IrIo::blast(irBuf, n);
  copyStr(actMode, sizeof(actMode), c.mode);
  actSetpoint = c.setpoint;
  Serial.printf("[panel] transmitted %u transitions -> %s %d\n", n, c.mode, c.setpoint);

  // Publish state WITHOUT an ack: there is no req_id to match, but state_handler
  // still mirrors mode/setpoint into redis_state_service so the app and web see the
  // new state immediately (now with a realtime tick too -- state_handler calls
  // live_events.publish_change, so the app no longer waits for a telemetry tick).
  pending.reqId[0] = '\0';
  copyStr(pending.mode, sizeof(pending.mode), c.mode);
  pending.setpoint = c.setpoint;
  publishState();

  // Then REQUEST THE OVERRIDE WINDOW -- the other half, and the half that makes the
  // override survive.
  //
  // Placed AFTER the "no code learned" branch above, DELIBERATELY: blocking the
  // comfort loop while the node cannot transmit a single frame would leave the air
  // conditioner stuck in its old state for the whole `override_hours`. If we cannot
  // transmit, better to let the server keep trying -- it may pick a different
  // temperature that the node DOES have a code for. "Taking control" only means
  // something when it comes with the ability to act.
  //
  // Not retained (final argument = false): this is a one-off INTENT, not a state.
  // Retained, the broker would replay it on every reconnect and the override would
  // switch itself back on forever -- the same reason recorded at buildTopics() and
  // in the resync branch.
  JsonDocument ov;
  ov["mode"]     = c.mode;
  ov["setpoint"] = c.setpoint;
  char ovBuf[64];
  const size_t ovLen = serializeJson(ov, ovBuf);
  const bool ovOk = mqtt.connected() &&
                    mqtt.publish(tOverride.c_str(), (const uint8_t *)ovBuf, ovLen, false);
  Serial.printf("[panel] requesting override %s %d -> %s\n",
                c.mode, c.setpoint, ovOk ? "sent" : "SEND ERROR");

  // Say what actually happened, with two different sentences for two different
  // outcomes. The old sentence ("the server will take control back") is now WRONG
  // when the send succeeds -- once the override window is open the server does not
  // take control back until it expires or AUTO is pressed.
  Ui::reply(ovOk ? "ĐÃ GỬI"
                 : "ĐÃ PHÁT");  
}

// ---------------------------------------------------------------------------
//  Arduino UNO Q (edge AI)
// ---------------------------------------------------------------------------
/// Convert the project's mode strings ("COOL"...) to the 1-byte wire code, and
/// back. These two small functions are the ONLY BOUNDARY between the two
/// representations -- scattering strcmp("COOL") everywhere is the kind of place
/// where one typo becomes a wrong mode.
static uint8_t modeToWire(const char *mode) {
  if (mode == nullptr || mode[0] == '\0') return AC_UNOQ_MODE_UNKNOWN;
  if (strcmp(mode, "OFF")  == 0) return AC_UNOQ_MODE_OFF;
  if (strcmp(mode, "COOL") == 0) return AC_UNOQ_MODE_COOL;
  if (strcmp(mode, "DRY")  == 0) return AC_UNOQ_MODE_DRY;
  if (strcmp(mode, "FAN")  == 0) return AC_UNOQ_MODE_FAN;
  return AC_UNOQ_MODE_UNKNOWN;
}

static const char *modeFromWire(uint8_t wire) {
  switch (wire) {
    case AC_UNOQ_MODE_OFF:  return "OFF";
    case AC_UNOQ_MODE_COOL: return "COOL";
    case AC_UNOQ_MODE_DRY:  return "DRY";
    case AC_UNOQ_MODE_FAN:  return "FAN";
    default:                return nullptr;   // nullptr = DO NOT execute
  }
}

/// How often to push a snapshot to the UNO Q (ms). Far less often than the screen
/// draw interval (200ms): the UNO Q recomputes every 30 seconds, and pushing more
/// often only burns BLE airtime shared with WiFi.
static const uint32_t UNOQ_PUSH_MS = 5000;

/// Package everything the UNO Q needs to decide for itself, then notify.
///
/// `cloud_silence_sec` is the most important field: it is the ONLY thing telling
/// the UNO Q whether the server is alive, and the gateway knows this more reliably
/// than the UNO Q could because it is the one holding the MQTT session. Without it
/// the fallback layer has to guess, and guessing wrong in either direction is bad:
/// taking control too early means both sides fight over the compressor, too late
/// means the house stays hot for the whole outage.
static void pushUnoQSnapshot() {
  static uint32_t lastPush = 0;
  if (millis() - lastPush < UNOQ_PUSH_MS) return;
  lastPush = millis();

  AcUnoQSnapshot snap{};
  snap.magic   = AC_UNOQ_MAGIC;
  snap.version = AC_UNOQ_VERSION;

  float tin = NAN, hin = NAN;
  uint8_t voting = 0;
  RoomRegistry::median(tin, hin, &voting);   // on failure -> stays NaN, encoded as INVALID
  snap.room_count = voting;
  snap.t_in_c100  = acUnoQEncodeTemp(tin);
  snap.h_in_x100  = acUnoQEncodeRh(hin);
  snap.t_out_c100 = acUnoQEncodeTemp(lastSlaveT);
  snap.h_out_x100 = acUnoQEncodeRh(lastSlaveH);

  for (uint8_t i = 0; i < AC_UNOQ_MAX_ROOMS; i++) {
    const RoomRegistry::Room *r = RoomRegistry::at(i);
    const bool live = r != nullptr && RoomRegistry::online(i);
    snap.room_t_c100[i]  = live ? acUnoQEncodeTemp(r->t) : AC_UNOQ_T_INVALID;
    snap.room_h_x100[i]  = live ? acUnoQEncodeRh(r->h)   : AC_UNOQ_H_INVALID;
    snap.room_corner[i]  = r ? r->corner : AC_CORNER_NONE;
  }

  const bool outOnline =
      lastSlaveMs && (millis() - lastSlaveMs < SlaveWatch::SLAVE_TIMEOUT_MS);
  snap.flags = (uint8_t)((WiFi.status() == WL_CONNECTED ? AC_UNOQ_FLAG_WIFI_UP : 0) |
                         (mqtt.connected() ? AC_UNOQ_FLAG_MQTT_UP : 0) |
                         (overrideLocal ? AC_UNOQ_FLAG_OVERRIDE : 0) |
                         (outOnline ? AC_UNOQ_FLAG_OUT_ONLINE : 0));

  // lastCmdMs = 0 means the server has NEVER been heard issuing a command -- quite
  // different from "it just spoke". Sending 0 for both cases would make the UNO Q
  // believe the cloud had just spoken and never take control in a household where
  // the cloud has never worked at all.
  const uint32_t silence = lastCmdMs ? (millis() - lastCmdMs) / 1000UL : 0xFFFFFFFFUL;
  snap.cloud_silence_sec =
      silence >= AC_UNOQ_SILENCE_NEVER ? AC_UNOQ_SILENCE_NEVER : (uint16_t)silence;

  snap.ac_mode     = modeToWire(actMode);
  snap.ac_setpoint = (int8_t)(actSetpoint >= 0 ? actSetpoint : -1);
  snap.uptime_min  = (uint16_t)(millis() / 60000UL);
  acUnoQSealSnapshot(&snap);

  UnoQLink::publish(snap);
  SerialTrace::snapshotOut(snap, UnoQLink::connected());
}

// --- The UNO Q's latest advice, kept FOR DISPLAY -----------------------------
//  The EDGE AI tab reads these four variables. Kept separately rather than
//  borrowing `pending` or `actMode`: those describe the air conditioner as it is
//  RUNNING, while these are what the UNO Q PROPOSED -- and the entire value of that
//  tab lies in the two being able to DIFFER. Merging them loses the one piece of
//  information the screen exists to convey.
static char     unoqMode[8] = "";
static int      unoqSetpoint = -1;
static bool     unoqWasCommand = false;
static uint32_t unoqLastMs = 0;

/// Execute (or merely record) what the UNO Q has just sent over.
static void runUnoQIncoming() {
  UnoQLink::Incoming in;
  if (!UnoQLink::poll(in)) return;

  const char *mode = modeFromWire(in.mode);
  if (mode == nullptr) {
    Serial.printf("[unoq] unknown mode (%u) - discarded\n", in.mode);
    return;
  }

  // RECORD IT BEFORE ANY EARLY RETURN. There is a "no code learned - not
  // transmitting" path below, and recording at the end would mean the times the
  // edge wanted to do something the panel could not do are exactly the ones that do
  // NOT appear on screen -- losing precisely the most interesting case. The UNO Q
  // did make a proposal, and that is true whether or not the panel could transmit
  // it.
  copyStr(unoqMode, sizeof(unoqMode), mode);
  unoqSetpoint   = in.setpoint;
  unoqWasCommand = in.isCommand;
  unoqLastMs     = millis();

  if (!in.isCommand) {
    // ADVICE: record it, do NOT transmit IR. This is the boundary that keeps every
    // experiment on the UNO Q from going straight to the compressor -- see
    // unoq-link.h §2.
    Serial.printf("[unoq] advice %s %d (recorded only, not transmitted)\n", mode, in.setpoint);
    Ui::CmdLog entry{};
    copyStr(entry.mode,   sizeof(entry.mode),   mode);
    copyStr(entry.reason, sizeof(entry.reason), "edge advice");
    entry.setpoint = in.setpoint;
    entry.result   = Ui::CmdLog::NO_CODE;   // "not transmitted" -- exactly what happened
    Ui::logCommand(entry);
    return;
  }

  // A REAL COMMAND -- the UNO Q is in control because the server has gone silent.
  //
  // DELIBERATELY NOT ROUTED THROUGH runPanelCommand(), even though that path exists
  // and does almost the right thing: it sets `overrideLocal` and asks the server to
  // open an OVERRIDE window. Override exists so a user can take control AWAY FROM
  // the server; the UNO Q is STANDING IN FOR the server. Taking that path means
  // that when the network returns, the server is locked out for the whole
  // `override_hours` (2 hours by default) by the very fallback layer that just
  // rescued it -- and the screen shows "GHI ĐÈ" with nobody having pressed anything.
  Serial.printf("[unoq] COMMAND %s %d (the edge is in control)\n", mode, in.setpoint);

  const uint16_t n = IrStore::loadAlias(mode, aliasTemp(mode, in.setpoint),
                                        irBuf, IrIo::RAW_MAX);
  Ui::CmdLog entry{};
  copyStr(entry.mode,   sizeof(entry.mode),   mode);
  copyStr(entry.reason, sizeof(entry.reason), "edge takeover");
  entry.setpoint = in.setpoint;

  if (n == 0) {
    Serial.printf("[unoq] %s %d: this code has not been learned - not transmitting\n", mode, in.setpoint);
    entry.result = Ui::CmdLog::NO_CODE;
    Ui::logCommand(entry);
    return;
  }

  IrIo::blast(irBuf, n);
  copyStr(actMode, sizeof(actMode), mode);   // the REAL state transmitted to the unit
  actSetpoint = in.setpoint;
  entry.result = Ui::CmdLog::SENT;
  Ui::logCommand(entry);

  // Report the new state to the cloud if there is still a path -- if the network is
  // down, never mind, the network being down is precisely why the UNO Q is in
  // control. No `ack` included: there is no server req_id to match.
  pending.reqId[0] = '\0';
  copyStr(pending.mode, sizeof(pending.mode), mode);
  pending.setpoint = in.setpoint;
  if (mqtt.connected()) publishState();
}

/// Build the snapshot for the screen. The IR code bitmask is computed here because
/// NVS belongs to core 1.
static void pushUiModel() {
  Ui::Model m;
  m.wifiUp = (WiFi.status() == WL_CONNECTED);
  m.mqttUp = mqtt.connected();
  m.rssi   = m.wifiUp ? (int)WiFi.RSSI() : 0;
  m.channel = (uint8_t)WiFi.channel();
  // "The router decides this channel" and "the panel remembered this channel" are
  // two different things, and when chasing missing readings they lead to two
  // different places. The INFO screen states it plainly rather than leaving the
  // installer to guess.
  m.channelPinned = EspNowChannel::pinned();
  strncpy(m.ip,   m.wifiUp ? WiFi.localIP().toString().c_str() : "", sizeof(m.ip) - 1);
  strncpy(m.ssid, WIFI_SSID, sizeof(m.ssid) - 1);
  strncpy(m.mac,  WiFi.macAddress().c_str(), sizeof(m.mac) - 1);

  // Each room corner individually, so the screen can say which corner is drifting --
  // rather than only a median number that hides exactly that. This is what justifies
  // having four sensors in the first place.
  //
  // The slot count comes from HOW MANY CORNERS HAVE BEEN HEARD, not from a constant
  // declared in advance: the gateway keeps no list of room nodes at all (an ESP-NOW
  // packet carries its own uuid), so installing another corner makes it appear on
  // screen by itself with no reflash.
  m.roomSlots = RoomRegistry::knownCount() < Ui::Model::MAX_ROOMS
                    ? RoomRegistry::knownCount() : Ui::Model::MAX_ROOMS;
  for (uint8_t i = 0; i < m.roomSlots; i++) {
    const RoomRegistry::Room *r = RoomRegistry::at(i);
    m.roomOnline[i] = RoomRegistry::online(i);
    m.roomT[i] = (r && m.roomOnline[i]) ? r->t : NAN;
    m.roomH[i] = (r && m.roomOnline[i]) ? r->h : NAN;
    m.roomCorner[i] = r ? r->corner : AC_CORNER_NONE;
    m.roomAgeSec[i] = RoomRegistry::ageSec(i);
  }
  m.roomOnlineCount = RoomRegistry::onlineCount();
  // If no corner has a reading, median() leaves m.tIn/m.hIn at Model's default NaN
  // and the screen shows a skeleton. They must NOT be replaced with 0.0 or a stale
  // value (ui.h §Model).
  RoomRegistry::median(m.tIn, m.hIn, &m.roomVoting);   // roomVoting = corners WITH a reading
  m.unoqUp  = UnoQLink::connected();
  m.unoqRx  = UnoQLink::rxCount();
  m.unoqRejected = UnoQLink::rejectedCount();
  copyStr(m.unoqMode, sizeof(m.unoqMode), unoqMode);
  m.unoqSetpoint    = unoqSetpoint;
  m.unoqWasCommand  = unoqWasCommand;
  m.unoqEverAdvised = (unoqLastMs != 0);
  m.unoqAgeSec      = unoqLastMs ? (millis() - unoqLastMs) / 1000 : 0;

  m.tOut = lastSlaveT; m.hOut = lastSlaveH;
  // The same threshold as SlaveWatch, so the screen and the status topic can never
  // say two different things about the same node.
  m.outOnline = lastSlaveMs && (millis() - lastSlaveMs < SlaveWatch::SLAVE_TIMEOUT_MS);
  m.outAgeSec = lastSlaveMs ? (millis() - lastSlaveMs) / 1000 : 0;
  m.espnowRx   = EspNowRelay::receivedCount();
  m.espnowDrop = EspNowRelay::droppedCount();

  copyStr(m.mode, sizeof(m.mode), actMode);
  m.setpoint      = actSetpoint;
  m.overrideLocal = overrideLocal;
  m.lastCmdSec    = lastCmdMs ? (millis() - lastCmdMs) / 1000 : 0;
  m.cloudEverCommanded = (lastCmdMs != 0);

  // Scan NVS exactly once and keep the result -- see the note at `aliasDirty`.
  static uint16_t coolMask = 0;
  static bool     hasDry = false, hasFan = false, hasOff = false;
  static uint8_t  fanMask = 0;
  static bool     humidHasOn = false, humidHasOff = false;
  if (aliasDirty) {
    coolMask = 0;
    for (uint8_t i = 0; i < 15; i++) {
      if (IrStore::hasAlias("COOL", 16 + i)) coolMask |= (uint16_t)(1u << i);
    }
    hasDry = IrStore::hasAlias("DRY", -1);
    hasFan = IrStore::hasAlias("FAN", -1);
    hasOff = IrStore::hasAlias("OFF", -1);

    // Discrete buttons. THE SAME `aliasDirty` flag as the matrix above, not a
    // second one: every code-writing path (learning locally, a backend command, a
    // resync) already sets that flag, so a separate one would only create another
    // place to forget to set.
    fanMask = 0;
    for (uint8_t i = 0; i < AcActions::FAN_COUNT; i++) {
      if (IrStore::hasAlias(AcActions::fanWire(i), -1)) fanMask |= (uint8_t)(1u << i);
    }
    humidHasOn  = IrStore::hasAlias(AcActions::humidWire(true), -1);
    humidHasOff = IrStore::hasAlias(AcActions::humidWire(false), -1);

    aliasDirty = false;
  }
  m.coolMask = coolMask;
  m.hasDry   = hasDry;
  m.hasFan   = hasFan;
  m.hasOff   = hasOff;
  m.fanMask  = fanMask;
  m.fanLast  = lastFanIdx;

  {
    const HumidifierControl::Status st = HumidifierControl::status(millis());
    m.humidOn       = st.on;
    m.humidOverride = st.overriding;
    m.humidRh       = st.rh;
    m.humidNote     = HumidifierControl::reasonText(st.reason);
    m.humidOverrideLeftSec = st.overrideLeftSec;
    m.humidHasOn    = humidHasOn;
    m.humidHasOff   = humidHasOff;
  }

  m.learning = IrIo::learning();
  copyStr(m.learnLabel, sizeof(m.learnLabel), learnLabel);
  m.learnRemainSec = IrIo::learnRemainingMs() / 1000;

  m.irCodeCount = IrStore::count();
  m.uptimeSec   = millis() / 1000;
  m.fw          = FW_VERSION;
  Ui::publish(m);
}

void loop() {
  // USER COMMANDS COME BEFORE THE NETWORK. This ordering is a decision, not an
  // accident: runPanelCommand() transmits IR without needing a network, while
  // serviceNetwork() can block for several seconds (TCP handshake, waiting for
  // CONNACK). Putting the network first means every touch queues behind a
  // connection attempt -- exactly the "buttons stop working when the connection is
  // down" symptom the whole edge architecture exists to avoid.
  //
  // Collected EVERY iteration (buttons must respond instantly), unlike the state
  // snapshot which is only pushed to the UI every 200ms -- matching the screen's
  // redraw interval (Interface/README.md §7.4), which the eye cannot beat. Pushing
  // every iteration would be pure waste: each push pulls in a whole run of
  // WiFi.RSSI()/millis()/memcpy.
  Ui::Command panelCmd;
  while (Ui::pollCommand(panelCmd)) runPanelCommand(panelCmd);

  // NEVER call connectWifi()/mqttTryConnect() directly here -- both of them wait.
  // serviceNetwork() retries on a schedule and returns immediately, so everything
  // below always runs even with the network completely down. See its comment for
  // why.
  serviceNetwork();

  // Collect AGAIN. serviceNetwork() may still have just blocked for several seconds
  // (the timeouts are clamped but cannot be zero), and anyone who pressed during
  // that window deserves to be served now rather than after the rest of the loop.
  while (Ui::pollCommand(panelCmd)) runPanelCommand(panelCmd);

  static unsigned long lastUiPush = 0;
  if (millis() - lastUiPush >= 200) {
    lastUiPush = millis();
    pushUiModel();
  }

  // Execute commands HERE rather than in the callback: see the note at the pending
  // struct.
  if (pending.hasFrame) {
    pending.hasFrame = false;
    IrIo::blast(irBuf, pending.frameLen);
    strncpy(lastReqId, pending.reqId, sizeof(lastReqId) - 1);
    lastReqId[sizeof(lastReqId) - 1] = '\0';
    copyStr(actMode, sizeof(actMode), pending.mode);   // the REAL state transmitted to the unit
    actSetpoint = pending.setpoint;
    Serial.printf("[ir] transmitted %u transitions to the air conditioner\n", pending.frameLen);
  }
  if (pending.needAck) {
    pending.needAck = false;
    publishState();
  }
  if (pending.needRawId[0]) {
    publishNeedRaw(pending.needRawId);
    // Clear it AFTER sending, and clear it even if the send failed: with the network
    // down, the next command for the same combination lands in that same branch and
    // queues the request again. Keeping it to retry forever would turn a one-off
    // request into a send loop running every loop() iteration.
    pending.needRawId[0] = '\0';
  }

  // While learning, wait for the user to press the remote.
  if (IrIo::learning()) {
    uint16_t n = IrIo::learnPoll(irBuf, IrIo::RAW_MAX);
    if (n > 0) publishLearned(irBuf, n);
  }
  if (IrIo::learnTimedOut()) {
    Serial.printf("[learn] timed out waiting for \"%s\" - no signal captured. "
                  "Check: does the remote have batteries? is it aimed at the receiver? "
                  "closer than 1m?\n", learnLabel);
  }

  // Drain the ESP-NOW queue: the 4 room corners and the outdoor node all come
  // through here. The callback only copies the packet; publishing happens here --
  // see espnow-relay.h.
  EspNowRelay::poll(onSlavePacket);
  // One timeout sweeper for BOTH the outdoor node and the room corners: SlaveWatch
  // is keyed by device_uuid so it does not need to know which node is which kind.
  SlaveWatch::checkTimeouts(publishSlaveStatus);

  // Advice/commands from the Arduino UNO Q. Collected here rather than executed in
  // the BLE callback -- the same rule already applied to the MQTT and ESP-NOW
  // callbacks (see unoq-link.h §1).
  runUnoQIncoming();
  pushUnoQSnapshot();

  // --- Humidifier: one measure-and-decide round -----------------------------
  //  THE INTERVAL LIVES HERE rather than being hidden inside the module: its
  //  EMA_ALPHA is tightly coupled to the call interval (0.2 at 5 seconds = a time
  //  constant of ~25 seconds), so that interval has to sit somewhere visible. See
  //  HumidifierControl::tick().
  //
  //  THE HUMIDITY IS THE CORNERS' MEDIAN, the same number the screen and the cloud
  //  use -- not a separate measurement. If the median fails (no corner has a reading
  //  yet) `h` stays NaN, and tick() reads that as "no reading" and cuts the unit off
  //  after SENSOR_STALE_SEC. Exactly the "when in doubt, turn it OFF" principle.
  {
    static uint32_t lastHumidMs = 0;
    const uint32_t nowMs = millis();
    if (nowMs - lastHumidMs >= HumidifierControl::TICK_MS) {
      lastHumidMs = nowMs;
      float ht = NAN, hh = NAN;
      uint8_t hv = 0;
      RoomRegistry::median(ht, hh, &hv);
      HumidifierControl::tick(hh, nowMs);
    }
  }

  // THIS BOARD NO LONGER HAS A TELEMETRY PUBLISH BLOCK OF ITS OWN.
  //
  // The board has no sensor any more, and sending a room-corner node's borrowed
  // numbers under the gateway's name fabricates a measurement that never happened --
  // exactly what ui.h's "NaN rather than zero" rule forbids, just at a different
  // layer. The gateway's alive/dead state is handled by the `status` topic (retained
  // + Last Will), and its MAC ships with the `state` packet.
  //
  // Indoor readings reach the cloud on EACH CORNER'S OWN topic, sent on their behalf
  // by publishRoomTelemetry(); the backend computes the median itself
  // (services/redis_room_state_service.py).

  unsigned long now = millis();
  if (lastPub != 0 && now - lastPub < TELEMETRY_MS) return;  // not yet time for the summary
  lastPub = now;

  float tin = NAN, hin = NAN;
  uint8_t voting = 0;
  const bool haveIndoor = RoomRegistry::median(tin, hin, &voting);
  if (haveIndoor) {
    Serial.printf("[indoor] median %.1f°C %.0f%% from %u/%u corners - espnow rx=%lu dropped=%lu"
                  " - unoq=%s\n",
                  tin, hin, voting, RoomRegistry::knownCount(),
                  (unsigned long)EspNowRelay::receivedCount(),
                  (unsigned long)EspNowRelay::droppedCount(),
                  UnoQLink::connected() ? "connected" : "not connected");
  } else {
    // Three completely different failure cases, and the two counters are what
    // distinguishes them:
    //   rx=0                -> no ESP-NOW heard at all: a wrong WIFI_SSID on the room
    //                          nodes (so they locked onto the wrong channel), or the
    //                          nodes are not powered
    //   rx>0, 0 corners     -> packets heard but all of them from the outdoor node
    //   corners, voting=0   -> the corners are alive but every sensor is faulty (NaN)
    Serial.printf("[indoor] NO READING YET - espnow rx=%lu dropped=%lu - "
                  "%u corners known, %u alive. See the 3 cases in main.cpp.\n",
                  (unsigned long)EspNowRelay::receivedCount(),
                  (unsigned long)EspNowRelay::droppedCount(),
                  RoomRegistry::knownCount(), RoomRegistry::onlineCount());
  }

  // The per-node average-delta table. The totals above answer "are packets being
  // lost"; this table answers "WHICH node is losing them" -- two different questions,
  // and only the second leads to the place that needs fixing.
  SerialTrace::summary();
}
