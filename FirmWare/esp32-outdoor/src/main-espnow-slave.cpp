// ============================================================================
//  BreezeLink — ESP32 · node NGOÀI TRỜI · vai trò SLAVE (ESP-NOW)
// ----------------------------------------------------------------------------
//  KHÔNG dùng WiFi/MQTT. Đọc DHT rồi bắn gói ESP-NOW về node trong nhà; node đó
//  mới là node chuyển tiếp lên cloud theo topic riêng của node này.
//
//  Vì sao gửi BROADCAST thay vì unicast tới MAC của master:
//    lắp đặt không phải đi lấy MAC master rồi nạp vào đây — cứ bật là chạy.
//    Trong một hộ chỉ có một master nên không sợ nhầm địa chỉ.
//
//  Bẫy lớn nhất của ESP-NOW là LỆCH KÊNH: hai bên bắt buộc cùng kênh WiFi, mà
//  master thì đang bám theo kênh của router. Nên node này KHÔNG cắm cứng kênh —
//  nó quét tìm SSID của nhà để biết router đang ở kênh nào rồi bám theo. Router
//  đổi kênh thì lần quét sau tự cập nhật.
//
//  Bản dự phòng nối WiFi/MQTT thẳng vẫn còn ở main-wifi-direct.cpp:
//      pio run -e esp32-wifi -t upload
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>          // esp_wifi_set_channel()
#include <esp_idf_version.h>
#include <DHT.h>
#include "config.h"
#include "espnow-message.h"

static DHT dht(DHT_PIN, DHT_TYPE);

// Địa chỉ quảng bá — mọi node trong tầm sóng, cùng kênh, đều nhận được.
static uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static int  currentChannel = 0;
static bool lastSendOk     = false;

/// Chu kỳ dò lại kênh router. 5 phút: đủ nhanh để bắt kịp router đổi kênh,
/// đủ thưa để việc quét (làm radio bận ~1s) không ảnh hưởng nhịp tim 5s.
static const unsigned long RESCAN_INTERVAL_MS = 300000UL;

/// Quét các mạng quanh đây để biết router nhà đang phát ở kênh nào.
/// Chỉ quét, KHÔNG đăng nhập — node này không cần mật khẩu WiFi.
/// Trả 0 nếu không thấy SSID (router tắt / ngoài vùng phủ).
static int findRouterChannel() {
  int n = WiFi.scanNetworks(false /*async*/, true /*show hidden*/);
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == WIFI_SSID) {
      int ch = WiFi.channel(i);
      WiFi.scanDelete();
      return ch;
    }
  }
  WiFi.scanDelete();
  return 0;
}

// Chữ ký callback GỬI đổi ở Arduino-ESP32 core 3.2 (IDF 5.4): tham số đầu từ
// `const uint8_t *mac` thành `const wifi_tx_info_t *`. Phải dò theo ESP_IDF_VERSION
// chứ KHÔNG dùng được ESP_ARDUINO_VERSION_MAJOR >= 3 như callback NHẬN bên
// espnow-relay.cpp của node trong nhà: hai callback đổi ở hai mốc phiên bản
// khác nhau (nhận đổi từ core 3.0, gửi mãi core 3.2).
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
static void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  lastSendOk = (status == ESP_NOW_SEND_SUCCESS);
}
#else
static void onSent(const uint8_t *mac, esp_now_send_status_t status) {
  lastSendOk = (status == ESP_NOW_SEND_SUCCESS);
}
#endif

/// Bám vào kênh `ch` và đăng ký peer quảng bá trên kênh đó.
static bool bindToChannel(int ch) {
  if (ch <= 0) return false;

  // WIFI_SECOND_CHAN_NONE: chỉ dùng kênh 20MHz. ESP-NOW không cần kênh phụ, mà
  // khai kênh phụ lệch với master là hỏng việc.
  esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);

  // Bỏ peer cũ nếu đang ở kênh khác. Peer chưa tồn tại thì hàm trả
  // ESP_ERR_ESPNOW_NOT_FOUND — vô hại, bỏ qua giá trị trả về.
  esp_now_del_peer(BROADCAST_MAC);

  // ESP32 khai peer bằng struct (khác ESP8266 truyền rời từng tham số) và KHÔNG
  // có khái niệm "role" — bỏ hẳn esp_now_set_self_role() của bản ESP8266.
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST_MAC, 6);
  peer.channel = (uint8_t)ch;
  peer.encrypt = false;
  peer.ifidx   = WIFI_IF_STA;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Khong them duoc peer ESP-NOW");
    return false;
  }
  currentChannel = ch;
  Serial.printf("ESP-NOW bam kenh %d\n", ch);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n== BreezeLink · ESP32 · NGOAI TROI (SLAVE / ESP-NOW) ==");
  dht.begin();

  // STA nhưng KHÔNG connect: ESP-NOW cần giao diện station bật, không cần vào mạng.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Tắt tiết kiệm điện của modem. Mặc định ESP32 cho radio ngủ giữa các beacon,
  // mà đang ngủ thì gói ESP-NOW gửi đi bị hoãn/rơi — cùng lỗi "chết ngầm" đã
  // gặp ở node master (xem git log: "master điếc ESP-NOW sau vài phút").
  WiFi.setSleep(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("esp_now_init THAT BAI — khoi dong lai");
    delay(2000);
    ESP.restart();
  }
  esp_now_register_send_cb(onSent);

  // Quét vài lần: lần quét đầu ngay sau khi bật radio hay trượt, mà bám sai kênh
  // thì gói bay vào khoảng không và KHÔNG có cách nào biết (broadcast không ACK).
  int ch = 0;
  for (uint8_t attempt = 1; attempt <= 3 && ch == 0; attempt++) {
    delay(300);
    ch = findRouterChannel();
    if (ch == 0) Serial.printf("Lan quet %u: chua thay \"%s\"\n", attempt, WIFI_SSID);
  }
  if (ch == 0) {
    Serial.printf("Khong thay SSID \"%s\" — tam kenh 1, se do lai moi %lus\n",
                  WIFI_SSID, RESCAN_INTERVAL_MS / 1000);
    ch = 1;
  }
  bindToChannel(ch);
}

static unsigned long lastSend   = 0;
static unsigned long lastRescan = 0;

void loop() {
  unsigned long now = millis();

  // Dò lại kênh ĐỊNH KỲ thay vì dựa vào lỗi gửi: gói broadcast không có ACK nên
  // callback luôn báo thành công kể cả khi bắn sai kênh, chẳng ai phát hiện ra.
  // Quét lại cũng là cách bắt kịp khi router tự đổi kênh.
  if (now - lastRescan >= RESCAN_INTERVAL_MS) {
    lastRescan = now;
    int ch = findRouterChannel();
    if (ch <= 0) ch = currentChannel;   // quét trượt -> giữ nguyên kênh đang dùng

    // BẮT BUỘC bám lại kênh SAU MỖI LẦN QUÉT, kể cả khi kênh không đổi:
    // scanNetworks() nhảy qua tất cả các kênh và bỏ radio lại ở kênh cuối cùng
    // nó dừng, KHÔNG tự trả về kênh cũ. Trước đây chỉ bám lại khi kênh đổi, nên
    // sau mỗi lần quét radio bị lạc kênh và mọi gói gửi đi đều rơi vào hư không
    // — mà broadcast không có ACK nên không hề báo lỗi, node cứ thế "chết ngầm".
    bindToChannel(ch);
  }

  if (lastSend != 0 && now - lastSend < TELEMETRY_MS) {
    delay(50);
    return;
  }

  lastSend = now;

  // Cảm biến hỏng KHÔNG có nghĩa là node chết. Vẫn gửi nhịp tim, chỉ để giá trị
  // là NaN — master sẽ hiểu là "còn sống nhưng chưa có số đo", giữ node ở trạng
  // thái online thay vì báo mất kết nối oan. Trước đây chỗ này return thẳng nên
  // DHT lỗi là node bị coi như đã chết.
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  bool sensorOk = !(isnan(t) || isnan(h));
  if (!sensorOk) {
    Serial.println("Doc cam bien loi (NaN) — van gui nhip tim, kiem tra day DHT");
    t = NAN;
    h = NAN;
  }

  AcEspNowPacket pkt;
  pkt.magic    = AC_ESPNOW_MAGIC;
  pkt.version  = AC_ESPNOW_VERSION;
  pkt.temp     = t;
  pkt.humidity = h;
  strncpy(pkt.device_uuid, DEVICE_UUID, sizeof(pkt.device_uuid) - 1);
  pkt.device_uuid[sizeof(pkt.device_uuid) - 1] = '\0';

  lastSendOk = false;
  esp_now_send(BROADCAST_MAC, (uint8_t *)&pkt, sizeof(pkt));
  delay(60);   // chờ callback báo kết quả gửi

  // LƯU Ý: với broadcast, "da gui" chỉ nghĩa là radio đã phát đi — KHÔNG có ACK
  // nên không thể biết master có nhận được hay không. Muốn biết chắc thì xem
  // log master, hoặc xem web/app có số của node này không.
  if (sensorOk) {
    Serial.printf("[espnow] t=%.1f h=%.0f kenh=%d -> %s\n",
                  t, h, currentChannel, lastSendOk ? "da phat" : "RADIO LOI");
  } else {
    Serial.printf("[espnow] nhip tim (chua co so do) kenh=%d -> %s\n",
                  currentChannel, lastSendOk ? "da phat" : "RADIO LOI");
  }
}
