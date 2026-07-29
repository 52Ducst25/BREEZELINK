#include "ui-screens.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
//  Nội dung 4 màn + lớp phủ học remote. Xem ui-screens.h cho ranh giới trách
//  nhiệm, và ../../Interface/README.md §5 cho wireframe + lý do bố cục.
// ============================================================================
namespace Screens {
namespace {

using namespace Theme;

CommandFn gOnCmd     = nullptr;
SettingFn gOnSetting = nullptr;

// --- Khung chung -------------------------------------------------------------
lv_obj_t *gStatusBar, *gNav;
lv_obj_t *gLblClock, *gDotWifi, *gDotMqtt;
lv_obj_t *gTabBtn[4], *gTabMark[4];
lv_obj_t *gPage[4];                 // 4 vùng nội dung, ẩn/hiện theo tab
uint8_t   gTab = 0;

lv_obj_t *gToast, *gToastLbl;
uint32_t  gToastUntil = 0;

// --- TRANG CHU ---------------------------------------------------------------
lv_obj_t *gInTemp, *gInHum, *gOutTemp, *gOutHum, *gOutDot, *gOutNote;
lv_obj_t *gInSkel, *gOutSkel;   // che chỗ con số khi CHƯA có số đo
lv_obj_t *gAcMode, *gAcSet, *gAcBadge, *gAcBadgeLbl, *gAcAge;

// --- DIEU KHIEN --------------------------------------------------------------
lv_obj_t *gSetBig, *gModeBtn[4], *gSendBtn, *gAutoBtn, *gLimitLbl;
int   gPendSet  = 26;               // thay đổi được GOM LẠI, chỉ gửi khi bấm GUI
int   gPendMode = 0;                // 0=COOL 1=DRY 2=FAN 3=OFF
const char *const kModeName[4] = {"COOL", "DRY", "FAN", "OFF"};
const char *const kModeLabel[4] = {"LẠNH", "KHÔ", "QUẠT", "TẮT"};
bool  gModeOk[4] = {false, false, false, false};   // có mã IR trong NVS chưa

// --- THONG TIN ---------------------------------------------------------------
const char *const kInfoRow[8] = {"WIFI", "IP", "SÓNG", "MQTT",
                                 "ESP-NOW", "NGOÀI TRỜI", "MÃ IR", "FW"};
lv_obj_t *gInfoVal[8], *gInfoFoot;

// --- CAI DAT -----------------------------------------------------------------
lv_obj_t *gBrightLbl, *gBuzzOn, *gBuzzOff;

// --- HOC REMOTE (lớp phủ) ----------------------------------------------------
lv_obj_t *gLearn, *gLearnLbl, *gLearnBar, *gLearnSec;

// ---------------------------------------------------------------------------
//  Tiện nội bộ
// ---------------------------------------------------------------------------
/// Đặt text chỉ khi khác — tránh làm bẩn vùng vẽ một cách vô ích.
void setText(lv_obj_t *l, const char *s) {
  if (l && strcmp(lv_label_get_text(l), s) != 0) lv_label_set_text(l, s);
}

/// Số đo -> chuỗi. NAN thì ra "—" chứ KHÔNG BAO GIỜ ra "0.0" (README §4.1).
void fmtNum(char *buf, size_t n, float v, int decimals) {
  if (isnan(v)) { snprintf(buf, n, "--"); return; }
  snprintf(buf, n, decimals ? "%.1f" : "%.0f", v);
}

lv_obj_t *dot(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_color_t c) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, 8, 8);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(o, c, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

void showTab(uint8_t i) {
  if (i > 3) return;
  const bool changed = (gTab != i);
  gTab = i;
  for (uint8_t k = 0; k < 4; k++) {
    if (k == i) {
      lv_obj_clear_flag(gPage[k], LV_OBJ_FLAG_HIDDEN);
      // Nội dung trượt vào từ hai bên. CHỈ khi thật sự đổi tab — bấm lại đúng
      // tab đang mở mà cũng chạy animation thì thành nhiễu, không phải phản hồi.
      if (changed) slideIn(gPage[k]);
    } else {
      lv_obj_add_flag(gPage[k], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_opa(gTabMark[k], k == i ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    // Tab đang mở tô màu NHẤN chứ không phải màu chữ thường. Chỉ còn biểu tượng,
    // không còn chữ, nên tín hiệu "đang ở đây" phải mạnh hơn trước — vạch trên
    // đầu tab cộng với màu xanh nhấn.
    lv_obj_set_style_text_color(lv_obj_get_child(gTabBtn[k], 0),
                                k == i ? accentText() : textMuted(), 0);
  }
}

void onTab(lv_event_t *e) { showTab((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }

// ---------------------------------------------------------------------------
//  Khung: thanh trạng thái + thanh điều hướng
// ---------------------------------------------------------------------------
void buildChrome() {
  lv_obj_t *scr = lv_scr_act();

  // --- thanh trạng thái ---
  gStatusBar = lv_obj_create(scr);
  lv_obj_remove_style_all(gStatusBar);
  lv_obj_set_pos(gStatusBar, 0, 0);
  lv_obj_set_size(gStatusBar, SCREEN_W, STATUS_H);
  lv_obj_set_style_bg_color(gStatusBar, bgSecondary(), 0);
  lv_obj_set_style_bg_opa(gStatusBar, 216, 0);   // kính mờ như thẻ
  lv_obj_set_style_border_color(gStatusBar, borderSubtle(), 0);
  lv_obj_set_style_border_width(gStatusBar, 1, 0);
  lv_obj_set_style_border_side(gStatusBar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_clear_flag(gStatusBar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *brandBox = lv_obj_create(gStatusBar);
  lv_obj_remove_style_all(brandBox);
  lv_obj_set_pos(brandBox, 6, 6);
  lv_obj_set_size(brandBox, 9, 9);
  lv_obj_set_style_bg_color(brandBox, accent(), 0);
  lv_obj_set_style_bg_opa(brandBox, LV_OPA_COVER, 0);

  lv_obj_t *brand = label(gStatusBar, 20, 4, "AIRCON", fontLabel(), accent());
  lv_obj_set_style_text_letter_space(brand, 1, 0);

  gDotWifi  = dot(gStatusBar, 238, 7, textMuted());
  gDotMqtt  = dot(gStatusBar, 252, 7, textMuted());
  gLblClock = label(gStatusBar, 270, 4, "--:--", fontLabel(), textPrimary());

  // --- thanh điều hướng ---
  gNav = lv_obj_create(scr);
  lv_obj_remove_style_all(gNav);
  lv_obj_set_pos(gNav, 0, NAV_Y);
  lv_obj_set_size(gNav, SCREEN_W, NAV_H);
  lv_obj_set_style_bg_color(gNav, bgSecondary(), 0);
  lv_obj_set_style_bg_opa(gNav, 216, 0);         // kính mờ như thẻ
  lv_obj_set_style_border_color(gNav, borderSubtle(), 0);
  lv_obj_set_style_border_width(gNav, 1, 0);
  lv_obj_set_style_border_side(gNav, LV_BORDER_SIDE_TOP, 0);
  lv_obj_clear_flag(gNav, LV_OBJ_FLAG_SCROLLABLE);

  // BIỂU TƯỢNG thay cho chữ. Lấy từ bộ LV_SYMBOL_* nhúng sẵn trong font
  // Montserrat dựng sẵn của LVGL (bật LV_FONT_MONTSERRAT_20 trong lv_conf.h) —
  // không cần thêm file ảnh nào.
  //
  // CHỌN BIỂU TƯỢNG THEO VIỆC NÓ LÀM, không theo cái nó vẽ:
  //   nhà       -> TRANG CHỦ, quy ước ai cũng hiểu
  //   nguồn     -> ĐIỀU KHIỂN; việc chính ở trang đó là bật/tắt và đổi chế độ
  //   danh sách -> THÔNG TIN; đúng là 8 dòng chẩn đoán xếp thành danh sách
  //   bánh răng -> CÀI ĐẶT, quy ước ai cũng hiểu
  // Không có biểu tượng "thanh trượt" trong bộ này, nên "nguồn" là cái gần
  // nghĩa nhất — đừng đổi sang bút chì hay cây kéo cho lạ mắt.
  static const char *kTab[4] = {
      LV_SYMBOL_HOME, LV_SYMBOL_POWER, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS};
  for (uint8_t i = 0; i < 4; i++) {
    // Ô chạm nguyên 80×34 — kiosk.css đòi ô lớn, và đây là ô nhỏ nhất trên màn.
    gTabBtn[i] = lv_btn_create(gNav);
    lv_obj_remove_style_all(gTabBtn[i]);
    lv_obj_set_pos(gTabBtn[i], NAV_W * i, 0);
    lv_obj_set_size(gTabBtn[i], NAV_W, NAV_H);
    lv_obj_add_event_cb(gTabBtn[i], onTab, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    pressFeedback(gTabBtn[i]);   // tab cũng phải kêu và đổi màu như mọi nút khác

    lv_obj_t *l = lv_label_create(gTabBtn[i]);
    lv_label_set_text(l, kTab[i]);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(l, textMuted(), 0);
    lv_obj_center(l);

    // Vạch nhấn phía trên tab đang chọn.
    gTabMark[i] = lv_obj_create(gNav);
    lv_obj_remove_style_all(gTabMark[i]);
    lv_obj_set_pos(gTabMark[i], NAV_W * i, 0);
    lv_obj_set_size(gTabMark[i], NAV_W, 3);
    lv_obj_set_style_bg_color(gTabMark[i], accent(), 0);
    lv_obj_set_style_bg_opa(gTabMark[i], LV_OPA_TRANSP, 0);
  }

  // --- toast ---
  gToast = lv_obj_create(scr);
  lv_obj_remove_style_all(gToast);
  lv_obj_set_pos(gToast, PAD, 178);
  lv_obj_set_size(gToast, SCREEN_W - 2 * PAD, 24);
  lv_obj_set_style_bg_color(gToast, textPrimary(), 0);
  lv_obj_set_style_bg_opa(gToast, LV_OPA_COVER, 0);
  lv_obj_clear_flag(gToast, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(gToast, LV_OBJ_FLAG_HIDDEN);
  gToastLbl = lv_label_create(gToast);
  lv_obj_set_style_text_font(gToastLbl, fontLabel(), 0);
  lv_obj_set_style_text_color(gToastLbl, bgSecondary(), 0);
  lv_obj_center(gToastLbl);
  chamfer(gToast, CH_SM, bgPrimary());

  // --- 4 vùng nội dung ---
  for (uint8_t i = 0; i < 4; i++) {
    gPage[i] = lv_obj_create(scr);
    lv_obj_remove_style_all(gPage[i]);
    lv_obj_set_pos(gPage[i], 0, CONTENT_Y);
    lv_obj_set_size(gPage[i], SCREEN_W, CONTENT_H);
    lv_obj_clear_flag(gPage[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gPage[i], LV_OBJ_FLAG_HIDDEN);
  }
}

// ---------------------------------------------------------------------------
//  Màn 1 — TRANG CHU
// ---------------------------------------------------------------------------
void buildHome() {
  lv_obj_t *p = gPage[0];   // toạ độ bên trong p là y-24 so với màn

  lv_obj_t *cIn = card(p, PAD, 2, 152, 84);
  labelCaps(cIn, 10, 8, "TRONG NHÀ");
  gInTemp = label(cIn, 10, 24, "", fontHero(), textMuted());
  label(cIn, 10, 64, "ĐỘ ẨM", fontTiny(), textMuted());
  gInHum = label(cIn, 100, 62, "--", fontLabel(), textPrimary());
  // Skeleton nằm ĐÈ lên chỗ con số, hiện khi chưa có số đo. Tạo SAU nhãn để nằm
  // trên. Xem Theme::skeleton() cho lý do dùng nó thay vì để dấu "--".
  gInSkel = skeleton(cIn, 10, 30, 116, 30);

  lv_obj_t *cOut = card(p, 162, 2, 152, 84);
  labelCaps(cOut, 10, 8, "NGOÀI TRỜI");
  gOutDot = dot(cOut, 134, 9, textMuted());
  gOutTemp = label(cOut, 10, 24, "", fontHero(), textMuted());
  // Lệch pha 400 ms so với ô trong nhà: hai vệt sáng chạy đồng bộ nhìn như một
  // thanh duy nhất bị gãy đôi, lệch pha thì đọc ra là hai phép đo riêng biệt.
  gOutSkel = skeleton(cOut, 10, 30, 116, 30, 400);
  gOutNote = label(cOut, 10, 64, "ĐỘ ẨM", fontTiny(), textMuted());
  gOutHum  = label(cOut, 100, 62, "--", fontLabel(), textPrimary());

  lv_obj_t *cAc = card(p, PAD, 92, 308, 86);

  label(cAc, 12, 10, "MÁY LẠNH", fontTitle(), textPrimary());

  // Icon dàn lạnh, đặt CẠNH tiêu đề chứ không làm hình chìm phía sau chữ.
  //
  // Bản làm hình chìm 110×45 mờ 18% nằm ngay dưới vùng phải vẽ lại mỗi khi
  // trạng thái máy lạnh đổi — mỗi điểm ảnh phải trộn alpha, trả phí cho thứ
  // không mang tin tức. Ở đây 44×18, đục hoàn toàn, nằm cạnh chữ trong vùng
  // tĩnh: gần như không tốn gì mà vẫn nhận ra ngay thẻ này nói về máy lạnh.
  lv_obj_t *art = lv_img_create(cAc);
  lv_img_set_src(art, &img_ac_unit);
  lv_obj_set_pos(art, 108, 11);
  gAcBadge = badge(cAc, 228, 10, 68, "TỰ ĐỘNG", accent());
  gAcBadgeLbl = lv_obj_get_child(gAcBadge, 0);
  gAcMode = label(cAc, 12, 40, "--", fontLabel(), textCode());
  gAcAge  = label(cAc, 12, 60, "", fontTiny(), textMuted());
  gAcSet  = label(cAc, 210, 38, "--", fontBig(), accent());
}

// ---------------------------------------------------------------------------
//  Màn 2 — DIEU KHIEN
// ---------------------------------------------------------------------------
void refreshControl() {
  char b[8];
  snprintf(b, sizeof b, "%d", gPendSet);
  setText(gSetBig, b);
  for (uint8_t i = 0; i < 4; i++) {
    buttonSelect(gModeBtn[i], i == gPendMode);
    buttonDisable(gModeBtn[i], !gModeOk[i]);
  }
  // Chỉ COOL mới có nhiệt độ; các chế độ khác không dùng setpoint.
  lv_obj_set_style_text_color(gSetBig, gPendMode == 0 ? accent() : textMuted(), 0);

  // KHÔNG làm mờ nút THỦ CÔNG dù chưa học mã.
  //
  // Tôi đã thử làm mờ nó và ĐÓ LÀ MỘT BƯỚC LÙI: buttonDisable() gắn
  // LV_STATE_DISABLED, mà LVGL không gửi sự kiện bấm tới đối tượng ở trạng thái
  // đó. Nút từ chỗ "bấm được nhưng chưa bắn được mã" thành "bấm không ăn" —
  // người dùng mất luôn đường chuyển về thủ công.
  //
  // Bốn nút CHẾ ĐỘ thì làm mờ là đúng: chọn một chế độ không có mã thì thật sự
  // không có gì để làm. Nhưng THỦ CÔNG còn mang nghĩa thứ hai — "giành quyền
  // khỏi máy chủ" — và nghĩa đó luôn dùng được. Đừng khoá một nút chỉ vì MỘT
  // trong hai việc nó làm đang bị chặn.

  // Dòng dưới ô nhiệt độ nói trước là bấm vào sẽ không bắn được mã, để người
  // dùng biết trước chứ không phải bấm rồi mới thấy thông báo lỗi.
  bool anyCode = false;
  for (uint8_t i = 0; i < 4; i++) anyCode = anyCode || gModeOk[i];
  if (!anyCode)               setText(gLimitLbl, "CHƯA HỌC MÃ — VÀO APP ĐỂ HỌC");
  else if (!gModeOk[gPendMode]) setText(gLimitLbl, "CHẾ ĐỘ NÀY CHƯA HỌC MÃ");
  else if (gPendMode == 0)      setText(gLimitLbl, "GIỚI HẠN 16 - 30");
  else                          setText(gLimitLbl, "");
}

void onAdjust(lv_event_t *e) {
  int d = (int)(intptr_t)lv_event_get_user_data(e);
  gPendSet += d;
  if (gPendSet < 16) gPendSet = 16;
  if (gPendSet > 30) gPendSet = 30;
  refreshControl();
}

void onMode(lv_event_t *e) {
  uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  if (!gModeOk[i]) {          // "không phím chết": nói lý do thay vì im lặng
    toast("CHƯA HỌC MÃ — VÀO APP ĐỂ HỌC", true);
    return;
  }
  gPendMode = i;
  refreshControl();
}

void onSend(lv_event_t *) {
  if (!gOnCmd) return;
  Ui::Command c{};
  c.kind = Ui::Command::MANUAL;
  snprintf(c.mode, sizeof c.mode, "%s", kModeName[gPendMode]);
  c.setpoint = gPendSet;
  gOnCmd(c);
  toast("ĐANG GỬI...");
}

void onAuto(lv_event_t *) {
  if (!gOnCmd) return;
  Ui::Command c{};
  c.kind = Ui::Command::AUTO;
  gOnCmd(c);
  toast("ĐÃ TRẢ QUYỀN CHO MÁY CHỦ");
}

void buildControl() {
  lv_obj_t *p = gPage[1];

  lv_obj_t *minus = buttonImg(p, 8, 4, 68, 76, &img_minus);
  lv_obj_add_event_cb(minus, onAdjust, LV_EVENT_CLICKED, (void *)(intptr_t)-1);

  lv_obj_t *box = card(p, 84, 4, 152, 76);
  gSetBig = label(box, 0, 8, "26", fontHero(), accent());
  lv_obj_set_width(gSetBig, 152);
  lv_obj_set_style_text_align(gSetBig, LV_TEXT_ALIGN_CENTER, 0);
  gLimitLbl = label(box, 0, 56, "GIỚI HẠN 16 - 30", fontTiny(), textMuted());
  lv_obj_set_width(gLimitLbl, 152);
  lv_obj_set_style_text_align(gLimitLbl, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *plus = buttonImg(p, 244, 4, 68, 76, &img_plus);
  lv_obj_add_event_cb(plus, onAdjust, LV_EVENT_CLICKED, (void *)(intptr_t)1);

  for (uint8_t i = 0; i < 4; i++) {
    gModeBtn[i] = button(p, PAD + 78 * i, 86, 74, 44, kModeLabel[i]);
    lv_obj_add_event_cb(gModeBtn[i], onMode, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
  }

  // "THỦ CÔNG" chứ không phải "GỬI": nút này đối xứng với "TỰ ĐỘNG" bên cạnh, và
  // cặp đối lập đúng là thủ công/tự động. "GỬI" mô tả CÁCH LÀM (đẩy một gói tin)
  // chứ không nói ra HẬU QUẢ — mà hậu quả mới là điều người dùng cần biết: bấm
  // vào đây là giành quyền điều khiển khỏi máy chủ.
  // KHÔNG primary=true. Cờ đó gắn nền xanh VĨNH VIỄN, và buttonSelect() ở dưới
  // không gỡ được nó — nút sẽ sáng kể cả khi máy đang chạy tự động. Đúng lỗi đã
  // xảy ra với nút BẬT của ÂM BÁO.
  gSendBtn = button(p, PAD, 136, 150, 42, "THỦ CÔNG");
  lv_obj_add_event_cb(gSendBtn, onSend, LV_EVENT_CLICKED, nullptr);
  gAutoBtn = button(p, 164, 136, 150, 42, "TỰ ĐỘNG");
  lv_obj_add_event_cb(gAutoBtn, onAuto, LV_EVENT_CLICKED, nullptr);

  refreshControl();
}

// ---------------------------------------------------------------------------
//  Màn 3 — THONG TIN
// ---------------------------------------------------------------------------
void buildInfo() {
  lv_obj_t *p = gPage[2];
  for (uint8_t i = 0; i < 8; i++) {
    const lv_coord_t y = 4 + 20 * i;
    labelCaps(p, 12, y, kInfoRow[i]);
    gInfoVal[i] = label(p, 0, y, "--", fontLabel(), textPrimary());
    lv_obj_set_width(gInfoVal[i], SCREEN_W - 24);
    lv_obj_set_style_text_align(gInfoVal[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(gInfoVal[i], 12, y);

    lv_obj_t *ln = lv_obj_create(p);
    lv_obj_remove_style_all(ln);
    lv_obj_set_pos(ln, 12, y + 17);
    lv_obj_set_size(ln, SCREEN_W - 24, 1);
    lv_obj_set_style_bg_color(ln, borderSubtle(), 0);
    lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
  }
  gInfoFoot = label(p, 12, 166, "", fontTiny(), textMuted());
}

// ---------------------------------------------------------------------------
//  Màn 4 — CAI DAT
// ---------------------------------------------------------------------------
void onSetting(lv_event_t *e) {
  if (gOnSetting) gOnSetting((Setting)(uintptr_t)lv_event_get_user_data(e));
}

void buildSettings() {
  lv_obj_t *p = gPage[3];

  lv_obj_t *r0 = card(p, PAD, 4, 308, 40);
  label(r0, 12, 12, "ĐỘ SÁNG", fontBody(), textPrimary());
  // GIỮ ĐỂ CHẠY LIÊN TỤC. Độ sáng nhảy 10% mỗi bước nên đi từ 10% lên 100% là 9
  // lần bấm — đủ nhiều để thành khó chịu trên màn cảm ứng nhỏ.
  //
  // Cặp sự kiện phải là SHORT_CLICKED + LONG_PRESSED_REPEAT, KHÔNG PHẢI
  // CLICKED + LONG_PRESSED_REPEAT: LVGL vẫn bắn CLICKED lúc nhả tay kể cả sau
  // một lượt giữ dài, nên dùng CLICKED thì mỗi lần giữ bị cộng thêm đúng một
  // bước thừa ở cuối. SHORT_CLICKED chỉ bắn khi nhả TRƯỚC ngưỡng giữ lâu, nên
  // hai đường không chồng nhau: chạm = 1 bước, giữ = lặp cho tới khi nhả.
  //
  // Nhịp lặp lấy mặc định của LVGL (100 ms) — quét hết dải 10 bước mất ~1 giây,
  // vừa đủ nhanh mà vẫn dừng đúng chỗ được. Không đụng tới `pressFeedback`: nó
  // gắn vào LV_EVENT_PRESSED nên chỉ kêu MỘT tiếng lúc chạm xuống, không kêu
  // theo từng bước lặp — giữ nút mà còi kêu 10 lần thì thành tiếng ồn.
  lv_obj_t *bd = button(r0, 190, 6, 28, 28, "-");
  lv_obj_add_event_cb(bd, onSetting, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)BRIGHT_DOWN);
  lv_obj_add_event_cb(bd, onSetting, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(uintptr_t)BRIGHT_DOWN);
  gBrightLbl = label(r0, 224, 12, "70%", fontLabel(), accent());
  lv_obj_t *bu = button(r0, 262, 6, 28, 28, "+");
  lv_obj_add_event_cb(bu, onSetting, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)BRIGHT_UP);
  lv_obj_add_event_cb(bu, onSetting, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(uintptr_t)BRIGHT_UP);

  lv_obj_t *r1 = card(p, PAD, 50, 308, 40);
  label(r1, 12, 12, "ÂM BÁO", fontBody(), textPrimary());
  // KHÔNG dùng primary=true ở đây. `primary` gắn style nền xanh VĨNH VIỄN, mà
  // buttonSelect() chỉ thêm/bớt style "đang chọn" — nó không gỡ nổi style
  // primary, nên nút BẬT sáng mãi kể cả sau khi đã bấm TẮT.
  //
  // Sâu hơn: đây là cặp CHỌN MỘT TRONG HAI, không phải một hành động chính kèm
  // một hành động phụ. "Primary" nghĩa là "việc nên làm ở màn này" — với bật/tắt
  // thì không có việc nào nên làm hơn việc nào. Trạng thái do buttonSelect()
  // quyết định, và chỉ nó thôi.
  gBuzzOn  = button(r1, 218, 6, 36, 28, "BẬT");
  lv_obj_add_event_cb(gBuzzOn, onSetting, LV_EVENT_CLICKED, (void *)(uintptr_t)BUZZER_ON);
  gBuzzOff = button(r1, 258, 6, 36, 28, "TẮT");
  lv_obj_add_event_cb(gBuzzOff, onSetting, LV_EVENT_CLICKED, (void *)(uintptr_t)BUZZER_OFF);

  // Hàng "ĐỒNG BỘ GIỜ" đã bỏ -> KHỞI ĐỘNG LẠI dời lên y=96 thế chỗ. Để nguyên
  // y=142 thì màn hở một khoảng trống bằng đúng một hàng, nhìn ra là thiếu mất
  // một mục chứ không phải là bố cục có chủ đích.
  lv_obj_t *r2 = card(p, PAD, 96, 308, 40);
  label(r2, 12, 12, "KHỞI ĐỘNG LẠI", fontBody(), textPrimary());
  lv_obj_t *br = button(r2, 226, 6, 68, 28, "CHẠY");
  lv_obj_set_style_border_color(br, err(), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(br, 0), err(), 0);
  lv_obj_add_event_cb(br, onSetting, LV_EVENT_CLICKED, (void *)(uintptr_t)REBOOT);
}

// ---------------------------------------------------------------------------
//  Lớp phủ — HOC REMOTE
// ---------------------------------------------------------------------------
void buildLearn() {
  lv_obj_t *scr = lv_scr_act();
  gLearn = lv_obj_create(scr);
  lv_obj_remove_style_all(gLearn);
  lv_obj_set_pos(gLearn, 0, 0);
  lv_obj_set_size(gLearn, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(gLearn, bgPrimary(), 0);
  lv_obj_set_style_bg_opa(gLearn, LV_OPA_COVER, 0);
  lv_obj_clear_flag(gLearn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(gLearn, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *box = card(gLearn, 16, 30, 288, 168);
  lv_obj_set_style_border_color(box, accent(), 0);
  lv_obj_set_style_border_width(box, 2, 0);

  lv_obj_t *t = label(box, 0, 14, "ĐANG HỌC REMOTE", fontLabel(), accent());
  lv_obj_set_width(t, 288);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_letter_space(t, 2, 0);

  // fontTitle() CHỨ KHÔNG PHẢI fontHero(): nhãn này là CHỮ ("COOL 25",
  // "FAN_SPEED"...) do backend gửi kèm lệnh học, mà fontHero là aircon_num_40 —
  // font chỉ có chữ số (xem theme.h). Dùng nhầm thì LVGL không tìm ra glyph nên
  // nhãn hiện RỖNG: người đang cầm remote không biết máy đang đợi học nút nào,
  // đúng lúc thông tin đó là thứ duy nhất họ cần. Kèm theo là mỗi khung vẽ lại
  // phun một chùm "glyph dsc. not found for U+4F" ra serial — chôn luôn dòng
  // [learn] mà người ta mở log lên để đọc.
  gLearnLbl = label(box, 0, 48, "", fontTitle(), textPrimary());
  lv_obj_set_width(gLearnLbl, 288);
  lv_obj_set_style_text_align(gLearnLbl, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *h = label(box, 0, 100, "Hướng remote vào mắt thu, bấm nút",
                      fontTiny(), textMuted());
  lv_obj_set_width(h, 288);
  lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);

  gLearnBar = lv_bar_create(box);
  lv_obj_remove_style_all(gLearnBar);
  lv_obj_set_pos(gLearnBar, 24, 124);
  lv_obj_set_size(gLearnBar, 240, 10);
  lv_obj_set_style_bg_color(gLearnBar, borderSubtle(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(gLearnBar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(gLearnBar, accent(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(gLearnBar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_bar_set_range(gLearnBar, 0, 100);

  gLearnSec = label(box, 0, 140, "", fontLabel(), warn());
  lv_obj_set_width(gLearnSec, 288);
  lv_obj_set_style_text_align(gLearnSec, LV_TEXT_ALIGN_CENTER, 0);
}

} // namespace

// ===========================================================================
//  API
// ===========================================================================
void build(CommandFn onCmd, SettingFn onSetting) {
  gOnCmd     = onCmd;
  gOnSetting = onSetting;
  buildChrome();
  buildHome();
  buildControl();
  buildInfo();
  buildSettings();
  buildLearn();
  showTab(0);
}

void update(const Ui::Model &m) {
  char b[40];

  // --- thanh trạng thái ---
  lv_obj_set_style_bg_color(gDotWifi, m.wifiUp ? ok() : err(), 0);
  lv_obj_set_style_bg_color(gDotMqtt, m.mqttUp ? ok() : err(), 0);

  // --- TRANG CHU ---
  // Skeleton chỉ che khi CHƯA TỪNG có số. Node ngoài trời mất kết nối thì KHÔNG
  // dùng skeleton — vệt sáng chạy nghĩa là "đang tải, chờ chút", mà mất nhịp
  // tim thì chờ mãi cũng không có. Ca đó phải nói thẳng "MẤT NHỊP TIM %lus".
  skeletonShow(gInSkel, isnan(m.tIn));
  skeletonShow(gOutSkel, m.outOnline && isnan(m.tOut));

  fmtNum(b, sizeof b, m.tIn, 1);
  setText(gInTemp, b);
  lv_obj_set_style_text_color(gInTemp, thermal(m.tIn), 0);
  fmtNum(b, sizeof b, m.hIn, 0);
  if (!isnan(m.hIn)) strncat(b, " %", sizeof(b) - strlen(b) - 1);
  setText(gInHum, b);

  lv_obj_set_style_bg_color(gOutDot, m.outOnline ? ok() : textMuted(), 0);
  if (m.outOnline) {
    fmtNum(b, sizeof b, m.tOut, 1);
    setText(gOutTemp, b);
    lv_obj_set_style_text_color(gOutTemp, thermal(m.tOut), 0);
    setText(gOutNote, "ĐỘ ẨM");
    fmtNum(b, sizeof b, m.hOut, 0);
    if (!isnan(m.hOut)) strncat(b, " %", sizeof(b) - strlen(b) - 1);
    setText(gOutHum, b);
  } else {
    // Node ngoài trời mất nhịp tim: nói thẳng, KHÔNG đóng băng số cũ trên màn
    // như thể vẫn đang đo được.
    setText(gOutTemp, "--");
    lv_obj_set_style_text_color(gOutTemp, textMuted(), 0);
    snprintf(b, sizeof b, "MẤT NHỊP TIM %lus", (unsigned long)m.outAgeSec);
    setText(gOutNote, b);
    setText(gOutHum, "");
  }

  setText(gAcMode, m.mode[0] ? m.mode : "--");
  if (m.setpoint >= 0) snprintf(b, sizeof b, "%d", m.setpoint);
  else                 snprintf(b, sizeof b, "--");
  setText(gAcSet, b);

  if (m.overrideLocal) {
    lv_obj_set_style_bg_color(gAcBadge, warn(), 0);
    setText(gAcBadgeLbl, "GHI ĐÈ");
    // Nói đúng giới hạn của kiến trúc hiện tại (README §8.3) thay vì để người
    // dùng tưởng ghi đè từ màn này là vĩnh viễn.
    setText(gAcAge, "máy chủ sẽ giành lại quyền ở chu kỳ sau");
  } else {
    lv_obj_set_style_bg_color(gAcBadge, accent(), 0);
    setText(gAcBadgeLbl, "TỰ ĐỘNG");
    if (m.lastCmdSec > 0) {
      snprintf(b, sizeof b, "lệnh cuối %lu phút trước",
               (unsigned long)(m.lastCmdSec / 60));
      setText(gAcAge, b);
    } else {
      setText(gAcAge, "");
    }
  }

  // --- DIEU KHIEN: chế độ nào ĐANG chạy ---
  // Hai nút này vừa là HÀNH ĐỘNG vừa là ĐÈN BÁO. Trước đây chỉ là hành động,
  // nên nhìn vào trang ĐIỀU KHIỂN không biết máy đang do máy chủ điều khiển hay
  // đang bị ghi đè tại chỗ — muốn biết phải sang tận TRANG CHỦ đọc huy hiệu.
  //
  // Bấm lại nút đang sáng vẫn có tác dụng: THỦ CÔNG gửi lại giá trị vừa chỉnh,
  // TỰ ĐỘNG trả quyền lần nữa. Sáng nghĩa là "đang ở chế độ này", không phải
  // "đã khoá, hết bấm được".
  buttonSelect(gSendBtn, m.overrideLocal);
  buttonSelect(gAutoBtn, !m.overrideLocal);

  // --- DIEU KHIEN: mã IR nào đã có ---
  bool changed = false;
  bool ok0 = m.coolMask != 0;
  if (gModeOk[0] != ok0)      { gModeOk[0] = ok0;      changed = true; }
  if (gModeOk[1] != m.hasDry) { gModeOk[1] = m.hasDry; changed = true; }
  if (gModeOk[2] != m.hasFan) { gModeOk[2] = m.hasFan; changed = true; }
  if (gModeOk[3] != m.hasOff) { gModeOk[3] = m.hasOff; changed = true; }
  if (changed) refreshControl();

  // --- THONG TIN ---
  setText(gInfoVal[0], m.ssid[0] ? m.ssid : "--");
  setText(gInfoVal[1], m.ip[0] ? m.ip : "--");
  if (m.wifiUp) snprintf(b, sizeof b, "%d dBm", m.rssi); else snprintf(b, sizeof b, "--");
  setText(gInfoVal[2], b);
  setText(gInfoVal[3], m.mqttUp ? "ĐÃ NỐI" : "MẤT KẾT NỐI");
  lv_obj_set_style_text_color(gInfoVal[3], m.mqttUp ? ok() : err(), 0);
  // Mọi ký tự đặc biệt dùng ở đây (· – — … •) phải CÓ TRONG DẢI SINH FONT của
  // tools/make_lvgl_fonts.ps1. Thiếu một cái là LVGL vẽ ô trống ở đúng chỗ đó VÀ
  // mỗi khung vẽ lại phun "glyph dsc. not found for U+xxxx" ra serial, đủ để
  // chôn mọi dòng log khác. Thêm ký tự mới vào chuỗi thì mở script kiểm dải.
  snprintf(b, sizeof b, "nhận %lu · bỏ %lu",
           (unsigned long)m.espnowRx, (unsigned long)m.espnowDrop);
  setText(gInfoVal[4], b);
  if (m.outOnline) snprintf(b, sizeof b, "%lus trước", (unsigned long)m.outAgeSec);
  else             snprintf(b, sizeof b, "mất kết nối");
  setText(gInfoVal[5], b);
  snprintf(b, sizeof b, "%u mã", (unsigned)m.irCodeCount);
  setText(gInfoVal[6], b);
  snprintf(b, sizeof b, "%s - %luh%02lum", m.fw ? m.fw : "?",
           (unsigned long)(m.uptimeSec / 3600), (unsigned long)((m.uptimeSec % 3600) / 60));
  setText(gInfoVal[7], b);
  snprintf(b, sizeof b, "MAC %s · KÊNH %u", m.mac[0] ? m.mac : "?", (unsigned)m.channel);
  setText(gInfoFoot, b);

  // --- lớp phủ học remote ---
  if (m.learning) {
    lv_obj_clear_flag(gLearn, LV_OBJ_FLAG_HIDDEN);
    setText(gLearnLbl, m.learnLabel);
    snprintf(b, sizeof b, "còn lại %lus", (unsigned long)m.learnRemainSec);
    setText(gLearnSec, b);
    // 30 s là thời gian chờ đặt trong config.h (LEARN_TIMEOUT_MS).
    int pct = m.learnRemainSec > 30 ? 100 : (int)(m.learnRemainSec * 100 / 30);
    lv_bar_set_value(gLearnBar, pct, LV_ANIM_OFF);
  } else {
    lv_obj_add_flag(gLearn, LV_OBJ_FLAG_HIDDEN);
  }
}

void toast(const char *msg, bool isError) {
  if (!gToast) return;
  lv_label_set_text(gToastLbl, msg);
  lv_obj_set_style_bg_color(gToast, isError ? err() : textPrimary(), 0);
  lv_obj_clear_flag(gToast, LV_OBJ_FLAG_HIDDEN);
  // ĐƯA LÊN TRÊN CÙNG mỗi lần hiện.
  //
  // gToast được dựng trong buildChrome(), tức là TRƯỚC bốn trang nội dung — mà
  // LVGL vẽ theo thứ tự tạo, nên mọi trang đều nằm đè lên nó. Toast nằm ở y=178
  // còn hai nút THỦ CÔNG / TỰ ĐỘNG ở y=160..202, vừa đúng chồng nhau: bấm hai
  // nút đó thì thông báo hiện ra NGAY DƯỚI nút và bị che gần hết.
  //
  // Sửa ở đây chứ không phải đổi thứ tự dựng: toast về bản chất là lớp trên cùng
  // và phải luôn ở trên, kể cả khi sau này có thêm trang mới.
  lv_obj_move_foreground(gToast);
  gToastUntil = lv_tick_get() + 2500;
}

void tickToast(uint32_t nowMs) {
  if (gToastUntil && nowMs >= gToastUntil) {
    lv_obj_add_flag(gToast, LV_OBJ_FLAG_HIDDEN);
    gToastUntil = 0;
  }
}

void setClock(bool valid, uint8_t hh, uint8_t mm) {
  char b[8];
  if (valid) snprintf(b, sizeof b, "%02u:%02u", (unsigned)hh, (unsigned)mm);
  else       snprintf(b, sizeof b, "--:--");
  setText(gLblClock, b);
}

void setBrightness(uint8_t percent) {
  char b[8];
  snprintf(b, sizeof b, "%u%%", (unsigned)percent);
  setText(gBrightLbl, b);
}

void setBuzzer(bool on) {
  buttonSelect(gBuzzOn, on);
  buttonSelect(gBuzzOff, !on);
}

} // namespace Screens
