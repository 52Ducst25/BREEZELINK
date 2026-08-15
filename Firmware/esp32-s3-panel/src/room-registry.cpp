#include "room-registry.h"

#include <string.h>

namespace RoomRegistry {
namespace {

Room g_rooms[MAX_ROOMS];

Room *find(const char *uuid) {
  for (uint8_t i = 0; i < MAX_ROOMS; i++) {
    if (g_rooms[i].used && strncmp(g_rooms[i].uuid, uuid, sizeof(g_rooms[i].uuid)) == 0) {
      return &g_rooms[i];
    }
  }
  return nullptr;
}

bool isFresh(const Room &r) {
  // Unsigned arithmetic, so this stays correct across a millis() wrap (~49 days) --
  // the same rule already used in SlaveWatch::checkTimeouts().
  return r.used && (millis() - r.lastHeardMs) < ROOM_STALE_MS;
}

/// In-place insertion sort. n <= 6, so an O(n^2) algorithm is the right choice:
/// qsort() drags in a function pointer and a few hundred bytes of code for a
/// six-element problem.
void sortAsc(float *v, uint8_t n) {
  for (uint8_t i = 1; i < n; i++) {
    const float key = v[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
    v[j + 1] = key;
  }
}

float medianOf(float *v, uint8_t n) {
  sortAsc(v, n);
  const uint8_t mid = (uint8_t)(n / 2);
  // Even count -> average the two middle values, so the number drifts smoothly as
  // the room warms rather than stepping every time the two middle corners swap
  // places.
  return (n % 2 == 1) ? v[mid] : (v[mid - 1] + v[mid]) / 2.0f;
}

}  // namespace

bool update(const AcEspNowPacket &pkt) {
  Room *r = find(pkt.device_uuid);
  if (r == nullptr) {
    for (uint8_t i = 0; i < MAX_ROOMS; i++) {
      if (!g_rooms[i].used) { r = &g_rooms[i]; break; }
    }
    if (r == nullptr) return false;   // table full
    memset(r, 0, sizeof(*r));
    r->used = true;
    strncpy(r->uuid, pkt.device_uuid, sizeof(r->uuid) - 1);
  }

  r->corner      = pkt.corner;
  r->lastHeardMs = millis();
  // Storing NaN is deliberate: the node is alive but its sensor is faulty. Keeping
  // the old reading here would let a corner whose wire has come off keep voting in
  // the median with a temperature from half an hour ago.
  r->t = pkt.temp;
  r->h = pkt.humidity;
  return true;
}

bool median(float &tempC, float &humidity, uint8_t *usedOut) {
  float temps[MAX_ROOMS], hums[MAX_ROOMS];
  uint8_t n = 0;
  for (uint8_t i = 0; i < MAX_ROOMS; i++) {
    const Room &r = g_rooms[i];
    if (!isFresh(r) || isnan(r.t) || isnan(r.h)) continue;
    temps[n] = r.t;
    hums[n] = r.h;
    n++;
  }
  if (usedOut) *usedOut = n;
  if (n == 0) return false;

  // Temperature and humidity are median-ed INDEPENDENTLY, so the result can be a
  // (t, h) pair that no single corner actually reported. Deliberate: they are two
  // different physical quantities, and the humidity outlier is rarely the same
  // corner as the temperature outlier.
  tempC = medianOf(temps, n);
  humidity = medianOf(hums, n);
  return true;
}

uint8_t onlineCount() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < MAX_ROOMS; i++) {
    if (isFresh(g_rooms[i])) n++;
  }
  return n;
}

uint8_t knownCount() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < MAX_ROOMS; i++) {
    if (g_rooms[i].used) n++;
  }
  return n;
}

const Room *at(uint8_t index) {
  if (index >= MAX_ROOMS || !g_rooms[index].used) return nullptr;
  return &g_rooms[index];
}

uint32_t ageSec(uint8_t index) {
  const Room *r = at(index);
  if (r == nullptr || r->lastHeardMs == 0) return 0;
  return (millis() - r->lastHeardMs) / 1000UL;
}

bool online(uint8_t index) {
  const Room *r = at(index);
  return r != nullptr && isFresh(*r);
}

}  // namespace RoomRegistry
