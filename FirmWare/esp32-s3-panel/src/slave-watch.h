#pragma once
#include <Arduino.h>

// ============================================================================
//  Tracking whether a SLAVE node is alive (runs on the MASTER node).
// ----------------------------------------------------------------------------
//  WHY IT IS NEEDED: a slave node has no MQTT connection, so the broker has NO
//  Last Will for it. If a slave loses power or fails, nothing reports it and the
//  web UI shows "Trực tuyến" forever -- which is worse than showing nothing,
//  because it asserts something false.
//
//  HOW IT WORKS: every ESP-NOW packet from a slave is a heartbeat. The master
//  records when it was last heard; after SLAVE_TIMEOUT_MS without hearing from it,
//  the master publishes "offline" to that node's status topic ON ITS BEHALF, and
//  publishes "online" again when it comes back.
// ============================================================================
namespace SlaveWatch {

/// How long without being heard before a node counts as disconnected.
///
/// THIS MUST BE DERIVED FROM THE NODE'S REAL HEARTBEAT. That heartbeat is 5
/// SECONDS on both node types:
///     esp32-room/src/main.cpp      ROOM_PUBLISH_MS = 5000
///     esp32-outdoor/src/config.h   TELEMETRY_MS    = 5000
///
///     35s / 5s  =  tolerates 7 CONSECUTIVE missed beats before reporting a
///                  disconnection.
///
/// 20 SECONDS WAS THE OLD NUMBER, AND IT WAS WRONG BECAUSE OF A WRONG ASSUMPTION.
/// The old comment said "the slave beats every 3s; a 20s threshold -> tolerates
/// ~6.6 missed beats". The intent was right, but NO NODE transmits every 3
/// seconds -- both use 5. At the real cadence, 20 seconds only tolerates 3 missed
/// beats, less than half of what that very comment claimed to have considered and
/// accepted.
///
/// THE SYMPTOM IN THE FIELD: a short run of lost packets -- routine for a node
/// mounted outdoors, through a wall, with its transmit power held down at 8 dBm
/// for power-supply reasons (see espnow-slave-radio.h) -- was enough for the panel
/// to report "MẤT KẾT NỐI" and then recover by itself a few tens of seconds later.
/// Users read that as a flaky board, exactly what choosing a WIDE threshold exists
/// to avoid.
///
/// FIXED AT THE PANEL AND NOT AT THE NODE, for two independent reasons that both
/// lead here: the design principle is a DENSE heartbeat with a WIDE threshold (a
/// dense heartbeat gives many chances to check in within the same window), so the
/// side to widen is the threshold; and the nodes have NO OTA -- fixing them means
/// climbing the wall and removing each board.
///
/// The cost is the same cost as before, just longer: a real power cut takes ~35
/// seconds to show as offline. Acceptable, because repeated false alarms destroy
/// the user's trust in the status light -- far worse than finding out fifteen
/// seconds later.
///
/// IF YOU CHANGE A NODE'S TRANSMIT CADENCE YOU MUST CHANGE THIS NUMBER TOO. Rule
/// of thumb: threshold ~= 7 x cadence.
static const uint32_t SLAVE_TIMEOUT_MS = 35000UL;

/// The 5s heartbeat exists to detect ALIVE/DEAD quickly, not to log readings
/// densely: room temperature does not change over 5 seconds, and storing all of it
/// would only bloat the DB and make the comfort algorithm recompute for nothing.
/// So readings are only pushed to the cloud every 15s.
static const uint32_t RELAY_INTERVAL_MS = 15000UL;

/// Maximum slaves tracked at once (a household currently has 1; leaving room for
/// multiple rooms).
static const uint8_t MAX_SLAVES = 8;

typedef void (*StatusChanged)(const char *deviceUuid, bool online);

/// Record that a slave was just heard. If this is the first time, or it has just
/// come back to life, [cb] is called with online=true (so the caller can publish
/// its status).
void heard(const char *deviceUuid, StatusChanged cb);

/// Periodically RE-ASSERT that a slave is online rather than only reporting state
/// transitions.
/// The reason: when a slave switches from WiFi mode to ESP-NOW, the broker has to
/// wait out the keepalive (~22s) before realising the old MQTT session is dead and
/// firing its "offline" Last Will -- which OVERWRITES the "online" the master just
/// sent. Reporting only once would leave a perfectly alive node showing as offline
/// on the web forever.
static const uint32_t STATUS_REFRESH_MS = 60000UL;

/// true if it is time to push this slave's readings to the cloud (>=
/// RELAY_INTERVAL_MS since the last push). Call after heard(); returning true
/// counts as having pushed.
bool dueForRelay(const char *deviceUuid);

/// true if it is time to re-assert this slave's online status.
bool dueForStatusRefresh(const char *deviceUuid);

/// Call every loop(): any slave past its deadline gets [cb] called with
/// online=false.
void checkTimeouts(StatusChanged cb);

} // namespace SlaveWatch
