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

// --- Chế độ NGHE NGÓNG (chỉ để truy lỗi) -----------------------------------
// Bật bằng `-D ESPNOW_SNIFF=1`. MẶC ĐỊNH TẮT: node góc phòng chỉ có việc phát,
// và một callback nhận chạy suốt ngày chỉ tổ tốn CPU cho việc không ai đọc.
//
// VÌ SAO ĐÁNG CÓ: khi gateway báo "nhận 0 gói" thì có hai khả năng hoàn toàn
// khác nhau — node không thật sự bức xạ, hay node bức xạ mà gateway không nghe.
// Callback GỬI của ESP-NOW không phân biệt được: với broadcast nó luôn báo
// SUCCESS vì không có ACK. Bật cờ này trên MỘT node là nó nghe được ba node anh
// em (cùng kênh, cùng không đăng nhập WiFi) và câu hỏi được chia đôi ngay.
#if defined(ESPNOW_SNIFF) && ESPNOW_SNIFF
inline volatile uint32_t sniffCount = 0;
inline char sniffLast[40] = "";

inline void onSniff(const uint8_t *mac, const uint8_t *data, int len) {
  sniffCount++;
  if (mac) {
    snprintf(sniffLast, sizeof(sniffLast), "%02X:%02X:%02X:%02X:%02X:%02X/%dB",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], len);
    // KHÔNG in ở đây. Callback chạy trong tác vụ WiFi, còn Serial trên bo này là
    // USB CDC — ghi từ tác vụ khác có thể chặn, và một phép đo tự làm hỏng chính
    // nó thì tệ hơn không đo. loop() in thay (xem esp32-room/src/main.cpp).
    (void)data;
  }
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
  //
  // KIỂM GIÁ TRỊ TRẢ VỀ. Bản trước gọi rồi bỏ qua, nên một lần đặt kênh thất bại
  // là node phát vào kênh khác trong im lặng, mà mọi log vẫn ghi kênh mong muốn.
  const esp_err_t chErr = esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);
  if (chErr != ESP_OK) {
    Serial.printf("esp_wifi_set_channel(%d) THAT BAI: %s — node se phat sai kenh\n",
                  ch, esp_err_to_name(chErr));
  }

  // Bỏ peer cũ nếu đang ở kênh khác. Peer chưa tồn tại thì hàm trả
  // ESP_ERR_ESPNOW_NOT_FOUND — vô hại, bỏ qua giá trị trả về.
  esp_now_del_peer(detail::BROADCAST_MAC);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, detail::BROADCAST_MAC, 6);
  // channel = 0 nghĩa là "gửi trên kênh hiện tại của giao diện", KHÔNG phải "kênh
  // bất kỳ". Radio đã được esp_wifi_set_channel() ở ngay trên đặt vào đúng kênh
  // cần, nên 0 và ch trỏ tới cùng một kênh — nhưng chỉ 0 mới chạy.
  //
  // ĐÃ ĐO ĐƯỢC, ĐỪNG ĐỔI LẠI: với channel = ch, node báo ESP_NOW_SEND_SUCCESS cho
  // mọi gói mà KHÔNG thiết bị nào nghe được — không gateway, không cả một bo cùng
  // loại đặt cách 20cm. Đổi đúng một số này là gói tới nơi ngay. Lý do: khi peer
  // khai kênh khác 0, ESP-NOW tự chuyển kênh quanh mỗi lần gửi; trên giao diện
  // station KHÔNG đăng nhập WiFi, khung bị bắn ra đúng lúc đang chuyển kênh nên
  // rơi hết — mà callback gửi thì vẫn báo thành công vì broadcast không có ACK.
  //
  // Cách phát hiện: cho gateway phát ngược lại (nó khai channel = 0) thì node
  // nghe được sạch sẽ. Thu tốt mà phát câm là dấu hiệu của chỗ này.
  peer.channel = 0;
  peer.encrypt = false;
  peer.ifidx   = WIFI_IF_STA;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Khong them duoc peer ESP-NOW");
    return false;
  }
  detail::currentChannel = ch;
  // In cả kênh MONG MUỐN lẫn kênh THẬT: hai số này lệch nhau là đã tìm ra lỗi.
  uint8_t primary = 0;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&primary, &second);
  Serial.printf("ESP-NOW bam kenh %d · radio dang thuc te o kenh %u (phu=%d)\n",
                ch, primary, (int)second);
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

  // ÉP PHY VỀ B/G/N TƯỜNG MINH, ĐỪNG TIN MẶC ĐỊNH.
  //
  // Đã đo được trên bo ESP32-C3: node báo `ESP_NOW_SEND_SUCCESS` cho từng gói,
  // radio đúng kênh 1, mà KHÔNG một thiết bị nào nghe được — kể cả một bo khác
  // đặt cách 20cm. Trong khi chiều ngược lại (gateway phát, node này nghe) thì
  // gần như không mất gói. Thu tốt mà phát không ai hiểu là dấu hiệu của PHY:
  // nếu giao diện lỡ ở chế độ LR (long-range, độc quyền Espressif) thì khung
  // phát ra chỉ thiết bị LR mới giải mã được, còn thu khung chuẩn vẫn bình
  // thường — đúng y triệu chứng.
  //
  // Khai tường minh thì không phụ thuộc vào việc mặc định của core/board là gì.
  const esp_err_t protoErr =
      esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  if (protoErr != ESP_OK) {
    Serial.printf("esp_wifi_set_protocol THAT BAI: %s\n", esp_err_to_name(protoErr));
  }

  // CÔNG SUẤT PHÁT 8dBm, KHÔNG PHẢI TỐI ĐA — VÀ ĐÂY LÀ THỨ ĐÃ LÀM CẢ HỆ CÂM.
  //
  // Đơn vị 0.25dBm, nên 32 = 8dBm còn 78 = 19.5dBm (mức tối đa, mặc định của
  // chip). Ở mức tối đa, bo ESP32-C3 ăn nguồn qua cổng USB phát ra sóng mà KHÔNG
  // MỘT THIẾT BỊ NÀO giải mã được: gateway nhận 0 gói, một bo C3 khác đặt cách
  // 20cm cũng nhận 0 gói, trong khi chiều ngược lại (gateway phát, node nghe) thì
  // sạch sẽ. Hạ xuống 8dBm là gói tới nơi ngay lập tức.
  //
  // Vì sao: đỉnh dòng lúc phát ở mức tối đa khoảng 350mA. Cổng USB/hub yếu không
  // gánh nổi, điện áp sụt ngay giữa lúc phát và tầng khuếch đại ra sóng rác.
  // KHÔNG CÓ MỘT DÒNG LỖI NÀO ở bất kỳ đâu: callback gửi vẫn báo
  // ESP_NOW_SEND_SUCCESS, vì broadcast không có ACK nên nó chỉ biết khung đã rời
  // tầng MAC, không biết cái gì thật sự bay ra ngoài không khí.
  //
  // 8dBm vẫn phủ thừa một căn phòng (ESP-NOW ở mức này đi được vài chục mét
  // trong nhà), nên đây không phải sự hy sinh — chỉ là bỏ đi phần công suất mà
  // nguồn không cấp nổi.
  //
  // Nâng lại bằng `-D SLAVE_TX_POWER=n` NẾU node được cấp nguồn tử tế (adapter
  // riêng, tụ lọc đủ) và cần tầm xa hơn. Nâng xong PHẢI kiểm lại bằng log
  // `[relay]` trên gateway, đừng tin vào việc node báo gửi thành công.
#ifndef SLAVE_TX_POWER
#define SLAVE_TX_POWER 32
#endif
  esp_wifi_set_max_tx_power(SLAVE_TX_POWER);
  int8_t txPower = 0;
  esp_wifi_get_max_tx_power(&txPower);

  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_send_cb(detail::onSent);
  Serial.printf("ESP-NOW: cong suat phat %.1f dBm\n", txPower * 0.25f);
#if defined(ESPNOW_SNIFF) && ESPNOW_SNIFF
  esp_now_register_recv_cb(detail::onSniff);
  Serial.println("ESP-NOW: che do NGHE NGONG bat — se dem goi cua node khac");
#endif

  // Quét vài lần: lần quét đầu ngay sau khi bật radio hay trượt, mà bám sai kênh
  // thì gói bay vào khoảng không và KHÔNG có cách nào biết (broadcast không ACK).
  // Đường tắt để TRUY LỖI: `-D SLAVE_FIXED_CHANNEL=n` bỏ hẳn phần quét và bám
  // thẳng kênh n. Quét là thứ duy nhất node này làm mà gateway không làm, nên khi
  // nghi ngờ chính việc quét làm hỏng đường phát thì đây là cách loại nó ra.
  // KHÔNG dùng khi chạy thật: router đổi kênh là node câm mà không ai biết.
#if defined(SLAVE_FIXED_CHANNEL) && SLAVE_FIXED_CHANNEL > 0
  Serial.printf("BO QUA QUET (SLAVE_FIXED_CHANNEL=%d) — chi dung khi truy loi\n",
                SLAVE_FIXED_CHANNEL);
  bindToChannel(SLAVE_FIXED_CHANNEL);
  detail::lastRescanMs = millis();
  return true;
#endif

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

/// Kênh THẬT của radio, đọc từ phần cứng.
///
/// TRƯỚC ĐÂY HÀM NÀY TRẢ VỀ BIẾN ĐÃ LƯU, và đó là một lời nói dối nguy hiểm:
/// bindToChannel() bỏ qua giá trị trả về của esp_wifi_set_channel(), nên khi lệnh
/// đó trượt thì biến vẫn ghi "kênh 1" còn radio nằm ở kênh khác. Log in ra "kenh=1"
/// đầy tự tin ở CẢ HAI node trong khi chúng không hề nghe được nhau — đúng kiểu
/// làm người đọc loại trừ nhầm nguyên nhân đúng.
inline int channel() {
  uint8_t primary = 0;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary != 0) return primary;
  return detail::currentChannel;
}

/// Kênh mà firmware ĐỊNH bám (để so với channel() ở trên khi truy lỗi).
inline int intendedChannel() { return detail::currentChannel; }

/// Số gói ESP-NOW node này NGHE ĐƯỢC từ thiết bị khác (0 khi chưa bật ESPNOW_SNIFF).
inline uint32_t sniffed() {
#if defined(ESPNOW_SNIFF) && ESPNOW_SNIFF
  return detail::sniffCount;
#else
  return 0;
#endif
}

/// Mô tả gói nghe được gần nhất ("MAC/độ dài"), rỗng nếu chưa nghe thấy gì.
inline const char *sniffedLast() {
#if defined(ESPNOW_SNIFF) && ESPNOW_SNIFF
  return detail::sniffLast;
#else
  return "";
#endif
}

}  // namespace EspNowSlaveRadio
