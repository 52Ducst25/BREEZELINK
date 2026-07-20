#include "espnow-relay.h"
#include <WiFi.h>
#include <esp_now.h>
#include <string.h>

namespace EspNowRelay {

// Hàng đợi vòng: callback (tác vụ WiFi) ghi vào head, loop() đọc ra ở tail.
// Một người ghi + một người đọc nên không cần khoá. 8 chỗ là dư: mỗi slave chỉ
// gửi 15s/lần, còn loop() rút hàng mỗi vòng.
static const uint8_t QUEUE_SIZE = 8;

struct Slot {
  AcEspNowPacket pkt;
  uint8_t        mac[6];
};

static Slot             queue[QUEUE_SIZE];
static volatile uint8_t head = 0, tail = 0;
static volatile uint32_t nRecv = 0, nDrop = 0;

static void enqueue(const uint8_t *mac, const uint8_t *data, int len) {
  // Bỏ ngay gói không đúng khuôn: trên cùng một kênh có thể có ESP-NOW của hệ
  // khác, đọc bừa sẽ ra số rác rồi đẩy thẳng lên cloud.
  if (len != (int)sizeof(AcEspNowPacket)) return;
  const AcEspNowPacket *p = (const AcEspNowPacket *)data;
  if (p->magic != AC_ESPNOW_MAGIC || p->version != AC_ESPNOW_VERSION) return;

  uint8_t next = (uint8_t)((head + 1) % QUEUE_SIZE);
  if (next == tail) {           // đầy — thà bỏ gói mới còn hơn ghi đè gói chưa gửi
    nDrop++;
    return;
  }
  memcpy(&queue[head].pkt, data, sizeof(AcEspNowPacket));
  if (mac) memcpy(queue[head].mac, mac, 6);
  else     memset(queue[head].mac, 0, 6);
  head = next;
  nRecv++;
}

// Chữ ký callback đổi giữa Arduino-ESP32 core 2.x và 3.x — giữ cả hai để nâng
// cấp core không làm vỡ build.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  enqueue(info ? info->src_addr : nullptr, data, len);
}
#else
static void onRecv(const uint8_t *mac, const uint8_t *data, int len) {
  enqueue(mac, data, len);
}
#endif

bool begin() {
  if (esp_now_init() != ESP_OK) return false;
  return esp_now_register_recv_cb(onRecv) == ESP_OK;
}

void poll(Handler handler) {
  while (tail != head) {
    Slot s = queue[tail];
    tail = (uint8_t)((tail + 1) % QUEUE_SIZE);
    s.pkt.device_uuid[sizeof(s.pkt.device_uuid) - 1] = '\0';  // phòng gói thiếu '\0'
    handler(s.pkt.device_uuid, s.mac, s.pkt.temp, s.pkt.humidity);
  }
}

uint32_t receivedCount() { return nRecv; }
uint32_t droppedCount()  { return nDrop; }

} // namespace EspNowRelay
