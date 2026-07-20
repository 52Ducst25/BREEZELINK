# Aircon — Firmware node (bản TEST)

Hai node cảm biến cho **1 nhà** (khách hàng *Khách hàng*):

| Thư mục | Chip | Loại node | Cổng (máy này) | Hiện trên app/web |
|---|---|---|---|---|
| `esp32s3-indoor-master/` | ESP32‑S3 | **Trong nhà** (master) | COM6 (CH343) | "Trong nhà" |
| `esp8266-outdoor/` | ESP8266 | **Ngoài trời** | COM5 (CP210x) | "Ngoài trời" |

> **Bản TEST:** cả 2 node nối **WiFi + MQTT (EMQX, TLS 8883) trực tiếp**, chỉ đọc DHT11 rồi đẩy nhiệt độ/độ ẩm lên cloud — để xác nhận phần cứng + đường truyền + nhãn indoor/outdoor. Bước sau (Phase B): node ngoài trời gửi về master qua **ESP‑NOW**, master gắn IR điều khiển máy lạnh.

---

## 1. Cắm chân DHT11

DHT11 có 2 loại: **module 3 chân** (đã có trở kéo — nối thẳng) hoặc **cảm biến 4 chân rời** (mắc thêm điện trở 10kΩ giữa DATA và 3V3).

### ESP32‑S3 (node trong nhà)
```
   DHT11                ESP32-S3
  ┌───────┐
  │  +  / VCC │──────── 3V3
  │  S  / DATA│──────── GPIO4   (chân silk "4" / "G4")
  │  -  / GND │──────── GND
  └───────┘
```

### ESP8266 NodeMCU/Wemos (node ngoài trời)
```
   DHT11                NodeMCU (ESP8266)
  ┌───────┐
  │  +  / VCC │──────── 3V3
  │  S  / DATA│──────── D2   (= GPIO4)
  │  -  / GND │──────── GND (G)
  └───────┘
```

⚠️ Cấp nguồn DHT11 bằng **3V3**, không dùng 5V (chân DATA vào GPIO 3.3V).
Muốn đổi chân: sửa `DHT_PIN` trong `src/config.h`.

---

## 2. Điền cấu hình (`src/config.h` mỗi node)

Lấy giá trị từ **web admin → Khách hàng "Khách hàng" → mở từng node → mục "Nạp firmware"**:

| Điền vào config.h | Lấy ở panel | Ghi chú |
|---|---|---|
| `ORG_ID` | ô ORG_ID | **giống nhau** cả 2 node |
| `DEVICE_UUID` | ô DEVICE_UUID | **khác nhau** mỗi node |
| `MQTT_USERNAME` | = DEVICE_UUID | |
| `MQTT_PASSWORD` | ô MQTT_PASSWORD | **khác nhau** mỗi node |
| `MQTT_HOST` | ô MQTT_HOST | **giống nhau**, dạng `*.emqxsl.com` |
| `MQTT_PORT` | 8883 | cố định |

WiFi (`WIFI_SSID`/`WIFI_PASSWORD`): điền mạng tại nơi lắp node.

---

## 3. Build & nạp (PlatformIO)

```bash
# Node trong nhà (ESP32-S3) — cắm cổng COM6
cd esp32s3-indoor-master
pio run -t upload --upload-port COM6
pio device monitor -p COM6 -b 115200      # xem log

# Node ngoài trời (ESP8266) — cắm cổng COM5
cd esp8266-outdoor
pio run -t upload --upload-port COM5
pio device monitor -p COM5 -b 115200
```

Log chạy đúng sẽ thấy:
```
WiFi -> "TEN_WIFI" .... OK  IP=192.168.x.x
MQTT ... connected
[telemetry] t=30.0°C h=60% -> da gui
```

---

## 4. Kiểm tra đồng bộ

- **Web:** node hiện **"Trực tuyến"**; mục *Nhiệt độ/Độ ẩm mới nhất* + biểu đồ cập nhật.
- **App:** thẻ thiết bị hiện *"Nhiệt độ trong nhà"* (ESP32) và *"Nhiệt độ ngoài trời"* (ESP8266).

---

## 5. Xử lý lỗi thường gặp

| Log | Nguyên nhân | Cách xử lý |
|---|---|---|
| `MQTT ... that bai rc=4` | Sai username/password | Copy lại DEVICE_UUID/MQTT_PASSWORD từ panel |
| `MQTT ... that bai rc=5` | Broker chưa cấp quyền cho device | Cần nạp cặp user/token của device vào EMQX (seed/sync auth) — xem *Câu hỏi mở* |
| `rc=-2` lặp mãi | TLS/mạng | Kiểm tra MQTT_HOST đúng, WiFi có internet; ESP8266 thiếu RAM → thử `net.setBufferSizes(512,512)` |
| `DHT11 doc loi (NaN)` | Sai chân/nguồn DHT11 | Kiểm lại VCC 3V3 + DATA đúng chân + (loại 4 chân) trở kéo 10k |
| Serial không ra chữ (S3) | Cắm nhầm cổng USB‑native | Cắm cổng "UART", hoặc bật `-D ARDUINO_USB_CDC_ON_BOOT=1` trong `platformio.ini` |

---

## Câu hỏi mở
- **Auth EMQX cho device mới:** backend lưu `mqtt_token` per‑device; cần xác nhận cơ chế nạp cặp (username=DEVICE_UUID, password=token) vào EMQX built‑in DB đã tự chạy khi tạo device trên web chưa. Nếu chưa, `rc=5` sẽ xuất hiện và phải seed thủ công.
