#pragma once
#include <Arduino.h>

#include "espnow-message.h"
#include "slave-watch.h"   // SLAVE_TIMEOUT_MS -- see ROOM_STALE_MS below

// ============================================================================
//  Table of the room-corner sensor nodes the gateway can currently hear.
// ----------------------------------------------------------------------------
//  THE GATEWAY NO LONGER HAS A SENSOR OF ITS OWN. The "indoor" number shown on
//  the screen, the number sent to the Arduino UNO Q, and (via the backend) the
//  number the comfort algorithm uses -- all of them come from here: the median of
//  the fresh corners.
//
//  KEYED BY device_uuid, NOT by corner number: the uuid is the real identity, it
//  arrives inside every ESP-NOW packet, and it is what the backend uses too.
//  `corner` is only a display label, so two boards sharing a label are still
//  counted as two nodes -- quite unlike keying by label, where a duplicate number
//  means one corner's readings vanish entirely with nobody noticing.
//
//  WHY THE MEDIAN AND NOT THE MEAN:
//  four sensors sit in four corners, and one of them will inevitably catch sun
//  through a window or sit directly under the air outlet. That corner is 3-4degC
//  away from the room, and a mean lets it drag the setpoint along -- permanently,
//  with no symptom beyond "it just feels wrong in here". A median ignores a single
//  outlier entirely as long as the other three corners agree.
//
//  THIS IS THE TWIN OF src/app/comfort/room_aggregate.py ON THE BACKEND SIDE, and
//  the two MUST produce the same number -- otherwise the panel on the wall and the
//  app in the user's hand report two different temperatures for the same room, and
//  neither is obviously wrong enough to fix. Change the rule on one side and you
//  must change both.
// ============================================================================
namespace RoomRegistry {

/// How many corners to track at once. Equal to the number of slots SlaveWatch can
/// track, minus one reserved for the outdoor node.
static const uint8_t MAX_ROOMS = 6;

/// How long without being heard before a node counts as disconnected (ms).
///
/// TAKEN DIRECTLY FROM SlaveWatch'S THRESHOLD rather than being a separate
/// number: SlaveWatch is the side that publishes "offline" to a room node's status
/// topic, so two divergent thresholds mean the wall panel and the web say
/// different things about the same corner -- exactly the kind of contradiction
/// where nobody believes either side.
static const uint32_t ROOM_STALE_MS = SlaveWatch::SLAVE_TIMEOUT_MS;

struct Room {
  bool     used;
  char     uuid[33];
  uint8_t  corner;      ///< display label; AC_CORNER_NONE if the node did not declare one
  float    t, h;        ///< NAN = node alive but the sensor is faulty
  uint32_t lastHeardMs;
};

/// Record a packet just heard from a room-corner node. The caller has already
/// filtered on node_kind.
/// Returns false if the table is full (a 7th node onwards is not tracked).
bool update(const AcEspNowPacket &pkt);

/// Median temperature/humidity across the corners that are FRESH and HAVE a
/// reading.
/// Returns false when no corner qualifies -- the caller must read that as "the
/// indoor temperature is unknown" and must NOT substitute 0.0 or a stale value.
/// [usedOut] receives how many corners took part (ignored if nullptr).
bool median(float &tempC, float &humidity, uint8_t *usedOut = nullptr);

/// How many corners are currently fresh (including a live corner whose sensor is
/// faulty).
uint8_t onlineCount();

/// How many slots have ever been heard from -- used to know how many corners are
/// installed without having to declare it up front in config.h.
uint8_t knownCount();

/// Slot [index], in first-heard order. nullptr if beyond knownCount().
const Room *at(uint8_t index);

/// Seconds since this slot was last heard; 0 if it has never been heard.
uint32_t ageSec(uint8_t index);

/// Is this slot fresh.
bool online(uint8_t index);

// REPORTING online/offline to the cloud does NOT belong here: SlaveWatch already
// does exactly that for the outdoor node, keyed by device_uuid, and the room nodes
// reuse it unchanged. Writing a second tracking mechanism would create two sources
// of truth for the same question, "is this node alive".

} // namespace RoomRegistry
