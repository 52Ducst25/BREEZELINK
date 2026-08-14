#pragma once
#include <Arduino.h>

// ============================================================================
//  Đọc DHT22 và làm mượt độ ẩm bằng EMA.
// ----------------------------------------------------------------------------
//  Đây là lớp CHỐNG DAO ĐỘNG THỨ NHẤT trong ba lớp (xem settings.h §3). Nó
//  nằm ở đây chứ không ở diffuser-control.cpp có lý do: bộ điều khiển phải
//  nhận vào một con số đã sạch và chỉ lo mỗi việc quyết định. Trộn hai việc
//  lại thì không thử được riêng phần quyết định bằng số giả.
//
//  QUY ƯỚC QUAN TRỌNG: khi cảm biến hỏng, hàm đọc trả về `false` và KHÔNG trả
//  số cũ. Giữ lại số của hai phút trước rồi tiếp tục điều khiển theo nó là
//  thiết bị nói dối một cách thuyết phục — cùng lý do đã ghi ở
//  ../esp32-room/src/room-sensor.cpp. Bên gọi phải hiểu "chưa biết độ ẩm" và
//  KHÔNG được thay bằng 0.0.
// ============================================================================
namespace HumiditySensor {

/// Bao nhiêu lần đọc trượt LIÊN TIẾP thì coi là cảm biến hỏng và xoá số cũ.
///
/// DHT22 trượt lẻ tẻ là bình thường (giao thức một dây rất nhạy với ngắt), nên
/// hỏng ngay ở lần trượt đầu là báo động giả suốt ngày. 3 lần ở nhịp 5 giây =
/// 15 giây im lặng — vẫn nhanh hơn nhiều so với ngưỡng cắt máy 120 giây.
static const uint8_t FAIL_LIMIT = 3;

void begin(uint8_t pin);

/// Gọi mỗi vòng loop(). Tự giữ nhịp SAMPLE_MS bên trong.
/// Trả true đúng vào vòng vừa thực hiện một lần đọc (để bên gọi ghi log).
bool poll();

/// Độ ẩm ĐÃ LÀM MƯỢT (%RH). Trả false khi chưa có số đo hợp lệ nào — hoặc khi
/// cảm biến đã trượt quá FAIL_LIMIT lần liên tiếp.
bool humidity(float &rhOut);

/// Số đo THÔ của lần đọc gần nhất (%RH), NAN nếu không có. Chỉ để in ra serial:
/// nhìn được cả thô lẫn mượt thì mới phân biệt "cảm biến nhiễu" với "phòng đang
/// thay đổi thật".
float rawHumidity();

/// Nhiệt độ của lần đọc gần nhất (°C), NAN nếu không có.
///
/// Bo này KHÔNG điều khiển gì theo nhiệt độ — DHT22 trả kèm nên in ra cho
/// người lắp đối chiếu với các node góc phòng. Đừng thêm logic nào dùng nó:
/// nhiệt độ của phòng là việc của bốn node góc và trung vị của chúng, một cảm
/// biến đơn lẻ ở đây không đại diện cho phòng.
float temperature();

/// Bao nhiêu giây kể từ lần đọc HỢP LỆ gần nhất. Trả UINT32_MAX nếu chưa có
/// lần nào — cố ý chọn giá trị lớn nhất để mọi phép so "quá hạn chưa?" ở bên
/// gọi đều đúng ngay từ lúc khởi động mà không phải viết thêm nhánh riêng.
uint32_t secSinceGoodRead();

uint8_t consecutiveFailures();

/// Vì sao lần đọc gần nhất trượt — "" nếu lần gần nhất thành công.
///
/// PHẢI PHÂN BIỆT ĐƯỢC BA CA, vì chúng dẫn tới ba việc phải làm khác hẳn nhau
/// mà triệu chứng bên ngoài giống hệt ("không có số đo"):
///
///   không trả lời  -> dây tuột / sai chân / chưa cấp nguồn
///   sai checksum   -> nhiễu trên đường dữ liệu, thiếu trở kéo, dây quá dài
///   số vô lý       -> KHAI SAI LOẠI CẢM BIẾN (settings.h, DHT_SENSOR_IS_DHT11)
///
/// Gộp cả ba thành một chữ "trượt" là đủ để đi nghi oan cho nguồn điện trong
/// khi thật ra chỉ khai nhầm DHT11 thành DHT22 — đã xảy ra đúng một lần rồi.
const char *lastFailReason();

}  // namespace HumiditySensor
