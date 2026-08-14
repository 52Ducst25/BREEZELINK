#include "diffuser-control.h"

#include <math.h>

#include "ir-slots.h"
#include "settings.h"

namespace DiffuserControl {
namespace {

Emitter g_emit = nullptr;

bool   g_on = false;
Reason g_reason = Reason::BOOT;

uint32_t g_lastSwitchMs = 0;   ///< lần cuối trạng thái ĐỔI (hoặc thử đổi)
uint32_t g_onSinceMs = 0;      ///< lần cuối máy được bật

bool     g_override = false;
uint32_t g_overrideStartMs = 0;

bool     g_lockout = false;
uint32_t g_lockoutStartMs = 0;

/// Số giây đã trôi kể từ [sinceMs]. Trừ số học CÓ DẤU để vẫn đúng khi millis()
/// tràn ở ~49 ngày. Bo treo tường chạy liên tục nên mốc đó sẽ tới thật; dùng
/// phép trừ không dấu ở đây thì đúng ngày thứ 49 mọi bộ đếm giờ nhảy vọt và
/// máy bị cắt vì "chạy quá lâu" dù vừa bật.
uint32_t elapsedSec(uint32_t sinceMs, uint32_t nowMs) {
  const int32_t d = (int32_t)(nowMs - sinceMs);
  return d > 0 ? (uint32_t)d / 1000u : 0u;
}

/// Đổi trạng thái THẬT: bắn IR trước, chỉ ghi nhận khi bắn được.
void applyState(bool want, Reason why, uint32_t nowMs) {
  if (g_emit == nullptr) return;

  if (!g_emit(want)) {
    // Không bắn được (chưa học mã). KHÔNG đổi g_on — máy thật có đổi gì đâu.
    //
    // Vẫn nạp lại đồng hồ dwell, có chủ đích: không nạp thì vòng lặp kế tiếp
    // lại thử ngay và log phun ra mỗi 5 giây cho tới khi có người học mã. Nạp
    // vào thì nó thử lại mỗi DWELL_SEC — vẫn tự khỏi ngay khi mã được học, mà
    // log đọc được.
    g_lastSwitchMs = nowMs;
    g_reason = Reason::NO_CODE;
    return;
  }

  g_on = want;
  g_reason = why;
  g_lastSwitchMs = nowMs;
  if (want) g_onSinceMs = nowMs;
  IrSlots::rememberOn(want);

  Serial.printf("[dieu-khien] %s - %s\n", want ? "BAT may xong" : "TAT may xong",
                reasonText(why));
}

}  // namespace

void begin(Emitter emit) {
  g_emit = emit;

  // Nạp lại niềm tin từ NVS. KHÔNG bắn IR ở đây: ta không biết máy thật đang
  // thế nào, và bắn một phát "cho chắc" với remote bập bênh là đảo đúng cái
  // trạng thái mình vừa khôi phục.
  g_on = IrSlots::recallOn();
  g_reason = Reason::BOOT;

  const uint32_t now = millis();
  g_lastSwitchMs = now;
  g_onSinceMs = now;   // tính giờ chạy từ lúc boot, không từ lúc bật thật —
                       // ta không biết lúc đó, và ước lượng THẤP hơn thực tế
                       // sẽ làm MAX_RUN cắt muộn. Đếm lại từ 0 là ước lượng
                       // an toàn theo chiều ngược lại chỉ khi bo vừa mất điện
                       // cùng máy; chấp nhận được vì MAX_RUN là lưới thứ hai,
                       // không phải cơ chế chính.

  Serial.printf("[dieu-khien] khoi dong, tin rang may dang %s\n",
                g_on ? "CHAY" : "TAT");
}

void update(float humidityRh, bool sensorOk, uint32_t nowMs) {
  // --- 1) Chạy quá lâu: CẮT, đè lên cả ghi đè tay ---------------------------
  // Đặt trên cùng vì đây là nhánh bảo vệ phần cứng. Để nó dưới nhánh ghi đè
  // thì một lần bấm tay có thể giữ máy chạy vô hạn — mà "bấm tay rồi quên"
  // chính là ca MAX_RUN sinh ra để chặn.
  if (g_on && elapsedSec(g_onSinceMs, nowMs) >= MAX_RUN_SEC) {
    g_override = false;
    applyState(false, Reason::MAX_RUN, nowMs);
    g_lockout = true;
    g_lockoutStartMs = nowMs;
    Serial.println("[dieu-khien] chay lien tuc qua lau -> cat va khoa. DI XEM BINH NUOC.");
    return;
  }

  // --- 2) Ghi đè tay --------------------------------------------------------
  if (g_override) {
    if (elapsedSec(g_overrideStartMs, nowMs) >= OVERRIDE_HOLD_SEC) {
      g_override = false;
      Serial.println("[dieu-khien] het han GHI DE -> tro ve TU DONG");
    } else {
      g_reason = Reason::MANUAL;
      return;
    }
  }

  // --- 3) Mất số đo: CẮT ----------------------------------------------------
  if (!sensorOk || isnan(humidityRh)) {
    if (g_on) applyState(false, Reason::SENSOR_LOST, nowMs);
    else g_reason = Reason::SENSOR_LOST;
    return;
  }

  // --- 4) Khoá chờ đổ nước --------------------------------------------------
  if (g_lockout) {
    if (elapsedSec(g_lockoutStartMs, nowMs) < REFILL_LOCKOUT_SEC) {
      if (g_on) applyState(false, Reason::LOCKOUT, nowMs);
      else g_reason = Reason::LOCKOUT;
      return;
    }
    g_lockout = false;
    Serial.println("[dieu-khien] het khoa do nuoc -> tu dong tro lai");
  }

  // --- 5) Trễ (deadband) ----------------------------------------------------
  bool   want = g_on;
  Reason why = Reason::DEADBAND;
  if (humidityRh < HUMID_ON_BELOW_RH) {
    want = true;
    why = Reason::AUTO_DRY;
  } else if (humidityRh > HUMID_OFF_ABOVE_RH) {
    want = false;
    why = Reason::AUTO_WET;
  }

  if (want == g_on) {
    g_reason = why;
    return;
  }

  // --- 6) Dwell -------------------------------------------------------------
  if (elapsedSec(g_lastSwitchMs, nowMs) < DWELL_SEC) {
    g_reason = Reason::DWELL_HOLD;
    return;
  }

  applyState(want, why, nowMs);
}

void manualSet(bool on, uint32_t nowMs) {
  // Bấm tay dọn luôn khoá đổ nước: ca dùng chính của nút này chính là "vừa đổ
  // nước xong, chạy lại đi".
  g_lockout = false;
  g_override = true;
  g_overrideStartMs = nowMs;

  if (on == g_on) {
    // Trạng thái đã đúng ý rồi — vẫn vào GHI ĐÈ (để giữ nó khỏi bị tự động đổi)
    // nhưng KHÔNG bắn IR. Bắn thừa một phát với remote bập bênh là đảo ngược
    // đúng cái vừa yêu cầu.
    g_reason = Reason::MANUAL;
    Serial.printf("[dieu-khien] GHI DE: giu nguyen %s\n", on ? "BAT" : "TAT");
    return;
  }
  applyState(on, Reason::MANUAL, nowMs);
  g_reason = Reason::MANUAL;
}

void manualToggle(uint32_t nowMs) { manualSet(!g_on, nowMs); }

void backToAuto(uint32_t nowMs) {
  g_override = false;
  g_reason = Reason::BOOT;
  (void)nowMs;
  Serial.println("[dieu-khien] tro ve TU DONG");
}

Status status(uint32_t nowMs) {
  Status s;
  s.on = g_on;
  s.overriding = g_override;
  s.reason = g_reason;
  s.stateAgeSec = elapsedSec(g_lastSwitchMs, nowMs);

  s.overrideLeftSec = 0;
  if (g_override) {
    const uint32_t used = elapsedSec(g_overrideStartMs, nowMs);
    s.overrideLeftSec = used < OVERRIDE_HOLD_SEC ? OVERRIDE_HOLD_SEC - used : 0;
  }

  s.dwellLeftSec =
      s.stateAgeSec < DWELL_SEC ? DWELL_SEC - s.stateAgeSec : 0;

  s.lockoutLeftSec = 0;
  if (g_lockout) {
    const uint32_t used = elapsedSec(g_lockoutStartMs, nowMs);
    s.lockoutLeftSec = used < REFILL_LOCKOUT_SEC ? REFILL_LOCKOUT_SEC - used : 0;
  }
  return s;
}

const char *reasonText(Reason r) {
  switch (r) {
    case Reason::AUTO_DRY:    return "phong kho hon nguong BAT";
    case Reason::AUTO_WET:    return "phong am hon nguong TAT";
    case Reason::DEADBAND:    return "nam trong vung tre, giu nguyen";
    case Reason::DWELL_HOLD:  return "muon doi nhung chua du thoi gian giu toi thieu";
    case Reason::MANUAL:      return "nguoi dung dang GHI DE";
    case Reason::MAX_RUN:     return "chay lien tuc qua lau -> cat";
    case Reason::LOCKOUT:     return "dang khoa cho do nuoc";
    case Reason::SENSOR_LOST: return "mat so do cam bien";
    case Reason::NO_CODE:     return "CHUA HOC MA IR - giu nut BOOT 3 giay de hoc";
    case Reason::BOOT:        return "vua khoi dong";
  }
  return "?";
}

}  // namespace DiffuserControl
