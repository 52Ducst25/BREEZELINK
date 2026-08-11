# Pha 03 — Firmware node cảm biến góc phòng

## Liên kết

- Tổng quan: [plan.md](plan.md) · Phụ thuộc: [pha 01](phase-01-mo-hinh-phong-va-di-tru.md)
- `FirmWare/shared/espnow-message.h`, `FirmWare/esp32-outdoor/src/main-espnow-slave.cpp`
- `FirmWare/esp32-indoor/src/espnow-relay.cpp`, `slave-watch.h`

## Tổng quan

**Ưu tiên:** Trung bình. **Trạng thái:** Chưa bắt đầu.

Firmware cho node ESP32 + DHT22 đặt ở góc phòng: chỉ đo và gửi ESP-NOW về master.
Không WiFi, không MQTT, không IR, không màn.

## Nhận định quan trọng

**Phần khó đã làm xong từ trước.** Gói ESP-NOW **tự mô tả** — nó mang sẵn
`device_uuid` của chính slave (`espnow-message.h`), nên master chỉ việc publish
vào `bl/{org}/{uuid}/telemetry` mà **không cần bảng ánh xạ MAC → uuid**. Thêm node
mới chỉ cần nạp firmware cho nó; master không sửa một dòng nào.

**`MAX_SLAVES = 8` đã có sẵn** (`slave-watch.h:35`), chú thích ghi đúng ý định:
*"1 hộ hiện có 1; chừa chỗ cho nhiều phòng"*.

**Node ngoài trời đã là đúng khuôn mẫu cần.** `main-espnow-slave.cpp` chính là
node chỉ-đo-và-gửi. Việc của pha này gần như là dùng lại nó, không viết mới.

**Cạm bẫy kênh ESP-NOW.** Slave chỉ **quét kênh lúc boot** rồi bám theo. Router
đổi kênh là toàn bộ cảm biến câm cho tới khi cấp điện lại — đã gặp thật trong lúc
làm việc trước (kênh 9 → 1). Với 4 node/phòng thì đây thành phiền phức thật.

## Yêu cầu

**Chức năng**
- Đo T/RH mỗi 15–30s, gửi ESP-NOW về master phòng.
- Không cần cấu hình gì ngoài `DEVICE_UUID` + `ORG_ID`.
- Mất master thì tự thử lại, không treo.

**Phi chức năng**
- Chạy được trên ESP32 DevKit V1 trần (rẻ, dễ mua).
- Nên có đường ngủ sâu nếu chạy pin — để pha sau, không làm ngay (YAGNI).

## Kiến trúc

```
[sensor góc 1] ─┐
[sensor góc 2] ─┼─ ESP-NOW ─→ [room master] ─ MQTT ─→ broker
[sensor góc 3] ─┘                 (IR + màn + WiFi)
```

Không đổi `AcEspNowPacket` — bố cục hiện tại đã đủ (`magic`, `version`,
`device_uuid[33]`, `temp`, `humidity` = 43 byte).

## File liên quan

**Tạo**
- `FirmWare/esp32-sensor/platformio.ini`
- `FirmWare/esp32-sensor/src/main.cpp`
- `FirmWare/esp32-sensor/src/config.h.example`

**Sửa (nhỏ)**
- `FirmWare/esp32-indoor/src/espnow-relay.cpp` — kiểm lại khi có >1 slave
- `FirmWare/README.md` — mô tả loại node thứ ba

## Các bước

1. Tạo project `esp32-sensor` từ `esp32-outdoor` env `esp32-espnow`. Giữ nguyên
   cách quét kênh + gửi gói.
2. `config.h.example` chỉ cần `WIFI_SSID` (để quét kênh), `ORG_ID`, `DEVICE_UUID`.
   **Không** cần `MQTT_*` — node này không nối broker.
3. Bỏ mọi thứ liên quan WiFi station/MQTT khỏi bản sao để tiết kiệm flash.
4. Kiểm `espnow-relay` với 3 slave gửi đồng thời: đếm gói nhận/bỏ, xác nhận
   `MAX_SLAVES` đủ và không tràn.
5. Web admin: tạo node loại `sensor` gán vào phòng, sinh `DEVICE_UUID`.

## Todo

- [ ] Project `esp32-sensor` chạy được trên DevKit V1
- [ ] Gửi ESP-NOW đúng định dạng, master nhận và chuyển tiếp
- [ ] Thử 3 slave đồng thời, đo tỉ lệ bỏ gói
- [ ] Web admin tạo được node `sensor`
- [ ] Tài liệu: sơ đồ chân DHT22 + trở kéo 4.7k

## Tiêu chí hoàn thành

- 3 node cảm biến + 1 master trong một phòng, cả 4 số đo lên cloud đúng uuid.
- Rút một node: master báo `OFFLINE` node đó, ba node còn lại vẫn chạy.
- Master không phải sửa firmware khi thêm node thứ tư.

## Rủi ro

| Rủi ro | Giảm thiểu |
|---|---|
| Router đổi kênh → toàn bộ cảm biến câm | Cho slave **quét lại kênh** khi mất master quá N phút, thay vì chỉ quét lúc boot. Đây là cải tiến thật, đã gặp sự cố thực tế |
| DHT22 thiếu trở kéo → NaN xen kẽ | Ghi rõ trong tài liệu; firmware bỏ lượt đọc hỏng chứ không ghi NaN |
| Nhiều slave cùng gửi một lúc gây va chạm | ESP-NOW có CSMA; thêm lệch pha ngẫu nhiên theo uuid để tránh đồng bộ |

## Bảo mật

Gói ESP-NOW **không xác thực** — bất kỳ thiết bị nào trong tầm sóng cũng tự xưng
được một uuid (đã ghi rõ trong `espnow-message.h`). Chấp nhận được cho mạng một
hộ. Muốn chặt hơn thì bật PMK/LMK của ESP-NOW hoặc cho master lọc theo danh sách
MAC — cả hai nằm ngoài phạm vi pha này.

## Tiếp theo

Không chặn pha nào. Nên làm song song với pha 02.
