# Pha 01 — Mô hình phòng + di trú dữ liệu

## Liên kết

- Tổng quan: [plan.md](plan.md)
- Model hiện tại: `src/app/models/device.py`, `src/app/models/ir_code.py`
- Migration mẫu: `src/app/alembic/versions/260714_2105_create_aircon_domain.py`

## Tổng quan

**Ưu tiên:** Cao nhất — chặn toàn bộ các pha còn lại.
**Trạng thái:** Chưa bắt đầu.

Đưa khái niệm "phòng" vào miền dữ liệu, và tách `ir_codes` khỏi phạm vi hộ.

## Nhận định quan trọng

**`ir_codes` là chỗ hỏng nặng nhất, không phải bảng `devices`.** Ràng buộc
`UniqueConstraint("org_id","mode","temp")` (`ir_code.py:39`) nghĩa là mỗi hộ chỉ
giữ được MỘT mã cho mỗi cặp (chế độ, nhiệt độ). Hai phòng dùng hai máy khác hãng
thì mã của phòng thứ hai **không chèn được** — lỗi ràng buộc, không phải sai số
liệu. Đây là thứ phải đổi trước tiên.

**`Device.location` không dùng lại được.** Nó là `String(200)` tự do, không khoá
ngoại, không nhóm. Gõ lệch một ký tự là hai node cùng phòng thành hai phòng.

**Backend đã hé cửa sẵn.** `telemetry_service.get_device_by_org_and_node` đã bỏ
`scalar_one_or_none`, giờ lấy node cũ nhất kèm cảnh báo *"Per-room control is
Phase 2 — the others are not controlled"*. Đúng pha này.

## Yêu cầu

**Chức năng**
- Một hộ có N phòng; mỗi phòng có đúng 1 master (IR) và 0..N node cảm biến.
- Mã IR học ở phòng nào chỉ dùng cho phòng đó.
- Hộ đang chạy phải tiếp tục hoạt động sau khi deploy, không cần thao tác tay.

**Phi chức năng**
- Migration một chiều, có đường lùi rõ ràng (`downgrade`).
- Không đổi wire contract MQTT ở pha này (topic giữ nguyên).

## Kiến trúc

```
organizations 1──N rooms 1──N devices
                    │
                    └──N ir_codes   (đổi từ org_id sang room_id)
```

`NodeType` thêm giá trị thứ ba:

| Giá trị | Vai trò | Radio |
|---|---|---|
| `outdoor` | Đo ngoài trời | ESP-NOW (hoặc WiFi dự phòng) |
| `indoor` | Master phòng: đo + IR + màn + WiFi/MQTT | WiFi + ESP-NOW + BLE |
| `sensor` | **MỚI** — chỉ đo, đặt ở góc phòng | Chỉ ESP-NOW |

## File liên quan

**Sửa**
- `src/app/models/device.py` — thêm `room_id` FK
- `src/app/models/ir_code.py` — `org_id` → `room_id`, đổi unique constraint
- `src/app/models/enums.py` — thêm `NodeType.sensor`
- `src/app/utils/mqtt_naming.py` — `NodeType` bản sao ở đây cũng phải thêm
- `src/app/services/ir_code_service.py` — mọi truy vấn đổi sang `room_id`
- `src/app/services/device_service.py` — tạo/sửa node kèm phòng

**Tạo**
- `src/app/models/room.py`
- `src/app/services/room_service.py`
- `src/app/alembic/versions/{stamp}_add_rooms_and_scope_ir_codes.py`

## Các bước

1. Tạo model `Room`: `id`, `org_id` FK, `name`, `created_at`. Unique `(org_id, name)`.
2. Thêm `Device.room_id` — **nullable trước**, để migration điền rồi mới siết.
3. Thêm `NodeType.sensor` ở cả `models/enums.py` lẫn `utils/mqtt_naming.py` (hai
   bản sao, lệch nhau là worker bỏ gói im lặng).
4. Migration, đúng thứ tự này:
   - `create table rooms`
   - Mỗi org: tạo một hàng `rooms` tên `"Phòng chính"`
   - `add column devices.room_id nullable` → `UPDATE` gán hết vào phòng đó
   - `add column ir_codes.room_id nullable` → `UPDATE` gán theo `org_id`
   - Bỏ `uq_ircode_snap`, tạo `UniqueConstraint("room_id","mode","temp")`
   - Siết cả hai cột thành `NOT NULL`
   - Bỏ cột `ir_codes.org_id`
5. Đổi `ir_code_service` sang khoá theo `room_id`; `find_ir_code_id` nhận `room_id`.
6. `room_service`: CRUD + `get_master(room_id)` trả node `indoor` của phòng.

## Todo

- [ ] Model `Room` + `Device.room_id`
- [ ] `NodeType.sensor` ở CẢ HAI chỗ khai enum
- [ ] Migration tạo phòng mặc định + gán dữ liệu cũ
- [ ] Đổi `ir_codes` sang `room_id` + unique mới
- [ ] `ir_code_service` / `device_service` theo `room_id`
- [ ] `room_service` mới
- [ ] Chạy `alembic upgrade head` trên bản sao DB thật trước khi deploy

## Tiêu chí hoàn thành

- Hộ đang chạy: sau migration vẫn điều khiển được, mã IR cũ vẫn phát được.
- Tạo phòng thứ hai + học "COOL 25" cho phòng đó **không** đụng mã phòng một.
- `alembic downgrade` chạy sạch, không mất mã IR.

## Rủi ro

| Rủi ro | Giảm thiểu |
|---|---|
| Migration làm mất mã IR của khách đang dùng | Thử trên bản sao DB thật (dump từ VPS) trước. `downgrade` phải khôi phục được `org_id` từ `rooms.org_id` |
| Quên đồng bộ `NodeType` giữa hai file | Ghi chú chéo ở cả hai; worker bỏ gói **không báo lỗi** nếu lệch |
| `redis_ir_cache` giữ id cũ sau khi đổi khoá | Id mã không đổi nên cache vẫn đúng; chỉ cần không đổi `ir_codes.id` |

## Bảo mật

- `room_id` phải kiểm thuộc đúng org trước mọi thao tác — cùng luật với
  `get_device_for_topic`: đừng tin org lấy từ đường dẫn/topic.
- Migration không được ghi log nội dung `mqtt_token`.

## Tiếp theo

Pha 02 (trạng thái + comfort theo phòng) chỉ bắt đầu được khi `room_id` đã
`NOT NULL` trên cả `devices` lẫn `ir_codes`.
