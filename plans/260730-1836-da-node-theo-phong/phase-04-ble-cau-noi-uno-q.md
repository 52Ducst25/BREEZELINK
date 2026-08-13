# Pha 04 — Cầu nối BLE tới Arduino UNO Q

## Liên kết

- Tổng quan: [plan.md](plan.md) · Phụ thuộc: [pha 01](phase-01-mo-hinh-phong-va-di-tru.md)
- `FirmWare/esp32-indoor/src/main.cpp`, `FirmWare/esp32-indoor/platformio.ini`
- Ngân sách bộ nhớ hiện tại: `ui.h` cuối file

## Tổng quan

**Ưu tiên:** Thấp nhất trong các pha — **cố ý**. **Trạng thái:** Chưa bắt đầu.

Room master phát số đo của phòng qua BLE để Arduino UNO Q đọc và chạy AI
giám sát/dự báo.

## Nhận định quan trọng

**AI chỉ giám sát/dự báo, không can thiệp vòng điều khiển.** Đây là điều quan
trọng nhất của cả pha, và nó quyết định thiết kế: BLE **không nằm trên đường điều
khiển**. Mất BLE, treo BLE, hay không đủ heap để bật BLE thì máy lạnh vẫn phải
chạy y nguyên. Vì vậy BLE phải **tách rời và fail-safe**, không phải một tính năng
gắn chặt.

**Heap là ràng buộc thật, và nó mỏng.** Log boot đo được trên bo thật:

```
Heap: tu do ~70 KB, thap nhat tung cham 63 KB
```

Trên đó đã có LVGL (2 bộ đệm vẽ 30 KB), bộ đệm MQTT 12 KB, `irBuf` 1.2 KB, ngăn
xếp tác vụ UI 8 KB. NimBLE cần ~30–40 KB. **Biên còn lại gần như bằng không.**
Không được cam kết pha này chạy được trước khi đo thật.

Flash thì thoải mái: đang dùng 46% của phân vùng app 2.75 MB.

**Một radio, ba việc.** ESP32 chỉ có một bộ thu phát 2.4 GHz. Thêm BLE nghĩa là
WiFi (MQTT) + ESP-NOW (N cảm biến) + BLE cùng chia khe thời gian. ESP-NOW vốn đã
phải bám đúng kênh router; thêm BLE sẽ làm tăng tỉ lệ mất gói cảm biến. Phải **đo
tỉ lệ bỏ gói trước và sau** khi bật BLE, không đoán.

**Vì sao vẫn làm BLE dù tôi đã nêu nghi ngại:** bạn đã chọn sau khi nghe phân
tích. Kế hoạch này tôn trọng lựa chọn đó nhưng dựng hàng rào quanh nó.

## Yêu cầu

**Chức năng**
- Master phát T/RH của phòng (đã gộp) + trạng thái máy lạnh qua BLE.
- UNO Q đọc được mà không cần ghép đôi thủ công phức tạp.

**Phi chức năng — bắt buộc**
- Cờ biên dịch `ENABLE_BLE` **mặc định TẮT**. Bật là lựa chọn có ý thức.
- NimBLE khởi tạo thất bại (thiếu heap) → **log rồi chạy tiếp**, tuyệt đối không
  chặn khởi động hay vòng điều khiển.
- Heap tự do sau khi bật BLE phải còn **≥ 25 KB** ở mức thấp nhất. Dưới ngưỡng đó
  thì pha này coi như thất bại và phải chuyển sang UART hoặc MQTT.

## Kiến trúc

```
[room master ESP32]
   ├─ WiFi/MQTT ──→ broker            (đường điều khiển — KHÔNG ĐƯỢC ẢNH HƯỞNG)
   ├─ ESP-NOW  ←── N cảm biến góc
   └─ BLE GATT ──→ [Arduino UNO Q]    (chỉ đọc, chỉ giám sát)
```

Dùng **NimBLE** chứ không Bluedroid: nhẹ hơn khoảng một nửa. BLE **notify một
chiều**, master là peripheral, UNO Q là central. Không nhận lệnh qua BLE — mở
đường ghi là mở một đường điều khiển không xác thực vào máy lạnh.

## File liên quan

**Tạo**
- `FirmWare/esp32-indoor/src/ble-bridge.h` / `.cpp`

**Sửa**
- `platformio.ini` — `lib_deps` NimBLE-Arduino, `build_flags` `-DENABLE_BLE=0`
- `main.cpp` — gọi `BleBridge::begin()` sau khi mọi thứ khác đã lên, và
  `BleBridge::publish()` cùng nhịp telemetry

## Các bước

1. **Đo trước, viết sau.** Thêm NimBLE vào `lib_deps`, khởi tạo trần, in heap
   trước/sau. Nếu heap thấp nhất < 25 KB → dừng, báo lại, chuyển phương án.
2. `ble-bridge.cpp`: một service, một characteristic notify, payload JSON gọn
   (`{"t":25.3,"h":62,"mode":"COOL","sp":25,"n":4}` — `n` = số cảm biến còn sống).
3. Cờ `ENABLE_BLE`; toàn bộ file bọc trong `#if ENABLE_BLE` để bản tắt không tốn
   một byte flash nào.
4. `begin()` trả `bool`; thất bại thì `main.cpp` chỉ in cảnh báo và đi tiếp.
5. Đo tỉ lệ bỏ gói ESP-NOW trong 30 phút, có BLE và không BLE, so sánh.
6. Phía UNO Q: script Python đọc GATT (`bleak`), ghi ra chỗ AI dùng.

## Todo

- [ ] Đo heap với NimBLE khởi tạo trần — **cổng chặn, không qua thì dừng pha**
- [ ] `ble-bridge` sau cờ `ENABLE_BLE`, mặc định tắt
- [ ] `begin()` fail-safe, không chặn boot
- [ ] Đo bỏ gói ESP-NOW có/không BLE
- [ ] Script đọc BLE trên UNO Q
- [ ] Ghi ngân sách bộ nhớ mới vào `ui.h`

## Tiêu chí hoàn thành

- Bật `ENABLE_BLE=1`: heap thấp nhất **≥ 25 KB**, node chạy 24h không khởi động lại.
- Tỉ lệ bỏ gói ESP-NOW tăng **không quá 2%** so với khi tắt BLE.
- Rút UNO Q ra: master không đổi hành vi gì.
- Ép NimBLE lỗi khởi tạo: node vẫn boot, vẫn điều khiển, log nói rõ.

## Rủi ro

| Rủi ro | Giảm thiểu |
|---|---|
| **Không đủ heap** — khả năng thật, không phải giả định | Bước 1 là cổng chặn. Phương án lùi: UART tới UNO Q (không tốn heap, không tranh radio), hoặc UNO Q tự nối MQTT |
| BLE làm ESP-NOW mất gói | Đo trước/sau; quá 2% thì tắt BLE |
| Heap cạn dần rồi sập sau nhiều giờ | Chạy 24h, theo dõi `thấp nhất`, không chỉ `tự do` |
| BLE mở đường điều khiển ngoài kiểm soát | Chỉ notify, **không** characteristic ghi |

## Bảo mật

BLE quảng bá **không mã hoá** trong bán kính vài chục mét: hàng xóm đọc được nhiệt
độ phòng. Chấp nhận được với dữ liệu này. Nhưng vì vậy tuyệt đối **không** đưa
`DEVICE_UUID`, `ORG_ID` hay token vào payload BLE — chỉ số đo trần.

## Tiếp theo

Không chặn pha nào. Làm cuối cùng, sau khi đường điều khiển đa phòng đã ổn định —
để nếu có sự cố thì biết chắc không phải do BLE.
