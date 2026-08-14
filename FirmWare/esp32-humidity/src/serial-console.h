#pragma once
#include <Arduino.h>

// ============================================================================
//  Bàn phím chẩn đoán qua serial (115200).
// ----------------------------------------------------------------------------
//  Bo này KHÔNG có màn, KHÔNG có WiFi, KHÔNG có MQTT. Nên đây là giao diện đầy
//  đủ duy nhất của nó — nút BOOT chỉ làm được hai việc thường dùng nhất.
//
//  Cắm cáp USB vào là dùng được ngay: cổng đi qua chip cầu CP210x nên `Serial`
//  ra thẳng đó, không cần cấu hình gì thêm.
// ============================================================================
namespace SerialConsole {

/// Gọi mỗi vòng loop(). Đọc từng dòng, không chặn.
void poll();

/// In bảng trạng thái đầy đủ. main.cpp cũng gọi sau mỗi lần đổi trạng thái.
void printStatus();

void printHelp();

/// Đang ở chế độ xem trực tiếp? main.cpp dùng để rút nhịp in từ 60 giây xuống
/// 5 giây (đúng nhịp đọc cảm biến — in dày hơn cũng chỉ lặp lại số cũ).
bool watching();

}  // namespace SerialConsole
