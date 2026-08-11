#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>        // esp_wifi_set_channel()
#include <esp_idf_version.h>

// ============================================================================
//  Radio ESP-NOW cho một node SLAVE (không nối WiFi, chỉ bám kênh của router).
// ----------------------------------------------------------------------------
//  Dùng chung giữa node NGOÀI TRỜI và 4 node GÓC PHÒNG. Trước đây logic này nằm
//  hẳn trong main của node ngoài trời; tách ra vì bốn node phòng cần y hệt, và
//  hai bản sao của một cái bẫy đã trả giá thì lần sửa sau chắc chắn quên một nửa.
//
//  CÁI BẪY LỚN NHẤT CỦA ESP-NOW LÀ LỆCH KÊNH: hai bên bắt buộc cùng kênh WiFi,
//  mà gateway thì đang bám theo kênh của router. Nên node này KHÔNG cắm cứng
//  kênh — nó QUÉT tìm SSID của nhà để biết router đang ở kênh nào rồi bám theo.
//  Router đổi kênh thì lần quét sau tự cập nhật.
//
//  Node này KHÔNG đăng nhập WiFi và KHÔNG cần mật khẩu WiFi — nó chỉ *quét* để
//  đọc số kênh. Đó là lý do config.h của nó không chứa bí mật nào.
//
//  Gửi BROADCAST thay vì unicast tới MAC của gateway: lắp đặt không phải đi lấy
//  MAC gateway rồi nạp vào từng bo — cứ bật là chạy. Một hộ chỉ có một gateway
//  nên không sợ nhầm địa chỉ.
// ============================================================================
namespace EspNowSlaveRadio {

/// Chu kỳ dò lại kênh router (ms). 5 phút: đủ nhanh để bắt kịp router đổi kênh,
/// đủ thưa để việc quét (làm radio bận ~1s) không ảnh hưởng nhịp gửi.
static const unsigned long RESCAN_INTERVAL_MS = 300000UL;

namespace detail {

inline uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
inline int     currentChannel = 0;
inline bool    lastSendOk = false;
inline unsigned long lastRescanMs = 0;

// Chữ ký callback GỬI đổi ở Arduino-ESP32 core 3.2 (IDF 5.4): tham số đầu từ
// `const uint8_t *mac` thành `const wifi_tx_info_t *`. Phải dò theo
// ESP_IDF_VERSION chứ KHÔNG dùng được ESP_ARDUINO_VERSION_MAJOR >= 3 như
// callback NHẬN bên gateway: hai callback đổi ở hai mốc phiên bản khác nhau
// (nhận đổi từ core 3.0, gửi mãi core 3.2).
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
inline void onSent(const wifi_tx_info_t *, esp_now_send_status_t status) {
  lastSendOk = (status == ESP_NOW_SEND_SUCCESS);
}
#else
inline void onSent(const uint8_t *, esp_now_send_status_t status) {
  lastSendOk = (status == ESP_NOW_SEND_SUCCESS);
}
#endif

}  // namespace detail

/// Quét các mạng quanh đây để biết router nhà đang phát ở kênh nào.
/// Trả 0 nếu không thấy SSID (router tắt / ngoài vùng phủ).
inline int findRouterChannel(const char *ssid) {
  const int n = WiFi.scanNetworks(false /*async*/, true /*show hidden*/);
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == ssid) {
      const int ch = WiFi.channel(i);
      WiFi.scanDelete();
      return ch;
    }
  }
  WiFi.scanDelete();
  return 0;
}

/// Bám vào kênh [ch] và đăng ký lại peer quảng bá trên kênh đó.
inline bool bindToChannel(int ch) {
  if (ch <= 0) return false;

  // WIFI_SECOND_CHAN_NONE: chỉ dùng kênh 20MHz. ESP-NOW không cần kênh phụ, mà
  // khai kênh phụ lệch với gateway là hỏng việc.
  esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);

  // Bỏ peer cũ nếu đang ở kênh khác. Peer chưa tồn tại thì hàm trả
  // ESP_ERR_ESPNOW_NOT_FOUND — vô hại, bỏ qua giá trị trả về.
  esp_now_del_peer(detail::BROADCAST_MAC);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, detail::BROADCAST_MAC, 6);
  peer.channel = (uint8_t)ch;
  peer.encrypt = false;
  peer.ifidx   = WIFI_IF_STA;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Khong them duoc peer ESP-NOW");
    return false;
  }
  detail::currentChannel = ch;
  Serial.printf("ESP-NOW bam kenh %d\n", ch);
  return true;
}

/// Dựng radio ở chế độ station-không-đăng-nhập và bám kênh của [ssid].
/// Gọi trong setup(). Trả false nếu esp_now_init() hỏng (bên gọi tự quyết định
/// khởi động lại hay chạy tiếp).
inline bool begin(const char *ssid) {
  // STA nhưng KHÔNG connect: ESP-NOW cần giao diện station bật, không cần vào mạng.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Tắt tiết kiệm điện của modem. Mặc định ESP32 cho radio ngủ giữa các beacon,
  // mà đang ngủ thì gói ESP-NOW gửi đi bị hoãn/rơi — đúng lỗi "chết ngầm" đã
  // gặp trên gateway (git log: "master điếc ESP-NOW sau vài phút").
  WiFi.setSleep(false);

  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_send_cb(detail::onSent);

  // Quét vài lần: lần quét đầu ngay sau khi bật radio hay trượt, mà bám sai kênh
  // thì gói bay vào khoảng không và KHÔNG có cách nào biết (broadcast không ACK).
  int ch = 0;
  for (uint8_t attempt = 1; attempt <= 3 && ch == 0; attempt++) {
    delay(300);
    ch = findRouterChannel(ssid);
    if (ch == 0) Serial.printf("Lan quet %u: chua thay \"%s\"\n", attempt, ssid);
  }
  if (ch == 0) {
    Serial.printf("Khong thay SSID \"%s\" — tam kenh 1, se do lai moi %lus\n",
                  ssid, RESCAN_INTERVAL_MS / 1000);
    ch = 1;
  }
  bindToChannel(ch);
  detail::lastRescanMs = millis();
  return true;
}

/// Gọi mỗi vòng loop(): dò lại kênh ĐỊNH KỲ.
///
/// Định kỳ chứ không dựa vào lỗi gửi: gói broadcast không có ACK nên callback
/// luôn báo thành công kể cả khi bắn sai kênh, chẳng ai phát hiện ra.
///
/// VÀ PHẢI BÁM LẠI KÊNH SAU MỖI LẦN QUÉT, kể cả khi kênh không đổi:
/// scanNetworks() nhảy qua tất cả các kênh và bỏ radio lại ở kênh cuối cùng nó
/// dừng, KHÔNG tự trả về kênh cũ. Trước đây chỉ bám lại khi kênh đổi, nên sau
/// mỗi lần quét radio bị lạc kênh và mọi gói gửi đi đều rơi vào hư không — mà
/// broadcast không có ACK nên không hề báo lỗi, node cứ thế "chết ngầm".
inline void tickRescan(const char *ssid) {
  const unsigned long now = millis();
  if (now - detail::lastRescanMs < RESCAN_INTERVAL_MS) return;
  detail::lastRescanMs = now;

  int ch = findRouterChannel(ssid);
  if (ch <= 0) ch = detail::currentChannel;   // quét trượt -> giữ nguyên kênh đang dùng
  bindToChannel(ch);
}

/// Bắn một gói quảng bá và chờ callback báo kết quả.
///
/// LƯU Ý: với broadcast, true chỉ nghĩa là RADIO ĐÃ PHÁT ĐI — không có ACK nên
/// không thể biết gateway có nhận được hay không. Muốn biết chắc thì xem log
/// gateway, hoặc xem web/app có số của node này không.
inline bool broadcast(const void *payload, size_t len) {
  detail::lastSendOk = false;
  esp_now_send(detail::BROADCAST_MAC, (const uint8_t *)payload, len);
  delay(60);   // chờ callback
  return detail::lastSendOk;
}

inline int channel() { return detail::currentChannel; }

}  // namespace EspNowSlaveRadio
