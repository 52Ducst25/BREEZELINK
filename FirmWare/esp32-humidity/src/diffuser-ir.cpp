#include "diffuser-ir.h"

#include "ir-remote.h"
#include "ir-slots.h"
#include "settings.h"

namespace DiffuserIr {
namespace {

/// TĨNH, không đặt trên ngăn xếp — 300 mốc = 600 byte, và hàm này được gọi từ
/// loop() nơi ngăn xếp còn phải chứa cả thư viện IR lúc sendRaw().
uint16_t g_buf[IrRemote::RAW_MAX];

/// Nhắc "chưa học mã" thưa thôi (ms). Không có cái này thì mỗi vòng loop() in
/// một dòng và log trôi mất mọi thứ khác. DiffuserControl cũng đã nạp lại đồng
/// hồ dwell khi bắn trượt, nhưng lệnh `test` thì không đi qua đó.
const uint32_t WARN_EVERY_MS = 30000;
uint32_t g_lastWarnMs = 0;

void warnNoCode(IrSlots::Slot s) {
  const uint32_t now = millis();
  if (g_lastWarnMs != 0 && now - g_lastWarnMs < WARN_EVERY_MS) return;
  g_lastWarnMs = now;
  Serial.printf(
      "[ir] CHUA HOC MA cho o %s - khong bat duoc gi.\n"
      "     Giu nut BOOT 3 giay (hoac go `learn`) roi chia remote may xong vao mat thu.\n",
      IrSlots::name(s));
}

}  // namespace

bool send(bool on) {
#if DIFFUSER_IR_TOGGLE
  // Remote chỉ có MỘT nút nguồn: cả bật lẫn tắt đều là đúng khung đó, bắn một
  // lần. Bo không thể xác nhận kết quả — xem settings.h §6.
  const IrSlots::Slot s = IrSlots::Slot::ON;
  (void)on;
#else
  const IrSlots::Slot s = on ? IrSlots::Slot::ON : IrSlots::Slot::OFF;
#endif

  const uint16_t len = IrSlots::load(s, g_buf, IrRemote::RAW_MAX);
  if (len == 0) {
    warnNoCode(s);
    return false;
  }

  IrRemote::blast(g_buf, len);
  return true;
}

}  // namespace DiffuserIr
