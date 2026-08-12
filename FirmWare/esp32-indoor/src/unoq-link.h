#pragma once
#include <Arduino.h>

#include "unoq-link-protocol.h"

// ============================================================================
//  Đường UART tới Arduino UNO Q (edge AI).
// ----------------------------------------------------------------------------
//  Bố cục gói, khoá liên kết, lý do chia vai: ../../shared/unoq-link-protocol.h
//  — KHÔNG đổi khi chuyển từ Bluetooth sang UART. Chỉ đường ống đổi.
//
//  VÌ SAO BỎ BLUETOOTH — ĐÂY LÀ SỐ ĐO, KHÔNG PHẢI SỞ THÍCH:
//
//    Gateway, BLE bật:   0,31 gói ESP-NOW/giây
//    Gateway, BLE tắt:   0,80 gói/giây  ← đúng bằng 4 node × 5 giây
//
//  Bật Bluetooth thì chip BẮT BUỘC phải bật ngủ WiFi — nó abort chứ không chạy
//  kém đi (`Should enable WiFi modem sleep when both WiFi and Bluetooth are
//  enabled`). Mà radio ngủ thì gói ESP-NOW đến đúng lúc đó là mất, không ai đệm
//  hộ, và broadcast không có ACK nên node vẫn báo "đã phát". Node ngoài trời rơi
//  ~50% và nhấp nháy ONLINE/OFFLINE liên tục; tắt BLE là nó vào sạch, 0 gói rơi.
//
//  Nói cách khác: BLE ăn mất ~60% khả năng thu của chính cái gateway mà nó phục
//  vụ. UART không đụng tới radio, nên lấy lại toàn bộ. Cái giá là một sợi dây.
//
//  KÈM THEO, MẤT LUÔN CẢ MỘT MỚ LỘN XỘN: không quét, không ghép đôi, không
//  thương lượng MTU (ảnh chụp 39 byte từng bị nghi cắt cụt vì BlueZ báo MTU 23),
//  không NimBLE ~100KB flash, và không còn ai tranh ăng-ten 2.4GHz.
//
//  BA LUẬT CỦA FILE NÀY GIỮ NGUYÊN TỪ BẢN BLE:
//
//  1. CHỈ ĐẶT HÀNG, KHÔNG THI HÀNH. poll() trả gói ra cho loop(); nó không bắn
//     IR và không publish MQTT — cùng luật đã áp cho callback ESP-NOW và MQTT.
//
//  2. ĐỀ XUẤT KHÁC LỆNH. `kind` trong gói ghi rõ, và file này KHÔNG suy diễn:
//     UNO Q gửi ADVICE thì gateway chỉ ghi lại; chỉ COMMAND mới ra máy lạnh.
//     Gộp hai cái là mọi phép thử trên UNO Q chạy thẳng vào máy nén.
//
//  3. SAI link_key LÀ VỨT. Gói không mang đúng khoá băm từ ORG_ID bị bỏ và đếm
//     vào rejectedCount() — để một bo UNO Q của hộ khác không lái nhầm máy lạnh.
// ============================================================================
namespace UnoQLink {

/// Lệnh/đề xuất vừa nhận từ UNO Q, đã kiểm magic/version/CRC/link_key và lọc
/// trùng `seq`. Rút bằng poll() trong loop().
struct Incoming {
  bool     isCommand;   ///< false = chỉ đề xuất, KHÔNG được bắn IR
  uint8_t  mode;        ///< AcUnoQMode
  int8_t   setpoint;
  uint16_t seq;
};

/// Mở cổng UART và chuẩn bị khoá liên kết. [orgId] dùng để băm ra link_key.
///
/// Gọi được BẤT CỨ LÚC NÀO trong setup() — khác hẳn bản BLE vốn phải xếp sau
/// WiFi vì tranh radio. UART không liên quan gì tới sóng.
bool begin(const char *orgId);

/// Đẩy một ảnh chụp sang UNO Q. Rẻ và không chặn: 39 byte ở 115200 baud mất
/// ~3,4ms, mà đệm truyền của driver nuốt gọn nên hàm trả về ngay.
void publish(const AcUnoQSnapshot &snapshot);

/// Rút lệnh/đề xuất UNO Q vừa gửi. Trả false nếu không có. Gọi mỗi vòng loop().
bool poll(Incoming &out);

/// UNO Q có đang nói chuyện không.
///
/// UART KHÔNG CÓ TRẠNG THÁI KẾT NỐI như BLE — dây cắm vào là "nối" kể cả khi
/// đầu kia đã chết. Nên ở đây "nối" nghĩa là ĐÃ NGHE THẤY GÓI HỢP LỆ GẦN ĐÂY.
/// Đó mới là thứ người đọc log thật sự muốn biết.
bool connected();

uint32_t rxCount();        ///< số gói hợp lệ đã nhận
uint32_t rejectedCount();  ///< số gói bị bỏ (sai magic/version/CRC/link_key/lặp)

} // namespace UnoQLink
