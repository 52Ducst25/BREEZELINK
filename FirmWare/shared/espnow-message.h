#pragma once
#include <stdint.h>
#include <string.h>

// ============================================================================
//  Gói tin ESP-NOW: node SLAVE -> node MASTER (gateway)
// ----------------------------------------------------------------------------
//  Dùng chung cho MỌI node cảm biến của một hộ: 4 node góc phòng (ESP32-C3) và
//  node ngoài trời (ESP32 DevKit). Cả ba dòng chip đều là little-endian nên
//  float tương thích byte-với-byte.
//
//  Bố cục CỐ ĐỊNH (packed) vì hai bên khác nhau phải đọc ra cùng một byte.
//
//  Gói TỰ MÔ TẢ: slave gửi kèm device_uuid của chính nó, nhờ vậy master chỉ việc
//  publish vào bl/{org}/{uuid}/telemetry mà KHÔNG cần bảng ánh xạ MAC->uuid.
//  Thêm node slave mới thì chỉ nạp firmware cho nó, master không phải sửa gì.
//
//  ĐÂY LÀ TÍNH CHẤT ĐÁNG GIÁ NHẤT CỦA KHUÔN NÀY, và nó là lý do 4 node góc phòng
//  đi ESP-NOW chứ không đi BLE: gói BLE advertising cổ điển chỉ có 31 byte, chở
//  không nổi một uuid 32 ký tự, nên bên nhận buộc phải giữ một bảng tra id->uuid
//  và nạp lại mỗi khi thêm/bớt node. Ở đây thì không.
//
//  Đánh đổi đã biết: bất kỳ thiết bị nào trong tầm sóng cũng có thể tự xưng một
//  uuid. Chấp nhận được cho mạng nội bộ một hộ; muốn chặt hơn thì bật mã hoá
//  ESP-NOW (PMK/LMK) hoặc cho master lọc theo danh sách MAC.
// ============================================================================

#define AC_ESPNOW_MAGIC   0xAC  // lọc nhanh gói rác/khác hệ trên cùng kênh

// Phiên bản 2 thêm `node_kind` + `corner` ở CUỐI struct.
//
// TƯƠNG THÍCH NGƯỢC LÀ CÓ CHỦ ĐÍCH, KHÔNG PHẢI TIỆN TAY: node ngoài trời đã lắp
// ngoài thực địa đang chạy bản v1 và firmware nó KHÔNG CÓ OTA (audit §3) — muốn
// nâng nó lên v2 là phải tới tận nơi cắm USB-TTL. Nên master chấp nhận cả hai:
// gói 43 byte (v1) được hiểu là node ngoài trời, vì mọi node v1 từng tồn tại
// đều là node ngoài trời. Xem acEspNowParse().
#define AC_ESPNOW_VERSION 2
#define AC_ESPNOW_V1_SIZE 43

/// Loại node — quyết định master xếp số đo này vào đâu để hiển thị, và (gián
/// tiếp) backend đưa nó vào trung vị trong nhà hay vào trung bình trượt ngoài
/// trời. Master KHÔNG cần biết loại để trung chuyển; nó chỉ cần để vẽ màn hình.
enum AcNodeKind : uint8_t {
  AC_NODE_OUTDOOR = 0,
  AC_NODE_ROOM    = 1,
};

/// `corner` chỉ là NHÃN HIỂN THỊ ("góc 1".."góc 4") cho màn hình tại chỗ —
/// device_uuid mới là định danh thật.
///
/// Nên HAI BO TRÙNG SỐ GÓC LÀ VÔ HẠI: cả hai vẫn có topic riêng, vẫn vào trung
/// vị, chỉ là màn hình ghi nhãn trùng nhau. Khác hẳn phương án BLE từng cân
/// nhắc, nơi id trùng nghĩa là bên nhận coi hai bo là MỘT và vứt hẳn số của bo
/// thứ hai — hệ chạy trên ba cảm biến trong khi bốn đèn vẫn sáng trên tường.
#define AC_CORNER_NONE 0xFF

typedef struct __attribute__((packed)) {
  uint8_t  magic;             // = AC_ESPNOW_MAGIC
  uint8_t  version;           // = AC_ESPNOW_VERSION
  char     device_uuid[33];   // uuid của SLAVE (32 ký tự + '\0')
  float    temp;              // °C — NAN = node còn sống nhưng cảm biến hỏng
  float    humidity;          // %  — NAN, như trên
  uint8_t  node_kind;         // v2: AcNodeKind
  uint8_t  corner;            // v2: 0..3 cho node phòng, AC_CORNER_NONE cho outdoor
} AcEspNowPacket;             // 45 byte — dư sức dưới giới hạn 250 byte của ESP-NOW

/// Bóc một gói vừa nhận, chấp nhận cả v1 lẫn v2.
///
/// Trả false nếu không phải gói của hệ này. Gói v1 (43 byte) được điền
/// node_kind = OUTDOOR và corner = AC_CORNER_NONE — xem chú thích ở
/// AC_ESPNOW_VERSION cho lý do.
static inline bool acEspNowParse(const uint8_t *data, int len, AcEspNowPacket *out) {
  if (data == nullptr || len < AC_ESPNOW_V1_SIZE) return false;
  if (data[0] != AC_ESPNOW_MAGIC) return false;

  const uint8_t version = data[1];
  if (version == AC_ESPNOW_VERSION && len >= (int)sizeof(AcEspNowPacket)) {
    memcpy(out, data, sizeof(AcEspNowPacket));
    return true;
  }
  if (version == 1 && len >= AC_ESPNOW_V1_SIZE) {
    memset(out, 0, sizeof(*out));
    memcpy(out, data, AC_ESPNOW_V1_SIZE);
    out->node_kind = AC_NODE_OUTDOOR;
    out->corner    = AC_CORNER_NONE;
    return true;
  }
  return false;   // phiên bản lạ -> bỏ, đừng đoán bố cục
}

/// Điền một gói để gửi đi. Người gọi tự lo NaN cho giá trị không đo được —
/// hàm này không đoán hộ, và NaN là câu trả lời ĐÚNG cho "cảm biến hỏng"
/// (khác hẳn 0.0, vốn là một nhiệt độ hợp lệ).
static inline void acEspNowFill(AcEspNowPacket *pkt, const char *deviceUuid,
                                float temp, float humidity,
                                uint8_t nodeKind, uint8_t corner) {
  memset(pkt, 0, sizeof(*pkt));
  pkt->magic     = AC_ESPNOW_MAGIC;
  pkt->version   = AC_ESPNOW_VERSION;
  pkt->temp      = temp;
  pkt->humidity  = humidity;
  pkt->node_kind = nodeKind;
  pkt->corner    = corner;
  strncpy(pkt->device_uuid, deviceUuid, sizeof(pkt->device_uuid) - 1);
  pkt->device_uuid[sizeof(pkt->device_uuid) - 1] = '\0';
}
