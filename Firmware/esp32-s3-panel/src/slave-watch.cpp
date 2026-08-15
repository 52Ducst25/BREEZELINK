#include "slave-watch.h"
#include <string.h>

namespace SlaveWatch {

struct Entry {
  char     uuid[33];
  uint32_t lastHeardMs;
  uint32_t lastRelayMs;   // last time this slave's readings were pushed to the cloud
  uint32_t lastStatusMs;  // last time its online status was re-asserted
  bool     used;
  bool     online;
  bool     everRelayed;   // so the FIRST packet is pushed at once instead of waiting 15s
};

static Entry table[MAX_SLAVES];

static Entry *find(const char *uuid) {
  for (uint8_t i = 0; i < MAX_SLAVES; i++) {
    if (table[i].used && strncmp(table[i].uuid, uuid, sizeof(table[i].uuid)) == 0) {
      return &table[i];
    }
  }
  return nullptr;
}

static Entry *claimFreeSlot() {
  for (uint8_t i = 0; i < MAX_SLAVES; i++) {
    if (!table[i].used) return &table[i];
  }
  return nullptr;   // table full: a 9th slave onwards gets no status tracking
}

void heard(const char *deviceUuid, StatusChanged cb) {
  if (deviceUuid == nullptr || deviceUuid[0] == '\0') return;

  Entry *e = find(deviceUuid);
  if (e == nullptr) {
    e = claimFreeSlot();
    if (e == nullptr) return;
    memset(e, 0, sizeof(*e));
    strncpy(e->uuid, deviceUuid, sizeof(e->uuid) - 1);
    e->used   = true;
    e->online = false;      // so the branch below reports "just came online"
  }

  e->lastHeardMs = millis();
  if (!e->online) {
    e->online = true;
    if (cb) cb(e->uuid, true);
  }
}

bool dueForRelay(const char *deviceUuid) {
  Entry *e = find(deviceUuid);
  if (e == nullptr) return false;

  uint32_t now = millis();
  // Push the first packet after the master boots immediately: waiting another 15s
  // for the first reading makes the installer think the system is not running.
  if (!e->everRelayed || now - e->lastRelayMs >= RELAY_INTERVAL_MS) {
    e->everRelayed = true;
    e->lastRelayMs = now;
    return true;
  }
  return false;
}

bool dueForStatusRefresh(const char *deviceUuid) {
  Entry *e = find(deviceUuid);
  if (e == nullptr || !e->online) return false;

  uint32_t now = millis();
  if (now - e->lastStatusMs >= STATUS_REFRESH_MS) {
    e->lastStatusMs = now;
    return true;
  }
  return false;
}

void checkTimeouts(StatusChanged cb) {
  uint32_t now = millis();
  for (uint8_t i = 0; i < MAX_SLAVES; i++) {
    Entry *e = &table[i];
    if (!e->used || !e->online) continue;
    // Unsigned arithmetic, so this stays correct across a millis() wrap (~49 days).
    if (now - e->lastHeardMs >= SLAVE_TIMEOUT_MS) {
      e->online = false;
      if (cb) cb(e->uuid, false);
    }
  }
}

} // namespace SlaveWatch
