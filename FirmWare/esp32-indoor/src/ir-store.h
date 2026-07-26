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

/// Số mã đang giữ (đếm bằng bộ đếm riêng, xem .cpp) — cho màn THONG TIN.
uint16_t count();

// ---------------------------------------------------------------------------
//  Chỉ mục BÍ DANH (mode, temp) -> ir_code_id
// ---------------------------------------------------------------------------
//  VÌ SAO CẦN: kho trên khoá theo ir_code_id — một UUID do backend sinh. Node
//  không có bảng tra ngược, nên khi người dùng bấm "COOL 26" TRÊN MÀN HÌNH của
//  chính node thì nó không biết phát khung nào.
//
//  Cách giải rẻ nhất: mỗi lần backend gửi một lệnh, nó đã kèm sẵn `mode` +
//  `setpoint` + `ir_code_id`. Lưu thêm một khoá "aCOOL26" -> id là node tự dựng
//  được bảng tra ngược mà backend không phải đổi gì.
//
//  HỆ QUẢ PHẢI NÓI THẬT TRÊN GIAO DIỆN: panel chỉ điều khiển được những tổ hợp
//  mà server ĐÃ TỪNG gửi ít nhất một lần. Bo vừa nạp firmware thì mọi nút chế
//  độ đều mờ cho tới khi vòng lặp comfort chạy vài chu kỳ — đó là lý do màn
//  ĐIỀU KHIỂN bắt buộc có trạng thái "mờ + giải thích", không có phím chết.
//
//  [temp] < 0 nghĩa là mã cố định không kèm nhiệt độ (DRY/FAN/OFF).
// ---------------------------------------------------------------------------
bool saveAlias(const char *mode, int temp, const char *irCodeId);

/// true nếu có mã cho tổ hợp này (dùng để làm mờ nút chưa học).
bool hasAlias(const char *mode, int temp);

/// Tra bí danh rồi đọc luôn mảng thời gian. Trả 0 nếu chưa có.
uint16_t loadAlias(const char *mode, int temp, uint16_t *out, uint16_t maxLen);

} // namespace IrStore
