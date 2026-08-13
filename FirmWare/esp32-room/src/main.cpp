// ============================================================================
//  BreezeLink — ESP32-C3-DevKitM-1 · node CẢM BIẾN GÓC PHÒNG
// ----------------------------------------------------------------------------
//  Bốn bo như thế này đặt ở bốn góc một phòng. Mỗi bo làm đúng một việc: đọc
//  DHT22 rồi bắn gói ESP-NOW về gateway đặt gần máy lạnh.
//
//  KHÔNG có WiFi, KHÔNG có MQTT, KHÔNG có credential nào ở đây. Gateway đứng tên
//  publish hộ, đúng khuôn đã dùng cho node ngoài trời — và toàn bộ phần radio
//  dùng chung một file với nó (../../shared/espnow-slave-radio.h).
//
//  VÌ SAO ESP-NOW CHỨ KHÔNG PHẢI BLE:
//    gói ESP-NOW chở được 250 byte nên nó mang thẳng device_uuid 32 ký tự của
//    chính node — gateway cứ thế publish vào topic của node đó mà không cần bảng
//    tra nào. Gói BLE advertising cổ điển chỉ có 31 byte, chở không nổi uuid, nên
//    sẽ buộc gateway phải giữ một bảng id->uuid và NẠP LẠI mỗi lần thêm/bớt node
//    phòng. Bluetooth trong hệ này dành cho đường gateway <-> Arduino UNO Q, nơi
//    hai bên có kết nối GATT thật và không bị trần 31 byte.
//
//  BỐN BO CÓ CÙNG SỐ GÓC LÀ VÔ HẠI: `corner` chỉ là nhãn hiển thị trên màn tại
//  chỗ, còn định danh thật là DEVICE_UUID — vốn đã khác nhau vì mỗi bo lấy một
//  hàng devices riêng trên web. Trùng nhãn thì màn ghi "góc 1" hai lần, không
//  mất số đo của ai cả.
// ============================================================================
#include <Arduino.h>

#include "config.h"
#include "espnow-message.h"
#include "espnow-slave-radio.h"
#include "room-sensor.h"

/// Chu kỳ gửi (ms). KHÁC với nhịp đọc cảm biến (2.5s): đọc dày để lọc nhiễu,
/// gửi thưa hơn vì nhiệt độ phòng không đổi trong vài giây và mỗi gói gửi đi là
/// một hàng telemetry trong Postgres.
///
/// 15s khớp TELEMETRY_MS của gateway.
///
/// Cho phép ép nhanh hơn lúc truy lỗi bằng `-D ROOM_PUBLISH_MS=3000`: đo mất gói
/// ở nhịp 15s cần hàng phút mới đủ mẫu, mà lúc đang dò sóng thì chờ lâu là mất
/// hẳn khả năng thử-sai. KHÔNG dùng nhịp nhanh khi chạy thật — mỗi gói là một
/// hàng telemetry trong Postgres.
///
/// 5 GIÂY, KHÔNG PHẢI 15. Đây là NHỊP TIM, không phải nhịp ghi dữ liệu:
/// gateway đã tự chặn việc đẩy lên cloud ở 15s (SlaveWatch::RELAY_INTERVAL_MS),
/// nên phát dày hơn KHÔNG sinh thêm một hàng telemetry nào trong Postgres.
///
/// Vì sao phải dày: broadcast ESP-NOW không có ACK nên gói rơi là mất hẳn, và
/// gateway phải chia sóng 2.4GHz với WiFi + Bluetooth nên tỉ lệ rơi là đáng kể
/// (đo được: mất khoảng hai phần ba số gói khi BLE bật). Với nhịp 15s và ngưỡng
/// mất kết nối 20s của SlaveWatch, chỉ cần RƠI MỘT GÓI là node bị báo mất kết
/// nối — đo trên bàn thì bốn góc nhấp nháy online/offline liên tục. 5s cho phép
/// rơi ba nhịp liên tiếp mà vẫn được coi là còn sống.
///
/// Con số này khớp với giả định đã ghi sẵn trong slave-watch.h ("nhịp tim dày,
/// ngưỡng rộng"); bản 15s trước đây là chỗ lệch khỏi thiết kế đó.
#ifndef ROOM_PUBLISH_MS
#define ROOM_PUBLISH_MS 5000UL
#endif
static const unsigned long PUBLISH_PERIOD_MS = ROOM_PUBLISH_MS;

/// Nhịp in dòng tổng kết ra serial. Thưa hơn nhịp gửi nhiều: bo này chạy hàng
/// tháng không ai cắm cáp, log dày chỉ tổ trôi mất dòng đáng đọc lúc truy lỗi.
static const unsigned long LOG_PERIOD_MS = 60000UL;

static unsigned long lastSend = 0;
static unsigned long lastLog = 0;
static uint32_t      sentCount = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n== BreezeLink · ESP32-C3 · CAM BIEN GOC PHONG (ESP-NOW) ==");
  Serial.printf("goc=%d · DHT22 @GPIO%d · fw=%s\n", ROOM_CORNER, DHT_PIN, FW_VERSION);
  Serial.printf("uuid=%s\n", DEVICE_UUID);

  RoomSensor::begin(DHT_PIN);

  if (!EspNowSlaveRadio::begin(WIFI_SSID)) {
    Serial.println("esp_now_init THAT BAI — khoi dong lai");
    delay(2000);
    ESP.restart();
  }
}

void loop() {
  RoomSensor::poll();
  EspNowSlaveRadio::tickRescan(WIFI_SSID);

  const unsigned long now = millis();

  if (lastSend == 0 || now - lastSend >= PUBLISH_PERIOD_MS) {
    lastSend = now;

    float t = NAN, h = NAN;
    // read() trả false khi cảm biến đã trượt đủ nhiều lần LIÊN TIẾP. Vẫn gửi gói
    // (t/h giữ NaN) chứ không bỏ qua: gói không có số đo VẪN là một nhịp tim, và
    // gateway phân biệt được "góc này chết" với "góc này còn sống nhưng cảm biến
    // hỏng". Gộp hai chuyện đó lại là gửi người đi kiểm tra sai thứ.
    const bool ok = RoomSensor::read(t, h);

    AcEspNowPacket pkt;
    acEspNowFill(&pkt, DEVICE_UUID, t, h, AC_NODE_ROOM, (uint8_t)ROOM_CORNER);
    const bool sent = EspNowSlaveRadio::broadcast(&pkt, sizeof(pkt));
    if (sent) sentCount++;

    if (!ok) {
      Serial.printf("[room] chua co so do (%u lan truot lien tiep) — kiem tra day DHT22 "
                    "tren GPIO%d va tro keo 4.7k len 3.3V\n",
                    RoomSensor::consecutiveFailures(), DHT_PIN);
    }
  }

#if defined(ESPNOW_SNIFF) && ESPNOW_SNIFF
  // Báo cáo NGHE NGÓNG mỗi 5s, tách khỏi nhịp log 60s: lúc đang dò sóng thì chờ
  // một phút cho mỗi lần thử là hết kiên nhẫn trước khi hết giả thuyết.
  {
    static unsigned long lastSniffLog = 0;
    static uint32_t      lastSniffCount = 0;
    if (now - lastSniffLog >= 5000UL) {
      lastSniffLog = now;
      const uint32_t c = EspNowSlaveRadio::sniffed();
      Serial.printf("  <nghe> tong %lu goi (+%lu tu 5s truoc) · gan nhat [%s] · kenh that=%d\n",
                    (unsigned long)c, (unsigned long)(c - lastSniffCount),
                    EspNowSlaveRadio::sniffedLast(), EspNowSlaveRadio::channel());
      lastSniffCount = c;
    }
  }
#endif

  if (now - lastLog >= LOG_PERIOD_MS) {
    lastLog = now;
    float t = NAN, h = NAN;
    RoomSensor::read(t, h);
    // "da phat" KHÔNG có nghĩa là gateway đã nhận: broadcast không có ACK. Muốn
    // biết chắc thì xem log gateway hoặc xem web có số của node này không.
    Serial.printf("[room goc %d] t=%.1f h=%.0f · da phat %lu goi · kenh=%d · up=%lus",
                  ROOM_CORNER, t, h, (unsigned long)sentCount,
                  EspNowSlaveRadio::channel(), now / 1000UL);
#if defined(ESPNOW_SNIFF) && ESPNOW_SNIFF
    // Chỉ có khi build kèm -D ESPNOW_SNIFF=1 (xem espnow-slave-radio.h).
    Serial.printf(" · NGHE duoc %lu goi tu node khac [%s]",
                  (unsigned long)EspNowSlaveRadio::sniffed(),
                  EspNowSlaveRadio::sniffedLast());
#endif
    Serial.println();
  }

  delay(10);   // nhường CPU — ESP32-C3 chỉ có MỘT lõi
}
