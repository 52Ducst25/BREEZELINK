#include "humidifier-control.h"

#include <Preferences.h>
#include <math.h>

namespace HumidifierControl {
namespace {

Emitter g_emit = nullptr;

bool   g_on = false;
Reason g_reason = Reason::BOOT;

float    g_rh = NAN;           ///< độ ẩm đã làm mượt
uint32_t g_lastGoodMs = 0;     ///< lần cuối có số đo hợp lệ (0 = chưa lần nào)

uint32_t g_lastSwitchMs = 0;   ///< lần cuối trạng thái ĐỔI (hoặc thử đổi)
uint32_t g_onSinceMs = 0;      ///< lần cuối máy được bật

bool     g_override = false;
uint32_t g_overrideStartMs = 0;

bool     g_lockout = false;
uint32_t g_lockoutStartMs = 0;

// Niềm tin trạng thái, giữ qua mất điện.
//
// PHẢI CÓ VÌ REMOTE BẬP BÊNH: hộ nào máy tạo ẩm chỉ có một nút nguồn thì panel
// không đo được máy đang bật hay tắt — nó chỉ NHỚ lần cuối nó bắn gì. Quên mất
// niềm tin đó sau mỗi lần mất điện nghĩa là mọi lệnh sau đều có 50% cơ hội đi
// ngược. Với remote có hai nút rời thì trường này gần như vô hại: bắn lại không
// đổi gì, nên niềm tin sai tự sửa ở lần quyết định kế tiếp.
//
// NAMESPACE RIÊNG, không dùng chung "aircon-ir" của IrStore: kho mã IR có lúc
// bị wipe() để ép backend gửi lại toàn bộ, và cuốn theo niềm tin trạng thái là
// tác dụng phụ không ai đoán được từ tên hàm đó.
Preferences g_nvs;
bool        g_nvsReady = false;
const char *const NVS_NS  = "bl-humid";
const char *const NVS_KEY = "on";

void rememberOn(bool on) {
  if (g_nvsReady) g_nvs.putBool(NVS_KEY, on);
}

/// Cùng nội dung với reasonText() nhưng KHÔNG DẤU, dành cho serial.
///
/// HAI BẢN CHỮ CHO MỘT DANH SÁCH LÀ CÓ CHỦ ĐÍCH, không phải quên gộp. Hai nơi đọc
/// khác nhau và luật của chúng ngược nhau:
///   màn LVGL -> tiếng Việt CÓ DẤU; font ui/fonts/ có đủ dải, và mọi nhãn khác
///               trên panel đều có dấu nên riêng dòng này không dấu sẽ lạc lõng.
///   serial   -> KHÔNG DẤU, theo lệ của mọi log trong dự án: nhiều cửa sổ serial
///               monitor không dựng nổi UTF-8 và biến chữ có dấu thành rác — mà
///               log là thứ người ta đọc đúng lúc đang bí.
/// Gộp làm một thì phải hy sinh một trong hai, và cả hai đều không đáng hy sinh.
const char *reasonAscii(Reason r) {
  switch (r) {
    case Reason::AUTO_DRY:    return "phong kho hon nguong bat";
    case Reason::AUTO_WET:    return "phong am hon nguong tat";
    case Reason::DEADBAND:    return "trong vung tre - giu nguyen";
    case Reason::DWELL_HOLD:  return "cho du thoi gian giu toi thieu";
    case Reason::MANUAL:      return "dang ghi de bang tay";
    case Reason::MAX_RUN:     return "chay qua lau - da cat";
    case Reason::LOCKOUT:     return "khoa cho do nuoc";
    case Reason::SENSOR_LOST: return "mat so do do am";
    case Reason::NO_CODE:     return "chua hoc ma - vao app de hoc";
    case Reason::BOOT:        return "vua khoi dong";
  }
  return "?";
}

/// Số giây đã trôi kể từ [sinceMs]. Trừ số học CÓ DẤU để vẫn đúng khi millis()
/// tràn ở ~49 ngày. Panel treo tường chạy liên tục nên mốc đó sẽ tới thật; dùng
/// phép trừ không dấu thì đúng ngày thứ 49 mọi bộ đếm giờ nhảy vọt và máy bị cắt
/// vì "chạy quá lâu" dù vừa bật.
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
    // Vẫn nạp lại đồng hồ dwell, có chủ đích: không nạp thì vòng kế tiếp lại thử
    // ngay và log phun ra mỗi 5 giây cho tới khi có người vào app học mã. Nạp
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
  rememberOn(want);

  Serial.printf("[am] %s may tao am - %s\n", want ? "BAT" : "TAT", reasonAscii(why));
}

/// Một lượt quyết định. Tách khỏi tick() để tick() chỉ còn lo nhịp và EMA.
void decide(uint32_t nowMs) {
  // --- 1) Chạy quá lâu: CẮT, đè lên cả ghi đè tay ---------------------------
  // Trên cùng vì đây là nhánh bảo vệ phần cứng. Để nó dưới nhánh ghi đè thì một
  // lần bấm tay có thể giữ máy chạy vô hạn — mà "bấm tay rồi quên" chính là ca
  // MAX_RUN sinh ra để chặn.
  if (g_on && elapsedSec(g_onSinceMs, nowMs) >= MAX_RUN_SEC) {
    g_override = false;
    applyState(false, Reason::MAX_RUN, nowMs);
    g_lockout = true;
    g_lockoutStartMs = nowMs;
    Serial.println("[am] chay lien tuc qua lau -> cat va khoa. DI XEM BINH NUOC.");
    return;
  }

  // --- 2) Ghi đè tay --------------------------------------------------------
  if (g_override) {
    if (elapsedSec(g_overrideStartMs, nowMs) >= OVERRIDE_HOLD_SEC) {
      g_override = false;
      Serial.println("[am] het han GHI DE -> tro ve TU DONG");
    } else {
      g_reason = Reason::MANUAL;
      return;
    }
  }

  // --- 3) Mất số đo: CẮT ----------------------------------------------------
  const bool sensorOk =
      !isnan(g_rh) && g_lastGoodMs != 0 &&
      elapsedSec(g_lastGoodMs, nowMs) < SENSOR_STALE_SEC;
  if (!sensorOk) {
    if (g_on) applyState(false, Reason::SENSOR_LOST, nowMs);
    else      g_reason = Reason::SENSOR_LOST;
    return;
  }

  // --- 4) Khoá chờ đổ nước --------------------------------------------------
  if (g_lockout) {
    if (elapsedSec(g_lockoutStartMs, nowMs) < REFILL_LOCKOUT_SEC) {
      if (g_on) applyState(false, Reason::LOCKOUT, nowMs);
      else      g_reason = Reason::LOCKOUT;
      return;
    }
    g_lockout = false;
    Serial.println("[am] het khoa do nuoc -> tu dong tro lai");
  }

  // --- 5) Trễ (deadband) ----------------------------------------------------
  bool   want = g_on;
  Reason why  = Reason::DEADBAND;
  if (g_rh < ON_BELOW_RH) {
    want = true;
    why  = Reason::AUTO_DRY;
  } else if (g_rh > OFF_ABOVE_RH) {
    want = false;
    why  = Reason::AUTO_WET;
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

}  // namespace

void begin(Emitter emit) {
  g_emit = emit;

  g_nvsReady = g_nvs.begin(NVS_NS, false /*read-write*/);
  // Nạp lại niềm tin. KHÔNG bắn IR ở đây: ta không biết máy thật đang thế nào,
  // và bắn một phát "cho chắc" với remote bập bênh là đảo đúng cái trạng thái
  // mình vừa khôi phục.
  g_on = g_nvsReady && g_nvs.getBool(NVS_KEY, false);
  g_reason = Reason::BOOT;

  const uint32_t now = millis();
  g_lastSwitchMs = now;
  // Tính giờ chạy từ lúc boot, không từ lúc bật thật — ta không biết lúc đó, và
  // ước lượng THẤP hơn thực tế sẽ làm MAX_RUN cắt muộn. Đếm lại từ 0 chỉ sai
  // theo chiều an toàn (cắt muộn hơn) khi panel vừa mất điện cùng máy; chấp nhận
  // được vì MAX_RUN là lưới thứ hai, không phải cơ chế chính.
  g_onSinceMs = now;

  Serial.printf("[am] khoi dong, tin rang may tao am dang %s%s\n",
                g_on ? "CHAY" : "TAT",
                g_nvsReady ? "" : " (NVS loi — niem tin khong giu qua mat dien)");
}

void tick(float rhRaw, uint32_t nowMs) {
  // EMA CHỈ CHẠY TRÊN SỐ HỢP LỆ. Trộn NaN vào là g_rh thành NaN vĩnh viễn —
  // một góc rụng nửa giây cũng đủ để giết luôn đường lọc, và triệu chứng là máy
  // tắt hẳn với lý do SENSOR_LOST trong khi ba góc kia vẫn báo số đều đặn.
  if (!isnan(rhRaw)) {
    g_rh = isnan(g_rh) ? rhRaw : (EMA_ALPHA * rhRaw + (1.0f - EMA_ALPHA) * g_rh);
    g_lastGoodMs = nowMs;
  }

  decide(nowMs);
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
    // đúng cái vừa được yêu cầu.
    g_reason = Reason::MANUAL;
    Serial.printf("[am] GHI DE: giu nguyen %s\n", on ? "BAT" : "TAT");
    return;
  }
  applyState(on, Reason::MANUAL, nowMs);
  // applyState() có thể rơi vào nhánh NO_CODE và đặt lý do khác. Chỉ ghi đè lý
  // do khi nó đã đổi được thật — nếu không, màn sẽ nói "người dùng đang GHI ĐÈ"
  // trong khi thứ người dùng cần đọc là "CHƯA HỌC MÃ".
  if (g_on == on) g_reason = Reason::MANUAL;
}

void backToAuto(uint32_t nowMs) {
  g_override = false;
  // KHÔNG đặt lại về BOOT: lượt quyết định kế tiếp (trong vòng TICK_MS) sẽ điền
  // lý do thật. Đặt BOOT ở đây làm màn hiện "vừa khởi động" cho một panel đã
  // chạy nhiều ngày — một câu sai, dù chỉ sai trong 5 giây.
  decide(nowMs);
  Serial.println("[am] tro ve TU DONG");
}

Status status(uint32_t nowMs) {
  Status s;
  s.on          = g_on;
  s.overriding  = g_override;
  s.reason      = g_reason;
  s.rh          = g_rh;
  s.stateAgeSec = elapsedSec(g_lastSwitchMs, nowMs);

  s.overrideLeftSec = 0;
  if (g_override) {
    const uint32_t used = elapsedSec(g_overrideStartMs, nowMs);
    s.overrideLeftSec = used < OVERRIDE_HOLD_SEC ? OVERRIDE_HOLD_SEC - used : 0;
  }

  s.dwellLeftSec = s.stateAgeSec < DWELL_SEC ? DWELL_SEC - s.stateAgeSec : 0;

  s.lockoutLeftSec = 0;
  if (g_lockout) {
    const uint32_t used = elapsedSec(g_lockoutStartMs, nowMs);
    s.lockoutLeftSec = used < REFILL_LOCKOUT_SEC ? REFILL_LOCKOUT_SEC - used : 0;
  }
  return s;
}

const char *reasonText(Reason r) {
  switch (r) {
    case Reason::AUTO_DRY:    return "phòng khô hơn ngưỡng bật";
    case Reason::AUTO_WET:    return "phòng ẩm hơn ngưỡng tắt";
    case Reason::DEADBAND:    return "trong vùng trễ — giữ nguyên";
    case Reason::DWELL_HOLD:  return "chờ đủ thời gian giữ tối thiểu";
    case Reason::MANUAL:      return "đang ghi đè bằng tay";
    case Reason::MAX_RUN:     return "chạy quá lâu — đã cắt";
    case Reason::LOCKOUT:     return "khoá chờ đổ nước";
    case Reason::SENSOR_LOST: return "mất số đo độ ẩm";
    case Reason::NO_CODE:     return "chưa học mã — vào app để học";
    case Reason::BOOT:        return "vừa khởi động";
  }
  return "?";
}

}  // namespace HumidifierControl
