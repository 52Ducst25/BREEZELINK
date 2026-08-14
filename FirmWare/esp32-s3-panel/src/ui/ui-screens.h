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

/// Người dùng bấm trong màn CAI DAT. ui.cpp thi hành (đèn nền, NTP, reset) vì đó
/// là phần cứng thuộc quyền tác vụ UI.
///
/// KHÔNG CÒN BUZZER_ON / BUZZER_OFF. Hàng "ÂM THANH" đã bị gỡ khỏi màn Cài đặt:
/// bo panel ESP32-S3 KHÔNG CÓ CÒI (board-pins.h: `BUZZER_PIN = PIN_NONE` — bốn
/// chân tự do đã đi hết cho IR và UART sang UNO Q). Một công tắc bật/tắt cho phần
/// cứng không tồn tại là kiểu điều khiển tệ nhất: nó bấm được, nó đổi màu, và nó
/// không làm gì cả — người dùng sẽ đi tìm lỗi ở loa.
///
/// Tiếng bấm thì GIỮ NGUYÊN đường dây (Theme::setPressSound -> BoardIo::beep):
/// beep() tự im khi chân = 255, còn bo QR Box cũ dùng chung mã nguồn này thì vẫn
/// có còi thật.
enum Setting : uint8_t { BRIGHT_DOWN, BRIGHT_UP, REBOOT };
using SettingFn = void (*)(Setting s);

/// Dựng toàn bộ. Gọi một lần trong Ui::begin(), sau Theme::init().
void build(CommandFn onCmd, SettingFn onSetting);

/// Đổ số liệu mới vào. Gọi theo nhịp vẽ (200 ms) từ tác vụ UI.
void update(const Ui::Model &m);

/// Dòng thông báo ngắn ở đáy vùng nội dung. [isError] tô đỏ.
void toast(const char *msg, bool isError = false);

/// Gọi mỗi vòng tác vụ UI để tự tắt toast sau vài giây.
void tickToast(uint32_t nowMs);

/// Đồng hồ trên thanh trạng thái, có GIÂY. [valid]=false hiện "--:--:--" —
/// DS1307 chưa từng được đặt giờ thì nói thẳng, không hiện 00:00:00 như thể đó
/// là giờ thật.
///
/// Giây có ích hơn vẻ ngoài của nó: nó là dấu hiệu SỐNG duy nhất trên màn khi
/// mọi số đo đứng yên — nhìn giây nhảy là biết tác vụ giao diện còn chạy, không
/// phải màn đã treo với một khung hình cũ.
void setClock(bool valid, uint8_t hh, uint8_t mm, uint8_t ss);

/// Phản ánh trạng thái phần cứng lên màn CAI DAT.
void setBrightness(uint8_t percent);

/// Thêm một dòng vào nhật ký lệnh (mới nhất lên đầu, giữ 8 dòng gần nhất).
///
/// [clockValid]/[hh]/[mm] là giờ DS1307 đọc được lúc nhận. Chưa từng đặt giờ thì
/// clockValid=false và dòng đó hiện mốc TƯƠNG ĐỐI tính từ [nowSec] (uptime giây)
/// — "3 phút trước". Không bịa 00:00: cùng luật với đồng hồ trên thanh trạng
/// thái, thà nói "không biết mấy giờ" còn hơn đưa ra một con số sai.
void addLog(const Ui::CmdLog &e, bool clockValid, uint8_t hh, uint8_t mm, uint32_t nowSec);

} // namespace Screens
