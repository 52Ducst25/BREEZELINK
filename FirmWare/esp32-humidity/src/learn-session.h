#pragma once
#include <Arduino.h>

#include "ir-slots.h"

// ============================================================================
//  Một lần học mã: mở mắt thu -> chờ người bấm remote -> cất vào NVS.
// ----------------------------------------------------------------------------
//  Tách riêng khỏi main.cpp vì có HAI đường cùng khởi động nó (nút BOOT và
//  lệnh serial), và cả hai phải chạy đúng một trình tự. Để mỗi bên tự gọi
//  IrRemote rồi tự cất IrSlots là sớm muộn hai đường lệch nhau — thường là
//  đường ít dùng hơn quên mất một bước, và nó chỉ lộ ra khi có người dùng
//  đúng đường đó.
//
//  TRONG LÚC HỌC, MÁY XÔNG KHÔNG ĐƯỢC ĐIỀU KHIỂN. main.cpp tạm dừng vòng quyết
//  định: bắn IR trong lúc mắt thu đang mở là bo tự học lại chính khung của
//  mình (IrRemote::blast có chống, nhưng dừng hẳn thì không phải tin vào đó).
// ============================================================================
namespace LearnSession {

/// Bắt đầu học vào ô [s]. Gọi lại khi đang học dở là chuyển sang ô mới —
/// an toàn, IrRemote::learnStart() tự dọn lần trước.
void start(IrSlots::Slot s);

/// Gọi mỗi vòng loop(). Trả true khi phiên học VỪA kết thúc (thành công hoặc
/// hết giờ) — bên gọi dùng để in lại bảng trạng thái.
bool poll();

bool active();

IrSlots::Slot slot();

/// Ô nào nên học tiếp theo khi người dùng chỉ bấm nút mà không nói rõ ô nào.
///
/// Ưu tiên ô còn TRỐNG, và với remote một-nút-bập-bênh thì luôn là ô BẬT (ô
/// TẮT không dùng tới — xem settings.h §6). Nhờ vậy người lắp chỉ cần bấm nút
/// rồi bấm remote, không phải nhớ cú pháp gì.
IrSlots::Slot suggestNext();

}  // namespace LearnSession
