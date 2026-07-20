#pragma once
#include <Arduino.h>

// ============================================================================
//  Kho mã hồng ngoại trong NVS (flash), khoá theo ir_code_id của backend.
// ----------------------------------------------------------------------------
//  VÌ SAO CẦN: backend chỉ gửi kèm `ir_raw` cho LẦN ĐẦU của mỗi ir_code_id;
//  những lần sau nó tin rằng node đã tự giữ mã trong flash và chỉ gửi id
//  (command_publisher._resolve_ir_raw + redis_ir_cache). Không có kho này thì
//  từ lệnh thứ hai trở đi node cầm id mà không biết phát gì.
//
//  Mã sống qua mất điện và qua cả nạp lại firmware (NVS nằm ở phân vùng riêng,
//  `pio run -t upload` không đụng tới) — nhưng `erase_flash` thì mất, xem
//  ../README.md §6 phần "lệch cache".
// ============================================================================
namespace IrStore {

/// Mở namespace NVS. Trả false nếu phân vùng NVS hỏng/đầy.
bool begin();

/// Lưu (hoặc ghi đè) mảng thời gian của một ir_code_id.
bool save(const char *irCodeId, const uint16_t *raw, uint16_t len);

/// Đọc ra [out]. Trả về số mốc đọc được, 0 nghĩa là KHÔNG có (chưa lưu, hoặc
/// dài hơn maxLen). Không bao giờ trả về mảng cắt dở — khung IR thiếu đuôi là
/// một lệnh khác hẳn, phát đi còn tệ hơn không phát.
uint16_t load(const char *irCodeId, uint16_t *out, uint16_t maxLen);

/// Xoá sạch kho (dùng khi muốn ép backend gửi lại toàn bộ ir_raw).
void wipe();

} // namespace IrStore
