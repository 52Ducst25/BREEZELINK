#pragma once
#include <Arduino.h>

#include "espnow-message.h"
#include "unoq-link-protocol.h"

// ============================================================================
//  Nhật ký từng gói VÀO/RA của gateway — chỉ để truy lỗi.
// ----------------------------------------------------------------------------
//  BẬT BẰNG `-D GATEWAY_TRACE=1`. MẶC ĐỊNH TẮT, và khi tắt thì mọi hàm dưới đây
//  là thân rỗng inline — trình biên dịch bỏ hẳn lời gọi, không tốn một byte flash
//  nào. Nhờ vậy để lời gọi nằm sẵn trong main.cpp là vô hại.
//
//  VÌ SAO KHÔNG BẬT LUÔN: bo QR Box treo tường có màn hình để nhìn, và một dòng
//  log cho MỖI gói (5 node × 5 giây) sẽ nhấn chìm những dòng thật sự đáng đọc —
//  `[cmd]`, `[learn]`, cảnh báo NVS đầy. Log dày làm mất log quan trọng chứ không
//  làm nó rõ hơn.
//
//  THỨ ĐÁNG GIÁ NHẤT Ở ĐÂY LÀ CỘT `Δ`: khoảng cách thời gian tới gói TRƯỚC của
//  ĐÚNG node đó. Node phát mỗi 5s mà Δ=15.0s nghĩa là rơi hai gói — nhìn một dòng
//  là biết, không phải ngồi trừ dấu thời gian. Broadcast ESP-NOW không có ACK nên
//  đây là cách duy nhất thấy được mất gói từ phía nhận.
// ============================================================================
namespace SerialTrace {

#if defined(GATEWAY_TRACE) && GATEWAY_TRACE

/// Một gói ESP-NOW vừa tới (in kèm Δ và số thứ tự riêng của node đó).
void packetIn(const AcEspNowPacket &pkt, const uint8_t mac[6]);

/// Vừa publish lên MQTT. [ok] là giá trị PubSubClient trả về — false nghĩa là
/// gói KHÔNG rời khỏi bo (thường do vượt bộ đệm), thứ rất dễ tưởng là mạng lỗi.
void mqttOut(const char *topic, const uint8_t *payload, size_t len, bool ok);

/// Vừa nhận một gói MQTT. Payload bị cắt bớt khi in: lệnh mang `ir_raw` vài trăm
/// mốc thời gian, in đủ là trôi hết màn hình.
void mqttIn(const char *topic, const uint8_t *payload, size_t len);

/// Vừa đẩy một ảnh chụp sang Arduino UNO Q.
void snapshotOut(const AcUnoQSnapshot &snap, bool linkUp);

/// Bảng tổng kết: mỗi node một dòng, kèm số gói và Δ trung bình.
void summary();

#else   // ----- TẮT: thân rỗng, trình biên dịch xoá sạch lời gọi -----

inline void packetIn(const AcEspNowPacket &, const uint8_t[6]) {}
inline void mqttOut(const char *, const uint8_t *, size_t, bool) {}
inline void mqttIn(const char *, const uint8_t *, size_t) {}
inline void snapshotOut(const AcUnoQSnapshot &, bool) {}
inline void summary() {}

#endif

} // namespace SerialTrace
