#include "serial-console.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#include "diffuser-control.h"
#include "diffuser-ir.h"
#include "humidity-sensor.h"
#include "ir-remote.h"
#include "ir-slots.h"
#include "learn-session.h"
#include "pin-diagnostics.h"
#include "settings.h"

namespace SerialConsole {
namespace {

char   g_line[48];
size_t g_len = 0;

/// Chế độ xem trực tiếp — chỉ đổi NHỊP IN, không đổi hành vi điều khiển gì cả.
/// Tách rời như vậy để "ngồi nhìn" không bao giờ làm lệch thứ đang đo, và để
/// quên tắt cũng vô hại (chỉ tốn log).
bool g_watch = false;

// ---------------------------------------------------------------------------
//  `burst` — bắn lặp lại để CHỈNH HƯỚNG / ĐO TẦM
// ---------------------------------------------------------------------------
//  Một mình thì không thể vừa gõ lệnh vừa đứng cạnh máy xông mà nhìn. Lệnh này
//  bắn đều tay trong ~30 giây để rảnh chân đi tới chỗ máy, xoay LED, lùi ra xa
//  dần cho tới lúc mất tác dụng — đó mới là cách biết tầm thật.
//
//  KHÔNG CHẶN vòng lặp: đếm nhịp bằng millis() ngay trong poll(). Nếu ngồi
//  delay() 30 giây thì cảm biến ngừng đọc và bộ điều khiển ngừng chạy suốt
//  ngần ấy — mà đây là thứ dùng lúc đang lắp, đúng lúc cần nhìn số nhất.
const uint8_t  BURST_SHOTS = 15;
const uint32_t BURST_GAP_MS = 2000;

uint8_t  g_burstLeft = 0;
uint32_t g_burstNextMs = 0;

void burstStop(const char *why) {
  if (g_burstLeft == 0) return;
  g_burstLeft = 0;
  Serial.printf("[burst] dung: %s\n", why);
}

const char *yesNo(bool b) { return b ? "co" : "khong"; }

/// Câu trả lời cho "còn bao lâu" — 0 nghĩa là không áp dụng, và in "0s" ở đó
/// thì đọc như "sắp hết" chứ không phải "không liên quan".
void printCountdown(const char *label, uint32_t sec) {
  if (sec == 0) return;
  Serial.printf("  %-18s %lu phut %lu giay\n", label, (unsigned long)(sec / 60),
                (unsigned long)(sec % 60));
}

bool eq(const char *a, const char *b) { return strcmp(a, b) == 0; }

void dispatch(const char *cmd) {
  const uint32_t now = millis();

  if (cmd[0] == '\0') { printStatus(); return; }

  if (eq(cmd, "help") || eq(cmd, "?"))  { printHelp(); return; }
  if (eq(cmd, "status") || eq(cmd, "s")) { printStatus(); return; }

  if (eq(cmd, "on"))   { DiffuserControl::manualSet(true, now);  printStatus(); return; }
  if (eq(cmd, "off"))  { DiffuserControl::manualSet(false, now); printStatus(); return; }
  if (eq(cmd, "auto")) { DiffuserControl::backToAuto(now);       printStatus(); return; }

  if (eq(cmd, "learn"))     { LearnSession::start(LearnSession::suggestNext()); return; }
  if (eq(cmd, "learn on"))  { LearnSession::start(IrSlots::Slot::ON);  return; }
  if (eq(cmd, "learn off")) { LearnSession::start(IrSlots::Slot::OFF); return; }

  if (eq(cmd, "forget on")) {
    Serial.println(IrSlots::clear(IrSlots::Slot::ON) ? "da quen ma o BAT" : "o BAT von trong");
    return;
  }
  if (eq(cmd, "forget off")) {
    Serial.println(IrSlots::clear(IrSlots::Slot::OFF) ? "da quen ma o TAT" : "o TAT von trong");
    return;
  }

  // `test` bắn khung mà KHÔNG đổi niềm tin trạng thái — để ngắm LED lúc lắp.
  // Với remote bập bênh thì mỗi lần test là máy thật đảo trạng thái trong khi
  // bo vẫn nghĩ như cũ; nói thẳng ra thay vì để người dùng tự phát hiện.
  if (eq(cmd, "test on") || eq(cmd, "test off") || eq(cmd, "test")) {
    const bool on = !eq(cmd, "test off");
    Serial.printf("ban thu khung %s...\n", on ? "BAT" : "TAT");
    if (!DiffuserIr::send(on)) { Serial.println("-> that bai (chua hoc ma)"); return; }
    Serial.println("-> da ban.");
#if DIFFUSER_IR_TOGGLE
    Serial.println("   LUU Y: remote bap benh - may that vua DAO trang thai, con bo thi");
    Serial.println("   van nghi nhu cu. Go `on` hoac `off` de dong bo lai.");
#endif
    return;
  }

  if (eq(cmd, "watch")) {
    g_watch = !g_watch;
    Serial.printf("[watch] %s - in mot dong moi %s giay\n",
                  g_watch ? "BAT" : "TAT", g_watch ? "5" : "60");
    return;
  }

  if (eq(cmd, "diag"))  { PinDiagnostics::checkIrReceiver(); return; }
  if (eq(cmd, "blink")) { PinDiagnostics::blinkIrEmitter();  return; }

  if (eq(cmd, "burst")) {
    g_burstLeft = BURST_SHOTS;
    g_burstNextMs = millis();
    Serial.printf(
        "[burst] se ban %u phat, cach nhau %lus (~%lus).\n"
        "        Cam bo di toi cho may xong, xoay LED, lui xa dan de do tam that.\n"
        "        Go bat ky lenh nao de dung som.\n",
        BURST_SHOTS, (unsigned long)(BURST_GAP_MS / 1000),
        (unsigned long)(BURST_SHOTS * BURST_GAP_MS / 1000));
    return;
  }

  if (eq(cmd, "wipe")) {
    IrSlots::wipe();
    Serial.println("da xoa sach ma IR va niem tin trang thai. Hoc lai bang `learn`.");
    return;
  }

  Serial.printf("khong hieu lenh: \"%s\" - go `help`\n", cmd);
}

}  // namespace

void printHelp() {
  Serial.println(
      "\n--- LENH ---\n"
      "  status | s     bang trang thai (Enter khong go gi cung duoc)\n"
      "  on / off       bat/tat TAY (vao GHI DE, tu het han)\n"
      "  auto           bo GHI DE, tra ve TU DONG ngay\n"
      "  learn          hoc ma cho o con trong\n"
      "  learn on|off   hoc ma cho dung mot o\n"
      "  forget on|off  quen ma mot o\n"
      "  test on|off    ban thu khung, KHONG doi trang thai (de ngam LED)\n"
      "  burst          ban lap 15 phat / 2s de chinh huong va do tam\n"
      "  diag           do chan mat thu: day co toi GPIO27 khong (khoi doan)\n"
      "  blink          nhay LED phat 5s de soi bang camera dien thoai\n"
      "  watch          xem truc tiep: in mot dong moi 5 giay (go lai de tat)\n"
      "  wipe           xoa sach ma IR + niem tin trang thai\n");
}

void printStatus() {
  const uint32_t now = millis();
  const DiffuserControl::Status st = DiffuserControl::status(now);

  float ema = NAN;
  const bool haveEma = HumiditySensor::humidity(ema);

  Serial.println("\n=========== NODE DO AM ===========");

  if (haveEma) {
    Serial.printf("  do am (muot)     %.1f %%RH   (tho %.1f, nhiet %.1f C)\n", ema,
                  HumiditySensor::rawHumidity(), HumiditySensor::temperature());
  } else {
    Serial.printf("  do am            KHONG CO SO DO (%u lan doc truot lien tiep)\n",
                  HumiditySensor::consecutiveFailures());
    const char *why = HumiditySensor::lastFailReason();
    if (why[0] != '\0') Serial.printf("  vi sao truot     %s\n", why);
  }
  Serial.printf("  loai cam bien    %s\n", DHT_SENSOR_IS_DHT11 ? "DHT11" : "DHT22");
  Serial.printf("  nguong           BAT duoi %.1f  |  TAT tren %.1f %%RH\n",
                (double)HUMID_ON_BELOW_RH, (double)HUMID_OFF_ABOVE_RH);

  Serial.printf("  may xong         %s   (%s)\n", st.on ? "DANG CHAY" : "TAT",
                st.overriding ? "GHI DE tay" : "TU DONG");
  Serial.printf("  ly do            %s\n", DiffuserControl::reasonText(st.reason));
  Serial.printf("  giu trang thai   %lu phut %lu giay\n",
                (unsigned long)(st.stateAgeSec / 60), (unsigned long)(st.stateAgeSec % 60));
  printCountdown("con DWELL", st.dwellLeftSec);
  printCountdown("con GHI DE", st.overrideLeftSec);
  printCountdown("con KHOA nuoc", st.lockoutLeftSec);

  Serial.printf("  ma IR            BAT: %s", yesNo(IrSlots::has(IrSlots::Slot::ON)));
#if DIFFUSER_IR_TOGGLE
  Serial.println("   |  TAT: khong dung (remote mot nut bap benh)");
#else
  Serial.printf("   |  TAT: %s\n", yesNo(IrSlots::has(IrSlots::Slot::OFF)));
#endif

  if (LearnSession::active()) {
    Serial.printf("  >> DANG HOC o %s, con %lu giay\n", IrSlots::name(LearnSession::slot()),
                  (unsigned long)(IrRemote::learnRemainingMs() / 1000));
  }
  Serial.println("==================================");
}

bool watching() { return g_watch; }

void poll() {
  // --- nhịp bắn của `burst` ---------------------------------------------
  // Học mã thì hoãn: mắt thu đang mở, bắn vào đó là bo tự nghe lại khung của
  // chính mình. IrRemote::blast() có chống, nhưng không việc gì phải dựa vào.
  if (g_burstLeft > 0 && !LearnSession::active() &&
      (int32_t)(millis() - g_burstNextMs) >= 0) {
    g_burstNextMs = millis() + BURST_GAP_MS;
    const uint8_t shot = (uint8_t)(BURST_SHOTS - g_burstLeft + 1);
    if (DiffuserIr::send(true)) {
      Serial.printf("[burst] phat %u/%u\n", shot, BURST_SHOTS);
      g_burstLeft--;
    } else {
      burstStop("chua hoc ma IR");
    }
    if (g_burstLeft == 0) Serial.println("[burst] xong.");
  }

  while (Serial.available() > 0) {
    const char c = (char)Serial.read();

    if (c == '\r') continue;
    if (c == '\n') {
      g_line[g_len] = '\0';
      // Bỏ khoảng trắng thừa ở cuối — dán lệnh từ chỗ khác rất hay dính.
      while (g_len > 0 && g_line[g_len - 1] == ' ') g_line[--g_len] = '\0';
      // Gõ bất cứ gì cũng dừng burst — nó là cái phanh duy nhất, và người đang
      // đứng cạnh máy xông nhấp nháy sẽ gõ theo phản xạ chứ không tra cú pháp.
      burstStop("co lenh moi");
      dispatch(g_line);
      g_len = 0;
      continue;
    }

    // Dòng dài quá thì bỏ phần thừa chứ KHÔNG tràn bộ đệm. Không có nhánh này
    // thì một lần dán nhầm cả đoạn văn vào terminal là ghi đè ngăn xếp.
    if (g_len + 1 < sizeof(g_line)) g_line[g_len++] = (char)tolower((unsigned char)c);
  }
}

}  // namespace SerialConsole
