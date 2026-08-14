#include "ir-slots.h"

#include <Preferences.h>
#include <nvs.h>

namespace IrSlots {
namespace {

Preferences g_prefs;
bool g_ready = false;

/// Khoá NVS của mảng thời gian. Tên ngắn hơn nhiều giới hạn 15 ký tự, nên
/// không cần băm gì cả — xem chú thích đầu ir-slots.h.
const char *rawKey(Slot s) { return s == Slot::ON ? "rON" : "rOFF"; }

/// Khoá của niềm tin trạng thái.
const char *const STATE_KEY = "on";

}  // namespace

const char *name(Slot s) { return s == Slot::ON ? "BAT" : "TAT"; }

bool begin() {
  g_ready = g_prefs.begin("bl-diffuser", false /*read-write*/);
  if (!g_ready) {
    Serial.println("[nvs] KHONG mo duoc namespace - ma IR se khong luu duoc!");
    return false;
  }

  // In sức chứa NGAY Ở BOOT. Khi NVS đầy thì save() trả false và MÃ MỚI BỊ MẤT
  // trong lúc mọi thứ khác vẫn chạy bình thường; không có con số này thì triệu
  // chứng duy nhất là "học xong mà máy vẫn không chạy", rất dễ đổ oan cho mắt
  // thu. (Cùng lý do panel in dòng tương tự — nhưng ở đây hai khung ngắn thì
  // đầy là chuyện gần như không xảy ra, in ra chủ yếu để loại trừ.)
  nvs_stats_t st;
  if (nvs_get_stats(nullptr, &st) == ESP_OK) {
    Serial.printf("[nvs] dung %u/%u o (con %u)\n", (unsigned)st.used_entries,
                  (unsigned)st.total_entries, (unsigned)st.free_entries);
  }
  return true;
}

bool save(Slot s, const uint16_t *raw, uint16_t len) {
  if (!g_ready || raw == nullptr || len == 0) return false;
  const size_t bytes = (size_t)len * sizeof(uint16_t);
  if (g_prefs.putBytes(rawKey(s), raw, bytes) != bytes) {
    Serial.println("[nvs] ghi ma that bai - NVS day?");
    return false;
  }
  return true;
}

uint16_t load(Slot s, uint16_t *out, uint16_t maxLen) {
  if (!g_ready || out == nullptr) return 0;
  const char *key = rawKey(s);

  // isKey() TRƯỚC. Preferences của Arduino-ESP32 coi "khoá không có" là LỖI và
  // in một dòng [E] cho MỖI lần trượt — mà trượt ở đây là chuyện bình thường
  // (bo chưa học gì, hoặc remote một nút nên ô TAT vĩnh viễn trống). Không bọc
  // thì log đỏ liên tục và chôn mất những dòng lỗi THẬT.
  if (!g_prefs.isKey(key)) return 0;

  const size_t bytes = g_prefs.getBytesLength(key);
  if (bytes == 0 || bytes % sizeof(uint16_t) != 0) return 0;

  const uint16_t len = (uint16_t)(bytes / sizeof(uint16_t));
  if (len > maxLen) {
    Serial.printf("[nvs] ma %s dai %u moc, vuot bo dem %u - bo qua\n", name(s),
                  len, maxLen);
    return 0;
  }
  if (g_prefs.getBytes(key, out, bytes) != bytes) return 0;
  return len;
}

bool has(Slot s) {
  if (!g_ready) return false;
  const char *key = rawKey(s);
  return g_prefs.isKey(key) && g_prefs.getBytesLength(key) > 0;
}

bool clear(Slot s) {
  if (!g_ready) return false;
  const char *key = rawKey(s);
  if (!g_prefs.isKey(key)) return false;
  return g_prefs.remove(key);
}

void wipe() {
  if (g_ready) g_prefs.clear();
}

void rememberOn(bool on) {
  if (!g_ready) return;
  g_prefs.putBool(STATE_KEY, on);
}

bool recallOn() {
  if (!g_ready) return false;
  // Mặc định false: bo mới tinh thì coi như máy đang TẮT. Đoán sai theo chiều
  // này chỉ khiến lệnh BẬT đầu tiên bị "lãng phí" một lần bấm bập bênh (máy
  // đang chạy sẽ tắt), và nhánh dwell sẽ sửa lại sau DWELL_SEC. Đoán ngược lại
  // (mặc định true) thì bo tin là máy đang chạy và có thể KHÔNG BẬT gì cả
  // trong khi phòng khô — hỏng câm, tệ hơn nhiều.
  return g_prefs.getBool(STATE_KEY, false);
}

}  // namespace IrSlots
