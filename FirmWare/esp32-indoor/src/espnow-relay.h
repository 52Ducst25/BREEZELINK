#pragma once
#include <Arduino.h>
#include "espnow-message.h"

// ============================================================================
//  Nhận gói ESP-NOW từ các node SLAVE (vai trò MASTER).
// ----------------------------------------------------------------------------
//  Callback của ESP-NOW chạy trong tác vụ WiFi, KHÔNG được publish MQTT ngay tại
//  đó (PubSubClient không an toàn đa luồng và callback phải trả về thật nhanh).
//  Nên callback chỉ chép gói vào hàng đợi vòng, còn loop() mới lấy ra gửi đi.
//
//  Kèm luôn MAC người gửi: ESP-NOW cho biết MAC nguồn, nhờ vậy master báo được
//  MAC của slave lên cloud mà slave không phải tự khai.
// ============================================================================
namespace EspNowRelay {

/// Được gọi trong loop() cho từng gói nhận được, theo đúng thứ tự đến.
typedef void (*Handler)(const char *deviceUuid, const uint8_t mac[6],
                        float temp, float humidity);

/// Khởi tạo ESP-NOW. Gọi SAU khi WiFi đã kết nối: ESP-NOW dùng chính kênh mà
/// WiFi đang bám, nên phải để WiFi chốt kênh trước.
bool begin();

/// Lấy hết gói đang chờ trong hàng đợi. Gọi mỗi vòng loop().
void poll(Handler handler);

uint32_t receivedCount();
uint32_t droppedCount();

} // namespace EspNowRelay
