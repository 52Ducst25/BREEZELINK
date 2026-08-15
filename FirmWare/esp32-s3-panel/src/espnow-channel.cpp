#include "espnow-channel.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>

namespace EspNowChannel {

// Namespace NVS RIÊNG, không dùng ké "aircon-ir" của IrStore: kho mã IR có lúc
// đầy (phân vùng nvs chỉ 20 KB) và khi đầy thì mọi lệnh ghi vào namespace đó
// trượt. Số kênh mà mất theo mã IR thì panel mù kênh chỉ vì người dùng học thêm
// một cái remote — hai chuyện không liên quan gì đến nhau.
static Preferences prefs;
static bool    ready    = false;
static uint8_t current  = FALLBACK_CHANNEL;
static bool    isPinned = false;

/// Số kênh có nằm trong dải 2.4 GHz hợp lệ không.
///
/// KIỂM Ở MỌI ĐƯỜNG VÀO. `WiFi.channel()` trả 0 khi chưa biết, và ghi số 0 vào
/// NVS thì lần boot sau panel bám "kênh 0" — esp_wifi_set_channel() từ chối, hàm
/// trả lỗi, và panel đứng ở đâu đó không ai biết.
static bool valid(int ch) { return ch >= 1 && ch <= 14; }

void begin() {
  ready = prefs.begin("bl-radio", false /*read-write*/);
  if (!ready) {
    Serial.printf("[kenh] khong mo duoc NVS — se dung kenh %u va KHONG nho duoc\n",
                  FALLBACK_CHANNEL);
    return;
  }
  const int saved = prefs.getUChar("ch", 0);
  if (valid(saved)) {
    current = (uint8_t)saved;
    Serial.printf("[kenh] nho tu lan truoc: kenh %u\n", current);
  } else {
    // Chưa từng thấy router. Nói rõ đây là PHỎNG ĐOÁN chứ không phải số đo —
    // nếu node đã bám kênh khác thì đây chính là dòng giải thích vì sao câm.
    Serial.printf("[kenh] chua tung thay router — tam dung kenh %u (nhu node luc boot)\n",
                  FALLBACK_CHANNEL);
  }
}

void note(uint8_t channel) {
  if (!valid(channel)) return;
  if (channel == current) return;

  const uint8_t before = current;
  current = channel;
  if (ready) prefs.putUChar("ch", current);
  Serial.printf("[kenh] router o kenh %u (truoc la %u) — da ghi nho\n", current, before);
}

uint8_t last() { return current; }

bool park() {
  const esp_err_t err = esp_wifi_set_channel(current, WIFI_SECOND_CHAN_NONE);
  if (err != ESP_OK) {
    Serial.printf("[kenh] KHONG ghim duoc kenh %u: %s\n", current, esp_err_to_name(err));
    return false;
  }

  // ĐỌC NGƯỢC TỪ PHẦN CỨNG, đừng tin lệnh vừa gửi. Cùng lý do đã ghi ở
  // EspNowSlaveRadio::channel(): một biến nội bộ nói "kênh 6" trong khi radio
  // nằm ở chỗ khác là kiểu log tự tin mà dối, và nó làm người đọc loại trừ nhầm
  // đúng cái nguyên nhân thật.
  uint8_t primary = 0;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&primary, &second);

  const bool wasPinned = isPinned;
  isPinned = true;
  // In MỘT dòng cho mỗi lần CHUYỂN sang trạng thái ghim, không in mỗi lần bám
  // lại: hàm này được gọi sau mỗi lần thử nối trượt, mà mất mạng qua đêm là
  // hàng trăm lần — đủ để chôn mọi dòng log khác.
  if (!wasPinned || primary != current) {
    Serial.printf("[kenh] mat WiFi -> ghim kenh %u (radio that o kenh %u) · "
                  "ESP-NOW van thu binh thuong\n", current, primary);
  }
  return primary == current;
}

void release() {
  if (!isPinned) return;
  isPinned = false;
  Serial.println("[kenh] WiFi da noi lai -> thoi ghim, router giu kenh ho");
}

bool pinned() { return isPinned; }

void hold() {
  if (!isPinned) return;

  uint8_t primary = 0;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  if (esp_wifi_get_channel(&primary, &second) != ESP_OK) return;
  if (primary == current) return;

  // In MỌI lần trôi, không giới hạn tần suất: đây không phải chuyện bình thường.
  // Nếu dòng này lặp đi lặp lại thì có một chỗ khác đang lái radio, và biết được
  // nó xảy ra bao nhiêu lần mỗi phút chính là manh mối để tìm ra chỗ đó.
  Serial.printf("[kenh] radio troi sang kenh %u — keo ve %u\n", primary, current);
  esp_wifi_set_channel(current, WIFI_SECOND_CHAN_NONE);
}

bool rescan(const char *ssid) {
  if (ssid == nullptr || ssid[0] == '\0') return false;

  const int n = WiFi.scanNetworks(false /*async*/, true /*show hidden*/);
  int found = 0;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == ssid) { found = WiFi.channel(i); break; }
  }
  WiFi.scanDelete();

  if (valid(found)) {
    note((uint8_t)found);
  } else {
    Serial.printf("[kenh] do lai: khong thay \"%s\" — giu nguyen kenh %u (nhu node)\n",
                  ssid, current);
  }

  // BÁM LẠI VÔ ĐIỀU KIỆN — kể cả khi kênh không đổi, kể cả khi quét trượt. Xem
  // chú thích của hàm này trong espnow-channel.h: quét xong radio đang nằm ở
  // kênh cuối cùng nó dừng, không phải chỗ ta muốn.
  //
  // Ép in lại một dòng bằng cách hạ cờ trước: sau một lần quét thì việc panel
  // quay về đúng kênh nào là tin đáng ghi, không phải nhiễu.
  isPinned = false;
  park();
  return valid(found);
}

} // namespace EspNowChannel
