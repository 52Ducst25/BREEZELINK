#include "ir-store.h"
#include <Preferences.h>
#include <string.h>

namespace IrStore {

static Preferences prefs;
static bool ready = false;

/// FNV-1a 32-bit. NVS giới hạn TÊN KHOÁ 15 ký tự mà ir_code_id là UUID 36 ký
/// tự, nên không thể dùng thẳng id làm khoá — phải băm ngắn lại.
static uint32_t hash32(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) {
    h ^= (uint8_t)(*s++);
    h *= 16777619u;
  }
  return h;
}

/// Băm 32-bit CÓ THỂ đụng nhau. Nên mỗi mã chiếm 2 khoá: "u<băm>" giữ lại UUID
/// gốc để đối chiếu, "r<băm>" giữ mảng thời gian. Lúc đọc mà UUID không khớp
/// thì coi như chưa có — thà để backend gửi lại ir_raw còn hơn phát nhầm mã của
/// nhiệt độ khác.
static void makeKeys(const char *irCodeId, char *keyUuid, char *keyRaw) {
  uint32_t h = hash32(irCodeId);
  snprintf(keyUuid, 12, "u%08x", h);
  snprintf(keyRaw, 12, "r%08x", h);
}

bool begin() {
  ready = prefs.begin("aircon-ir", false /*read-write*/);
  return ready;
}

bool save(const char *irCodeId, const uint16_t *raw, uint16_t len) {
  if (!ready || irCodeId == nullptr || irCodeId[0] == '\0' || raw == nullptr || len == 0) {
    return false;
  }
  char keyUuid[12], keyRaw[12];
  makeKeys(irCodeId, keyUuid, keyRaw);

  // Ghi mảng TRƯỚC, ghi uuid SAU: uuid là thứ load() dùng để xác nhận "mã này
  // có thật". Ghi ngược lại, mà mất điện đúng giữa chừng, thì lần đọc sau thấy
  // uuid khớp nhưng mảng rỗng/cũ.
  if (prefs.putBytes(keyRaw, raw, (size_t)len * sizeof(uint16_t)) == 0) {
    Serial.println("[ir-store] ghi mang that bai — NVS day?");
    return false;
  }
  if (prefs.putString(keyUuid, irCodeId) == 0) {
    prefs.remove(keyRaw);   // dọn mảng mồ côi, khỏi chiếm chỗ vô ích
    Serial.println("[ir-store] ghi uuid that bai — NVS day?");
    return false;
  }
  return true;
}

uint16_t load(const char *irCodeId, uint16_t *out, uint16_t maxLen) {
  if (!ready || irCodeId == nullptr || irCodeId[0] == '\0' || out == nullptr) return 0;

  char keyUuid[12], keyRaw[12];
  makeKeys(irCodeId, keyUuid, keyRaw);

  if (prefs.getString(keyUuid, "") != irCodeId) return 0;   // chưa lưu, hoặc đụng băm

  size_t bytes = prefs.getBytesLength(keyRaw);
  if (bytes == 0 || bytes % sizeof(uint16_t) != 0) return 0;

  uint16_t len = (uint16_t)(bytes / sizeof(uint16_t));
  if (len > maxLen) {
    Serial.printf("[ir-store] ma %s dai %u moc, vuot bo dem %u — bo qua\n",
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
