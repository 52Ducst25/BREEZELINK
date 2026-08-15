#include "ir-store.h"
#include <Preferences.h>
#include <nvs.h>
#include <string.h>

namespace IrStore {

static Preferences prefs;
static bool ready = false;

/// FNV-1a 32-bit. NVS limits a KEY NAME to 15 characters while an ir_code_id is a
/// 36-character UUID, so the id cannot be used directly as a key -- it has to be
/// hashed down.
static uint32_t hash32(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) {
    h ^= (uint8_t)(*s++);
    h *= 16777619u;
  }
  return h;
}

/// A 32-bit hash CAN collide. So each code occupies 2 keys: "u<hash>" keeps the
/// original UUID for verification, "r<hash>" keeps the timing array. If the UUID
/// does not match on read, treat it as absent -- better to make the backend resend
/// ir_raw than to transmit another temperature's code by mistake.
static void makeKeys(const char *irCodeId, char *keyUuid, char *keyRaw) {
  uint32_t h = hash32(irCodeId);
  snprintf(keyUuid, 12, "u%08x", h);
  snprintf(keyRaw, 12, "r%08x", h);
}

/// Read a string, returning "" if the key does not exist -- WITHOUT printing an
/// error.
///
/// This wrapper is necessary because Arduino-ESP32's Preferences treats "key not
/// present" as an ERROR and prints an [E] line for EVERY miss. And misses are
/// routine here: at startup the UI asks about all 15 COOL temperatures + DRY + FAN
/// + OFF to find out which have been learned, so a machine that has learned
/// nothing spews 18 red lines. The consequence is not just ugly -- it buries the
/// REAL error lines, and makes the user think the board is broken.
static String getStr(const char *key) {
  if (!prefs.isKey(key)) return String();
  return prefs.getString(key, "");
}

bool begin() {
  ready = prefs.begin("aircon-ir", false /*read-write*/);

  // Print the capacity RIGHT AT BOOT. The NVS partition is only 20 KB
  // (huge_app.csv: nvs 0x5000) while one air conditioner frame is ~600 bytes, so
  // "full" is a real outcome and not a hypothesis -- and once full, save() returns
  // false and NEW CODES ARE LOST while everything else carries on normally.
  // Without this number the only symptom is "learned it and the button is still
  // dimmed", which is very easy to blame on the receiver or the backend.
  nvs_stats_t st;
  if (nvs_get_stats(nullptr, &st) == ESP_OK) {
    Serial.printf("NVS: using %u/%u entries (%u free) - holding %u IR codes\n",
                  (unsigned)st.used_entries, (unsigned)st.total_entries,
                  (unsigned)st.free_entries, (unsigned)count());
  }
  return ready;
}

/// The alias key: "a" + mode + temperature. The longest, "aCOOL30", is 7
/// characters, comfortably inside NVS's limit of 15. temp < 0 -> a fixed code
/// (DRY/FAN/OFF), with the number omitted.
static void makeAliasKey(const char *mode, int temp, char *out, size_t n) {
  if (temp >= 0) snprintf(out, n, "a%s%d", mode, temp);
  else           snprintf(out, n, "a%s", mode);
}

bool save(const char *irCodeId, const uint16_t *raw, uint16_t len) {
  if (!ready || irCodeId == nullptr || irCodeId[0] == '\0' || raw == nullptr || len == 0) {
    return false;
  }
  char keyUuid[12], keyRaw[12];
  makeKeys(irCodeId, keyUuid, keyRaw);
  const bool isNew = (getStr(keyUuid) != irCodeId);

  // Write the array FIRST and the uuid SECOND: the uuid is what load() uses to
  // confirm "this code really exists". The other order, with a power cut in
  // between, would leave the next read finding a matching uuid with an empty or
  // stale array.
  if (prefs.putBytes(keyRaw, raw, (size_t)len * sizeof(uint16_t)) == 0) {
    Serial.println("[ir-store] failed to write the array - NVS full?");
    return false;
  }
  if (prefs.putString(keyUuid, irCodeId) == 0) {
    prefs.remove(keyRaw);   // clean up the orphaned array so it does not waste space
    Serial.println("[ir-store] failed to write the uuid - NVS full?");
    return false;
  }
  // Count separately rather than enumerating the namespace: Preferences has no
  // key-listing API, and the THONG TIN screen only needs one number so the
  // installer can tell whether the node has any codes yet.
  if (isNew) prefs.putUShort("cnt", (uint16_t)(prefs.getUShort("cnt", 0) + 1));
  return true;
}

uint16_t count() {
  return ready ? prefs.getUShort("cnt", 0) : 0;
}

/// The prefix of an id THE NODE ITSELF generates when it learns a code, to
/// distinguish it from a backend UUID. A UUID never begins with this string (it
/// contains only hex digits and dashes), so a prefix comparison is enough.
static const char *const LOCAL_PREFIX = "local-";

/// Delete both of a code's keys and decrement the counter. Only used to clean up
/// TEMPORARY codes -- for a backend code, upsert_learned_code keeps the same UUID
/// when relearning so it overwrites in place and creates no garbage.
static void removeBlob(const char *irCodeId) {
  char keyUuid[12], keyRaw[12];
  makeKeys(irCodeId, keyUuid, keyRaw);
  if (!prefs.isKey(keyUuid)) return;
  prefs.remove(keyRaw);
  prefs.remove(keyUuid);
  const uint16_t c = prefs.getUShort("cnt", 0);
  if (c > 0) prefs.putUShort("cnt", (uint16_t)(c - 1));
}

bool saveAlias(const char *mode, int temp, const char *irCodeId) {
  if (!ready || mode == nullptr || mode[0] == '\0' || irCodeId == nullptr || irCodeId[0] == '\0') {
    return false;
  }
  char key[16];
  makeAliasKey(mode, temp, key, sizeof(key));
  const String old = getStr(key);
  if (old == irCodeId) return true;   // avoid wearing the flash for nothing
  if (prefs.putString(key, irCodeId) == 0) return false;

  // The alias has just moved away from a TEMPORARY code -> that code is now
  // orphaned with nothing pointing at it. Clean it up immediately: NVS here is
  // only 20 KB (0x5000) while one air conditioner frame is already ~600 bytes, so
  // keeping both a temporary and a real copy of all 18 required combinations would
  // overflow it. Once NVS overflows, save() returns false and NEW codes are lost --
  // a failure in the least expected place.
  if (old.length() > 0 && old.startsWith(LOCAL_PREFIX)) removeBlob(old.c_str());
  return true;
}

/// The temporary id: "local-" + mode + temperature. Only ever used as a VALUE (it
/// gets hashed into a key), so it is not bound by NVS's 15-character key-name
/// limit.
static void makeLocalId(const char *mode, int temp, char *out, size_t n) {
  if (temp >= 0) snprintf(out, n, "%s%s-%d", LOCAL_PREFIX, mode, temp);
  else           snprintf(out, n, "%s%s", LOCAL_PREFIX, mode);
}

bool saveLearned(const char *mode, int temp, const uint16_t *raw, uint16_t len) {
  if (!ready || mode == nullptr || mode[0] == '\0') return false;
  char id[32];
  makeLocalId(mode, temp, id, sizeof(id));
  if (!save(id, raw, len)) return false;
  return saveAlias(mode, temp, id);
}

bool hasAlias(const char *mode, int temp) {
  if (!ready || mode == nullptr || mode[0] == '\0') return false;
  char key[16];
  makeAliasKey(mode, temp, key, sizeof(key));
  const String id = getStr(key);
  if (id.length() == 0) return false;

  // The alias points at an id, but the timing array is what can actually be
  // transmitted. The two can diverge (NVS full while writing the array, or a hash
  // collision) -- checking both is what makes the on-screen button reflect the real
  // question, "does pressing this produce a command".
  char keyUuid[12], keyRaw[12];
  makeKeys(id.c_str(), keyUuid, keyRaw);
  return getStr(keyUuid) == id && prefs.getBytesLength(keyRaw) > 0;
}

bool removeAlias(const char *mode, int temp) {
  if (!ready || mode == nullptr || mode[0] == '\0') return false;
  char key[16];
  makeAliasKey(mode, temp, key, sizeof(key));
  const String id = getStr(key);
  if (id.length() == 0) return false;   // never learned -- nothing to delete

  // Delete the ARRAY FIRST and the ALIAS SECOND -- the reverse of save()'s write
  // order, and for a reason: a power cut partway through here leaves an alias
  // pointing at an empty array, and since hasAlias() checks both it still
  // correctly reports "no code". The other order would orphan the array,
  // occupying NVS space with nothing pointing at it and no way to clean it up.
  removeBlob(id.c_str());
  prefs.remove(key);
  return true;
}

uint16_t loadAlias(const char *mode, int temp, uint16_t *out, uint16_t maxLen) {
  if (!ready || mode == nullptr || mode[0] == '\0' || out == nullptr) return 0;
  char key[16];
  makeAliasKey(mode, temp, key, sizeof(key));
  const String id = getStr(key);
  if (id.length() == 0) return 0;
  return load(id.c_str(), out, maxLen);
}

uint16_t load(const char *irCodeId, uint16_t *out, uint16_t maxLen) {
  if (!ready || irCodeId == nullptr || irCodeId[0] == '\0' || out == nullptr) return 0;

  char keyUuid[12], keyRaw[12];
  makeKeys(irCodeId, keyUuid, keyRaw);

  if (getStr(keyUuid) != irCodeId) return 0;   // never stored, or a hash collision

  size_t bytes = prefs.getBytesLength(keyRaw);
  if (bytes == 0 || bytes % sizeof(uint16_t) != 0) return 0;

  uint16_t len = (uint16_t)(bytes / sizeof(uint16_t));
  if (len > maxLen) {
    Serial.printf("[ir-store] code %s is %u transitions, exceeding the %u buffer - discarded\n",
                  irCodeId, len, maxLen);
    return 0;
  }
  if (prefs.getBytes(keyRaw, out, bytes) != bytes) return 0;
  return len;
}

void wipe() {
  if (ready) prefs.clear();
}

} // namespace IrStore
