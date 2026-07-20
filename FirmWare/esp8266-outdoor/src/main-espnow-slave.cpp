// ============================================================================
//  Aircon — ESP8266 · node NGOÀI TRỜI · vai trò SLAVE (ESP-NOW)
// ----------------------------------------------------------------------------
//  KHÔNG dùng WiFi/MQTT. Đọc DHT rồi bắn gói ESP-NOW về node master (ESP32-S3);
//  master mới là node chuyển tiếp lên cloud theo topic riêng của node này.
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
//      pio run -e esp8266-wifi -t upload
// ============================================================================
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DHT.h>
extern "C" {
#include <espnow.h>
#include <user_interface.h>   // wifi_set_channel()
}
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

static void onSent(uint8_t *mac, uint8_t status) {
  lastSendOk = (status == 0);
}

/// Bám vào kênh `ch` và đăng ký peer quảng bá trên kênh đó.
static bool bindToChannel(int ch) {
  if (ch <= 0) return false;
  wifi_set_channel(ch);
  esp_now_del_peer(BROADCAST_MAC);   // bỏ peer cũ nếu đang ở kênh khác
  if (esp_now_add_peer(BROADCAST_MAC, ESP_NOW_ROLE_SLAVE, ch, NULL, 0) != 0) {
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
  Serial.println("\n== Aircon · ESP8266 · NGOAI TROI (SLAVE / ESP-NOW) ==");
  dht.begin();

  // STA nhưng KHÔNG connect: ESP-NOW cần giao diện station bật, không cần vào mạng.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != 0) {
    Serial.println("esp_now_init THAT BAI — khoi dong lai");
    delay(2000);
    ESP.restart();
  }
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);   // node này chỉ gửi
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
    if (ch > 0 && ch != currentChannel) {
      Serial.printf("Router doi kenh %d -> %d, bam lai\n", currentChannel, ch);
      bindToChannel(ch);
    }
  }

  if (lastSend != 0 && now - lastSend < TELEMETRY_MS) {
    delay(50);
    return;
  }

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    // Chu kỳ lấy mẫu tối thiểu của DHT22 là 2s — chờ 3s để cảm biến hồi.
    Serial.println("Doc cam bien loi (NaN) — kiem tra day/nguon DHT");
    delay(3000);
    return;
  }
  lastSend = now;

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
  Serial.printf("[espnow] t=%.1f h=%.0f kenh=%d -> %s\n",
                t, h, currentChannel, lastSendOk ? "da phat" : "RADIO LOI");
}
