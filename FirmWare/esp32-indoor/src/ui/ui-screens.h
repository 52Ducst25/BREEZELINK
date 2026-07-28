#pragma once
#include <lvgl.h>
#include "ui.h"

// ============================================================================
//  Dựng và cập nhật cây widget LVGL của 4 màn + lớp phủ học remote.
// ----------------------------------------------------------------------------
//  TÁCH KHỎI ui.cpp để mỗi file một việc: ui.cpp lo cổng nối LVGL với phần cứng
//  (màn, cảm ứng, tác vụ, hàng đợi lệnh), file này lo NỘI DUNG hiển thị.
//
//  CHỈ ĐƯỢC GỌI TỪ TÁC VỤ UI. LVGL không an toàn đa luồng — loop() chạm vào là
//  hỏng cây widget theo kiểu ngẫu nhiên, rất khó tìm.
//
//  Dựng MỘT LẦN rồi chỉ đổi nội dung: LVGL tự biết vùng nào bẩn và chỉ vẽ lại
//  đúng vùng đó. Đây là thứ bản TFT_eSPI phải làm tay bằng cách giữ bản sao
//  "đã vẽ lần trước" cho từng trường.
// ============================================================================
namespace Screens {

/// Người dùng bấm GUI / TU DONG. ui.cpp đẩy tiếp vào hàng đợi cho loop().
using CommandFn = void (*)(const Ui::Command &cmd);

/// Người dùng bấm trong màn CAI DAT. ui.cpp thi hành (đèn nền, còi, NTP, reset)
/// vì đó là phần cứng thuộc quyền tác vụ UI.
///
/// BUZZER_ON và BUZZER_OFF là HAI mục riêng, không phải một BUZZER_TOGGLE.
/// Trước đây cả nút BẬT lẫn nút TẮT cùng gửi TOGGLE, nên hành vi phụ thuộc vào
/// trạng thái hiện tại chứ không phải vào nút vừa bấm: bấm TẮT hai lần là còi
/// bật lại, bấm BẬT lúc đang bật là thành tắt. Nút có nhãn khẳng định ("BẬT")
/// thì phải LUÔN cho ra trạng thái đó, bấm bao nhiêu lần cũng vậy.
enum Setting : uint8_t { BRIGHT_DOWN, BRIGHT_UP, BUZZER_ON, BUZZER_OFF, NTP_SYNC, REBOOT };
using SettingFn = void (*)(Setting s);

/// Dựng toàn bộ. Gọi một lần trong Ui::begin(), sau Theme::init().
void build(CommandFn onCmd, SettingFn onSetting);

/// Đổ số liệu mới vào. Gọi theo nhịp vẽ (200 ms) từ tác vụ UI.
void update(const Ui::Model &m);

/// Dòng thông báo ngắn ở đáy vùng nội dung. [isError] tô đỏ.
void toast(const char *msg, bool isError = false);

/// Gọi mỗi vòng tác vụ UI để tự tắt toast sau vài giây.
void tickToast(uint32_t nowMs);

/// Đồng hồ trên thanh trạng thái. [valid]=false hiện "--:--" — DS1307 chưa
/// từng được đặt giờ thì nói thẳng, không hiện 00:00 như thể đó là giờ thật.
void setClock(bool valid, uint8_t hh, uint8_t mm);

/// Phản ánh trạng thái phần cứng lên màn CAI DAT.
void setBrightness(uint8_t percent);
void setBuzzer(bool on);

} // namespace Screens
