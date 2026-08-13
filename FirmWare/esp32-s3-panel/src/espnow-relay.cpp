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
  //
  // acEspNowParse() nhận CẢ v1 (43 byte) lẫn v2 (45 byte) — node ngoài trời đã
  // lắp ngoài thực địa vẫn chạy v1 và firmware nó không có OTA, nên gateway phải
  // hiểu được nó. Gói v1 được điền node_kind = OUTDOOR; xem espnow-message.h.
  AcEspNowPacket parsed;
  if (!acEspNowParse(data, len, &parsed)) {
    // KÊU LÊN, ĐỪNG IM. Bản trước `return` thẳng, nên gói sai khuôn không vào bất
    // kỳ bộ đếm nào — và "nhan 0, rot 0" trông y hệt ca KHÔNG CÓ GÓI NÀO TỚI.
    // Hai ca đó cần đi tìm ở hai chỗ hoàn toàn khác nhau: một bên là sóng/kênh,
    // bên kia là khuôn gói lệch giữa hai firmware. Đã mất thời gian vì đúng chỗ
    // này một lần.
    //
    // In thưa dần: 3 gói đầu rồi mỗi 50 gói, để một node hàng xóm phát ESP-NOW
    // liên tục không nhấn chìm log.
    static uint32_t nBad = 0;
    if (++nBad <= 3 || nBad % 50 == 0) {
      Serial.printf("[espnow] bo goi LA: %d byte", len);
      if (mac) Serial.printf(" tu %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
                             mac[3], mac[4], mac[5]);
      if (len >= 2) Serial.printf(" · magic=0x%02X version=%u", data[0], data[1]);
      Serial.printf(" (mong doi %u hoac %u byte, magic=0x%02X)\n",
                    (unsigned)AC_ESPNOW_V1_SIZE, (unsigned)sizeof(AcEspNowPacket),
                    AC_ESPNOW_MAGIC);
    }
    return;
  }

  uint8_t next = (uint8_t)((head + 1) % QUEUE_SIZE);
  if (next == tail) {           // đầy — thà bỏ gói mới còn hơn ghi đè gói chưa gửi
    nDrop++;
    return;
  }
  queue[head].pkt = parsed;
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
  // ĐÃ THỬ VÀ KHÔNG PHẢI: khai thêm peer quảng bá ở phía nhận không làm callback
  // chạy. Nhận broadcast đúng là không cần peer — ghi lại ở đây để lần sau khỏi
  // thử lại cùng một thứ.
  return esp_now_register_recv_cb(onRecv) == ESP_OK;
}

void poll(Handler handler) {
  while (tail != head) {
    Slot s = queue[tail];
    tail = (uint8_t)((tail + 1) % QUEUE_SIZE);
    s.pkt.device_uuid[sizeof(s.pkt.device_uuid) - 1] = '\0';  // phòng gói thiếu '\0'
    handler(s.pkt, s.mac);
  }
}

uint32_t receivedCount() { return nRecv; }
uint32_t droppedCount()  { return nDrop; }

} // namespace EspNowRelay
