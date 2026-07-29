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
//  HAI NGUỒN ĐIỀN VÀO BẢNG NÀY, và phải có cả hai:
//    1. Backend gửi lệnh kèm `ir_raw` -> save() + saveAlias() với UUID thật.
//    2. Node tự học được mã -> saveLearned() với id tạm (xem bên dưới).
//  Chỉ có nguồn 1 thì panel không dùng được chính mã nó vừa dạy, phải chờ server
//  tình cờ gửi xuống một lệnh cho đúng tổ hợp đó — nhìn y như hỏng.
//
//  Tổ hợp nào CHƯA có mã từ nguồn nào thì nút vẫn mờ, và màn ĐIỀU KHIỂN bắt
//  buộc nói rõ lý do — không có phím chết.
//
//  [temp] < 0 nghĩa là mã cố định không kèm nhiệt độ (DRY/FAN/OFF).
// ---------------------------------------------------------------------------
bool saveAlias(const char *mode, int temp, const char *irCodeId);

/// Lưu mã VỪA HỌC ĐƯỢC ngay tại node, dưới một id tạm do node tự sinh.
///
/// VÌ SAO CẦN: lúc học xong, node mới chỉ ĐẨY mảng thời gian lên cloud —
/// `ir_code_id` là UUID do backend sinh nên node chưa biết. Nếu chỉ dựa vào
/// đường `save()` ở trên (chạy khi backend gửi lệnh kèm `ir_raw`) thì có một
/// khoảng trống rất khó chịu: **người dùng vừa dạy mã ngay trên panel, mà chính
/// panel đó vẫn báo "CHƯA HỌC MÃ" và mọi nút chế độ vẫn mờ** — cho tới khi vòng
/// lặp comfort trên server tình cờ gửi xuống một lệnh cho đúng tổ hợp đó. Trên
/// một bảng điều khiển treo tường thì đó bị đọc là hỏng, không phải là "đang chờ".
///
/// Id tạm mang tiền tố riêng để phân biệt với UUID thật. Khi backend gửi lệnh
/// thật cho cùng tổ hợp, `saveAlias()` trỏ bí danh sang UUID đó và DỌN LUÔN mảng
/// tạm — nếu không, mỗi mã chiếm chỗ hai lần và phân vùng NVS 20 KB không đủ cho
/// 18 tổ hợp bắt buộc (COOL 16..30 + DRY + FAN + OFF).
///
/// [temp] < 0 cho mã cố định (DRY/FAN/OFF), giống quy ước của saveAlias().
bool saveLearned(const char *mode, int temp, const uint16_t *raw, uint16_t len);

/// true nếu có mã cho tổ hợp này (dùng để làm mờ nút chưa học).
bool hasAlias(const char *mode, int temp);

/// Tra bí danh rồi đọc luôn mảng thời gian. Trả 0 nếu chưa có.
uint16_t loadAlias(const char *mode, int temp, uint16_t *out, uint16_t maxLen);

} // namespace IrStore
