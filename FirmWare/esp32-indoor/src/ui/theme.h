#pragma once
#include <Arduino.h>   // isnan() và kiểu cơ bản — lvgl.h không kéo theo math.h
#include <lvgl.h>

// Font tiếng Việt sinh bằng lv_font_conv, xem chú thích ở phần Font bên dưới.
// PHẢI khai ở phạm vi toàn cục, ngoài mọi namespace.
LV_FONT_DECLARE(aircon_viet_12);
LV_FONT_DECLARE(aircon_viet_16);
LV_FONT_DECLARE(aircon_num_28);
LV_FONT_DECLARE(aircon_num_40);

// Ảnh nhúng, sinh bằng tools/make_lvgl_images.py từ Interface/LVGL/assets/.
// Cũng phải ở phạm vi toàn cục, cùng lý do với font: đặt trong namespace thì
// C++ mangle tên biến, còn file .c định nghĩa nó ở phạm vi toàn cục — và lỗi
// chỉ lộ ra lúc LIÊN KẾT, không phải lúc dịch.
LV_IMG_DECLARE(img_bg_tech);   // 320×240 nền công nghệ tối
LV_IMG_DECLARE(img_ac_unit);   // 44×26  dàn lạnh
LV_IMG_DECLARE(img_plus);      // 22×22  dấu +
LV_IMG_DECLARE(img_minus);     // 22×22  dấu −

// ============================================================================
//  Hệ thiết kế "Titanium Command" cho màn 320×240.
// ----------------------------------------------------------------------------
//  LẤY TỪ HAI NGUỒN, CÓ CHỦ ĐÍCH — đọc kỹ chỗ này trước khi sửa màu:
//
//    HÌNH HỌC  <- src/app/web/static/ds/tokens/foundation/shapes.css
//                 + ds/platforms/kiosk.css  (vát 45°, không bo tròn, viền dày)
//    MÀU       <- app-flutter/lib/theme/ac_colors.dart  (bảng CARBON TỐI)
//
//  Nghe như mâu thuẫn nhưng không: hình học là thứ nhận diện thương hiệu và nó
//  GIỐNG NHAU ở mọi nơi; màu thì theo NGỮ CẢNH DÙNG. Panel treo tường phục vụ
//  khách hàng trong phòng — cùng đối tượng, cùng môi trường với app điện thoại
//  (nền tối). Web admin nền sáng #f0f2f5 là màn của nhân viên, xem trên laptop
//  giữa ban ngày. Bê nguyên nền sáng lên panel thì ban đêm chói vào mặt.
//
//  Neo giữ hai hệ lại với nhau là MÀU NHẤN #0055FF — giống hệt nhau ở cả web
//  lẫn app. Đổi màu nhấn là đứt liên kết đó.
//
//  BA CHỮ KÝ CỦA HỆ THIẾT KẾ, không được phép sai:
//
//  1. KHÔNG BO TRÒN GÓC. shapes.css ghi thẳng "NO ROUNDED CORNERS" — chỉ vát
//     45°, và chỉ ở HAI góc: TRÊN-TRÁI + DƯỚI-PHẢI. Vát cả bốn góc là một biểu
//     tượng khác hẳn, không phải khác biệt thẩm mỹ nhỏ.
//  2. NHÃN VIẾT HOA, GIÃN CHỮ. Thứ làm giao diện đọc ra "kỹ thuật".
//  3. VIỀN DÀY hơn web thường (kiosk.css: --border-width-md 2px) cho dễ nhìn ở
//     khoảng cách đứng.
//
//  Cỡ vát quy đổi theo màn: web dùng 8/12/20 px trên khung rộng 1920. Giữ
//  nguyên ở 320 px thì góc vát ăn hết cả nút — rút còn 4/6/10.
// ============================================================================
namespace Theme {

// --- Màu: bảng TỐI, lấy từ app-flutter/lib/theme/ac_colors.dart --------------
//  VÌ SAO THEO APP CHỨ KHÔNG THEO WEB ADMIN (web nền sáng #f0f2f5):
//  người đứng trước panel treo tường là KHÁCH HÀNG — họ cũng dùng app trên điện
//  thoại, vốn đã nền tối. Web admin là màn của NHÂN VIÊN bán hàng, xem trên
//  laptop. Panel nền trắng còn chói vào ban đêm trong phòng ngủ.
//
//  Điểm quan trọng: MÀU NHẤN #0055FF GIỐNG HỆT NHAU ở cả hai hệ. Chỉ nhóm màu
//  trung tính lật, còn chữ ký thương hiệu thì không đổi — nên panel vẫn đọc ra
//  "cùng một sản phẩm" với web.
//  ĐÂY LÀ NGUỒN SỰ THẬT DUY NHẤT CHO MÀU. Bảng này khớp đúng dải màu ở chân
//  trang Interface/mockup.svg — sửa ở đây thì chạy lại tools/make_mockup.py,
//  đừng để hai bên nói hai chuyện khác nhau.
//
//  Nền màn (#0C193B, xanh navy) và nền thẻ (#141C28, xám đá) gần bằng nhau về
//  độ sáng nhưng KHÁC TÔNG — thẻ tách khỏi nền nhờ sắc độ cộng với viền sáng
//  #3E5468, không phải nhờ chênh lệch sáng tối. Đây là chủ ý: chênh lệch sáng
//  tối lớn trên panel treo tường ban đêm sẽ thành từng ô chói.
inline lv_color_t bgPrimary()     { return lv_color_hex(0x0C193B); } // nền màn
inline lv_color_t bgSecondary()   { return lv_color_hex(0x141C28); } // nền thẻ
inline lv_color_t bgElevated()    { return lv_color_hex(0x1E2B3C); } // nền nút — nổi trên thẻ
inline lv_color_t bgSubtle()      { return lv_color_hex(0x101722); } // nút đã tắt, chìm xuống
inline lv_color_t textPrimary()   { return lv_color_hex(0xE7F1F8); }
inline lv_color_t textMuted()     { return lv_color_hex(0x8DA2B5); }
inline lv_color_t textCode()      { return lv_color_hex(0x8DA2B5); }
inline lv_color_t borderSubtle()  { return lv_color_hex(0x2A3B4C); } // carbonLine
inline lv_color_t borderDefault() { return lv_color_hex(0x3E5468); } // carbonLineBright
inline lv_color_t borderStrong()  { return lv_color_hex(0xE7F1F8); }
inline lv_color_t accent()        { return lv_color_hex(0x0055FF); } // ice — GIỐNG web
inline lv_color_t accentDown()    { return lv_color_hex(0x0044CC); }
inline lv_color_t accentText()    { return lv_color_hex(0x4D8DFF); } // iceText — số nhấn trên nền tối
inline lv_color_t ok()            { return lv_color_hex(0x22C55E); }
inline lv_color_t err()           { return lv_color_hex(0xFF4D4D); }
inline lv_color_t warn()          { return lv_color_hex(0xF5A623); }

/// Chữ ĐEN hay TRẮNG trên nền [bg] — chọn theo độ sáng cảm nhận, không đoán.
///
/// Cần vì huy hiệu và nút chính dùng nhiều màu nền khác nhau: trắng trên xanh
/// #0055FF thì rõ, nhưng trắng trên hổ phách #F5A623 thì gần như không đọc nổi.
/// Hardcode một màu chữ là chắc chắn sai ở một nửa số trường hợp.
lv_color_t onColor(lv_color_t bg);

/// Màu theo THANG NHIỆT — nói Ý NGHĨA (lạnh / dễ chịu / ấm / nóng), tuyệt đối
/// không nói NGUỒN ("cảm biến trong hay ngoài"). Cùng luật với --ac-cold /
/// --ac-neutral / --ac-warm / --ac-hot trong app.css.
lv_color_t thermal(float tempC);

// --- Lưới: HỢP ĐỒNG với ../../Interface/README.md §5 -------------------------
//  Sửa số ở đây thì sửa cả wireframe bên đó, nếu không lần sau đọc tài liệu ra sai.
enum : lv_coord_t {
  SCREEN_W  = 320, SCREEN_H = 240,
  PAD       = 6,
  STATUS_H  = 22,          // y 0..21
  CONTENT_Y = 24, CONTENT_H = 180,   // y 24..203
  NAV_Y     = 206, NAV_H = 34, NAV_W = 80,   // 4 tab × 80
  CH_SM = 4, CH_MD = 6, CH_LG = 10           // độ vát
};

/// Vát góc TRÊN-TRÁI + DƯỚI-PHẢI cho một đối tượng LVGL.
///
/// LVGL không có kiểu vát — `radius` chỉ bo tròn. Nên vẽ tay ở
/// LV_EVENT_DRAW_POST_END: hai tam giác tô màu nền phía sau để "cắt" góc đi,
/// rồi hai đoạn chéo màu viền để đường viền liền mạch. Rẻ hơn hẳn dựng mask, và
/// giữ đúng chữ ký hình học.
///
/// [bgBehind] là màu NGAY PHÍA SAU đối tượng (nền màn, hoặc nền thẻ nếu đối
/// tượng nằm trong thẻ). Sai màu này thì góc lộ vệt.
void chamfer(lv_obj_t *obj, lv_coord_t size, lv_color_t bgBehind);

// --- Font: TIẾNG VIỆT CÓ DẤU -------------------------------------------------
//  Sinh bằng tools/make_lvgl_fonts.ps1 (lv_font_conv) từ Arial, xuất ra
//  fonts/aircon_*.c. KHÔNG dùng Montserrat dựng sẵn của LVGL: nó chỉ có ASCII
//  + ° + •, nên "LÀM LẠNH" ra ô vuông hoặc mất dấu.
//
//  Hai nhóm font, khác nhau về chủ đích:
//   - viet_12 / viet_16 : đủ 0x1EA0-0x1EF9 (toàn bộ dấu tiếng Việt), 2 bpp.
//     2 bpp thay vì 4 để cắt nửa dung lượng — trên màn 2.8" ở khoảng cách đứng
//     mắt không phân biệt được, mà tiết kiệm ~70 KB flash.
//   - num_28 / num_40  : CHỈ chữ số + ° C % + - . — nhiệt độ và setpoint không
//     bao giờ chứa chữ tiếng Việt, nhúng cả bảng dấu vào cỡ 40 px là phí ~90 KB.
//     4 bpp vì số lớn nằm ngay tầm mắt, răng cưa lộ rõ.
//  Khai báo nằm NGOÀI namespace Theme (ở đầu file) — đặt trong namespace thì
//  trình biên dịch mangle thành `Theme::aircon_num_40` còn file font định nghĩa
//  ở phạm vi toàn cục, và lỗi chỉ lộ ra lúc LIÊN KẾT chứ không phải lúc dịch.

const lv_font_t *fontHero();   // 40, chỉ số — nhiệt độ / setpoint lớn
const lv_font_t *fontBig();    // 28, chỉ số — số vừa
const lv_font_t *fontTitle();  // 16 đậm, có dấu — tiêu đề thẻ
const lv_font_t *fontBody();   // 16 đậm, có dấu — chữ thường
const lv_font_t *fontLabel();  // 12, có dấu — nhãn
const lv_font_t *fontTiny();   // 12, có dấu — chú thích

// --- Tiện dựng ---------------------------------------------------------------
/// Gọi một lần trong Ui::begin(), sau lv_init().
void init();

/// Thẻ nền trắng, viền mảnh, đã vát góc.
lv_obj_t *card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h);

/// Nút. [primary] = nền xanh nhấn (hành động chính); ngược lại nền trắng viền.
lv_obj_t *button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                 const char *text, bool primary = false);

/// Nút mang ẢNH thay vì chữ (tăng / giảm nhiệt độ).
///
/// Dùng ảnh chứ không phải ký tự "+" và "-" của font vì dấu trừ ASCII (0x2D) là
/// GẠCH NỐI — nó ngắn cũn và nằm lệch trên đường giữa, đặt cạnh dấu "+" to đùng
/// thì thành một cặp lệch nhau. Dấu trừ toán học thật (U+2212) lại không có
/// trong bộ ký tự font số. Ảnh giải quyết cả hai, và khớp luôn với bộ icon
/// người dùng đã vẽ sẵn.
lv_obj_t *buttonImg(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                    const lv_img_dsc_t *src);

/// Gắn tiếng kêu khi bấm. ui.cpp truyền BoardIo::beep vào đây.
///
/// Đi qua con trỏ hàm chứ không #include "board-io.h": theme là tầng VẼ, nó
/// không được biết bo mạch có còi hay không. Đảo lại thì mỗi lần đổi phần cứng
/// là phải sửa cả file thiết kế giao diện.
void setPressSound(void (*fn)());

/// Cho một nút TỰ TẠO (không qua button()) có đủ phản hồi: đổi màu + kêu.
/// Dùng cho các nút tab, vốn dựng thẳng bằng lv_btn_create.
void pressFeedback(lv_obj_t *btn);

/// Nút đang được chọn (chế độ đang chạy) — tô nền nhấn.
void buttonSelect(lv_obj_t *btn, bool selected);

/// Mờ + không bấm được (chưa học mã IR). KHÔNG ẩn nút: người dùng phải thấy nó
/// tồn tại và biết vì sao chưa dùng được — "không phím chết" (README §4.1).
void buttonDisable(lv_obj_t *btn, bool disabled);

/// Nhãn thường. [font]=nullptr dùng fontBody().
lv_obj_t *label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const char *text,
                const lv_font_t *font = nullptr, lv_color_t color = textPrimary());

/// Nhãn HOA giãn chữ, màu --text-muted — kiểu nhãn của hệ thiết kế.
lv_obj_t *labelCaps(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const char *text);

// --- Token chuyển động (design-guiline/src/styles/animations/motion-tokens.css)
//  Chép nguyên số từ hệ thiết kế để panel và web nhịp giống nhau. LVGL không có
//  cubic-bezier tuỳ ý; lv_anim_path_ease_out là xấp xỉ gần nhất của --ease-out.
enum : uint32_t {
  DUR_INSTANT = 100,   // phản hồi chạm
  DUR_FAST    = 150,   // đổi màu, bật/tắt
  DUR_NORMAL  = 250,   // chọn thẻ, đổi trạng thái
  DUR_SLOW    = 400,   // chuyển trang
  DUR_SHIMMER = 1500   // một vòng quét của skeleton
};

/// Thanh SKELETON: chỗ giữ sẵn cho số liệu CHƯA VỀ, có vệt sáng quét ngang.
///
/// Vì sao không để "--": dấu gạch trông như một GIÁ TRỊ, và là giá trị sai. Người
/// đứng trước panel không phân biệt được "chưa đo xong" với "cảm biến hỏng".
/// Vệt sáng chuyển động nói rõ là ĐANG CHỜ — không cần một chữ nào.
///
/// Bản web (effects.css) làm bằng gradient 3 điểm dừng chạy background-position.
/// LVGL 8.3 chỉ có gradient 2 điểm, nên ở đây là một ô sáng trượt ngang bên
/// trong ô nền — cùng hiệu ứng, và rẻ hơn vì chỉ vẽ lại đúng vùng ô sáng.
lv_obj_t *skeleton(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                   lv_coord_t w, lv_coord_t h, uint32_t delayMs = 0);

/// Hiện/ẩn một skeleton. Ẩn thì dừng luôn animation — để chạy tiếp là vẽ lại
/// vô ích một vùng không ai nhìn thấy, 20 ms một lần, mãi mãi.
void skeletonShow(lv_obj_t *sk, bool show);

/// Cho các phần tử con của [page] TRƯỢT VÀO từ hai bên rồi về đúng chỗ.
/// Con thứ chẵn vào từ trái, con thứ lẻ vào từ phải.
///
/// [dist] CỐ Ý NGẮN (28 px, không phải cả màn) và [ms] cố ý nhanh. Đây là ràng
/// buộc của phần cứng chứ không phải khẩu vị: SPI 27 MHz đẩy hết 320×240 mất
/// ~57 ms, nên một cú trượt cả trang chỉ kịp vài khung hình và ra giật cục —
/// tức là làm hiệu ứng cho mượt lại thành kém mượt hơn không có. Trượt ngắn thì
/// vùng phải vẽ lại nhỏ, khung hình kịp, và mắt đọc ra là "khớp vào vị trí".
///
/// Vị trí đích của mỗi con được nhớ trong user_data ngay lần gọi đầu, nên bấm
/// đổi tab liên tục lúc animation đang chạy dở cũng không bị trôi dần.
void slideIn(lv_obj_t *page, lv_coord_t dist = 28, uint32_t ms = DUR_FAST);

/// Huy hiệu nhỏ (TU DONG / GHI DE). [bg] nền, chữ trắng.
lv_obj_t *badge(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                const char *text, lv_color_t bg);

} // namespace Theme
