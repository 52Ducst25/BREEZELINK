#pragma once
#include <stdint.h>

// ============================================================================
//  Gói tin ESP-NOW: node SLAVE -> node MASTER (dùng chung cho ESP32 & ESP8266)
// ----------------------------------------------------------------------------
//  Bố cục CỐ ĐỊNH (packed) vì hai chip khác nhau phải đọc ra cùng một byte.
//  Cả ESP32 và ESP8266 đều là Xtensa 32-bit little-endian nên float tương thích.
//
//  Gói TỰ MÔ TẢ: slave gửi kèm device_uuid của chính nó, nhờ vậy master chỉ việc
//  publish vào bl/{org}/{uuid}/telemetry mà KHÔNG cần bảng ánh xạ MAC->uuid.
//  Thêm node slave mới thì chỉ nạp firmware cho nó, master không phải sửa gì.
//
//  Đánh đổi đã biết: bất kỳ thiết bị nào trong tầm sóng cũng có thể tự xưng một
//  uuid. Chấp nhận được cho mạng nội bộ một hộ; muốn chặt hơn thì bật mã hoá
//  ESP-NOW (PMK/LMK) hoặc cho master lọc theo danh sách MAC.
// ============================================================================

#define AC_ESPNOW_MAGIC   0xAC  // lọc nhanh gói rác/khác hệ trên cùng kênh
#define AC_ESPNOW_VERSION 1     // đổi khi bố cục thay đổi -> master bỏ gói lạ

typedef struct __attribute__((packed)) {
  uint8_t magic;             // = AC_ESPNOW_MAGIC
  uint8_t version;           // = AC_ESPNOW_VERSION
  char    device_uuid[33];   // uuid của SLAVE (32 ký tự + '\0')
  float   temp;              // °C
  float   humidity;          // %
} AcEspNowPacket;             // 43 byte — dư sức dưới giới hạn 250 byte của ESP-NOW
