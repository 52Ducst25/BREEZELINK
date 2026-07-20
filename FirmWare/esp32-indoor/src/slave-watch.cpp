#include "slave-watch.h"
#include <string.h>

namespace SlaveWatch {

struct Entry {
  char     uuid[33];
  uint32_t lastHeardMs;
  uint32_t lastRelayMs;   // lần cuối đẩy số đo của slave này lên cloud
  uint32_t lastStatusMs;  // lần cuối khẳng định trạng thái online
  bool     used;
  bool     online;
  bool     everRelayed;   // để gói ĐẦU TIÊN được đẩy ngay, không phải chờ 15s
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
  return nullptr;   // bảng đầy: slave thứ 9 trở đi không được theo dõi trạng thái
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
    e->online = false;      // để nhánh dưới báo "vừa lên online"
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
  // Gói đầu tiên sau khi master khởi động đẩy ngay: chờ 15s nữa mới có số đầu
  // tiên khiến người lắp tưởng hệ thống không chạy.
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
    // Trừ số học không dấu nên vẫn đúng khi millis() tràn (~49 ngày).
    if (now - e->lastHeardMs >= SLAVE_TIMEOUT_MS) {
      e->online = false;
      if (cb) cb(e->uuid, false);
    }
  }
}

} // namespace SlaveWatch
