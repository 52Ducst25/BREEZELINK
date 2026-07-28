#include "theme.h"

// ============================================================================
//  Hiện thực hệ thiết kế bằng LVGL. Xem theme.h cho lý do từng lựa chọn.
// ============================================================================
namespace Theme {

// --- Thang nhiệt -------------------------------------------------------------
lv_color_t thermal(float c) {
  // Không có số đo -> màu "không biết". KHÔNG trả màu lạnh: xanh dương đọc ra
  // "phòng đang mát", tức là khẳng định một điều ta không biết.
  if (isnan(c)) return textMuted();
  if (c < 22.0f) return accent();   // --ac-cold  = --status-info (xanh kỹ thuật)
  if (c < 27.0f) return ok();       // --ac-neutral = --status-success
  if (c < 32.0f) return warn();     // --ac-warm  = --status-warning
  return err();                     // --ac-hot   = --status-error
}

// --- Chữ trên nền màu --------------------------------------------------------
lv_color_t onColor(lv_color_t bg) {
  // Độ sáng cảm nhận theo ITU-R BT.601 — mắt nhạy với lục hơn lam rất nhiều,
  // nên trung bình cộng (r+g+b)/3 cho kết quả sai: hổ phách #F5A623 và xanh
  // #0055FF ra gần bằng nhau, trong khi mắt thấy cái này sáng, cái kia tối.
  const uint32_t c = lv_color_to32(bg);
  const uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
  const uint32_t luma = (r * 299 + g * 587 + b * 114) / 1000;
  return luma > 140 ? lv_color_hex(0x0A0E14) : textPrimary();
}

// --- Font: tiếng Việt có dấu (xem theme.h cho lý do chia 2 nhóm) -------------
const lv_font_t *fontHero()  { return &aircon_num_40; }
const lv_font_t *fontBig()   { return &aircon_num_28; }
const lv_font_t *fontTitle() { return &aircon_viet_16; }
const lv_font_t *fontBody()  { return &aircon_viet_16; }
const lv_font_t *fontLabel() { return &aircon_viet_12; }
const lv_font_t *fontTiny()  { return &aircon_viet_12; }

// --- Vát góc -----------------------------------------------------------------
//  Cấp phát một lần cho mỗi đối tượng và KHÔNG giải phóng: mọi đối tượng ở đây
//  sống suốt đời chương trình (dựng một lần lúc khởi động rồi chỉ đổi nội dung).
//  Nếu sau này có màn nào tạo/xoá động thì phải bắt LV_EVENT_DELETE để trả bộ nhớ.
struct ChamferInfo {
  lv_coord_t size;
  lv_color_t bg;
};

static void chamferDraw(lv_event_t *e) {
  auto *ci = static_cast<ChamferInfo *>(lv_event_get_user_data(e));
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  if (ci == nullptr || ctx == nullptr) return;

  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  const lv_coord_t c = ci->size;

  // 1) Cắt góc: tô hai tam giác bằng màu nền PHÍA SAU đối tượng.
  lv_draw_rect_dsc_t fill;
  lv_draw_rect_dsc_init(&fill);
  fill.bg_color = ci->bg;
  fill.bg_opa   = LV_OPA_COVER;

  lv_point_t tl[3] = {{a.x1, a.y1}, {(lv_coord_t)(a.x1 + c), a.y1}, {a.x1, (lv_coord_t)(a.y1 + c)}};
  lv_draw_triangle(ctx, &fill, tl);
  lv_point_t br[3] = {{a.x2, a.y2}, {(lv_coord_t)(a.x2 - c), a.y2}, {a.x2, (lv_coord_t)(a.y2 - c)}};
  lv_draw_triangle(ctx, &fill, br);

  // 2) Nối lại đường viền bằng hai đoạn chéo — thiếu bước này thì viền bị hở
  //    đúng ở góc, nhìn như lỗi hiển thị chứ không ra hình vát.
  const lv_coord_t bw = lv_obj_get_style_border_width(obj, LV_PART_MAIN);
  if (bw > 0) {
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_obj_get_style_border_color(obj, LV_PART_MAIN);
    ld.width = bw;
    ld.opa   = LV_OPA_COVER;
    lv_point_t p1 = {a.x1, (lv_coord_t)(a.y1 + c)}, p2 = {(lv_coord_t)(a.x1 + c), a.y1};
    lv_draw_line(ctx, &ld, &p1, &p2);
    lv_point_t p3 = {(lv_coord_t)(a.x2 - c), a.y2}, p4 = {a.x2, (lv_coord_t)(a.y2 - c)};
    lv_draw_line(ctx, &ld, &p3, &p4);
  }
}

void chamfer(lv_obj_t *obj, lv_coord_t size, lv_color_t bgBehind) {
  auto *ci = static_cast<ChamferInfo *>(lv_mem_alloc(sizeof(ChamferInfo)));
  if (ci == nullptr) return;   // hết RAM: thà mất góc vát còn hơn treo máy
  ci->size = size;
  ci->bg   = bgBehind;
  lv_obj_add_event_cb(obj, chamferDraw, LV_EVENT_DRAW_POST_END, ci);
}

// --- Tiếng bấm ---------------------------------------------------------------
static void (*gPressSound)() = nullptr;

void setPressSound(void (*fn)()) { gPressSound = fn; }

static void pressCb(lv_event_t *) {
  if (gPressSound) gPressSound();
}

// --- Kiểu dùng chung ---------------------------------------------------------
static lv_style_t sCard, sBtn, sBtnPrimary, sBtnSel, sBtnOff, sScreen, sBtnPressed;

void pressFeedback(lv_obj_t *btn) {
  if (btn == nullptr) return;
  lv_obj_add_style(btn, &sBtnPressed, LV_STATE_PRESSED);
  // LV_EVENT_PRESSED chứ không phải CLICKED: kêu ngay lúc ngón tay CHẠM XUỐNG.
  // CLICKED chỉ bắn khi nhấc tay ra, nên tiếng kêu tới sau cả nửa giây nếu người
  // dùng giữ nút — lúc đó nó không còn là phản hồi nữa mà thành tiếng lạ.
  lv_obj_add_event_cb(btn, pressCb, LV_EVENT_PRESSED, nullptr);
}

void init() {
  // Nền màn: MÀU PHẲNG. Đã bỏ ảnh nền 12.png, và đây là quyết định về HIỆU NĂNG
  // chứ không phải khẩu vị thẩm mỹ:
  //
  // Với ảnh nền, mỗi lần bấm một nút là LVGL phải dựng lại vùng đó theo thứ tự
  // đọc ảnh RGB565 từ flash -> trộn alpha nền thẻ -> trộn alpha viền -> chạy
  // callback vẽ 2 tam giác + 2 đường vát -> trộn alpha nền nút -> vẽ chữ. Sáu
  // lớp cho một cái nhấn. Trên tấm SPI 27 MHz thì mắt NHÌN THẤY từng dải quét —
  // đó chính là cái "nhấp nháy khi bấm".
  //
  // Nền phẳng + thẻ đặc: mỗi lần bấm chỉ còn tô một hình chữ nhật. Ảnh nền vẫn
  // nằm trong src/ui/images/ nếu sau này đổi sang tấm màn nhanh hơn (RGB song
  // song, hoặc chip có PSRAM) — chỉ cần bật lại một dòng.
  lv_style_init(&sScreen);
  lv_style_set_bg_color(&sScreen, bgPrimary());
  lv_style_set_bg_opa(&sScreen, LV_OPA_COVER);
  lv_style_set_border_width(&sScreen, 0);
  lv_style_set_radius(&sScreen, 0);
  lv_style_set_pad_all(&sScreen, 0);

  // Thẻ ĐỤC HOÀN TOÀN. Bản trước để 216/255 cho ảnh nền lộ qua ("kính mờ") —
  // đẹp trên ảnh dựng, nhưng mỗi điểm ảnh phải trộn alpha lúc chạy, và nó nằm
  // đúng trên đường vẽ lại khi bấm nút. Nền đã phẳng thì trong suốt cũng chẳng
  // lộ ra gì để nhìn, nên đây là bỏ chi phí mà không mất gì.
  //
  // Độ tương phản với nền màn giờ do MÀU tạo ra (#0B1119 nền so với #18212E
  // thẻ), không nhờ lớp phủ. Cách này còn đọc rõ hơn ở góc nhìn xiên — tấm TFT
  // rẻ tiền bị nhạt màu khi nhìn chéo, mà lớp alpha thì nhạt trước tiên.
  lv_style_init(&sCard);
  lv_style_set_bg_color(&sCard, bgSecondary());
  lv_style_set_bg_opa(&sCard, LV_OPA_COVER);
  lv_style_set_border_color(&sCard, borderDefault());
  lv_style_set_border_width(&sCard, 1);
  lv_style_set_radius(&sCard, 0);          // KHÔNG BO TRÒN — chữ ký hệ thiết kế
  lv_style_set_pad_all(&sCard, 0);
  lv_style_set_text_color(&sCard, textPrimary());
  lv_style_set_text_font(&sCard, fontBody());

  // Nút thường: nền sáng hơn thẻ một bậc + viền 2 px (kiosk.css --border-width-md).
  // Chỗ bấm được phải TỰ NÓ nói ra là bấm được, và trên panel treo tường thì
  // phải nói được từ khoảng cách đứng — nên tách bằng cả màu nền lẫn độ dày viền.
  lv_style_init(&sBtn);
  lv_style_set_bg_color(&sBtn, bgElevated());
  lv_style_set_bg_opa(&sBtn, LV_OPA_COVER);
  lv_style_set_border_color(&sBtn, borderDefault());
  lv_style_set_border_width(&sBtn, 2);
  lv_style_set_radius(&sBtn, 0);
  lv_style_set_text_color(&sBtn, textPrimary());
  lv_style_set_text_font(&sBtn, fontLabel());
  lv_style_set_shadow_width(&sBtn, 0);

  // Trạng thái ĐANG BỊ NGÓN TAY ĐÈ. Hệ thiết kế (.btn:active) dùng scale(0.96);
  // ở đây đổi sang đảo màu vì transform_zoom bắt LVGL vẽ lại nút có phép co
  // giãn ở MỖI khung — đắt, mà trên tấm SPI này thì phản hồi chậm còn tệ hơn là
  // không có hiệu ứng. Đảo màu tức thì đúng tinh thần "chính xác, cơ khí" mà
  // guideline đòi, và tốn đúng một lần tô lại.
  //
  // Quan trọng hơn cả thẩm mỹ: đây là thứ DUY NHẤT nói cho người dùng biết máy
  // đã nhận cú chạm, trước cả khi lệnh đi tới máy lạnh.
  lv_style_init(&sBtnPressed);
  lv_style_set_bg_color(&sBtnPressed, accent());
  lv_style_set_border_color(&sBtnPressed, accentText());
  lv_style_set_text_color(&sBtnPressed, textPrimary());

  // Nút hành động chính: nền xanh nhấn, ĐỤC HẲN.
  // Đục 100% là cố ý: nút chính phải trông đặc, không "kính". Nền tối lộ qua sau
  // màu nhấn sẽ làm xanh #0055FF xỉn đi và mất luôn vai trò dẫn mắt.
  lv_style_init(&sBtnPrimary);
  lv_style_set_bg_color(&sBtnPrimary, accent());
  lv_style_set_bg_opa(&sBtnPrimary, LV_OPA_COVER);
  lv_style_set_border_color(&sBtnPrimary, accentText());
  lv_style_set_text_color(&sBtnPrimary, textPrimary());

  // Nút đang được chọn (chế độ máy lạnh đang chạy).
  lv_style_init(&sBtnSel);
  lv_style_set_bg_color(&sBtnSel, accent());
  lv_style_set_bg_opa(&sBtnSel, LV_OPA_COVER);
  lv_style_set_border_color(&sBtnSel, accentText());
  lv_style_set_text_color(&sBtnSel, textPrimary());

  // Nút mờ: chưa học mã IR. Vẫn thấy được, chỉ là bấm không ăn. Trong suốt hơn
  // hẳn phần còn lại — đó chính là tín hiệu "chưa dùng được".
  lv_style_init(&sBtnOff);
  lv_style_set_bg_color(&sBtnOff, bgSubtle());
  lv_style_set_bg_opa(&sBtnOff, LV_OPA_COVER);
  lv_style_set_border_color(&sBtnOff, borderSubtle());
  lv_style_set_text_color(&sBtnOff, textMuted());

  lv_obj_add_style(lv_scr_act(), &sScreen, 0);
}

// --- Tiện dựng ---------------------------------------------------------------
lv_obj_t *card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_add_style(o, &sCard, 0);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  chamfer(o, CH_MD, bgPrimary());
  return o;
}

lv_obj_t *button(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                 const char *text, bool primary) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_add_style(b, &sBtn, 0);
  if (primary) lv_obj_add_style(b, &sBtnPrimary, 0);
  pressFeedback(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);

  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  // Nhãn nút viết HOA + giãn chữ — kiểu chữ của hệ thiết kế. LVGL không có
  // text-transform nên chuỗi truyền vào phải sẵn chữ hoa.
  lv_obj_set_style_text_letter_space(l, 1, 0);

  chamfer(b, CH_SM, bgPrimary());
  return b;
}

lv_obj_t *buttonImg(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                    const lv_img_dsc_t *src) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_add_style(b, &sBtn, 0);
  pressFeedback(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);

  lv_obj_t *im = lv_img_create(b);
  lv_img_set_src(im, src);
  lv_obj_center(im);
  // Icon nguồn màu trắng gần như thuần (#FEFEFE) -> nhuộm lại theo màu chữ, để
  // sau này đổi bảng màu thì icon đi theo chứ không kẹt lại màu trắng.
  lv_obj_set_style_img_recolor(im, textPrimary(), 0);
  lv_obj_set_style_img_recolor_opa(im, LV_OPA_COVER, 0);
  // Bấm vào icon phải tính là bấm nút — mặc định lv_img KHÔNG cho sự kiện đi
  // xuyên qua, nên giữa nút sẽ thành vùng chết đúng chỗ ngón tay hay chạm nhất.
  lv_obj_add_flag(im, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_clear_flag(im, LV_OBJ_FLAG_CLICKABLE);

  chamfer(b, CH_SM, bgPrimary());
  return b;
}

// Hai hàm dưới KHÔNG LÀM GÌ khi trạng thái không đổi. Quan trọng vì Screens::
// update() gọi chúng 5 lần mỗi giây: nếu lần nào cũng invalidate thì các nút bị
// vẽ lại liên tục dù chẳng có gì thay đổi. Trên tấm SPI 27 MHz, vẽ thừa đều đặn
// như vậy chính là thứ mắt đọc ra thành "nhấp nháy".
//
// Dùng LV_STATE_USER_1 làm cờ nhớ "đang được chọn". Không mượn user_data của
// đối tượng được — Theme::slideIn() đã dùng chỗ đó để nhớ vị trí đích. Không có
// style nào đăng ký cho USER_1 nên nó chỉ là cái cờ, không ảnh hưởng hiển thị.
void buttonSelect(lv_obj_t *btn, bool selected) {
  if (btn == nullptr) return;
  if (lv_obj_has_state(btn, LV_STATE_USER_1) == selected) return;

  if (selected) {
    lv_obj_add_style(btn, &sBtnSel, 0);
    lv_obj_add_state(btn, LV_STATE_USER_1);
  } else {
    lv_obj_remove_style(btn, &sBtnSel, 0);
    lv_obj_clear_state(btn, LV_STATE_USER_1);
  }
  lv_obj_invalidate(btn);
}

void buttonDisable(lv_obj_t *btn, bool disabled) {
  if (btn == nullptr) return;
  if (lv_obj_has_state(btn, LV_STATE_DISABLED) == disabled) return;

  if (disabled) {
    lv_obj_add_style(btn, &sBtnOff, 0);
    lv_obj_add_state(btn, LV_STATE_DISABLED);
  } else {
    lv_obj_remove_style(btn, &sBtnOff, 0);
    lv_obj_clear_state(btn, LV_STATE_DISABLED);
  }
  lv_obj_invalidate(btn);
}

lv_obj_t *label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const char *text,
                const lv_font_t *font, lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_font(l, font ? font : fontBody(), 0);
  lv_obj_set_style_text_color(l, color, 0);
  return l;
}

lv_obj_t *labelCaps(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const char *text) {
  lv_obj_t *l = label(parent, x, y, text, fontLabel(), textMuted());
  lv_obj_set_style_text_letter_space(l, 1, 0);
  return l;
}

// --- Skeleton ----------------------------------------------------------------
static void shimmerAnim(void *obj, int32_t v) {
  lv_obj_set_x(static_cast<lv_obj_t *>(obj), (lv_coord_t)v);
}

lv_obj_t *skeleton(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                   lv_coord_t w, lv_coord_t h, uint32_t delayMs) {
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_remove_style_all(bar);
  lv_obj_set_pos(bar, x, y);
  lv_obj_set_size(bar, w, h);
  lv_obj_set_style_bg_color(bar, bgElevated(), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  // Vệt sáng chạy từ x = -shineW tới x = w, tức là thò hẳn ra ngoài hai đầu.
  // Không cần cắt tay: LVGL 8.3 mặc định cắt con theo khung cha (chỉ khi bật
  // LV_OBJ_FLAG_OVERFLOW_VISIBLE nó mới cho tràn). Bỏ mặc định đó đi thì vệt
  // sáng quét ngang qua cả các thẻ bên cạnh.

  const lv_coord_t shineW = w / 3;
  lv_obj_t *shine = lv_obj_create(bar);
  lv_obj_remove_style_all(shine);
  lv_obj_set_size(shine, shineW, h);
  lv_obj_set_y(shine, 0);
  lv_obj_set_style_bg_color(shine, bgSecondary(), 0);
  lv_obj_set_style_bg_grad_color(shine, borderDefault(), 0);
  lv_obj_set_style_bg_grad_dir(shine, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(shine, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(shine, 0, 0);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, shine);
  lv_anim_set_exec_cb(&a, shimmerAnim);
  lv_anim_set_values(&a, -shineW, w);
  lv_anim_set_time(&a, DUR_SHIMMER);
  lv_anim_set_delay(&a, delayMs);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);

  return bar;
}

void skeletonShow(lv_obj_t *sk, bool show) {
  if (sk == nullptr) return;
  if (lv_obj_has_flag(sk, LV_OBJ_FLAG_HIDDEN) == !show) return;   // đã đúng rồi

  lv_obj_t *shine = lv_obj_get_child(sk, 0);
  if (show) {
    lv_obj_clear_flag(sk, LV_OBJ_FLAG_HIDDEN);
    if (shine) {
      lv_anim_t a;
      lv_anim_init(&a);
      lv_anim_set_var(&a, shine);
      lv_anim_set_exec_cb(&a, shimmerAnim);
      lv_anim_set_values(&a, -lv_obj_get_width(shine), lv_obj_get_width(sk));
      lv_anim_set_time(&a, DUR_SHIMMER);
      lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
      lv_anim_start(&a);
    }
  } else {
    // Dừng animation TRƯỚC khi ẩn. Bỏ bước này thì LVGL vẫn nhích ô sáng và làm
    // bẩn vùng đó 20 ms một lần cho tới khi tắt nguồn — không nhìn thấy gì,
    // nhưng ăn đúng phần băng thông SPI mà giao diện đang cần.
    if (shine) lv_anim_del(shine, shimmerAnim);
    lv_obj_add_flag(sk, LV_OBJ_FLAG_HIDDEN);
  }
}

// --- Trượt vào khi đổi trang -------------------------------------------------
static void slideAnim(void *obj, int32_t v) {
  lv_obj_set_x(static_cast<lv_obj_t *>(obj), (lv_coord_t)v);
}

void slideIn(lv_obj_t *page, lv_coord_t dist, uint32_t ms) {
  const uint32_t n = lv_obj_get_child_cnt(page);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *c = lv_obj_get_child(page, i);
    if (c == nullptr) continue;

    // Nhớ vị trí đích ngay lần đầu. +1 để giá trị 0 không bị nhầm là "chưa nhớ".
    lv_coord_t home;
    void *saved = lv_obj_get_user_data(c);
    if (saved == nullptr) {
      home = lv_obj_get_x(c);
      lv_obj_set_user_data(c, (void *)(intptr_t)(home + 1));
    } else {
      home = (lv_coord_t)((intptr_t)saved - 1);
    }

    lv_anim_del(c, slideAnim);          // cắt cú trượt đang dở, nếu có

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c);
    lv_anim_set_exec_cb(&a, slideAnim);
    lv_anim_set_values(&a, home + ((i & 1) ? dist : -dist), home);
    lv_anim_set_time(&a, ms);
    // SO LE nhau, và đây là chuyện hiệu năng chứ không chỉ thẩm mỹ. Trang THÔNG
    // TIN có 25 phần tử; cho cả 25 chạy cùng lúc thì vùng cần vẽ lại mỗi khung
    // đúng bằng cả trang — tức là hiệu ứng "cho mượt" lại thành giật. So le thì
    // mỗi thời điểm chỉ vài phần tử đang động, vùng vẽ lại nhỏ, khung hình kịp.
    // Trùng luôn với mẫu "Entrance (Stagger)" trong 06-animation.md.
    // Chặn ở 10 để trang nhiều phần tử không kéo dài quá DUR_SLOW.
    lv_anim_set_delay(&a, (i < 10 ? i : 10) * 15);
    // ease_out = "vào nhanh, dừng chắc" — --ease-out của hệ thiết kế. Chuyển
    // động phải đọc ra là máy móc, dứt khoát, không phải nảy đàn hồi.
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
  }
}

lv_obj_t *badge(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                const char *text, lv_color_t bg) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, 18);
  lv_obj_set_style_bg_color(o, bg, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *l = lv_label_create(o);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, fontTiny(), 0);
  lv_obj_set_style_text_color(l, onColor(bg), 0);   // TỰ ĐỘNG (xanh->trắng, hổ phách->đen)
  lv_obj_set_style_text_letter_space(l, 1, 0);
  lv_obj_center(l);

  chamfer(o, CH_SM, bgSecondary());   // huy hiệu nằm TRÊN thẻ, không phải trên nền màn
  return o;
}

} // namespace Theme
