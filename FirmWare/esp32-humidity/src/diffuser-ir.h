#pragma once
#include <Arduino.h>

// ============================================================================
//  "Bắn khung BẬT" / "bắn khung TẮT" — chỗ DUY NHẤT dịch ý định thành sóng.
// ----------------------------------------------------------------------------
//  Có riêng một module cho đúng một việc nhỏ vì có HAI bên cần nó:
//      - DiffuserControl gọi qua con trỏ Emitter khi tự động đổi trạng thái
//      - lệnh `test` trên serial gọi thẳng để ngắm LED lúc lắp
//
//  Và vì đây là nơi luật remote-một-nút (settings.h §6) được áp dụng. Để luật
//  đó nằm rải ở hai bên gọi thì sớm muộn một bên quên, và triệu chứng là máy
//  xông chạy ngược pha mà log nói mọi thứ đều đúng.
// ============================================================================
namespace DiffuserIr {

/// Bắn khung tương ứng với [on].
///
/// Trả FALSE khi chưa học mã cho việc đó — bên gọi PHẢI coi đó là "chưa làm
/// được gì" chứ không phải "đã xong". Xem chú thích Emitter ở diffuser-control.h.
///
/// Với DIFFUSER_IR_TOGGLE=1 thì cả hai chiều đều bắn CÙNG một khung (nút bập
/// bênh), nên hàm này không thể tự kiểm chứng kết quả — nó chỉ báo "đã bắn".
bool send(bool on);

}  // namespace DiffuserIr
