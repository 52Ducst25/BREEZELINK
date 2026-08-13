#include "ir-store.h"
#include <Preferences.h>
#include <nvs.h>
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

/// Đọc chuỗi, trả "" nếu khoá chưa tồn tại — KHÔNG in lỗi.
///
/// Phải bọc lại vì Preferences của Arduino-ESP32 coi "khoá không có" là LỖI và
/// in ra một dòng [E] cho MỖI lần trượt. Mà trượt ở đây là chuyện bình thường:
/// lúc khởi động, giao diện hỏi cả 15 mức nhiệt COOL + DRY + FAN + OFF xem cái
/// nào đã học, nên máy chưa học gì là log phun ra 18 dòng đỏ. Hậu quả không chỉ
/// xấu — nó chôn mất những dòng lỗi THẬT, và làm người dùng tưởng bo hỏng.
static String getStr(const char *key) {
  if (!prefs.isKey(key)) return String();
  return prefs.getString(key, "");
}

bool begin() {
  ready = prefs.begin("aircon-ir", false /*read-write*/);

  // In sức chứa NGAY Ở BOOT. Phân vùng NVS chỉ 20 KB (huge_app.csv: nvs 0x5000)
  // mà một khung điều hoà ~600 byte, nên "đầy" là kết cục có thật chứ không phải
  // giả thuyết — và khi đầy thì save() trả false, MÃ MỚI BỊ MẤT trong lúc mọi
  // thứ khác vẫn chạy bình thường. Không có con số này thì triệu chứng duy nhất
  // là "học xong mà nút vẫn mờ", rất dễ đổ oan cho mắt thu hay cho backend.
  nvs_stats_t st;
  if (nvs_get_stats(nullptr, &st) == ESP_OK) {
    Serial.printf("NVS: dung %u/%u o (con %u) · dang giu %u ma IR\n",
                  (unsigned)st.used_entries, (unsigned)st.total_entries,
                  (unsigned)st.free_entries, (unsigned)count());
  }
  return ready;
}

/// Khoá bí danh: "a" + mode + nhiệt độ. Dài nhất "aCOOL30" = 7 ký tự, thừa chỗ
/// trong giới hạn 15 của NVS. temp < 0 -> mã cố định (DRY/FAN/OFF), bỏ số.
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
  // Đếm riêng thay vì duyệt namespace: Preferences không có API liệt kê khoá,
  // mà màn THONG TIN chỉ cần một con số để người lắp biết "node đã có mã chưa".
  if (isNew) prefs.putUShort("cnt", (uint16_t)(prefs.getUShort("cnt", 0) + 1));
  return true;
}

uint16_t count() {
  return ready ? prefs.getUShort("cnt", 0) : 0;
}

/// Tiền tố của id do CHÍNH NODE sinh khi tự học, phân biệt với UUID của backend.
/// UUID không bao giờ bắt đầu bằng chuỗi này (nó chỉ gồm hex và dấu gạch), nên
/// so tiền tố là đủ để nhận ra.
static const char *const LOCAL_PREFIX = "local-";

/// Xoá cả hai khoá của một mã và trừ bộ đếm. Chỉ dùng để dọn mã TẠM — mã của
/// backend thì upsert_learned_code giữ nguyên UUID khi học lại nên ghi đè tại
/// chỗ, không sinh rác.
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
  if (old == irCodeId) return true;   // khỏi mòn flash vô ích
  if (prefs.putString(key, irCodeId) == 0) return false;

  // Bí danh vừa rời khỏi một mã TẠM -> mã đó thành mồ côi, không ai trỏ tới nữa.
  // Dọn ngay: NVS ở đây chỉ có 20 KB (0x5000) mà một khung điều hoà đã ~600 byte,
  // giữ cả bản tạm lẫn bản thật cho 18 tổ hợp bắt buộc là tràn. Tràn NVS thì
  // save() trả false và mã MỚI bị mất — hỏng ở phía khó ngờ nhất.
  if (old.length() > 0 && old.startsWith(LOCAL_PREFIX)) removeBlob(old.c_str());
  return true;
}

/// Id tạm: "local-" + mode + nhiệt độ. Chỉ dùng làm GIÁ TRỊ (bị băm lại thành
/// khoá), nên không vướng giới hạn 15 ký tự của tên khoá NVS.
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

  // Bí danh trỏ tới id, nhưng mảng thời gian mới là thứ phát được. Hai thứ có
  // thể lệch nhau (NVS đầy lúc ghi mảng, hoặc băm đụng) — kiểm cả hai thì nút
  // trên màn mới phản ánh đúng "bấm vào có ra lệnh không".
  char keyUuid[12], keyRaw[12];
  makeKeys(id.c_str(), keyUuid, keyRaw);
  return getStr(keyUuid) == id && prefs.getBytesLength(keyRaw) > 0;
}

bool removeAlias(const char *mode, int temp) {
  if (!ready || mode == nullptr || mode[0] == '\0') return false;
  char key[16];
  makeAliasKey(mode, temp, key, sizeof(key));
  const String id = getStr(key);
  if (id.length() == 0) return false;   // vốn chưa học — không có gì để xoá

  // Xoá MẢNG TRƯỚC, BÍ DANH SAU — ngược với thứ tự ghi của save(), và có lý do:
  // mất điện giữa chừng ở đây thì còn lại một bí danh trỏ vào mảng rỗng, mà
  // hasAlias() kiểm cả hai nên vẫn báo đúng "chưa có mã". Làm ngược lại thì
  // mảng thành mồ côi, chiếm chỗ NVS mà không ai trỏ tới và không ai dọn nổi.
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

  if (getStr(keyUuid) != irCodeId) return 0;   // chưa lưu, hoặc đụng băm

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
