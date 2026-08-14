#pragma once
#include <Arduino.h>

// ============================================================================
//  Kho mã hồng ngoại trong NVS — ĐÚNG HAI Ô, và niềm tin về trạng thái máy.
// ----------------------------------------------------------------------------
//  ĐƠN GIẢN HƠN HẲN ../../esp32-s3-panel/src/ir-store.cpp, và đó là điểm chính.
//
//  Panel phải giữ 18 tổ hợp (COOL 16..30 + DRY + FAN + OFF) khoá theo `ir_code_id`
//  là UUID 36 ký tự do backend sinh — mà tên khoá NVS chỉ được 15 ký tự. Nên nó
//  cần băm FNV-1a, cần khoá đối chiếu để bắt đụng băm, cần bảng bí danh
//  (mode,temp) -> id, cần id tạm "local-" cho mã tự học rồi dọn khi backend gửi
//  mã thật xuống.
//
//  Bo này không có backend, không có UUID, và chỉ cần HAI ô. Tên ô nhét thẳng
//  vào khoá NVS được ("rON" = 3 ký tự), nên toàn bộ tầng băm + bí danh + dọn
//  rác ở trên là thừa. Chép nguyên bộ máy đó sang đây chỉ để dùng 2/18 công
//  suất là mang theo cả những chỗ hỏng của nó mà không được lợi gì.
//
//  Mã sống qua mất điện và qua cả `pio run -t upload` (NVS ở phân vùng riêng),
//  nhưng `esptool erase_flash` thì mất sạch — phải học lại.
// ============================================================================
namespace IrSlots {

/// Hai ô mã. Với remote một-nút-bập-bênh (settings.h §6, DIFFUSER_IR_TOGGLE=1)
/// thì CHỈ Ô `ON` ĐƯỢC DÙNG, ô `OFF` bỏ trống.
enum class Slot : uint8_t { ON = 0, OFF = 1 };

/// Mở namespace NVS. Trả false nếu phân vùng NVS hỏng/đầy.
bool begin();

/// "BAT" / "TAT" — để in log, không phải khoá NVS.
const char *name(Slot s);

/// Lưu (hoặc ghi đè) mảng thời gian của một ô.
bool save(Slot s, const uint16_t *raw, uint16_t len);

/// Đọc ra [out]. Trả số mốc đọc được, 0 = KHÔNG có (chưa học, hoặc dài hơn
/// maxLen). KHÔNG BAO GIỜ trả mảng cắt dở: khung IR thiếu đuôi là một lệnh khác
/// hẳn, phát đi còn tệ hơn không phát.
uint16_t load(Slot s, uint16_t *out, uint16_t maxLen);

bool has(Slot s);

/// Quên mã của một ô (để học lại). false nếu vốn chưa có gì.
bool clear(Slot s);

/// Xoá sạch cả hai ô VÀ niềm tin trạng thái.
void wipe();

// ---------------------------------------------------------------------------
//  Niềm tin về trạng thái máy xông
// ---------------------------------------------------------------------------
//  VÌ SAO PHẢI LƯU: với remote một-nút-bập-bênh, bo không có cách nào ĐO được
//  máy đang bật hay tắt — nó chỉ nhớ lần cuối nó bắn gì. Giữ trong RAM thì mỗi
//  lần bo reset (mất điện chớp nhoáng, bấm nút EN, nạp lại firmware) niềm tin
//  đó về 0 trong khi máy xông vẫn đang chạy, và lệnh kế tiếp sẽ TẮT máy đúng
//  lúc phòng đang khô nhất.
//
//  ĐÂY KHÔNG PHẢI PHÉP MÀU. Nó chỉ cứu được lần reset của CHÍNH BO NÀY. Người
//  cầm remote bấm tay, hay rút điện máy xông, thì niềm tin vẫn sai và không
//  phần mềm nào biết được — xem settings.h §6.
//
//  Ghi THƯA, không ghi mỗi vòng lặp: NVS có giới hạn số lần xoá/ghi. Chỉ gọi
//  rememberOn() đúng lúc trạng thái ĐỔI, và Preferences tự bỏ qua khi giá trị
//  không đổi.
void rememberOn(bool on);
bool recallOn();

}  // namespace IrSlots
