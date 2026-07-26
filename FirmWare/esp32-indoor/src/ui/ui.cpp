#include "ui.h"
#include "theme.h"
#include "touch.h"
#include "board-io.h"
#include "../config.h"

using namespace Theme;

namespace Ui {

static TFT_eSPI tft;

// ===========================================================================
//  Ranh giới giữa hai tác vụ
// ---------------------------------------------------------------------------
//  Tất cả những gì đi qua ranh giới lõi 1 <-> lõi 0 nằm gọn trong khối này.
//  Ngoài ba thứ dưới đây, hai tác vụ không đụng chung một biến nào.
// ===========================================================================
static SemaphoreHandle_t modelMx = nullptr;   // bảo vệ `shared`
static Model             shared;              // loop() ghi, tác vụ UI đọc
static QueueHandle_t     cmdQ    = nullptr;   // UI -> loop(): người dùng vừa bấm
static QueueHandle_t     replyQ  = nullptr;   // loop() -> UI: kết quả để hiện toast
static TaskHandle_t      uiTask  = nullptr;

struct Reply { char msg[40]; };

// Lõi 0. Arduino chạy loop() ở lõi 1 (ARDUINO_RUNNING_CORE), và IR bit-bang
// 38kHz trong loop() không chịu nổi việc bị xen giữa — xem ui.h §2.
static const BaseType_t UI_CORE     = 0;
static const uint32_t   UI_STACK    = 8192;   // TFT_eSPI + snprintf, đo còn dư ~2.5KB
static const UBaseType_t UI_PRIO    = 2;      // trên loopTask (1), dưới ngăn xếp WiFi (18+)
static const TickType_t UI_PERIOD   = pdMS_TO_TICKS(15);   // ~66Hz quét chạm

void publish(const Model &m) {
  if (!modelMx) return;
  // Chờ 0 tick: nếu tác vụ UI đang giữ khoá để chép ra thì bỏ qua vòng này.
  // loop() KHÔNG được đứng chờ giao diện — đúng lý do đã tách tác vụ ngay từ
  // đầu. Bỏ một ảnh chụp không mất gì: vòng sau (vài ms) lại có cái mới.
  if (xSemaphoreTake(modelMx, 0) != pdTRUE) return;
  shared = m;
  xSemaphoreGive(modelMx);
}

bool pollCommand(Command &out) {
  return cmdQ && xQueueReceive(cmdQ, &out, 0) == pdTRUE;
}

// Số đo SHT3x: tác vụ UI ghi (nó sở hữu bus I2C), loop() đọc để gửi telemetry.
static portMUX_TYPE sensorMux = portMUX_INITIALIZER_UNLOCKED;
static float sensorT = NAN, sensorH = NAN;

bool readIndoor(float &tempC, float &humidity) {
  // Spinlock chứ không mutex: chỉ chép hai float, ngắn hơn cả chi phí đổi tác
  // vụ. Mutex ở đây sẽ khiến loop() có thể bị chặn — đúng thứ đang muốn tránh.
  portENTER_CRITICAL(&sensorMux);
  const float t = sensorT, h = sensorH;
  portEXIT_CRITICAL(&sensorMux);
  if (isnan(t) || isnan(h)) return false;
  tempC = t; humidity = h;
  return true;
}

void reply(const char *msg) {
  if (!replyQ) return;
  Reply r;
  strncpy(r.msg, msg ? msg : "DA GUI LENH", sizeof(r.msg) - 1);
  r.msg[sizeof(r.msg) - 1] = '\0';
  xQueueSend(replyQ, &r, 0);
}

// ===========================================================================
//  Từ đây trở xuống: CHỈ tác vụ UI chạm vào. Không cần khoá.
// ===========================================================================
enum Screen : uint8_t { S_HOME = 0, S_CONTROL, S_INFO, S_SETTINGS };
static const char *NAV_LABEL[4] = {"TRANG CHU", "DIEU KHIEN", "THONG TIN", "CAI DAT"};

static const char *MODE_WIRE[4]  = {"COOL", "DRY", "FAN", "OFF"};
static const char *MODE_LABEL[4] = {"LANH", "KHO", "QUAT", "TAT"};

static Screen screen = S_HOME;
static bool   needStatic = true;

// Bản nháp trên màn ĐIỀU KHIỂN: gom thay đổi rồi mới gửi khi bấm GUI (giống nút
// submit của OverridePanel bên app). Bắn IR theo từng lần chạm "+" thì đi từ 26
// lên 30 là năm lệnh, máy lạnh bíp năm lần và bốn lệnh đầu vô nghĩa.
static int     draftSetpoint = 25;
static uint8_t draftMode     = 0;

static char     toastMsg[40] = "";
static uint32_t toastUntil = 0;
static bool     learnShown = false;

// Tự hạ sáng khi không ai đụng tới: bảng treo tường sáng trắng cả đêm vừa chói
// vừa làm đèn nền già nhanh. Chạm bất kỳ đâu là sáng lại, và cú chạm đánh thức
// KHÔNG được tính là bấm nút — người dùng chỉ định đánh thức màn.
static const uint32_t DIM_AFTER_MS = 60000;
static const uint8_t  DIM_LEVEL    = 15;
static uint8_t  brightFull  = 70;
static bool     dimmed      = false;
static uint32_t lastActivity = 0;

// Đồng bộ giờ NTP chạy từng bước qua nhiều vòng, KHÔNG chặn 8 giây trong tác vụ
// UI: đứng hình 8s ngay sau khi người dùng bấm nút là dấu hiệu kinh điển của
// "máy treo", họ sẽ bấm loạn hoặc rút điện.
static uint32_t ntpDeadline = 0;

struct Drawn {
  float    tIn = -999, hIn = -999, tOut = -999, hOut = -999;
  bool     outOnline = false;
  char     mode[8] = "\x01";
  int      setpoint = -999;
  bool     overrideLocal = false;
  uint32_t lastCmdSec = 0xFFFFFFFF, outAgeSec = 0xFFFFFFFF, uptimeSec = 0xFFFFFFFF;
  bool     wifiUp = false, mqttUp = false;
  bool     statusDrawn = false;   // ba chấm trạng thái mặc định cũng là false,
                                  // nên cần cờ riêng để lần vẽ ĐẦU luôn xảy ra
  int      rssi = -999;
  uint32_t learnRemainSec = 0xFFFFFFFF;
  uint16_t coolMask = 0xFFFF;
  int      draftSetpoint = -999;
  uint8_t  draftMode = 255;
  uint8_t  backlight = 255;
  bool     buzzer = true;
  uint8_t  clockHH = 255, clockMM = 255;
};
static Drawn drawn;

static uint32_t lastRepaint = 0;
static const uint32_t REPAINT_MS = 200;   // mắt không phân biệt nhanh hơn mức này

// ===========================================================================
//  Tiện ích vẽ
// ===========================================================================

/// Ký hiệu độ C: font GFX chỉ có ASCII 0x20..0x7E nên không có ký tự "°".
/// Vẽ tay một vòng tròn nhỏ rẻ hơn nhiều so với nhúng cả một font Unicode.
static void degreeC(int16_t x, int16_t y, uint16_t color, uint16_t bg) {
  tft.fillRect(x, y, 14, 12, bg);
  tft.drawCircle(x + 2, y + 3, 2, color);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(color, bg);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("C", x + 7, y + 2);
}

/// "28.4" hoặc "--" khi thiếu số. KHÔNG bao giờ trả "0.0": bịa số đo là kiểu
/// sai tệ nhất ở đây (xem ghi chú trong models/comfort_preview.dart).
static void fmt1(float v, char *out, size_t n) {
  if (isnan(v)) snprintf(out, n, "--");
  else          snprintf(out, n, "%.1f", v);
}

static void fmtInt(float v, char *out, size_t n, const char *suffix) {
  if (isnan(v)) snprintf(out, n, "--");
  else          snprintf(out, n, "%d%s", (int)lroundf(v), suffix);
}

static void text(const char *s, const GFXfont *font, int16_t x, int16_t y,
                 uint8_t datum, uint16_t fg, uint16_t bg, uint16_t pad) {
  if (font) tft.setFreeFont(font);
  else      { tft.setTextFont(1); tft.setTextSize(1); }
  tft.setTextDatum(datum);
  tft.setTextColor(fg, bg);
  // setTextPadding xoá chữ cũ NGAY TRONG lượt vẽ chữ mới. Đây là lý do bản này
  // không cần sprite đệm 23KB: không có khoảnh khắc nào ô trống trên màn nên
  // mắt không thấy chớp, mà RAM tốn 0 byte — WROOM-32E không có PSRAM.
  tft.setTextPadding(pad);
  tft.drawString(s, x, y);
  tft.setTextPadding(0);
  tft.setTextDatum(TL_DATUM);
}

static void dot(int16_t x, int16_t y, uint16_t color) {
  tft.fillCircle(x, y, 4, color);
}

// ===========================================================================
//  Thanh trạng thái + thanh điều hướng
// ===========================================================================
static void drawStatusStatic() {
  tft.fillRect(0, 0, SCREEN_W, STATUS_H, carbon);
  tft.drawFastHLine(0, STATUS_H - 1, SCREEN_W, carbonLine);
  tft.fillRect(6, 7, 8, 8, ice);
  text("AIRCON", nullptr, 20, 8, TL_DATUM, white, carbon, 0);
}

static void drawStatusDynamic(const Model &m) {
  if (!drawn.statusDrawn || m.outOnline != drawn.outOnline ||
      m.mqttUp != drawn.mqttUp || m.wifiUp != drawn.wifiUp) {
    dot(214, 11, m.outOnline ? success : carbonLineHi);   // nghe được node ngoài trời
    dot(232, 11, m.mqttUp    ? success : error);
    dot(250, 11, m.wifiUp    ? success : error);
    drawn.statusDrawn = true;
  }

  BoardIo::Clock c;
  const bool ok = BoardIo::clockRead(c);
  const uint8_t hh = ok ? c.hh : 255, mm = ok ? c.mm : 255;
  if (hh != drawn.clockHH || mm != drawn.clockMM) {
    char buf[8];
    if (ok) snprintf(buf, sizeof(buf), "%02u:%02u", hh, mm);
    else    snprintf(buf, sizeof(buf), "--:--");   // chưa đặt giờ -> nói thật
    text(buf, nullptr, 314, 8, TR_DATUM, white, carbon, 40);
    drawn.clockHH = hh; drawn.clockMM = mm;
  }
}

static void drawNav() {
  for (uint8_t i = 0; i < 4; i++) {
    const bool on = (screen == i);
    tft.fillRect(NAV_W * i, NAV_Y, NAV_W, NAV_H, on ? ice : carbon);
    tft.drawFastVLine(NAV_W * i, NAV_Y, NAV_H, carbonLine);
    text(NAV_LABEL[i], nullptr, NAV_W * i + NAV_W / 2, NAV_Y + NAV_H / 2,
         MC_DATUM, on ? white : whiteDim, on ? ice : carbon, NAV_W - 4);
  }
  tft.drawFastHLine(0, NAV_Y - 1, SCREEN_W, carbonLine);
}

static void clearContent() {
  tft.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, carbon);
}

// ===========================================================================
//  TRANG CHU
// ===========================================================================
static void homeStatic() {
  clearContent();
  panel(tft, R_CARD_IN);
  panel(tft, R_CARD_OUT);
  panel(tft, R_CARD_AC);
  text("TRONG NHA",  nullptr, R_CARD_IN.x  + 10, R_CARD_IN.y  + 8, TL_DATUM, whiteDim, carbonPanel, 0);
  text("NGOAI TROI", nullptr, R_CARD_OUT.x + 10, R_CARD_OUT.y + 8, TL_DATUM, whiteDim, carbonPanel, 0);
  text("DO AM", nullptr, R_CARD_IN.x  + 10, R_CARD_IN.y  + 78, TL_DATUM, whiteDim, carbonPanel, 0);
  text("DO AM", nullptr, R_CARD_OUT.x + 10, R_CARD_OUT.y + 78, TL_DATUM, whiteDim, carbonPanel, 0);
}

static void homeCard(const Rect &r, float t, float h) {
  char buf[12];
  fmt1(t, buf, sizeof(buf));
  // Màu theo GIÁ TRỊ chứ không theo "trong nhà / ngoài trời" — cùng luật với
  // thang thermal* của app: màu nói Ý NGHĨA của số đo, không nói nguồn số đo.
  text(buf, &FreeSansBold24pt7b, r.x + 66, r.y + 44, MC_DATUM,
       thermalColor(t), carbonPanel, 108);
  degreeC(r.x + 126, r.y + 30, whiteDim, carbonPanel);

  fmtInt(h, buf, sizeof(buf), " %");
  text(buf, &FreeSans9pt7b, r.x + r.w - 10, r.y + 84, BR_DATUM, white, carbonPanel, 70);
}

static void homeDynamic(const Model &m) {
  if (m.tIn != drawn.tIn || m.hIn != drawn.hIn) homeCard(R_CARD_IN, m.tIn, m.hIn);

  if (m.tOut != drawn.tOut || m.hOut != drawn.hOut || m.outOnline != drawn.outOnline) {
    // Mất nhịp tim ESP-NOW -> hiện "--", KHÔNG giữ số cũ đóng băng. Số cũ trông
    // y hệt số mới nên người dùng không có cách nào biết nó đã chết.
    homeCard(R_CARD_OUT, m.outOnline ? m.tOut : NAN, m.outOnline ? m.hOut : NAN);
    dot(R_CARD_OUT.x + R_CARD_OUT.w - 14, R_CARD_OUT.y + 12,
        m.outOnline ? success : carbonLineHi);
  }

  if (strcmp(m.mode, drawn.mode) != 0 || m.setpoint != drawn.setpoint ||
      m.overrideLocal != drawn.overrideLocal || m.lastCmdSec != drawn.lastCmdSec) {
    const char *label = "CHUA CO LENH";
    for (uint8_t i = 0; i < 4; i++) if (strcmp(m.mode, MODE_WIRE[i]) == 0) label = MODE_LABEL[i];
    text(label, &FreeSansBold12pt7b, R_CARD_AC.x + 12, R_CARD_AC.y + 26,
         TL_DATUM, white, carbonPanel, 140);

    char buf[8];
    if (m.setpoint >= 0) snprintf(buf, sizeof(buf), "%d", m.setpoint);
    else                 snprintf(buf, sizeof(buf), "--");
    text(buf, &FreeSansBold24pt7b, 204, R_CARD_AC.y + 26, MC_DATUM, white, carbonPanel, 80);

    tft.fillRect(248, R_CARD_AC.y + 12, 58, 18, carbonPanel);
    badge(tft, 248, R_CARD_AC.y + 12,
          m.overrideLocal ? "GHI DE" : "TU DONG",
          m.overrideLocal ? warning : ice);

    char line[48];
    if (m.overrideLocal) {
      // Nói thẳng khoảng trống ở backend thay vì để người dùng tự phát hiện:
      // ghi đè từ màn hình chỉ là cục bộ, comfort_engine vẫn tự quyết chu kỳ
      // sau (../../Interface/README.md §8.3).
      snprintf(line, sizeof(line), "may chu se gianh lai quyen o chu ky sau");
    } else if (m.lastCmdSec == 0) {
      snprintf(line, sizeof(line), "chua nhan lenh nao tu may chu");
    } else if (m.lastCmdSec < 60) {
      snprintf(line, sizeof(line), "lenh cuoi %lus truoc", (unsigned long)m.lastCmdSec);
    } else {
      snprintf(line, sizeof(line), "lenh cuoi %lu phut truoc", (unsigned long)(m.lastCmdSec / 60));
    }
    text(line, nullptr, R_CARD_AC.x + 12, R_CARD_AC.y + 58, TL_DATUM, whiteDim, carbonPanel, 250);
  }
}

// ===========================================================================
//  DIEU KHIEN
// ===========================================================================
/// Tổ hợp này có mã IR chưa? Đọc từ bitmask trong Model — loop() (chủ sở hữu
/// NVS) đã tính sẵn, tác vụ UI không được tự hỏi IrStore.
static bool modeEnabled(uint8_t i, int setpoint, const Model &m) {
  switch (i) {
    case 0: return setpoint >= 16 && setpoint <= 30 && (m.coolMask & (1u << (setpoint - 16)));
    case 1: return m.hasDry;
    case 2: return m.hasFan;
    default: return m.hasOff;
  }
}

static void controlStatic() {
  clearContent();
  chamferRect(tft, R_MINUS,  carbonPanel, carbonLineHi);
  chamferRect(tft, R_PLUS,   carbonPanel, carbonLineHi);
  panel(tft, R_SETBOX);
  text("-", &FreeSansBold24pt7b, R_MINUS.x + R_MINUS.w / 2, R_MINUS.y + R_MINUS.h / 2,
       MC_DATUM, white, carbonPanel, 0);
  text("+", &FreeSansBold24pt7b, R_PLUS.x + R_PLUS.w / 2, R_PLUS.y + R_PLUS.h / 2,
       MC_DATUM, white, carbonPanel, 0);
  button(tft, R_SEND, "GUI", false);
  button(tft, R_AUTO, "TU DONG", false);
}

static void controlDynamic(const Model &m) {
  if (draftSetpoint != drawn.draftSetpoint) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", draftSetpoint);
    text(buf, &FreeSansBold24pt7b, R_SETBOX.x + R_SETBOX.w / 2 - 10,
         R_SETBOX.y + R_SETBOX.h / 2, MC_DATUM, white, carbonPanel, 100);
    degreeC(R_SETBOX.x + R_SETBOX.w - 34, R_SETBOX.y + 26, whiteDim, carbonPanel);
  }
  if (draftMode != drawn.draftMode || draftSetpoint != drawn.draftSetpoint ||
      m.coolMask != drawn.coolMask) {
    for (uint8_t i = 0; i < 4; i++) {
      button(tft, modeRect(i), MODE_LABEL[i], draftMode == i, modeEnabled(i, draftSetpoint, m));
    }
  }
}

// ===========================================================================
//  THONG TIN
// ===========================================================================
static const char *INFO_LABEL[8] = {"WIFI", "IP", "SONG", "MQTT",
                                    "ESP-NOW", "NGOAI TROI", "MA IR", "FW"};
static const int16_t INFO_Y0 = 30, INFO_STEP = 21;

static void infoStatic() {
  clearContent();
  for (uint8_t i = 0; i < 8; i++) {
    text(INFO_LABEL[i], &FreeSans9pt7b, 14, INFO_Y0 + INFO_STEP * i,
         TL_DATUM, whiteDim, carbon, 0);
  }
}

static void infoDynamic(const Model &m) {
  char v[40];
  auto row = [&](uint8_t i, const char *s, uint16_t color) {
    text(s, &FreeSans9pt7b, 306, INFO_Y0 + INFO_STEP * i, TR_DATUM, color, carbon, 200);
  };

  row(0, m.wifiUp ? (m.ssid[0] ? m.ssid : "OK") : "MAT KET NOI", m.wifiUp ? white : error);
  row(1, m.ip[0] ? m.ip : "--", white);
  snprintf(v, sizeof(v), "%d dBm", m.rssi);
  row(2, m.wifiUp ? v : "--", m.rssi > -70 ? success : warning);
  row(3, m.mqttUp ? "DA NOI" : "MAT KET NOI", m.mqttUp ? success : error);
  snprintf(v, sizeof(v), "nhan %lu / bo %lu",
           (unsigned long)m.espnowRx, (unsigned long)m.espnowDrop);
  row(4, v, white);
  if (m.espnowRx == 0)   snprintf(v, sizeof(v), "chua nghe thay");
  else if (!m.outOnline) snprintf(v, sizeof(v), "MAT (%lus)", (unsigned long)m.outAgeSec);
  else                   snprintf(v, sizeof(v), "%lus truoc", (unsigned long)m.outAgeSec);
  row(5, v, m.outOnline ? white : warning);
  snprintf(v, sizeof(v), "%u ma", m.irCodeCount);
  row(6, v, m.irCodeCount ? white : warning);
  snprintf(v, sizeof(v), "%s / %lum", m.fw, (unsigned long)(m.uptimeSec / 60));
  row(7, v, white);

  snprintf(v, sizeof(v), "MAC %s  ·  KENH %u", m.mac, m.channel);
  text(v, nullptr, 14, 192, TL_DATUM, whiteDim, carbon, 292);
}

// ===========================================================================
//  CAI DAT
// ===========================================================================
static const Rect R_BL_MINUS = {200,  28, 50, 40};
static const Rect R_BL_PLUS  = {258,  28, 50, 40};
static const Rect R_BUZZ     = {236,  74, 72, 40};
static const Rect R_NTP      = {216, 120, 92, 40};
static const Rect R_REBOOT   = {216, 166, 92, 40};

static void settingsStatic() {
  clearContent();
  const char *labels[4] = {"DO SANG", "AM BAO", "DONG BO GIO", "KHOI DONG LAI"};
  for (uint8_t i = 0; i < 4; i++) {
    const Rect r = settingRow(i);
    panel(tft, r);
    text(labels[i], &FreeSans9pt7b, r.x + 12, r.y + r.h / 2, ML_DATUM, white, carbonPanel, 0);
  }
  chamferRect(tft, R_BL_MINUS, carbonUp, carbonLineHi, 4);
  chamferRect(tft, R_BL_PLUS,  carbonUp, carbonLineHi, 4);
  text("-", &FreeSansBold12pt7b, R_BL_MINUS.x + 25, R_BL_MINUS.y + 21, MC_DATUM, white, carbonUp, 0);
  text("+", &FreeSansBold12pt7b, R_BL_PLUS.x  + 25, R_BL_PLUS.y  + 21, MC_DATUM, white, carbonUp, 0);
  button(tft, R_NTP,    "CHAY", false);
  button(tft, R_REBOOT, "CHAY", false);
}

static void settingsDynamic() {
  if (brightFull != drawn.backlight) {
    char v[8];
    snprintf(v, sizeof(v), "%u%%", brightFull);
    text(v, &FreeSans9pt7b, 175, settingRow(0).y + 20, MR_DATUM, iceText, carbonPanel, 60);
  }
  if (BoardIo::buzzerEnabled() != drawn.buzzer) {
    button(tft, R_BUZZ, BoardIo::buzzerEnabled() ? "BAT" : "TAT", BoardIo::buzzerEnabled());
  }
}

// ===========================================================================
//  Lớp phủ: HỌC REMOTE + toast
// ===========================================================================
static void learnStatic() {
  clearContent();
  chamferRect(tft, R_LEARN, carbonPanel, ice, 10);
  text("DANG HOC REMOTE", &FreeSansBold12pt7b, SCREEN_W / 2, R_LEARN.y + 24,
       MC_DATUM, iceText, carbonPanel, 0);
  text("Huong remote vao mat thu, bam nut", nullptr, SCREEN_W / 2, R_LEARN.y + 106,
       MC_DATUM, whiteDim, carbonPanel, 0);
}

static void learnDynamic(const Model &m) {
  text(m.learnLabel[0] ? m.learnLabel : "?", &FreeSansBold24pt7b, SCREEN_W / 2,
       R_LEARN.y + 66, MC_DATUM, white, carbonPanel, 250);

  const int16_t bx = R_LEARN.x + 24, by = R_LEARN.y + 126, bw = R_LEARN.w - 96;
  const uint32_t total = LEARN_TIMEOUT_MS / 1000;
  const uint32_t left  = m.learnRemainSec > total ? total : m.learnRemainSec;
  const int16_t  fill  = (int16_t)((uint32_t)(bw - 2) * left / total);
  tft.drawRect(bx, by, bw, 14, carbonLineHi);
  tft.fillRect(bx + 1, by + 1, fill, 12, ice);
  tft.fillRect(bx + 1 + fill, by + 1, bw - 2 - fill, 12, carbonPanel);

  char v[8];
  snprintf(v, sizeof(v), "%lus", (unsigned long)left);
  text(v, &FreeSans9pt7b, R_LEARN.x + R_LEARN.w - 16, by + 7, MR_DATUM, white, carbonPanel, 50);
}

static void showToast(const char *msg) {
  strncpy(toastMsg, msg, sizeof(toastMsg) - 1);
  toastMsg[sizeof(toastMsg) - 1] = '\0';
  toastUntil = millis() + 1800;

  const Rect r = {40, 88, 240, 64};
  chamferRect(tft, r, carbonUp, warning, 8);
  text(toastMsg, &FreeSans9pt7b, SCREEN_W / 2, 120, MC_DATUM, white, carbonUp, 220);
}

// ===========================================================================
//  Chạm
// ===========================================================================
static bool     lastDown = false;
static uint32_t lastTapMs = 0;

/// Chỉ nhận CẠNH XUỐNG + khoá 250ms. Màn điện dung trả về chuỗi toạ độ liên tục
/// khi ngón còn đặt trên mặt kính; không chặn thì một lần chạm "+" nhảy 5 độ,
/// tức là gửi sai lệnh cho máy lạnh.
static bool tapped(int16_t &x, int16_t &y) {
  int16_t tx, ty;
  const bool down = Touch::read(tx, ty);
  const bool edge = down && !lastDown && (millis() - lastTapMs > 250);
  lastDown = down;
  if (!edge) return false;
  lastTapMs = millis();
  x = tx; y = ty;
  return true;
}

/// Nháy nút vừa bấm rồi vẽ lại như cũ. Chỉ làm được vì đây là tác vụ riêng:
/// 90ms vTaskDelay ở đây không đụng gì tới MQTT/IR đang chạy bên lõi 1.
static void pressFlash(const Rect &r, const char *label) {
  button(tft, r, label, true);
  vTaskDelay(pdMS_TO_TICKS(90));
  button(tft, r, label, false);
}

static void handleTap(int16_t x, int16_t y, const Model &m) {
  // Đang học thì màn hình là của server, không phải của người dùng: chạm vào
  // đâu cũng không đổi màn, nếu không người dùng vừa bấm nút remote xong thì
  // không thấy kết quả ở đâu cả.
  if (m.learning) return;

  if (y >= NAV_Y) {
    const Screen s = (Screen)(x / NAV_W);
    if (s != screen) { screen = s; needStatic = true; }
    return;
  }

  switch (screen) {
    case S_HOME:
      if (R_CARD_AC.contains(x, y)) { screen = S_CONTROL; needStatic = true; }
      break;

    case S_CONTROL:
      if (R_MINUS.contains(x, y) && draftSetpoint > 16) draftSetpoint--;
      else if (R_PLUS.contains(x, y) && draftSetpoint < 30) draftSetpoint++;
      else if (R_SEND.contains(x, y)) {
        pressFlash(R_SEND, "GUI");
        if (!modeEnabled(draftMode, draftSetpoint, m)) {
          showToast("CHUA HOC MA - vao app de hoc");
        } else {
          // KHÔNG bắn IR ở đây: IR phải chạy trên lõi 1 cùng loop(), nếu không
          // sóng mang 38kHz bị xen giữa và khung phát ra sai (xem ui.h §2).
          Command c{Command::MANUAL, {0}, draftSetpoint};
          strncpy(c.mode, MODE_WIRE[draftMode], sizeof(c.mode) - 1);
          if (xQueueSend(cmdQ, &c, 0) == pdTRUE) showToast("DANG GUI...");
          else                                   showToast("BAN, THU LAI");
        }
      } else if (R_AUTO.contains(x, y)) {
        pressFlash(R_AUTO, "TU DONG");
        Command c{Command::AUTO, {0}, -1};
        xQueueSend(cmdQ, &c, 0);
        showToast("DA TRA VE TU DONG");
      } else {
        for (uint8_t i = 0; i < 4; i++) {
          if (modeRect(i).contains(x, y)) {
            if (modeEnabled(i, draftSetpoint, m)) draftMode = i;
            else showToast("CHUA HOC MA - vao app de hoc");
            break;
          }
        }
      }
      break;

    case S_INFO:
      break;

    case S_SETTINGS:
      if (R_BL_MINUS.contains(x, y)) {
        brightFull = brightFull > 20 ? brightFull - 10 : 10;
        BoardIo::backlightSet(brightFull);
      } else if (R_BL_PLUS.contains(x, y)) {
        brightFull = brightFull < 100 ? brightFull + 10 : 100;
        BoardIo::backlightSet(brightFull);
      } else if (R_BUZZ.contains(x, y)) {
        BoardIo::buzzerEnable(!BoardIo::buzzerEnabled());
      } else if (R_NTP.contains(x, y)) {
        pressFlash(R_NTP, "CHAY");
        BoardIo::ntpBegin();
        ntpDeadline = millis() + 10000;
        showToast("DANG LAY GIO...");
      } else if (R_REBOOT.contains(x, y)) {
        pressFlash(R_REBOOT, "CHAY");
        showToast("DANG KHOI DONG LAI");
        vTaskDelay(pdMS_TO_TICKS(600));
        ESP.restart();
      }
      break;
  }
}

// ===========================================================================
//  Tác vụ UI
// ===========================================================================
static void redrawStatic(const Model &m) {
  needStatic = false;
  drawn = Drawn();
  if (learnShown) learnStatic();
  else switch (screen) {
    case S_HOME:     homeStatic();     break;
    case S_CONTROL:  controlStatic();  break;
    case S_INFO:     infoStatic();     break;
    case S_SETTINGS: settingsStatic(); break;
  }
  drawNav();
  (void)m;
}

static void uiLoop(void *) {
  Model m;
  uint32_t lastSample = 0;
  uint8_t  sampleFails = 0;
  lastActivity = millis();

  for (;;) {
    // --- 1. Lấy ảnh chụp mới nhất từ loop() -------------------------------
    if (xSemaphoreTake(modelMx, pdMS_TO_TICKS(5)) == pdTRUE) {
      m = shared;
      xSemaphoreGive(modelMx);
    }
    // Lấy không được thì vẽ tiếp bằng ảnh chụp cũ: thà số trễ 15ms còn hơn
    // giao diện khựng — và loop() cũng không bao giờ phải chờ giao diện.

    BoardIo::buzzerTick();

    // --- 1b. Đo SHT3x mỗi 2s ----------------------------------------------
    // Nhịp 2s là để giao diện phản ánh phòng gần như tức thì; telemetry lên
    // cloud vẫn giữ nhịp TELEMETRY_MS riêng của loop().
    if (millis() - lastSample >= 2000) {
      lastSample = millis();
      float t, h;
      if (BoardIo::sht3xRead(t, h)) {
        sampleFails = 0;
        portENTER_CRITICAL(&sensorMux);
        sensorT = t; sensorH = h;
        portEXIT_CRITICAL(&sensorMux);
      } else if (++sampleFails >= 3) {
        // Ba lần liên tiếp hỏng = hỏng thật (tuột dây/mất nguồn), không phải
        // nhiễu một gói. Xoá số cũ để màn hiện "--" thay vì giữ số đóng băng —
        // số cũ trông y hệt số mới nên người dùng không cách nào biết nó chết.
        portENTER_CRITICAL(&sensorMux);
        sensorT = NAN; sensorH = NAN;
        portEXIT_CRITICAL(&sensorMux);
      }
    }

    // --- 2. Kết quả loop() trả về -> toast --------------------------------
    Reply r;
    while (xQueueReceive(replyQ, &r, 0) == pdTRUE) showToast(r.msg);

    // --- 3. Đồng bộ giờ NTP, từng bước, không chặn ------------------------
    if (ntpDeadline) {
      if (BoardIo::ntpPoll())        { ntpDeadline = 0; showToast("DA DONG BO GIO"); }
      else if (millis() > ntpDeadline) { ntpDeadline = 0; showToast("KHONG LAY DUOC GIO"); }
    }

    // --- 4. Chạm ----------------------------------------------------------
    int16_t x, y;
    if (tapped(x, y)) {
      lastActivity = millis();
      if (dimmed) {
        // Cú chạm đánh thức KHÔNG được tính là bấm nút: người dùng nhìn màn tối
        // thì chạm để xem, không phải để đổi nhiệt độ.
        dimmed = false;
        BoardIo::backlightSet(brightFull);
      } else {
        BoardIo::beep();
        handleTap(x, y, m);
      }
    } else if (!dimmed && millis() - lastActivity > DIM_AFTER_MS) {
      dimmed = true;
      BoardIo::backlightSet(DIM_LEVEL);
    }

    // --- 5. Lớp phủ đến/đi -> phải vẽ lại phần bị che ----------------------
    if (m.learning != learnShown) { learnShown = m.learning; needStatic = true; }
    if (toastUntil && millis() > toastUntil) { toastUntil = 0; needStatic = true; }

    if (needStatic) redrawStatic(m);

    // --- 6. Vẽ phần đổi ---------------------------------------------------
    if (millis() - lastRepaint >= REPAINT_MS && toastUntil == 0) {
      lastRepaint = millis();
      drawStatusDynamic(m);

      if (learnShown) {
        if (m.learnRemainSec != drawn.learnRemainSec) learnDynamic(m);
      } else switch (screen) {
        case S_HOME:     homeDynamic(m);    break;
        case S_CONTROL:  controlDynamic(m); break;
        case S_INFO:     infoDynamic(m);    break;
        case S_SETTINGS: settingsDynamic(); break;
      }

      // Server vừa gửi lệnh mới -> kéo bản nháp theo, để mở màn ĐIỀU KHIỂN ra
      // là thấy đúng cái máy đang chạy, không phải số cũ đặt từ hôm qua.
      if (!m.overrideLocal && m.setpoint >= 0 && m.setpoint != drawn.setpoint) {
        draftSetpoint = m.setpoint;
        for (uint8_t i = 0; i < 4; i++) if (strcmp(m.mode, MODE_WIRE[i]) == 0) draftMode = i;
      }

      drawn.tIn = m.tIn; drawn.hIn = m.hIn;
      drawn.tOut = m.tOut; drawn.hOut = m.hOut; drawn.outOnline = m.outOnline;
      strncpy(drawn.mode, m.mode, sizeof(drawn.mode) - 1);
      drawn.mode[sizeof(drawn.mode) - 1] = '\0';
      drawn.setpoint = m.setpoint;
      drawn.overrideLocal = m.overrideLocal;
      drawn.lastCmdSec = m.lastCmdSec;
      drawn.outAgeSec = m.outAgeSec;
      drawn.uptimeSec = m.uptimeSec;
      drawn.wifiUp = m.wifiUp; drawn.mqttUp = m.mqttUp; drawn.rssi = m.rssi;
      drawn.learnRemainSec = m.learnRemainSec;
      drawn.coolMask = m.coolMask;
      drawn.draftSetpoint = draftSetpoint;
      drawn.draftMode = draftMode;
      drawn.backlight = brightFull;
      drawn.buzzer = BoardIo::buzzerEnabled();
    }

    vTaskDelay(UI_PERIOD);
  }
}

bool begin() {
  // TXS0104 chỉ thông khi OE = HIGH, và hai chân IR đi qua nó (cổng P3). PHẢI
  // đặt ở đây, trong setup(), chứ không phải bằng trở kéo ngoài: GPIO12 là MTDI
  // — HIGH lúc reset thì ROM chọn mức flash 1.8V và bo không boot được nữa.
  // Xem ../../Interface/README.md §3.1.
  pinMode(EN_LEVEL_SHIFT_PIN, OUTPUT);
  digitalWrite(EN_LEVEL_SHIFT_PIN, HIGH);

  tft.init();
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(carbon);
  tft.setTextWrap(false);

  BoardIo::backlightBegin(LCD_BACKLIGHT_PIN);
  BoardIo::backlightSet(brightFull);
  BoardIo::buzzerBegin(BUZZER_PIN);

  // Touch::begin gọi Wire.begin + ghim 100kHz -> phải chạy TRƯỚC mọi thứ khác
  // trên bus (SHT3x, DS1307).
  const bool touchOk = Touch::begin(I2C_SDA_PIN, I2C_SCL_PIN, TOUCH_RST_PIN, TOUCH_INT_PIN);
  Serial.printf("LCD: ILI9341 320x240 · cam ung: %s%s\n", Touch::chipName(),
                touchOk ? "" : "  (KHONG BAM DUOC — kiem tra J1)");

  if (BoardIo::sht3xBegin()) {
    Serial.println("Cam bien: SHT3x @0x44 tren I2C");
  } else {
    Serial.println("Cam bien: KHONG THAY SHT3x @0x44 — node se khong gui duoc t_in/h_in");
  }

  drawStatusStatic();
  drawNav();

  modelMx = xSemaphoreCreateMutex();
  cmdQ    = xQueueCreate(4, sizeof(Command));
  replyQ  = xQueueCreate(4, sizeof(Reply));
  if (!modelMx || !cmdQ || !replyQ) {
    Serial.println("UI: khong cap phat duoc queue/mutex — man hinh se dung im");
    return false;
  }

  // Ghim lõi 0. KHÔNG dùng xTaskCreate (thả cho bộ lập lịch chọn lõi): nó có
  // thể xếp tác vụ UI vào lõi 1 cùng loop(), và khi đó IrIo::blast() bị xen
  // giữa -> khung IR sai. Xem ui.h §2.
  if (xTaskCreatePinnedToCore(uiLoop, "ui", UI_STACK, nullptr, UI_PRIO, &uiTask, UI_CORE) != pdPASS) {
    Serial.println("UI: khong tao duoc tac vu — het RAM?");
    return false;
  }
  Serial.printf("UI: tac vu chay tren loi %d, quet cham %ums\n",
                (int)UI_CORE, (unsigned)(UI_PERIOD * portTICK_PERIOD_MS));
  return touchOk;
}

} // namespace Ui
