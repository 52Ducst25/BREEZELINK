# Concept và payload khi tích hợp Arduino UNO Q + Edge AI

**Ngày:** 2026-07-29

Ràng buộc số một, chi phối toàn bộ thiết kế dưới đây: **không phá vỡ hợp đồng dây
hiện tại.** Tiền tố `bl/` và client-id `breezelink_` đã nằm trong firmware của các
node đang chạy ngoài thực địa ([mqtt_naming.py:21-27](../src/app/utils/mqtt_naming.py#L21-L27)).
Đổi tên là mọi thiết bị đang chạy im lặng biến mất khỏi hệ — broker vẫn nhận, không
báo lỗi gì, chỉ là wildcard của worker thôi khớp.

Nên UNO Q vào theo hướng **mở rộng**, không thay thế: thêm `kind` mới, giữ nguyên bốn
`kind` cũ.

---

## 1. Concept — UNO Q đứng ở đâu

### Hôm nay

```
[ESP32 ngoài trời] --ESP-NOW--> [ESP32 trong nhà] --MQTT--> [EMQX] --> [worker] --> [Postgres]
                                 (đo · IR · master ESP-NOW · màn hình)      |
                                                                    thuật toán comfort
                                        <---------- cmd ------------------- |
```

Bốn vai trò dồn lên một con ESP32, và **mọi quyết định phải đi vòng qua Internet**.

### Khi có UNO Q

```
[ESP32 ngoài trời] --ESP-NOW--\
[ESP32 trong nhà: màn hình]  --+--> [ UNO Q ]  --MQTT (bl/...)--> [EMQX] --> [worker]
[PIR / mmWave]  --------------/     ├── MCU: nhịp cảm biến, ESP-NOW, phát IR 38 kHz
                                    └── Linux: dự báo · hiện diện · comfort cục bộ
                                              ↑ model artifact               ↓
                                         [cloud huấn luyện lại]      [Postgres + audit]
```

Ba điều thay đổi, và chỉ ba:

1. **Vòng quyết định đóng lại trong nhà.** Comfort chạy trên Linux của UNO Q. Mất
   Internet thì máy lạnh vẫn điều khiển đúng; chỉ việc học và đồng bộ dừng.
2. **`T_rm` chuyển từ quá khứ sang dự báo.** Đầu ra mô hình cắm vào đúng chỗ `T_rm`
   đang đứng trong công thức. Hằng số hồi quy không đụng tới.
3. **ESP32 trong nhà nhẹ đi**, chỉ còn màn hình và cảm ứng — hết cảnh một chip gánh
   bốn việc.

### Nguyên tắc phân tầng

| Tầng | Nhịp | Làm | KHÔNG làm |
|---|---|---|---|
| ESP32 / cảm biến | ms → s | Đọc cảm biến, phát IR, giao diện, ESP-NOW | Suy luận, lưu lịch sử dài |
| **UNO Q (biên)** | s → phút | Dự báo, hiện diện, comfort, hàng đợi khi mất mạng | Huấn luyện lại từ đầu |
| Đám mây | giờ → ngày | Huấn luyện lại, tổng hợp nhiều nhà, kiểm toán | Quyết định thời gian thực |

---

## 2. Payload

### 2.1 Giữ nguyên — `telemetry` (node → cloud)

Các trường đã có trong bảng `telemetry` ([models/telemetry.py](../src/app/models/telemetry.py)):
`temp`, `humidity`, `rssi`, `batt` (chỉ node ngoài trời), `watt` (khi có PZEM).

```jsonc
// bl/{org_id}/{device_uuid}/telemetry     QoS1, retain off
{ "ts": 1785312000, "temp": 29.6, "humidity": 51.0, "rssi": -62, "batt": 3.94, "watt": 812.5 }
```

UNO Q **không đổi** gói này. Nó chuyển tiếp nguyên vẹn để dữ liệu thô lên cloud vẫn
là dữ liệu thô — nếu biên sửa số trước khi gửi thì mất luôn khả năng huấn luyện lại
trung thực.

### 2.2 Mới — `forecast` (biên → cloud)

Mục đích: **chấm điểm mô hình sau này**. Ghi lại dự báo *tại thời điểm dự báo*, để
sau đó đối chiếu với số đo thật. Không có gói này thì không bao giờ chứng minh được
mô hình đúng hay sai.

```jsonc
// bl/{org_id}/{device_uuid}/forecast      QoS1, retain off
{
  "ts": 1785312000,
  "model": "trm-gp-clue",         // tên mô hình
  "ver": "2026.07.3",             // phiên bản artifact đang chạy
  "t_rm_now": 30.2,               // T_rm theo cách tính CŨ (EMA quá khứ) — mốc để so
  "horizon": [
    { "min": 15, "t_rm": 30.8, "sigma": 0.31 },
    { "min": 30, "t_rm": 31.4, "sigma": 0.52 },
    { "min": 60, "t_rm": 32.1, "sigma": 0.94 }
  ],
  "used": 30                      // chân trời nào thực sự được dùng để quyết định
}
```

`sigma` là bắt buộc, không phải trang trí: Gaussian Process cho ra cả độ bất định, và
**quy tắc an toàn là khi `sigma` vượt ngưỡng thì bỏ dự báo, quay về `t_rm_now`**. Một
mô hình không biết mình đang đoán mò thì nguy hiểm hơn không có mô hình.

### 2.3 Mới — `presence` (biên → cloud)

```jsonc
// bl/{org_id}/{device_uuid}/presence      QoS1, retain ON
{
  "ts": 1785312000,
  "state": "occupied",            // occupied | vacant | unknown
  "src": "pir",                   // pir | mmwave | schedule | geofence
  "conf": 0.86,
  "last_motion_s": 45,            // giây kể từ chuyển động cuối
  "targets": [                    // chỉ có với mmWave; PIR bỏ trống
    { "x_cm": -40, "y_cm": 180 }
  ]
}
```

`retain ON` vì đây là **trạng thái**, không phải sự kiện: worker khởi động lại phải
biết ngay trong phòng có người hay không, chứ không chờ chuyển động kế tiếp.

`src` bắt buộc có để về sau đo được nguồn nào đáng tin — PIR sẽ báo `vacant` sai khi
người ngồi yên, và chỉ có trường này mới truy ra được.

### 2.4 Mới — `model` (cloud → biên)

```jsonc
// bl/{org_id}/{device_uuid}/model         QoS1, retain ON
{
  "ver": "2026.07.4",
  "url": "https://.../trm-gp-2026.07.4.tar.zst",
  "sha256": "9f2c…",
  "size": 184320,
  "rollback_to": "2026.07.3",     // quay về bản này nếu nạp lỗi
  "min_edge": "1.2.0"             // phiên bản phần mềm biên tối thiểu
}
```

`retain ON` để node vừa cắm điện là biết ngay phải chạy bản nào. `sha256` và
`rollback_to` là bắt buộc — đẩy nhầm một mô hình hỏng xuống toàn bộ thiết bị mà không
có đường lùi là sự cố không cứu được từ xa.

### 2.5 Mở rộng — `state` (biên → cloud)

Gói `state` đang có thêm phần giải thích quyết định, để `comfort_log` ghi được **vì
sao**, không chỉ **cái gì**:

```jsonc
// bl/{org_id}/{device_uuid}/state         QoS1, retain ON
{
  "ts": 1785312000,
  "power": "on", "mode": "COOL", "temp": 27, "fan": 3, "swing_v": "auto",
  "why": {
    "src": "edge",                // edge | cloud | manual — ai ra quyết định này
    "t_rm": 31.4, "forecast": true,
    "t_neutral": 27.5,
    "humid_penalty": -0.4,
    "night_offset": 0.5,
    "presence": "occupied",
    "clamped": false,
    "dwell_block": false
  }
}
```

Trường `src` là chốt an toàn về trách nhiệm: khi mô hình và người vận hành mâu thuẫn,
đây là chỗ duy nhất truy ra được ai đã đặt nhiệt độ đó.

---

## 3. Thứ tự triển khai

| Bước | Nội dung | Phần cứng mới |
|---|---|---|
| 1 | Thêm `presence` + PIR trên một node ESP-NOW; luật "vắng 15 phút thì tắt" | **PIR ~vài chục nghìn** |
| 2 | UNO Q chạy song song, chỉ *quan sát* và publish `forecast` — chưa cầm quyền | 1 bo UNO Q |
| 3 | Đối chiếu `forecast` với số đo thật, đủ tin thì chuyển quyết định xuống biên | – |
| 4 | Thêm `model` + kênh huấn luyện lại; mmWave cho hướng gió | mmWave |

Bước 1 **không cần UNO Q** — chạy được ngay trên phần cứng hiện tại, vì luật vắng
người chỉ là vài phép so sánh. Đây là hạng mục rẻ nhất và về tiền nhanh nhất.

Bước 2 quan trọng ở chỗ **chạy song song**: UNO Q dự báo và ghi lại, nhưng chưa được
điều khiển gì. Đủ dữ liệu chứng minh dự báo đúng rồi mới trao quyền — không đặt cược
uy tín sản phẩm vào một mô hình chưa ai chấm điểm.

---

## Việc chưa chốt

1. Ngưỡng `sigma` bao nhiêu thì bỏ dự báo quay về `t_rm_now` — phải đo trên dữ liệu
   thật rồi mới đặt, không đặt bằng cảm tính.
2. UNO Q chạy MQTT client đẩy thẳng lên EMQX, hay chạy luôn một broker biên rồi cầu
   nối lên? Bản cầu nối chịu mất mạng tốt hơn nhưng thêm một thành phần phải bảo trì.
3. Firmware ESP32 trong nhà bỏ vai trò master ESP-NOW ở phiên bản nào — cần phát hành
   đồng bộ với UNO Q, không thì cả hai cùng làm master.
