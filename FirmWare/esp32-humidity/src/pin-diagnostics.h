#pragma once
#include <Arduino.h>

// ============================================================================
//  Đo mức điện ở chân IR để biết dây có tới nơi không.
// ----------------------------------------------------------------------------
//  VÌ SAO CẦN: "học mãi không được" có ít nhất năm nguyên nhân — mất nguồn, sai
//  chân, cắm ngược VCC/GND, mắt thu hỏng, hết pin remote — và cả năm đều cho
//  ĐÚNG MỘT triệu chứng: im lặng. Không đo được chân thì chỉ còn cách tháo ra
//  cắm lại từng thứ và thử lại, mỗi vòng mất vài phút.
//
//  MẸO ĐO: bật trở kéo XUỐNG trong chip rồi đọc chân.
//
//    Mắt thu TSOP có ngõ ra ĐẨY-KÉO và ở trạng thái nghỉ nó giữ mức CAO. Ngõ ra
//    đó thừa sức thắng con trở kéo xuống ~45k trong chip. Nên:
//
//        đọc ra CAO  -> có thứ gì đó đang chủ động lái chân lên
//                       => mắt thu CÓ nguồn và DAT CÓ nối đúng chân
//        đọc ra THẤP -> không ai lái, chỉ có trở kéo xuống làm việc
//                       => dây chưa tới, hoặc module chưa có nguồn
//
//  Đây là phép thử KHÔNG phụ thuộc vào việc có ai bấm remote hay không — khác
//  hẳn chế độ học, nơi im lặng có thể chỉ vì chưa ai bấm gì.
// ============================================================================
namespace PinDiagnostics {

/// Đo chân mắt thu và in kết luận kèm việc phải làm.
void checkIrReceiver();

/// Nhấp nháy chân LED phát vài giây để soi bằng camera điện thoại.
///
/// Mắt người không thấy hồng ngoại, nhưng cảm biến camera thì thấy — LED sẽ
/// hiện thành chấm tím/trắng nhấp nháy. Đây là cách DUY NHẤT tách bạch "phát
/// yếu quá" với "không phát gì cả" mà không cần máy hiện sóng.
///
/// Nháy chậm (10Hz) chứ KHÔNG phát khung IR thật: khung thật chỉ dài vài chục
/// mili-giây, camera 30fps bắt không kịp và sẽ kết luận nhầm là LED chết.
void blinkIrEmitter();

}  // namespace PinDiagnostics
