# Công nghệ dự án & định hướng bổ sung

Tài liệu này tổng hợp toàn bộ công nghệ đang dùng trong dự án Aircon (Frontend, Backend,
Hardware, Firmware) và đối chiếu với thư mục [`Research/`](../Research) để chỉ ra những
công nghệ nào nên bổ sung, trong khi **giữ nguyên nguyên tắc topology phần cứng**:

> 1 node **indoor** điều khiển 1 máy lạnh · 1 node **indoor-master** trung chuyển · 1 node
> **outdoor** đo nhiệt độ/độ ẩm ngoài trời · máy lạnh được điều khiển qua **IR
> transmitter/receiver**.

---

## Mục lục

- [1. Frontend — App Flutter](#1-frontend--app-flutter)
- [2. Backend — FastAPI](#2-backend--fastapi)
- [3. Hardware hiện tại](#3-hardware-hiện-tại)
- [4. Firmware hiện tại](#4-firmware-hiện-tại)
- [5. Hạ tầng / Triển khai](#5-hạ-tầng--triển-khai)
- [6. Nghiên cứu (`Research/`) & khoảng trống công nghệ](#6-nghiên-cứu-research--khoảng-trống-công-nghệ)
- [7. Đề xuất bổ sung công nghệ](#7-đề-xuất-bổ-sung-công-nghệ)

---

## 1. Frontend — App Flutter

`app-flutter/`, Dart SDK `^3.12.2`, app **BreezeLink** v1.0.13+14.

| Nhóm | Package | Vai trò |
|---|---|---|
| State management | `provider` | Quản lý state toàn app |
| Networking | `dio` | REST client |
| Realtime | `web_socket_channel` | Kênh `/ws/live` |
| Lưu trữ | `shared_preferences`, `flutter_secure_storage` | Cache thường / credential mã hoá (Keystore, Keychain) |
| Biểu đồ | `fl_chart` | Biểu đồ nhiệt độ, lịch sử |
| Bản đồ | `flutter_map` + `latlong2` | Chọn vị trí nhà (OpenStreetMap, không cần API key) |
| Vị trí | `geolocator` | Lấy toạ độ GPS |
| OTA app | `package_info_plus`, `open_file`, `path_provider` | Tự tải & mở APK cập nhật |
| UI | `google_fonts`, `intl`, `cupertino_icons` | Font, i18n, icon |
| Dev | `flutter_lints`, `flutter_launcher_icons` | Lint, sinh icon |

**Khoảng trống:** chưa có `firebase_messaging` — cập nhật/cảnh báo hiện dựa vào **poll OTA**
thay vì **push notification**.

---

## 2. Backend — FastAPI

`src/app/` — modular monolith Python, phục vụ chung cho web quản trị (SSR) và app (JSON API).

| Lớp | Công nghệ |
|---|---|
| Ngôn ngữ / framework | Python ≥ 3.12, **FastAPI** + **Uvicorn** (ASGI) |
| ORM / migration | **SQLAlchemy 2** (async), **asyncpg**, **Alembic** |
| CSDL | **PostgreSQL 15** |
| Cache / pub-sub | **Redis 7** (live state, đẩy realtime tới WebSocket) |
| MQTT | `aiomqtt` — worker riêng tách khỏi tiến trình API |
| Auth | JWT (`python-jose`), `bcrypt` |
| Validate / config | `pydantic` v2, `pydantic-settings` |
| Templating | `jinja2` — SSR admin, design system "Titanium Command" (CSS thuần, không CDN) |
| Rate limit | `slowapi` |
| Mail | SMTP (Brevo ở production) |
| Test | `pytest`, `pytest-asyncio`, `httpx`, `aiosqlite`, `fakeredis` |

**Kiến trúc thư mục chính:** `api/` (REST v1 + OTA), `web/` (SSR), `services/` (nghiệp vụ
dùng chung), `comfort/` (thuật toán setpoint), `workers/` (MQTT consumer), `models/` (ORM),
`core/` (DB, MQTT client, mailer, security, tenant scoping).

**Thuật toán comfort hiện tại:** mô hình adaptive comfort de Dear & Brager / ASHRAE RP-884 —
`T_neutral = 0.31·T_rm + 17.8`, bù độ ẩm, offset ban đêm, deadband + dwell-time chống chạy
tắt liên tục máy nén. Tham số tinh chỉnh **theo từng khách hàng**, hằng số hồi quy cố định.
**Chưa có yếu tố cá nhân hoá theo sinh trắc học** (giới tính, tuổi, BMI…).

---

## 3. Hardware hiện tại

`FirmWare/` hiện có **2 loại node**, chưa đủ 3 loại theo nguyên tắc topology mong muốn:

| Node đã có | MCU | Vai trò | Trạng thái |
|---|---|---|---|
| `esp32s3-indoor-master` | ESP32-S3 | **indoor-master**: trung chuyển dữ liệu | Có khung ESP-NOW relay + theo dõi online/offline slave; **chưa có IR blaster** |
| `esp8266-outdoor` | ESP8266 (NodeMCU/D1 mini) | **outdoor**: đo T°/RH ngoài trời (DHT11/DHT22) | Hoạt động; hỗ trợ 2 chế độ build `espnow-slave` (đích) và `wifi-direct` (tạm dùng để test) |

> ⚠️ **Còn thiếu node `indoor` riêng cho từng máy lạnh.** Hiện chức năng IR đang dự kiến
> gộp vào chính node indoor-master (comment trong code: *"IR blaster + relay ESP-NOW cho
> node slave: BƯỚC SAU"*), chưa tách thành node indoor độc lập theo đúng nguyên tắc 1
> indoor/1 máy lạnh. Cũng **chưa có bất kỳ thư viện hay code IR transmitter/receiver**
> nào trong `platformio.ini` hiện tại.

---

## 4. Firmware hiện tại

- **Build system:** PlatformIO, `framework = arduino`
- **Thư viện:**
  - `DHT sensor library` + `Adafruit Unified Sensor` — cảm biến nhiệt độ/độ ẩm
  - `ArduinoJson` — encode/decode payload
  - `PubSubClient` — MQTT client
- **Giao tiếp:**
  - **Hiện tại (giai đoạn test):** cả 2 node kết nối trực tiếp WiFi + MQTTS (TLS 8883)
    tới broker EMQX production.
  - **Kiến trúc đích ("Phase B"):** node outdoor là **ESP-NOW slave** phát broadcast một
    struct đóng gói tự mô tả (`AcEspNowPacket`: magic byte, version, `device_uuid[33]`,
    temp/humidity — 43 byte, định nghĩa tại `FirmWare/shared/espnow-message.h`) tới
    indoor-master; indoor-master relay tiếp lên MQTT theo đúng UUID của slave (không cần
    bảng ánh xạ MAC↔UUID) và tự quản lý heartbeat/online-offline cho slave vì slave không
    có phiên MQTT/LWT riêng.
  - Chuẩn topic MQTT dùng chung với backend: `bl/{org_id}/{device_uuid}/{telemetry|status|cmd}`,
    client-id `breezelink_{DEVICE_UUID}`, username = UUID, password = token cấp riêng theo
    thiết bị.

---

## 5. Hạ tầng / Triển khai

| Thành phần | Công nghệ |
|---|---|
| Container | Docker Compose (`docker-compose.yml` dev, `docker-compose.vps.yml` prod) |
| MQTT broker | Dev: `eclipse-mosquitto:2` · Prod: `emqx/emqx:5.8` (self-host) hoặc EMQX Serverless (cloud) |
| Reverse proxy / TLS | **Cloudflare Tunnel** — cổng duy nhất vào hệ thống, không port nào mở `0.0.0.0` |
| Deploy | `scripts/deploy.sh` — rsync `src/`, rebuild container, health-check, thủ công qua SSH |
| CI/CD | **Chưa có** — không có GitHub Actions/GitLab CI nào trong repo |
| Secrets | `.env` / `docker/.env` bị `.gitignore`, chỉ commit `.env.example` |

---

## 6. Nghiên cứu (`Research/`) & khoảng trống công nghệ

Thư mục `Research/` gồm 3 file, xoay quanh **ASHRAE 55 – Adaptive Thermal Comfort** (đối
chiếu TCVN 5687:2024, TCVN 13521:2022):

1. **`Nhiệt Độ Thích Ứng ASHRAE 55.pdf`** — tài liệu nghiên cứu gốc.
2. **`Adaptive.html`** / **`Adaptive_V2.html`** — infographic đề xuất mô hình comfort
   **cá nhân hoá theo sinh trắc học** (giới tính, độ tuổi, BMI, vùng miền Bắc/Trung/Nam) và
   kiến trúc hệ thống thế hệ sau gồm:
   - **Cảm biến mm-Wave radar** — định vị người/tư thế (độ chính xác < 0.5 m), ước lượng
     tốc độ trao đổi chất (BMR) không cần thiết bị đeo.
   - **MAPPO** (Multi-Agent Proximal Policy Optimization — reinforcement learning) — tối
     ưu tần số máy nén + hướng gió, tuyên bố đạt ổn định nhiệt độ ±0.21 °C và tiết kiệm
     ~30.7% năng lượng so với điều khiển on/off thông thường.

**Kết luận:** đây là hướng nghiên cứu thế hệ sau, **chưa được triển khai** trong code hiện
tại — thuật toán comfort hiện chỉ dùng running-mean + độ ẩm, cảm biến chỉ có DHT11/DHT22,
chưa có thành phần ML/RL nào.

---

## 7. Đề xuất bổ sung công nghệ

Các đề xuất dưới đây **tương thích với nguyên tắc topology hiện tại** (1 indoor/1 máy lạnh,
1 indoor-master trung chuyển, 1 outdoor đo môi trường ngoài, điều khiển qua IR):

| # | Hạng mục | Đề xuất | Vị trí bổ sung |
|---|---|---|---|
| 1 | **Node indoor (còn thiếu)** | Tạo firmware riêng cho node indoor (1 node/1 máy lạnh), dùng ESP32/ESP8266 + thư viện IR (`IRremoteESP8266` cho ESP8266, hoặc fork `IRremote`/`IRremoteESP32` cho ESP32) để phát/nhận tín hiệu điều khiển máy lạnh | `FirmWare/espXX-indoor/` (mới), giao tiếp ESP-NOW với indoor-master |
| 2 | **Hoàn thiện ESP-NOW** | Chuyển hẳn outdoor + indoor sang chế độ ESP-NOW slave (bỏ `wifi-direct` tạm thời), indoor-master giữ vai trò gateway MQTT duy nhất | `esp32s3-indoor-master`, các node slave |
| 3 | **Cảm biến mm-Wave radar** | Thêm module radar (vd. Hi-Link LD2410/LD2450, Seeed MR60BHA2) vào node indoor để định vị người dùng, làm input cá nhân hoá | Phần cứng node indoor; dữ liệu gửi kèm telemetry qua MQTT |
| 4 | **Thuật toán comfort cá nhân hoá** | Mở rộng `src/app/comfort/` với module tính target temp/humidity theo giới tính/tuổi/BMI/vùng miền (như `Adaptive.html`), **cộng thêm** vào engine hiện có chứ không thay thế | `src/app/comfort/personalization.py` (mới) |
| 5 | **Điều khiển bằng RL (MAPPO)** | Thử nghiệm engine điều khiển reinforcement learning như lựa chọn thay thế rule-based hiện tại, benchmark trước khi đưa vào production, bật/tắt qua config flag | Module riêng trong `src/app/comfort/` |
| 6 | **Push notification** *(phụ, ngoài Research)* | Thêm `firebase_messaging` cho app Flutter thay cơ chế poll OTA/cảnh báo | `app-flutter/` |
| 7 | **CI/CD** *(phụ, ngoài Research)* | Thêm pipeline build/test/deploy tự động (vd. GitHub Actions) | Repo root |

---

<sub>Sinh ra và duy trì với sự hỗ trợ của Claude Code.</sub>
