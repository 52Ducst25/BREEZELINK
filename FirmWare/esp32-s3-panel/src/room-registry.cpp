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
  // Trừ số học không dấu nên vẫn đúng khi millis() tràn (~49 ngày) — cùng luật
  // đã dùng ở SlaveWatch::checkTimeouts().
  return r.used && (millis() - r.lastHeardMs) < ROOM_STALE_MS;
}

/// Sắp xếp chèn tại chỗ. n ≤ 6 nên thuật toán O(n²) là lựa chọn đúng: qsort()
/// kéo theo con trỏ hàm và vài trăm byte mã cho một bài toán sáu phần tử.
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
  // Số chẵn -> trung bình hai giá trị giữa, để con số nhích mượt theo phòng ấm
  // dần thay vì nhảy bậc mỗi khi thứ tự hai góc giữa hoán đổi.
  return (n % 2 == 1) ? v[mid] : (v[mid - 1] + v[mid]) / 2.0f;
}

}  // namespace

bool update(const AcEspNowPacket &pkt) {
  Room *r = find(pkt.device_uuid);
  if (r == nullptr) {
    for (uint8_t i = 0; i < MAX_ROOMS; i++) {
      if (!g_rooms[i].used) { r = &g_rooms[i]; break; }
    }
    if (r == nullptr) return false;   // bảng đầy
    memset(r, 0, sizeof(*r));
    r->used = true;
    strncpy(r->uuid, pkt.device_uuid, sizeof(r->uuid) - 1);
  }

  r->corner      = pkt.corner;
  r->lastHeardMs = millis();
  // NaN được ghi vào có chủ đích: node còn sống nhưng cảm biến hỏng. Giữ lại số
  // đo cũ ở đây là để một góc đã tuột dây tiếp tục bỏ phiếu vào trung vị bằng
  // nhiệt độ của nửa giờ trước.
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

  // Nhiệt độ và độ ẩm lấy trung vị ĐỘC LẬP, nên kết quả có thể là một cặp
  // (t, h) mà không góc nào báo cáo đúng như vậy. Cố ý: đó là hai đại lượng vật
  // lý khác nhau và góc lạc về độ ẩm hiếm khi là góc lạc về nhiệt độ.
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
