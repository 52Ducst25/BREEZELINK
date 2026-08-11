#pragma once
#include <Arduino.h>

#include "unoq-link-protocol.h"

// ============================================================================
//  Đường Bluetooth tới Arduino UNO Q (edge AI). Gateway đóng vai PERIPHERAL.
// ----------------------------------------------------------------------------
//  Giao thức, UUID, bố cục gói và lý do chia vai: ../../shared/unoq-link-protocol.h
//
//  BA LUẬT CỦA FILE NÀY, cả ba đều lấy từ những chỗ đã trả giá trong dự án:
//
//  1. CALLBACK CHỈ ĐẶT HÀNG. Callback của NimBLE chạy trong tác vụ BLE, không
//     phải lõi 1. Bắn IR hay publish MQTT ở đó là chạm vào đồ của lõi khác —
//     đúng luật đã áp cho callback PubSubClient (main.cpp, struct pending) và
//     cho callback ESP-NOW (espnow-relay.h).
//
//  2. ĐỀ XUẤT KHÁC LỆNH. `kind` trong gói ghi rõ, và file này KHÔNG tự suy diễn:
//     UNO Q gửi ADVICE thì gateway chỉ ghi lại; chỉ COMMAND mới đi ra máy lạnh.
//     Gộp hai cái là mọi phép thử trên UNO Q đều chạy thẳng vào máy nén.
//
//  3. KIỂM MTU VÀ KÊU TO NẾU HỤT. Ảnh chụp 44 byte; MTU mặc định của BLE là 23,
//     tức 20 byte dữ liệu. Notify vượt MTU bị CẮT CỤT TRONG IM LẶNG — không lỗi
//     ở cả hai bên, chỉ là mấy góc cuối biến mất khỏi màn UNO Q.
// ============================================================================
namespace UnoQLink {

/// Lệnh/đề xuất vừa nhận từ UNO Q, đã kiểm magic/version/link_key và lọc trùng
/// `seq`. Rút bằng poll() trong loop().
struct Incoming {
  bool    isCommand;   ///< false = chỉ đề xuất, KHÔNG được bắn IR
  uint8_t mode;        ///< AcUnoQMode
  int8_t  setpoint;
  uint16_t seq;
};

/// Dựng GATT server và bắt đầu quảng bá. [orgId] dùng để băm ra link_key.
/// Gọi SAU khi WiFi đã kết nối: cả hai chia chung một khối radio 2.4GHz, và bộ
/// đồng tồn tại xử lý tốt hơn khi WiFi đã chốt kênh xong.
bool begin(const char *orgId, const char *deviceName);

/// Đẩy một ảnh chụp sang UNO Q. Rẻ và không chặn — chỉ ghi vào đặc tính rồi
/// notify. Không có client nào kết nối thì hàm này lặng lẽ không làm gì.
void publish(const AcUnoQSnapshot &snapshot);

/// Rút lệnh/đề xuất UNO Q vừa gửi. Trả false nếu không có. Gọi mỗi vòng loop().
bool poll(Incoming &out);

bool connected();          ///< UNO Q có đang kết nối không
uint16_t negotiatedMtu();  ///< 0 khi chưa có client
uint32_t rxCount();        ///< số gói hợp lệ đã nhận
uint32_t rejectedCount();  ///< số gói bị từ chối (sai magic/version/link_key/lặp)

} // namespace UnoQLink
