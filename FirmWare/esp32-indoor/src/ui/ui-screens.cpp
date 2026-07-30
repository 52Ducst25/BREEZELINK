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

// Lần cuối người dùng sờ vào ± hoặc nút chế độ (lv_tick_get, ms). 0 = chưa sờ.
//
// VÌ SAO CẦN: gPendSet/gPendMode giờ ĐƯỢC ĐỒNG BỘ từ trạng thái thật của máy lạnh
// (xem syncPending() bên dưới). Trước đây chúng là biến cục bộ khởi tạo cứng 26 và
// không bao giờ đổi theo máy chủ, nên đặt 22 độ trong app thì màn vẫn trơ trơ hiện
// 26 — hai giao diện của cùng một máy nói hai con số khác nhau.
//
// Nhưng đồng bộ MÙ thì hỏng theo kiểu khác, tệ hơn: đúng lúc người ta đang bấm `+`
// mà một gói cmd tới là con số bị giật về giá trị của máy chủ NGAY DƯỚI NGÓN TAY,
// rồi cú bấm tiếp theo cộng từ một mốc khác hẳn mốc họ đang nhìn. Nên trong khoảng
// lặng dưới đây, người đang chỉnh luôn thắng.
uint32_t gPendTouchedMs = 0;
constexpr uint32_t PEND_EDIT_LOCK_MS = 5000;

// Ai đang cầm lái, bản sao ở lõi 0 của Ui::Model.overrideLocal. onAdjust() cần
// biết điều này mà nó chạy trong callback chạm, không có Model trong tay.
bool gOverrideNow = false;

// Hạn tự gửi sau khi bấm ± (lv_tick_get, ms). 0 = không có gì chờ gửi.
//
// VÌ SAO PHẢI CÓ: trước đây bấm ± trên panel chỉ đổi con số, phải bấm thêm THỦ
// CÔNG mới phát IR — trong khi app bấm ± là tự gửi. Cùng một máy, hai giao diện,
// hai cách hành xử; ở panel treo tường thì nó bị đọc là nút hỏng.
//
// Và phải là DEBOUNCE chứ không gửi ngay từng nhịp: đi từ 26 lên 30 là bốn cú
// bấm, gửi ngay là bốn khung IR liên tiếp. Mỗi khung chặn ~50-250ms trong
// IrIo::blast() và máy lạnh phải xử lý cả bốn — 26, 27, 28, rồi 30. Gộp lại chỉ
// phát mốc cuối, đúng con số người dùng muốn.
//
// 500ms bằng đúng app (override_panel.dart `_tempDebounce`) — hai giao diện
// không nên "cảm giác" khác nhau khi làm cùng một việc.
uint32_t gAutoSendAt = 0;
constexpr uint32_t AUTO_SEND_DEBOUNCE_MS = 500;
const char *const kModeName[4] = {"COOL", "DRY", "FAN", "OFF"};
const char *const kModeLabel[4] = {"LẠNH", "KHÔ", "QUẠT", "TẮT"};
bool  gModeOk[4] = {false, false, false, false};   // có mã IR trong NVS chưa

// --- THONG TIN ---------------------------------------------------------------
const char *const kInfoRow[8] = {"WIFI", "IP", "SÓNG", "MQTT",
                                 "ESP-NOW", "NGOÀI TRỜI", "MÃ IR", "FW"};
lv_obj_t *gInfoVal[8], *gInfoFoot;

// --- CAI DAT -----------------------------------------------------------------
lv_obj_t *gBrightLbl, *gBuzzOn, *gBuzzOff, *gIrCountLbl;

// --- MA IR DA HOC (lớp phủ) --------------------------------------------------
//  18 tổ hợp: COOL 16..30 (15 mức) + DRY + FAN + OFF — đúng dải mà `coolMask`
//  của Ui::Model mã hoá (bit i = COOL 16+i) và đúng bộ mã ir_service coi là bắt
//  buộc. Dựng đủ 18 hàng MỘT LẦN rồi ẩn/hiện, không tạo/huỷ widget lúc chạy:
//  lv_obj_del() giữa lúc tác vụ UI đang vẽ là cách chắc chắn nhất để hỏng cây
//  widget, mà bố cục flex của LVGL 8 vốn đã tự bỏ qua các con đang HIDDEN.
constexpr uint8_t IR_ROWS      = 18;
constexpr uint8_t IR_COOL_MIN  = 16;   // bit 0 của coolMask
constexpr uint8_t IR_COOL_N    = 15;   // COOL 16..30
lv_obj_t *gIrOverlay, *gIrList, *gIrEmpty, *gIrTitle;
lv_obj_t *gIrRow[IR_ROWS], *gIrRowLbl[IR_ROWS];
bool      gIrShown[IR_ROWS] = {false};   // trạng thái đang hiện, để khỏi ghi lại vô ích
uint16_t  gIrLastMask = 0xFFFF;          // ép lần cập nhật đầu tiên luôn chạy
bool      gIrLastFixed[3] = {true, true, true};

// --- Hộp xác nhận DÙNG CHUNG ------------------------------------------------
//  MỘT hộp cho mọi việc cần hỏi lại (xoá mã, khởi động lại). Trước đây nó gắn
//  cứng vào việc xoá mã; dựng thêm một hộp nữa cho nút khởi động lại là nhân đôi
//  ~15 widget cho cùng một bố cục, trong khi heap đã đi từ 110 KB xuống 72 KB
//  qua bốn lần thêm màn.
//
//  Câu hỏi, dòng phụ, chữ trên nút đồng ý và việc-sẽ-làm đều truyền vào lúc hỏi.
using ConfirmFn = void (*)(uint32_t arg);
lv_obj_t *gConfirm, *gConfirmLbl, *gConfirmNote, *gConfirmYesLbl;
ConfirmFn gConfirmCb  = nullptr;
uint32_t  gConfirmArg = 0;

// --- NHAT KY LENH (lớp phủ) --------------------------------------------------
//  8 lệnh gần nhất từ backend. Sống trong RAM, mất khi mất điện — đây là công cụ
//  chẩn đoán tại chỗ ("vừa nãy máy chủ có ra lệnh không, node làm gì với nó"),
//  không phải sổ lưu trữ. Lịch sử đầy đủ đã nằm trong bảng `commands` và
//  `comfort_log` trên server, xem được ở web và app.
constexpr uint8_t LOG_ROWS = 8;
struct LogEntry {
  bool     used;
  bool     hasClock;
  uint8_t  hh, mm;
  uint32_t atSec;               // uptime giây lúc nhận — mốc cho "x phút trước"
  char     what[24];            // "LẠNH 25°C"
  char     reason[24];          // nguyên văn từ backend
  Ui::CmdLog::Result result;
};
LogEntry  gLog[LOG_ROWS];
lv_obj_t *gLogOverlay, *gLogList, *gLogEmpty;
lv_obj_t *gLogRow[LOG_ROWS], *gLogTop[LOG_ROWS], *gLogSub[LOG_ROWS];
uint32_t  gLogLastMin = 0xFFFFFFFF;   // phút đã vẽ, để làm mới mốc tương đối

/// Hàng i -> (mode, temp). temp < 0 = mã cố định, khớp quy ước IrStore.
void irComboAt(uint8_t i, const char *&mode, int &temp) {
  if (i < IR_COOL_N) { mode = "COOL"; temp = IR_COOL_MIN + i; return; }
  temp = -1;
  mode = (i == IR_COOL_N) ? "DRY" : (i == IR_COOL_N + 1) ? "FAN" : "OFF";
}

/// Hàng i -> nhãn người đọc ("LẠNH 25°C", "KHÔ"...).
///
/// Một chỗ duy nhất vì chuỗi này xuất hiện ở HAI nơi — hàng trong danh sách và
/// câu hỏi trong hộp xác nhận. Hai nơi mà tự dựng chuỗi riêng thì sẽ có ngày
/// lệch nhau, mà đây đúng là chỗ không được lệch: hộp xác nhận tồn tại để người
/// dùng đối chiếu xem mình có bấm nhầm hàng không.
///
/// Nhãn tiếng Việt như màn ĐIỀU KHIỂN, KHÔNG phải mã máy "COOL"/"DRY" — cùng một
/// thứ mà gọi hai tên ở hai màn thì người dùng phải tự dịch.
void irRowLabel(uint8_t i, char *out, size_t n) {
  const char *mode;
  int temp;
  irComboAt(i, mode, temp);
  if (temp >= 0) {
    snprintf(out, n, "%s %d°C", kModeLabel[0], temp);
  } else {
    snprintf(out, n, "%s", (i == IR_COOL_N)       ? kModeLabel[1]
                           : (i == IR_COOL_N + 1) ? kModeLabel[2]
                                                  : kModeLabel[3]);
  }
}

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

  // Logo hai màu: "BREEZE" trắng + "LINK" xanh accent.
  //
  // Phải là HAI nhãn — LVGL 8 không có văn bản nhiều màu trong một lv_label
  // (không có RichText/TextSpan như Flutter), nên cách duy nhất là hai đối tượng
  // đặt cạnh nhau.
  //
  // ĐỪNG ghi cứng toạ độ x của nhãn thứ hai. Bề rộng "BREEZE" phụ thuộc font VLW
  // đang dùng và letter-space; đổi font (hoặc sinh lại font với dải ký tự khác)
  // là hai chữ dính vào nhau hoặc hở một khoảng — mà lỗi đó chỉ thấy khi soi màn
  // thật, không có gì trong lúc biên dịch cảnh báo. Nên đo bằng chính thước của
  // LVGL, có tính letter-space y như lúc vẽ.
  static const lv_coord_t BRAND_X = 20, BRAND_Y = 4, BRAND_LETTER_SPACE = 1;

  lv_obj_t *brand1 = label(gStatusBar, BRAND_X, BRAND_Y, "BREEZE", fontLabel(), textPrimary());
  lv_obj_set_style_text_letter_space(brand1, BRAND_LETTER_SPACE, 0);

  lv_point_t sz;
  lv_txt_get_size(&sz, "BREEZE", fontLabel(), BRAND_LETTER_SPACE, 0,
                  LV_COORD_MAX, LV_TEXT_FLAG_NONE);

  lv_obj_t *brand2 = label(gStatusBar, BRAND_X + sz.x, BRAND_Y, "LINK", fontLabel(), accent());
  lv_obj_set_style_text_letter_space(brand2, BRAND_LETTER_SPACE, 0);

  // Đồng hồ có giây nên rộng hơn trước ~18px ("14:32:07" so với "14:32"). Hai
  // chấm trạng thái lùi trái nhường chỗ, và đồng hồ CĂN PHẢI trong một ô cố
  // định thay vì neo trái: chuỗi đổi độ rộng mỗi khi nhảy giữa "--:--:--" và
  // giờ thật, neo trái thì nó thò ra khỏi mép phải màn.
  gDotWifi  = dot(gStatusBar, 214, 7, textMuted());
  gDotMqtt  = dot(gStatusBar, 228, 7, textMuted());
  gLblClock = label(gStatusBar, 240, 4, "--:--:--", fontLabel(), textPrimary());
  lv_obj_set_width(gLblClock, 74);   // 240..314, chừa 6px mép như thanh bên trái
  lv_obj_set_style_text_align(gLblClock, LV_TEXT_ALIGN_RIGHT, 0);

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
  else                          setText(gLimitLbl, "");
}

/// Đặt hàng lệnh THỦ CÔNG với (chế độ, nhiệt độ) đang chờ.
///
/// Dùng chung cho ba đường vào — bấm THỦ CÔNG, hết hạn debounce của ±, và đổi chế
/// độ khi đang giữ quyền — nên nội dung lệnh không thể trôi lệch giữa chúng.
/// Chỉ ĐẶT HÀNG vào queue; việc phát IR là của loop() ở lõi 1 (xem ui.h).
static void sendPending() {
  if (!gOnCmd) return;
  gAutoSendAt = 0;            // huỷ hạn đang chờ, khỏi gửi lặp ngay sau đó
  Ui::Command c{};
  c.kind = Ui::Command::MANUAL;
  snprintf(c.mode, sizeof c.mode, "%s", kModeName[gPendMode]);
  c.setpoint = gPendSet;
  gOnCmd(c);
  toast("ĐANG GỬI...");
}

void onAdjust(lv_event_t *e) {
  int d = (int)(intptr_t)lv_event_get_user_data(e);
  gPendSet += d;
  if (gPendSet < 16) gPendSet = 16;
  if (gPendSet > 30) gPendSet = 30;
  gPendTouchedMs = lv_tick_get();

  // Tự phát khi bấm ± — nhưng CHỈ khi cả hai điều kiện dưới đây đúng.
  //
  //  1. Đang giữ quyền. Máy chủ đang cầm lái thì ± chỉ đổi số chờ, không tự giành
  //     quyền: chạm nhầm vào màn treo tường không được phép hất máy chủ ra khỏi
  //     vòng điều khiển. Muốn giành thì bấm THỦ CÔNG, một cú bấm có chủ đích.
  //
  //  2. Đang ở COOL. Chỉ COOL có ma trận (chế độ, nhiệt độ); DRY/FAN/OFF dùng mã
  //     cố định và aliasTemp() bỏ hẳn nhiệt độ. Không có điều kiện này thì bấm ±
  //     trong FAN phát LẠI ĐÚNG CÙNG MỘT khung — đã thấy thật trong log: năm gói
  //     "FAN 23" liên tiếp. Và lặp lại KHÔNG vô hại: với nút xoay vòng (tốc độ
  //     quạt, đảo gió) lần phát thứ hai nhảy sang nấc khác, nên IR thừa ở đây có
  //     thể đổi trạng thái máy lạnh theo cách không ai yêu cầu.
  //     Cùng luật với app (override_panel.dart: `steppable = _on && COOL`).
  if (gOverrideNow && gPendMode == 0) gAutoSendAt = lv_tick_get() + AUTO_SEND_DEBOUNCE_MS;

  refreshControl();
}

void onMode(lv_event_t *e) {
  uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  if (!gModeOk[i]) {          // "không phím chết": nói lý do thay vì im lặng
    toast("CHƯA HỌC MÃ — VÀO APP ĐỂ HỌC", true);
    return;
  }
  gPendMode = i;
  gPendTouchedMs = lv_tick_get();
  refreshControl();

  // Không debounce như ±: đổi chế độ là một lựa chọn dứt khoát, không phải chuỗi
  // nhịp dồn. Đúng như app (`_cycleMode` gửi ngay, `_step` mới debounce).
  if (gOverrideNow) sendPending();
}

/// Kéo giá trị chờ gửi theo trạng thái máy lạnh thật (do máy chủ hoặc app đặt),
/// để màn này và app không bao giờ khoe hai con số khác nhau về cùng một máy.
///
/// Trả true nếu có gì đổi (người gọi phải vẽ lại trang ĐIỀU KHIỂN).
///
/// BA ĐIỀU KIỆN, thiếu một là sinh lỗi khó tìm:
///  1. Người dùng chưa sờ vào ± / nút chế độ trong PEND_EDIT_LOCK_MS — xem chú
///     thích ở gPendTouchedMs.
///  2. Model phải CÓ số thật. `setpoint = -1` và `mode` rỗng nghĩa là "chưa biết"
///     (ui.h nói rõ: NAN/-1 cho thiếu số, KHÔNG dùng 0) — đồng bộ theo nó là tự
///     tay ghi 16 độ vào màn lúc node vừa khởi động, trước khi có lệnh nào tới.
///  3. Chỉ nhận nhiệt độ trong dải nút bấm cho phép (16..30). Ngoài dải thì đó
///     là mã cố định hoặc dữ liệu lạ; kẹp im lặng sẽ hiện một con số máy lạnh
///     chưa từng nhận.
static bool syncPending(const Ui::Model &m) {
  if (gPendTouchedMs && (lv_tick_get() - gPendTouchedMs) < PEND_EDIT_LOCK_MS) return false;

  bool changed = false;

  if (m.mode[0]) {
    for (uint8_t i = 0; i < 4; i++) {
      if (strcmp(m.mode, kModeName[i]) == 0) {
        if (gPendMode != i) { gPendMode = i; changed = true; }
        break;
      }
    }
  }

  // Nhiệt độ chỉ có nghĩa với COOL — DRY/FAN/OFF không dùng setpoint, nên giữ
  // nguyên con số cũ để người dùng chuyển về LẠNH là thấy lại mốc quen thuộc.
  if (gPendMode == 0 && m.setpoint >= 16 && m.setpoint <= 30 && gPendSet != m.setpoint) {
    gPendSet = m.setpoint;
    changed = true;
  }

  return changed;
}

void onSend(lv_event_t *) { sendPending(); }

void onAuto(lv_event_t *) {
  if (!gOnCmd) return;
  gAutoSendAt = 0;    // trả quyền thì bỏ luôn lệnh ± đang chờ gửi
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

/// Mở/đóng lớp phủ danh sách mã. KHÔNG đi qua SettingFn: đây thuần tuý là
/// hiện/ẩn widget, không đụng phần cứng nào, nên gửi sang ui.cpp rồi quay về chỉ
/// thêm một vòng vô ích.
void onIrOpen(lv_event_t *) {
  if (!gIrOverlay) return;
  lv_obj_clear_flag(gIrOverlay, LV_OBJ_FLAG_HIDDEN);
  // KHÔNG move_foreground ở đây. Thứ tự dựng trong build() đã cho đúng z-order
  // cần có (4 trang < danh sách < học remote), mà đẩy lên trên cùng sẽ phá đúng
  // nửa sau: backend kích hoạt học trong lúc danh sách đang mở thì lớp phủ "bấm
  // nút trên remote đi" bị che, và người dùng ngồi nhìn một bảng liệt kê không
  // giải thích được vì sao máy đang chờ. Toast tự lo phần của nó (nó gọi
  // move_foreground mỗi lần hiện), nên vẫn nổi lên trên danh sách như thường.
  lv_obj_scroll_to_y(gIrList, 0, LV_ANIM_OFF);   // mở lại là về đầu danh sách
}

/// Gỡ hộp xác nhận và quên việc đang chờ. Gọi từ mọi lối thoát — đóng màn đứng
/// sau nó, bấm huỷ, hoặc vừa xác nhận xong.
void confirmDismiss() {
  gConfirmCb = nullptr;
  if (gConfirm) lv_obj_add_flag(gConfirm, LV_OBJ_FLAG_HIDDEN);
}

/// Hỏi lại trước khi làm một việc khó lùi.
///
/// [question] phải NÊU ĐÍCH DANH việc sắp làm ("XOÁ LẠNH 25°C ?"), không phải
/// "Bạn có chắc không?": người dùng cần kiểm chứng mình có bấm đúng chỗ không,
/// mà câu hỏi chung chung thì không trả lời được điều đó.
/// [note] nói hậu quả — cái quyết định người ta bấm tiếp hay lùi.
void confirmAsk(const char *question, const char *note, const char *yesLabel,
                ConfirmFn cb, uint32_t arg) {
  if (!gConfirm) return;
  setText(gConfirmLbl, question);
  setText(gConfirmNote, note);
  setText(gConfirmYesLbl, yesLabel);
  gConfirmCb  = cb;
  gConfirmArg = arg;
  lv_obj_clear_flag(gConfirm, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(gConfirm);
}

void onConfirmNo(lv_event_t *) { confirmDismiss(); }

void onConfirmYes(lv_event_t *) {
  // Chụp lại RỒI mới đóng: confirmDismiss() xoá gConfirmCb, nên đọc sau khi đóng
  // là gọi vào nullptr. Đóng trước khi chạy việc để người dùng thấy hộp biến mất
  // ngay, thay vì đứng nhìn nó trong lúc lõi 1 làm việc.
  ConfirmFn cb  = gConfirmCb;
  uint32_t  arg = gConfirmArg;
  confirmDismiss();
  if (cb) cb(arg);
}

void onIrClose(lv_event_t *) {
  // Đóng danh sách phải huỷ luôn câu hỏi đang treo. Không làm thì lần mở sau
  // hộp xác nhận hiện lại nguyên trạng, hỏi về một hàng người dùng đã quên —
  // và một cú chạm vào XOÁ lúc đó là xoá thứ họ không hề định xoá.
  confirmDismiss();
  if (gIrOverlay) lv_obj_add_flag(gIrOverlay, LV_OBJ_FLAG_HIDDEN);
}

/// Việc thật sự chạy khi người dùng xác nhận xoá. Chỉ ĐẶT HÀNG — việc xoá là ghi
/// NVS, mà NVS thuộc loop() ở lõi 1 (xem chú thích Ui::Command). Kết quả quay
/// lại bằng toast qua reply().
void doIrDelete(uint32_t arg) {
  const uint8_t i = (uint8_t)arg;
  if (i >= IR_ROWS || !gOnCmd) return;

  const char *mode;
  int temp;
  irComboAt(i, mode, temp);

  Ui::Command c{};
  c.kind = Ui::Command::DEL_CODE;
  snprintf(c.mode, sizeof c.mode, "%s", mode);
  c.setpoint = temp;
  gOnCmd(c);
}

/// Bấm XOÁ ở một hàng: chưa xoá gì, chỉ hỏi lại.
void onIrDelete(lv_event_t *e) {
  const uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  if (i >= IR_ROWS) return;

  char name[32], q[48];
  irRowLabel(i, name, sizeof name);
  snprintf(q, sizeof q, "XOÁ %s ?", name);
  confirmAsk(q, "Mã vẫn còn trên máy chủ và sẽ tự nạp lại", "XOÁ", doIrDelete, i);
}

/// Việc thật sự chạy khi xác nhận khởi động lại. ui.cpp mới là nơi gọi
/// ESP.restart() — nó sở hữu phần cứng, còn file này chỉ lo hỏi và hiển thị.
void doReboot(uint32_t) {
  if (gOnSetting) gOnSetting(REBOOT);
}

/// Xin máy chủ gửi lại toàn bộ kho mã. Không hỏi lại: việc này chỉ THÊM mã,
/// không xoá gì, và mã trùng thì ghi đè tại chỗ — không có gì để mất.
void onIrResync(lv_event_t *) {
  if (!gOnCmd) return;
  Ui::Command c{};
  c.kind = Ui::Command::RESYNC;
  gOnCmd(c);
}

void onRebootAsk(lv_event_t *) {
  // Nói ĐÚNG cái giá phải trả. "Bạn có chắc không?" không giúp ai quyết định
  // được; "mất kết nối ~20 giây" thì có — và nó cũng nói rõ đây là việc tự phục
  // hồi, không phải thao tác nguy hiểm, để người ta khỏi ngại bấm khi cần thật.
  confirmAsk("KHỞI ĐỘNG LẠI ?", "Mất kết nối khoảng 20 giây · mã IR không mất",
             "KHỞI ĐỘNG", doReboot, 0);
}

// ---------------------------------------------------------------------------
//  Nhật ký lệnh — vẽ lại
// ---------------------------------------------------------------------------
/// Kết quả -> câu chữ + màu. Đây là thứ log serial vốn có mà màn thì không, và
/// cũng là câu hỏi người lắp cần trả lời khi máy lạnh không nhúc nhích: lệnh
/// KHÔNG tới, hay tới rồi mà node không phát được?
const char *logResultText(Ui::CmdLog::Result r) {
  switch (r) {
    case Ui::CmdLog::SENT:      return "đã phát";
    case Ui::CmdLog::NO_CODE:   return "chưa học mã";
    case Ui::CmdLog::NEED_RAW:  return "thiếu mã · đang xin lại";
    default:                    return "bản lặp · bỏ qua";
  }
}

lv_color_t logResultColor(Ui::CmdLog::Result r) {
  switch (r) {
    case Ui::CmdLog::SENT:      return ok();
    case Ui::CmdLog::NO_CODE:   return err();
    case Ui::CmdLog::NEED_RAW:  return warn();
    default:                    return textMuted();
  }
}

/// Mốc thời gian của một dòng. Có DS1307 thì "14:32"; chưa đặt giờ thì mốc
/// TƯƠNG ĐỐI so với hiện tại. Không bao giờ bịa 00:00 — cùng luật với đồng hồ
/// trên thanh trạng thái.
void logStamp(const LogEntry &e, uint32_t nowSec, char *out, size_t n) {
  if (e.hasClock) {
    snprintf(out, n, "%02u:%02u", (unsigned)e.hh, (unsigned)e.mm);
    return;
  }
  const uint32_t ago = (nowSec > e.atSec) ? (nowSec - e.atSec) : 0;
  if (ago < 60)         snprintf(out, n, "vừa xong");
  else if (ago < 3600)  snprintf(out, n, "%lu phút trước", (unsigned long)(ago / 60));
  else                  snprintf(out, n, "%lu giờ trước", (unsigned long)(ago / 3600));
}

void renderLog(uint32_t nowSec) {
  if (!gLogList) return;
  uint8_t shown = 0;
  char stamp[20], line[48];

  for (uint8_t i = 0; i < LOG_ROWS; i++) {
    if (!gLog[i].used) {
      lv_obj_add_flag(gLogRow[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    logStamp(gLog[i], nowSec, stamp, sizeof stamp);
    snprintf(line, sizeof line, "%s · %s", stamp, gLog[i].what);
    setText(gLogTop[i], line);

    // Nguyên văn `reason` của backend đứng cạnh kết quả: nó nói lệnh này SINH RA
    // TỪ ĐÂU (vòng lặp tự động, người bấm trong app, hay một nút rời), thứ mà
    // mode/setpoint không phân biệt được.
    snprintf(line, sizeof line, "%s · %s", gLog[i].reason, logResultText(gLog[i].result));
    setText(gLogSub[i], line);
    lv_obj_set_style_text_color(gLogSub[i], logResultColor(gLog[i].result), 0);

    lv_obj_clear_flag(gLogRow[i], LV_OBJ_FLAG_HIDDEN);
    shown++;
  }

  if (shown) lv_obj_add_flag(gLogEmpty, LV_OBJ_FLAG_HIDDEN);
  else       lv_obj_clear_flag(gLogEmpty, LV_OBJ_FLAG_HIDDEN);
}

void onLogOpen(lv_event_t *) {
  if (!gLogOverlay) return;
  lv_obj_clear_flag(gLogOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_scroll_to_y(gLogList, 0, LV_ANIM_OFF);
}

void onLogClose(lv_event_t *) {
  if (gLogOverlay) lv_obj_add_flag(gLogOverlay, LV_OBJ_FLAG_HIDDEN);
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
  label(r1, 12, 12, "ÂM THANH", fontBody(), textPrimary());
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
  // Biểu tượng thay chữ "CHẠY" — chữ đó nói hành động chung chung, còn mũi tên
  // vòng lại thì nói đúng việc: khởi động lại.
  //
  // PHẢI ĐỔI FONT CỦA NHÃN sang Montserrat. Các glyph LV_SYMBOL_* nằm ở dải
  // 0xF000+ và CHỈ có trong font dựng sẵn của LVGL; font Việt trong ui/fonts/
  // sinh bằng lv_font_conv không có dải đó, nên để nguyên font mặc định của
  // button() thì nút hiện Ô VUÔNG và mỗi khung vẽ lại phun một dòng
  // "glyph dsc. not found for U+F021" ra serial — đủ để chôn mọi log khác.
  // Cùng khuôn với 4 tab ở buildChrome().
  //
  // REFRESH chứ không phải POWER: POWER đã là biểu tượng của tab ĐIỀU KHIỂN,
  // dùng lại ở đây thì cùng một hình mang hai nghĩa trên cùng một màn.
  lv_obj_t *br = button(r2, 226, 6, 68, 28, LV_SYMBOL_REFRESH);
  lv_obj_set_style_border_color(br, err(), 0);
  lv_obj_t *brLbl = lv_obj_get_child(br, 0);
  lv_obj_set_style_text_font(brLbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(brLbl, err(), 0);
  // Qua hộp xác nhận, KHÔNG gọi thẳng onSetting(REBOOT) nữa. Nút này nằm ngay
  // dưới hai hàng bấm-là-xong (độ sáng, âm báo) trên một màn cảm ứng, nên chạm
  // nhầm là chuyện có thật — và hậu quả ở đây là cả nhà mất điều khiển ~20 giây.
  lv_obj_add_event_cb(br, onRebootAsk, LV_EVENT_CLICKED, nullptr);

  // y=142 là chỗ KHỞI ĐỘNG LẠI từng nằm trước khi dời lên; hàng cuối vừa khít
  // vùng nội dung (142 + 40 = 182 < CONTENT_H 180+2 mép card).
  lv_obj_t *r3 = card(p, PAD, 142, 308, 40);
  label(r3, 12, 12, "MÃ IR ĐÃ HỌC", fontBody(), textPrimary());
  // Đếm ngay trên hàng, không bắt mở ra mới biết: người lắp cần trả lời "bo này
  // đã học đủ chưa" trong một cái liếc, giống ô MÃ IR ở màn THÔNG TIN.
  gIrCountLbl = label(r3, 190, 13, "0", fontTiny(), textMuted());
  lv_obj_set_width(gIrCountLbl, 30);
  lv_obj_set_style_text_align(gIrCountLbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_t *bl = button(r3, 226, 6, 68, 28, "XEM");
  lv_obj_add_event_cb(bl, onIrOpen, LV_EVENT_CLICKED, nullptr);

  // Hàng thứ 5 vượt quá vùng nội dung (188 + 40 = 228 > CONTENT_H 180), nên
  // trang này — VÀ CHỈ TRANG NÀY — được cuộn dọc. Ba trang kia vẫn cố định:
  // cho cuộn ở nơi không có gì để cuộn chỉ tạo ra cảm giác "trang bị xê dịch"
  // mỗi khi vuốt trượt tay.
  lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(p, LV_DIR_VER);

  lv_obj_t *r4 = card(p, PAD, 188, 308, 40);
  label(r4, 12, 12, "NHẬT KÝ LỆNH", fontBody(), textPrimary());
  lv_obj_t *bg = button(r4, 226, 6, 68, 28, "XEM");
  lv_obj_add_event_cb(bg, onLogOpen, LV_EVENT_CLICKED, nullptr);
}

// ---------------------------------------------------------------------------
//  Lớp phủ — MA IR DA HOC
// ---------------------------------------------------------------------------
void buildIrList() {
  lv_obj_t *scr = lv_scr_act();
  gIrOverlay = lv_obj_create(scr);
  lv_obj_remove_style_all(gIrOverlay);
  lv_obj_set_pos(gIrOverlay, 0, 0);
  lv_obj_set_size(gIrOverlay, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(gIrOverlay, bgPrimary(), 0);
  lv_obj_set_style_bg_opa(gIrOverlay, LV_OPA_COVER, 0);
  lv_obj_clear_flag(gIrOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(gIrOverlay, LV_OBJ_FLAG_HIDDEN);

  gIrTitle = label(gIrOverlay, 16, 10, "MÃ IR ĐÃ HỌC", fontLabel(), accent());
  lv_obj_set_style_text_letter_space(gIrTitle, 2, 0);

  // XIN MÃ đứng cạnh ĐÓNG vì đây là màn duy nhất câu hỏi "mã đâu mất rồi?" nảy
  // ra — nhìn thấy tổ hợp bị khuyết rồi bấm ngay, không phải đi tìm ở màn khác.
  lv_obj_t *bs = button(gIrOverlay, 156, 6, 74, 26, "XIN MÃ");
  lv_obj_add_event_cb(bs, onIrResync, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *bc = button(gIrOverlay, 236, 6, 68, 26, "ĐÓNG");
  lv_obj_add_event_cb(bc, onIrClose, LV_EVENT_CLICKED, nullptr);

  // Vùng cuộn. 18 hàng × 34 px = 612 px trong khung 166 px, nên cuộn là bắt buộc
  // chứ không phải phòng xa. LV_DIR_VER thôi: cho phép cuộn ngang thì chỉ cần
  // vuốt chéo một cái là danh sách trôi ngang và không có gì kéo nó về.
  gIrList = lv_obj_create(gIrOverlay);
  lv_obj_remove_style_all(gIrList);
  lv_obj_set_pos(gIrList, 16, 38);
  lv_obj_set_size(gIrList, 288, 166);
  lv_obj_set_flex_flow(gIrList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(gIrList, 4, 0);
  lv_obj_set_scroll_dir(gIrList, LV_DIR_VER);
  lv_obj_add_flag(gIrList, LV_OBJ_FLAG_SCROLLABLE);

  for (uint8_t i = 0; i < IR_ROWS; i++) {
    lv_obj_t *row = lv_obj_create(gIrList);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 272, 30);          // 288 trừ chỗ cho thanh cuộn
    lv_obj_set_style_bg_color(row, bgSecondary(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, borderSubtle(), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);   // update() mới quyết hàng nào hiện

    gIrRowLbl[i] = label(row, 10, 8, "", fontBody(), textPrimary());

    // Viền + chữ đỏ như nút KHỞI ĐỘNG LẠI: cùng một mức "việc này lấy đi thứ gì
    // đó", nên phải trông giống nhau. Không có bước xác nhận — mã vẫn còn trong
    // Postgres và tự nạp lại ở lệnh kế tiếp (xem IrStore::removeAlias), nên đây
    // là việc lùi được, khác hẳn xoá mã trên app.
    lv_obj_t *bd = button(row, 206, 3, 58, 24, "XOÁ");
    lv_obj_set_style_border_color(bd, err(), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(bd, 0), err(), 0);
    lv_obj_add_event_cb(bd, onIrDelete, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

    gIrRow[i] = row;
  }

  // Trạng thái rỗng nói việc PHẢI LÀM, không chỉ nói "trống". Học mã là bước bắt
  // buộc trước khi auto-control chạy được, mà nó chỉ khởi động từ app.
  gIrEmpty = label(gIrOverlay, 0, 104, "CHƯA HỌC MÃ NÀO\nVÀO APP ĐỂ HỌC REMOTE",
                   fontTiny(), textMuted());
  lv_obj_set_width(gIrEmpty, SCREEN_W);
  lv_obj_set_style_text_align(gIrEmpty, LV_TEXT_ALIGN_CENTER, 0);

}

// ---------------------------------------------------------------------------
//  Lớp phủ — HOP XAC NHAN (dùng chung)
// ---------------------------------------------------------------------------
void buildConfirm() {
  // Con của MÀN HÌNH, không của một lớp phủ nào: nó phục vụ cả nút XOÁ trong
  // danh sách mã lẫn nút KHỞI ĐỘNG LẠI ở màn Cài đặt, hai chỗ nằm ở hai tầng
  // khác nhau. Đổi lại, mọi lối thoát phải tự gọi confirmDismiss() — onIrClose()
  // làm việc đó.
  lv_obj_t *scr = lv_scr_act();
  gConfirm = lv_obj_create(scr);
  lv_obj_remove_style_all(gConfirm);
  lv_obj_set_pos(gConfirm, 0, 0);
  lv_obj_set_size(gConfirm, SCREEN_W, SCREEN_H);
  // Nền mờ phủ TOÀN màn, không chỉ quanh hộp. Hai việc cùng lúc: làm tối phần
  // phía sau để mắt dồn vào câu hỏi, và CHẶN CHẠM — thiếu nó thì ngón tay trượt
  // ra ngoài hộp vẫn bấm trúng nút nằm dưới, đúng cái tai nạn mà hộp này sinh ra
  // để ngăn.
  lv_obj_set_style_bg_color(gConfirm, bgPrimary(), 0);
  lv_obj_set_style_bg_opa(gConfirm, 235, 0);
  lv_obj_clear_flag(gConfirm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(gConfirm, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *box = card(gConfirm, 26, 62, 268, 116);
  lv_obj_set_style_border_color(box, err(), 0);
  lv_obj_set_style_border_width(box, 2, 0);

  gConfirmLbl = label(box, 0, 22, "", fontBody(), textPrimary());
  lv_obj_set_width(gConfirmLbl, 268);
  lv_obj_set_style_text_align(gConfirmLbl, LV_TEXT_ALIGN_CENTER, 0);

  gConfirmNote = label(box, 0, 46, "", fontTiny(), textMuted());
  lv_obj_set_width(gConfirmNote, 268);
  lv_obj_set_style_text_align(gConfirmNote, LV_TEXT_ALIGN_CENTER, 0);

  // HUỶ nằm BÊN TRÁI và là nút thường; nút đồng ý bên phải, viền đỏ. Đặt ngược
  // lại thì ngón cái đi từ nút vừa bấm xuống là rơi trúng nút đồng ý — hộp xác
  // nhận biến thành hai cú chạm liên tiếp ở gần cùng một chỗ, tức là không xác
  // nhận gì cả.
  lv_obj_t *bn = button(box, 26, 74, 100, 30, "HUỶ");
  lv_obj_add_event_cb(bn, onConfirmNo, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *by = button(box, 142, 74, 100, 30, "");
  lv_obj_set_style_border_color(by, err(), 0);
  gConfirmYesLbl = lv_obj_get_child(by, 0);
  lv_obj_set_style_text_color(gConfirmYesLbl, err(), 0);
  lv_obj_add_event_cb(by, onConfirmYes, LV_EVENT_CLICKED, nullptr);
}

// ---------------------------------------------------------------------------
//  Lớp phủ — NHAT KY LENH
// ---------------------------------------------------------------------------
void buildLogList() {
  lv_obj_t *scr = lv_scr_act();
  gLogOverlay = lv_obj_create(scr);
  lv_obj_remove_style_all(gLogOverlay);
  lv_obj_set_pos(gLogOverlay, 0, 0);
  lv_obj_set_size(gLogOverlay, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(gLogOverlay, bgPrimary(), 0);
  lv_obj_set_style_bg_opa(gLogOverlay, LV_OPA_COVER, 0);
  lv_obj_clear_flag(gLogOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(gLogOverlay, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *t = label(gLogOverlay, 16, 10, "NHẬT KÝ LỆNH", fontLabel(), accent());
  lv_obj_set_style_text_letter_space(t, 2, 0);

  lv_obj_t *bc = button(gLogOverlay, 236, 6, 68, 26, "ĐÓNG");
  lv_obj_add_event_cb(bc, onLogClose, LV_EVENT_CLICKED, nullptr);

  gLogList = lv_obj_create(gLogOverlay);
  lv_obj_remove_style_all(gLogList);
  lv_obj_set_pos(gLogList, 16, 38);
  lv_obj_set_size(gLogList, 288, 166);
  lv_obj_set_flex_flow(gLogList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(gLogList, 4, 0);
  lv_obj_set_scroll_dir(gLogList, LV_DIR_VER);
  lv_obj_add_flag(gLogList, LV_OBJ_FLAG_SCROLLABLE);

  for (uint8_t i = 0; i < LOG_ROWS; i++) {
    lv_obj_t *row = lv_obj_create(gLogList);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 272, 36);
    lv_obj_set_style_bg_color(row, bgSecondary(), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, borderSubtle(), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);

    gLogTop[i] = label(row, 8, 5, "", fontTiny(), textPrimary());
    gLogSub[i] = label(row, 8, 20, "", fontTiny(), textMuted());
    gLogRow[i] = row;
  }

  // Trạng thái rỗng phân biệt "chưa có gì" với "hỏng". Vòng lặp comfort chỉ ra
  // lệnh KHI QUYẾT ĐỊNH ĐỔI, nên im lặng hàng giờ là bình thường — nói thẳng để
  // người lắp khỏi đi tìm lỗi ở chỗ không có lỗi.
  gLogEmpty = label(gLogOverlay, 0, 100,
                    "CHƯA NHẬN LỆNH NÀO\nMáy chủ chỉ ra lệnh khi quyết định đổi",
                    fontTiny(), textMuted());
  lv_obj_set_width(gLogEmpty, SCREEN_W);
  lv_obj_set_style_text_align(gLogEmpty, LV_TEXT_ALIGN_CENTER, 0);
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
  // THỨ TỰ CÓ NGHĨA: LVGL vẽ theo thứ tự tạo, nên lớp phủ học remote phải dựng
  // SAU danh sách mã để nó luôn nằm trên. Backend có thể kích hoạt học trong lúc
  // người dùng đang mở danh sách, và khi đó thứ cần nhìn là "bấm nút trên remote
  // đi", không phải bảng liệt kê.
  buildIrList();
  buildLogList();
  // Hộp xác nhận nằm TRÊN hai lớp phủ danh sách (nó hỏi về thứ trong đó) nhưng
  // DƯỚI lớp phủ học remote: học có đồng hồ đếm ngược 30 giây, còn câu hỏi thì
  // chờ được — thứ có hạn phải thắng.
  buildConfirm();
  buildLearn();
  showTab(0);
}

void update(const Ui::Model &m) {
  char b[40];

  // Bản sao cho các callback chạm — chúng không nhận được Model.
  gOverrideNow = m.overrideLocal;

  // Hết hạn debounce của ± -> phát luôn.
  //
  // Kiểm ở ĐÂY chứ không dựng lv_timer riêng: file này vốn hẹn giờ bằng mốc thời
  // hạn (xem gToastUntil), và update() chạy đều 5 lần/giây nên đã là nhịp quét sẵn
  // có. Đổi lại: sai số tới ~200ms, không đáng kể với một debounce 500ms.
  if (gAutoSendAt && lv_tick_get() >= gAutoSendAt) sendPending();

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
    snprintf(b, sizeof b, "MẤT KẾT NỐI %lus", (unsigned long)m.outAgeSec);
    setText(gOutNote, b);
    setText(gOutHum, "");
  }

  setText(gAcMode, m.mode[0] ? m.mode : "--");
  if (m.setpoint >= 0) snprintf(b, sizeof b, "%d", m.setpoint);
  else                 snprintf(b, sizeof b, "--");
  setText(gAcSet, b);

  if (m.overrideLocal) {
    lv_obj_set_style_bg_color(gAcBadge, warn(), 0);
    setText(gAcBadgeLbl, "THỦ CÔNG");
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

  // --- DIEU KHIEN: mã IR nào đã có + giá trị chờ gửi ---
  // Gộp vào MỘT cờ `changed` rồi vẽ lại một lần: refreshControl() sờ vào 6 widget
  // và update() chạy 5 lần/giây, nên gọi nó hai lần cho hai lý do trong cùng một
  // nhịp là vẽ thừa nguyên một lượt.
  bool changed = syncPending(m);
  bool ok0 = m.coolMask != 0;
  if (gModeOk[0] != ok0)      { gModeOk[0] = ok0;      changed = true; }
  if (gModeOk[1] != m.hasDry) { gModeOk[1] = m.hasDry; changed = true; }
  if (gModeOk[2] != m.hasFan) { gModeOk[2] = m.hasFan; changed = true; }
  if (gModeOk[3] != m.hasOff) { gModeOk[3] = m.hasOff; changed = true; }
  if (changed) refreshControl();

  // --- CAI DAT: danh sách mã đã học ---
  // Chỉ dựng lại khi TẬP MÃ thật sự đổi. update() chạy 5 lần/giây, mà việc này
  // sờ vào 18 widget và ép LVGL tính lại bố cục flex — làm mỗi nhịp là phí, và
  // tệ hơn: bố cục tính lại giữa lúc người dùng đang cuộn sẽ giật vị trí cuộn.
  if (m.coolMask != gIrLastMask || m.hasDry != gIrLastFixed[0] ||
      m.hasFan != gIrLastFixed[1] || m.hasOff != gIrLastFixed[2]) {
    gIrLastMask     = m.coolMask;
    gIrLastFixed[0] = m.hasDry;
    gIrLastFixed[1] = m.hasFan;
    gIrLastFixed[2] = m.hasOff;

    uint8_t shown = 0;
    for (uint8_t i = 0; i < IR_ROWS; i++) {
      const bool have = (i < IR_COOL_N) ? ((m.coolMask >> i) & 1u)
                        : (i == IR_COOL_N)     ? m.hasDry
                        : (i == IR_COOL_N + 1) ? m.hasFan
                                               : m.hasOff;
      if (have) {
        irRowLabel(i, b, sizeof b);
        setText(gIrRowLbl[i], b);
        shown++;
      }
      if (gIrShown[i] != have) {
        gIrShown[i] = have;
        if (have) lv_obj_clear_flag(gIrRow[i], LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(gIrRow[i], LV_OBJ_FLAG_HIDDEN);
      }
    }

    if (shown) lv_obj_add_flag(gIrEmpty, LV_OBJ_FLAG_HIDDEN);
    else       lv_obj_clear_flag(gIrEmpty, LV_OBJ_FLAG_HIDDEN);

    snprintf(b, sizeof b, "%u", (unsigned)shown);
    setText(gIrCountLbl, b);
    snprintf(b, sizeof b, "MÃ IR ĐÃ HỌC · %u", (unsigned)shown);
    setText(gIrTitle, b);
  }

  // --- NHẬT KÝ: làm mới mốc tương đối ---
  // Chỉ khi SANG PHÚT MỚI. Các dòng "3 phút trước" phải già đi theo thời gian,
  // nhưng vẽ lại 5 lần/giây để đổi một con số mỗi 60 giây thì 299 lần kia là phí
  // — và mỗi lần đều làm bẩn vùng vẽ. Đồng hồ hợp lệ thì mốc là giờ tuyệt đối,
  // không bao giờ đổi, nên bỏ qua hẳn.
  if (!gLog[0].used || gLog[0].hasClock) {
    // không có gì già đi
  } else if (m.uptimeSec / 60 != gLogLastMin) {
    gLogLastMin = m.uptimeSec / 60;
    renderLog(m.uptimeSec);
  }

  // --- THONG TIN ---
  setText(gInfoVal[0], m.ssid[0] ? m.ssid : "--");
  setText(gInfoVal[1], m.ip[0] ? m.ip : "--");
  if (m.wifiUp) snprintf(b, sizeof b, "%d dBm", m.rssi); else snprintf(b, sizeof b, "--");
  setText(gInfoVal[2], b);
  setText(gInfoVal[3], m.mqttUp ? "KẾT NỐI" : "MẤT KẾT NỐI");
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

void setClock(bool valid, uint8_t hh, uint8_t mm, uint8_t ss) {
  char b[12];
  if (valid) snprintf(b, sizeof b, "%02u:%02u:%02u", (unsigned)hh, (unsigned)mm, (unsigned)ss);
  else       snprintf(b, sizeof b, "--:--:--");
  setText(gLblClock, b);
}

void setBrightness(uint8_t percent) {
  char b[8];
  snprintf(b, sizeof b, "%u%%", (unsigned)percent);
  setText(gBrightLbl, b);
}

void addLog(const Ui::CmdLog &e, bool clockValid, uint8_t hh, uint8_t mm, uint32_t nowSec) {
  // Đẩy xuống một chỗ, chèn vào đầu: mới nhất phải nằm trên. Dòng thứ 9 rơi ra
  // khỏi đáy — cố ý, đây là cửa sổ chẩn đoán tại chỗ chứ không phải sổ lưu trữ.
  for (uint8_t i = LOG_ROWS - 1; i > 0; i--) gLog[i] = gLog[i - 1];

  LogEntry &n = gLog[0];
  n.used     = true;
  n.hasClock = clockValid;
  n.hh       = hh;
  n.mm       = mm;
  n.atSec    = nowSec;
  n.result   = e.result;
  snprintf(n.reason, sizeof n.reason, "%s", e.reason[0] ? e.reason : "—");

  // Nhãn người đọc, khớp màn ĐIỀU KHIỂN. Backend gửi mã máy ("COOL"), nhưng
  // setpoint chỉ có nghĩa với COOL — DRY/FAN/OFF là mã cố định nên in kèm nhiệt
  // độ ở đó là bịa ra một thông tin máy lạnh không hề nhận.
  const char *vn = e.mode;
  for (uint8_t k = 0; k < 4; k++) {
    if (strcmp(e.mode, kModeName[k]) == 0) { vn = kModeLabel[k]; break; }
  }
  if (strcmp(e.mode, "COOL") == 0 && e.setpoint >= 0) {
    snprintf(n.what, sizeof n.what, "%s %d°C", vn, e.setpoint);
  } else {
    snprintf(n.what, sizeof n.what, "%s", vn);
  }

  gLogLastMin = nowSec / 60;
  renderLog(nowSec);
}

void setBuzzer(bool on) {
  buttonSelect(gBuzzOn, on);
  buttonSelect(gBuzzOff, !on);
}

} // namespace Screens
