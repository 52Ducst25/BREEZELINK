#pragma once
#include <Arduino.h>

// ============================================================================
//  Thu / phát hồng ngoại (module "IR Receiver" + "IR Transmitter" 3 chân).
// ----------------------------------------------------------------------------
//  Hai luồng dùng chung phần cứng này:
//
//  PHÁT  — backend gửi cmd kèm mảng thời gian, node bắn lại nguyên văn ra máy
//          lạnh. Không decode theo hãng: mỗi hãng một protocol, cứ phát lại
//          đúng dạng sóng đã học là chạy được với mọi máy.
//
//  HỌC   — backend gửi {"learn":"COOL 25"}, node bật mắt thu chờ người dùng
//          bấm remote thật, bắt lấy dạng sóng rồi gửi ngược lên topic learn.
//          Nhờ vậy thêm máy lạnh hãng mới KHÔNG phải nạp lại firmware.
//
//  Mắt thu chỉ được bật trong lúc HỌC, không bật thường trực: LED phát nằm
//  ngay cạnh mắt thu trên cùng một bo, bật cả hai thì node tự thu lại chính
//  khung nó vừa bắn.
// ============================================================================
namespace IrIo {

/// Khung lệnh dài nhất chấp nhận được (số mốc mark/space). Remote điều hoà gửi
/// cả trạng thái (mode+temp+quạt+hẹn giờ) trong một khung nên dài hơn remote TV
/// nhiều — 600 mốc phủ hết các hãng phổ thông, mà vẫn chỉ tốn 1.2KB RAM.
static const uint16_t RAW_MAX = 600;

void begin(uint8_t txPin, uint8_t rxPin);

/// Bắn một khung ra máy lạnh (sóng mang 38kHz). Chặn ~50-250ms tuỳ độ dài khung.
void blast(const uint16_t *raw, uint16_t len);

/// Vào chế độ học, chờ tối đa [timeoutMs].
void learnStart(uint32_t timeoutMs);

bool learning();

/// Còn bao nhiêu mili giây nữa thì hết giờ chờ. 0 khi không ở chế độ học.
/// Có để màn hình đếm ngược cho người đang cầm remote — không có nó thì họ chỉ
/// thấy một dòng chữ đứng im, không biết còn kịp hay đã hết giờ.
uint32_t learnRemainingMs();

/// Gọi mỗi vòng loop(). Trả về số mốc vừa bắt được (>0 = xong, tự thoát chế độ
/// học), 0 = chưa có gì. Nhiễu và khung dài bất thường bị bỏ, vẫn học tiếp.
uint16_t learnPoll(uint16_t *out, uint16_t maxLen);

/// true đúng MỘT lần, ngay sau khi hết giờ chờ mà không bắt được gì.
bool learnTimedOut();

void learnStop();

} // namespace IrIo
