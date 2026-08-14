#include "learn-session.h"

#include <string.h>

#include "ir-remote.h"
#include "settings.h"

namespace LearnSession {
namespace {

bool          g_active = false;
IrSlots::Slot g_slot = IrSlots::Slot::ON;

/// Bộ đệm khung vừa bắt được. TĨNH chứ không đặt trên ngăn xếp: 300 mốc = 600
/// byte, và poll() được gọi từ loop() nơi ngăn xếp còn phải chứa cả thư viện IR.
uint16_t g_buf[IrRemote::RAW_MAX];

}  // namespace

void start(IrSlots::Slot s) {
  g_slot = s;
  g_active = true;
  IrRemote::learnStart(LEARN_TIMEOUT_MS);

  Serial.printf(
      "\n=== HOC MA: o %s ===\n"
      "Chia remote MAY XONG vao mat thu (GPIO%d), cach ~5cm, roi bam nut duoi day.\n"
      "Con %lu giay.\n",
      IrSlots::name(s), IR_RX_PIN, (unsigned long)(LEARN_TIMEOUT_MS / 1000));

  // Nói thẳng tên nút trên remote 6 nút đang dùng. Chỉ ghi "bam nut can hoc"
  // thì người lắp phải tự suy ra, và với remote co ca [ON/OFF] lan [Continuous]
  // thi suy nham la hoc vao o sai — mã vẫn học được, log vẫn báo thành công, và
  // hỏng chỉ lộ ra nhiều giờ sau khi máy chạy ngược.
#if DIFFUSER_IR_TOGGLE
  if (s == IrSlots::Slot::ON) {
    Serial.println("=> Bam nut [ON/OFF] (goc tren trai).");
  } else {
    Serial.println(
        "LUU Y: dang dat DIFFUSER_IR_TOGGLE=1 (nut bap benh) nen o TAT KHONG BAO\n"
        "       GIO duoc dung — hoc vao day la phi cong. Xem settings.h muc 6:\n"
        "       thu bam [Continuous] luc may dang tat, neu may chay thi doi ve 0.");
  }
#else
  Serial.println(s == IrSlots::Slot::ON
                     ? "=> Bam nut [Continuous] (goc tren phai)."
                     : "=> Bam nut [ON/OFF] (goc tren trai).");
#endif
}

bool poll() {
  if (!g_active) return false;

  const uint16_t len = IrRemote::learnPoll(g_buf, IrRemote::RAW_MAX);
  if (len > 0) {
    g_active = false;
    if (IrSlots::save(g_slot, g_buf, len)) {
      const char *proto = IrRemote::lastProtocol();
      Serial.printf("=== DA HOC o %s: %u moc, giao thuc %s (%u bit) ===\n",
                    IrSlots::name(g_slot), len, proto, IrRemote::lastBits());

      // Chấm điểm ngay tại chỗ. Không có dòng này thì "60 mốc" là tất cả những
      // gì người lắp biết — mà 60 mốc vừa có thể là remote vừa có thể là nhiễu
      // đèn phòng, và họ chỉ phát hiện ra khi máy không nhúc nhích nhiều phút
      // sau, lúc đã quên mất mình vừa học trong điều kiện nào.
      if (strcmp(proto, "UNKNOWN") == 0) {
        Serial.println(
            "!!! GIAI MA KHONG RA GIAO THUC NAO.\n"
            "    Co the van dung (remote la thi phat lai nguyen van van chay),\n"
            "    NHUNG day cung dung la dau hieu cua NHIEU DEN PHONG bi hoc nham.\n"
            "    Nen: TAT BOT DEN, chia remote sat ~2cm, `wipe` roi `learn` lai.\n"
            "    Hoc hai lan lien ma deu ra cung mot giao thuc thi moi chac.");
      } else {
        Serial.println("    Giao thuc co ten -> gan nhu chac chan la remote that, khong phai nhieu.");
      }
      Serial.println();
    } else {
      // Học được mà cất không xong là ca tệ nhất: người dùng thấy "đã bắt được
      // remote" rồi tưởng xong việc. Phải nói thẳng là mã ĐÃ MẤT.
      Serial.printf(
          "=== LOI: bat duoc %u moc nhung GHI NVS THAT BAI - MA DA MAT. ===\n"
          "    Thu lenh `wipe` roi hoc lai.\n\n",
          len);
    }
    return true;
  }

  if (IrRemote::learnTimedOut()) {
    g_active = false;
    Serial.printf(
        "=== HET GIO, chua bat duoc gi cho o %s ===\n"
        "    Kiem: mat thu nuoi 3.3V chua? chan DATA vao GPIO%d chua? pin remote con?\n"
        "    Mat thu TSOP co 3 chan va DE CAM NGUOC - cam nguoc thi no khong chay\n"
        "    ma cung khong hong, tuc la khong co trieu chung nao ngoai viec nay.\n\n",
        IrSlots::name(g_slot), IR_RX_PIN);
    return true;
  }
  return false;
}

bool active() { return g_active; }

IrSlots::Slot slot() { return g_slot; }

IrSlots::Slot suggestNext() {
#if DIFFUSER_IR_TOGGLE
  return IrSlots::Slot::ON;
#else
  // Ô nào trống thì học ô đó trước; đủ cả hai rồi thì học lại ô BẬT.
  if (!IrSlots::has(IrSlots::Slot::ON)) return IrSlots::Slot::ON;
  if (!IrSlots::has(IrSlots::Slot::OFF)) return IrSlots::Slot::OFF;
  return IrSlots::Slot::ON;
#endif
}

}  // namespace LearnSession
