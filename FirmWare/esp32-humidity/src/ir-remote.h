#pragma once
#include <Arduino.h>

// ============================================================================
//  Thu / phát hồng ngoại tới MÁY XÔNG TINH DẦU.
// ----------------------------------------------------------------------------
//  BẢN VIẾT LẠI GỌN của ../../esp32-s3-panel/src/ir-io.cpp. Sửa lỗi ở một bên
//  thì NGÓ SANG BÊN KIA — hai bài học đắt nhất của file đó đã mang sang
//  nguyên vẹn và ghi rõ trong .cpp:
//      1. enableIRIn() gọi chồng nhau -> mắt thu chết CÂM
//      2. tắt mắt thu trong lúc bắn -> khỏi tự thu lại khung của chính mình
//
//  KHÁC PANEL Ở ĐÂU, và vì sao:
//
//  - KHÔNG decode theo hãng, chỉ phát lại nguyên văn dạng sóng đã học. Giống
//    panel. Nhờ vậy đổi sang máy xông hãng khác KHÔNG phải nạp lại firmware —
//    và đây là lý do "học thủ công bằng remote" là cách làm đúng chứ không
//    phải giải pháp tạm.
//
//  - RAW_MAX nhỏ hơn nhiều. Remote điều hoà chở cả trạng thái (chế độ + nhiệt
//    độ + quạt + hẹn giờ) trong một khung nên cần 600 mốc; remote máy xông chỉ
//    là remote gia dụng thường (NEC và họ hàng, ~68 mốc). Vẫn để dư gấp nhiều
//    lần vì bộ đệm rẻ, nhưng không cần bằng panel.
// ============================================================================
namespace IrRemote {

/// Khung dài nhất chấp nhận được (số mốc mark/space). 300 mốc = 600 byte.
///
/// Dư gấp ~4 lần một khung NEC. Nếu bạn chĩa nhầm REMOTE ĐIỀU HOÀ vào mắt thu
/// thì khung của nó dài hơn mức này và sẽ bị từ chối kèm log rõ ràng — đúng
/// điều ta muốn: học nhầm remote vào slot BẬT là máy xông không bao giờ chạy,
/// mà không có lỗi nào để đọc.
static const uint16_t RAW_MAX = 300;

void begin(uint8_t txPin, uint8_t rxPin);

/// Bắn một khung ra máy xông (sóng mang 38kHz). Chặn ~10-70ms tuỳ độ dài khung.
void blast(const uint16_t *raw, uint16_t len);

/// Vào chế độ học, chờ tối đa [timeoutMs].
void learnStart(uint32_t timeoutMs);

bool learning();

/// Còn bao nhiêu mili giây nữa thì hết giờ chờ. 0 khi không ở chế độ học.
uint32_t learnRemainingMs();

/// Gọi mỗi vòng loop(). Trả về số mốc vừa bắt được (>0 = xong, tự thoát chế độ
/// học), 0 = chưa có gì. Nhiễu và khung dài bất thường bị bỏ, vẫn học tiếp.
uint16_t learnPoll(uint16_t *out, uint16_t maxLen);

/// true đúng MỘT lần, ngay sau khi hết giờ chờ mà không bắt được gì.
bool learnTimedOut();

/// Tên giao thức mà thư viện nhận ra ở khung VỪA BẮT ĐƯỢC ("NEC", "SONY"...),
/// hoặc "UNKNOWN" nếu không khớp giao thức nào.
///
/// ĐÂY LÀ PHÉP THỬ "MÃ THẬT HAY RÁC" — thứ mà độ dài khung không trả lời được.
/// Nhiễu đèn phòng vẫn có thể lọt qua bộ lọc độ dài và được cất vào NVS, rồi
/// mọi lần bắn sau đều vô hiệu trong khi log nói "da ban". Nhiễu thì gần như
/// không bao giờ giải mã ra một giao thức có tên; remote gia dụng thì gần như
/// luôn ra NEC hoặc họ hàng.
///
/// UNKNOWN KHÔNG CHẮC CHẮN LÀ HỎNG (có remote dùng giao thức lạ, và phát lại
/// nguyên văn vẫn chạy) — nhưng nó là lý do để học lại và xem có ổn định không.
const char *lastProtocol();

/// Số bit giải mã được; 0 nếu UNKNOWN.
uint16_t lastBits();

void learnStop();

}  // namespace IrRemote
