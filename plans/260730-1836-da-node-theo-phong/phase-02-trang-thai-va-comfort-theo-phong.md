# Pha 02 — Trạng thái Redis + comfort chạy độc lập từng phòng

## Liên kết

- Tổng quan: [plan.md](plan.md) · Phụ thuộc: [pha 01](phase-01-mo-hinh-phong-va-di-tru.md)
- `src/app/services/redis_state_service.py`, `src/app/workers/handlers/telemetry_handler.py`
- `src/app/comfort/comfort_engine.py`, `src/app/workers/command_publisher.py`

## Tổng quan

**Ưu tiên:** Cao. **Trạng thái:** Chưa bắt đầu.

Tách trạng thái và vòng comfort ra theo phòng, và gộp N cảm biến thành một số đo
đại diện cho phòng.

## Nhận định quan trọng

**Đây mới là chỗ N node ghi đè nhau.** `redis_key()` chỉ sinh `bl:{org}:{suffix}`
và `set_indoor_state(org_id, {...})` ghi một bản ghi cho cả hộ. Cắm 4 node đo bốn
góc vào hệ hiện tại thì **node cuối gửi thắng** — không lỗi, không cảnh báo, chỉ
là số đo nhảy loạn.

**`_set_state` dùng `HSET` nên merge chứ không đè cả hash.** Nhờ đó `{t,h}` của
telemetry và `{mode,setpoint}` của state cùng sống trong một khoá. Giữ nguyên tính
chất này khi tách theo phòng.

**Ghi đè thủ công cũng đang theo org.** `bl:{org}:override` — ghi đè ở phòng khách
sẽ tắt tự động cho cả phòng ngủ. Phải tách cùng lúc, nếu không sẽ là lỗi khó tả.

**TTL 90 giây của `set_indoor_state`.** Số đo phòng sống nhờ telemetry 15s. Node
cảm biến góc phòng gửi qua ESP-NOW → master chuyển tiếp; nếu master nghỉ, cả phòng
mất trạng thái sau 90s. Đúng như thiết kế, nhưng cần biết.

## Yêu cầu

**Chức năng**
- Mỗi phòng có trạng thái, quyết định comfort, và ghi đè riêng.
- Số đo phòng = gộp từ master + N cảm biến góc.
- Cảm biến chết không được kéo cả phòng chết theo.

**Phi chức năng**
- Không tăng số vòng Redis mỗi tick quá tuyến tính theo số phòng.

## Kiến trúc

Khoá Redis thêm một tầng:

```
bl:{org}:indoor              ->  bl:{org}:room:{room_id}:indoor
bl:{org}:override            ->  bl:{org}:room:{room_id}:override
bl:{org}:last_switch         ->  bl:{org}:room:{room_id}:last_switch
bl:{org}:outdoor             giữ nguyên — ngoài trời dùng chung cả hộ
bl:{org}:tout_ema            giữ nguyên — cùng lý do
```

Số đo từng node giữ riêng để gộp được và để biết node nào chết:

```
bl:{org}:room:{room_id}:node:{device_uuid}   ->  {t, h, updated_at}   TTL 90s
```

**Gộp:** đọc mọi khoá `node:*` còn sống trong phòng → bỏ giá trị lạc → trung bình.

## File liên quan

**Sửa**
- `redis_state_service.py` — thêm tầng phòng, thêm `set_node_state`/`list_node_states`
- `redis_override_service.py` — khoá theo phòng
- `telemetry_handler.py` — ghi theo node, gộp theo phòng, chạy comfort mỗi phòng
- `command_publisher.py` — `find_ir_code_id` theo `room_id`
- `comfort_preview_service.py`, `live_state_service.py` — nhận `room_id`
- `api/v1/comfort_routes.py` — endpoint nhận `room_id`

**Tạo**
- `src/app/services/room_aggregation_service.py` — chỉ việc gộp N số đo

## Các bước

1. `redis_state_service`: thêm hàm theo phòng, **giữ hàm cũ** làm wrapper trỏ vào
   phòng mặc định cho tới khi mọi caller đã chuyển.
2. `set_node_state(org, room, uuid, {t,h})` TTL 90s — mỗi node một khoá.
3. `room_aggregation_service.aggregate(readings)`:
   - Bỏ node quá hạn (không có khoá = đã hết TTL, khỏi cần kiểm giờ)
   - < 3 node: trung bình đơn thuần
   - ≥ 3 node: bỏ giá trị lệch > 3 °C so với trung vị rồi mới trung bình
   - 0 node sống: trả `None` → comfort bỏ lượt, KHÔNG bịa số
4. `telemetry_handler`: node `sensor`/`indoor` → `set_node_state`; sau đó gộp và
   chạy comfort **cho đúng phòng của node vừa gửi**, không quét cả hộ.
5. Ghi đè + `last_switch` chuyển sang khoá theo phòng.
6. API: `room_id` bắt buộc; nếu hộ chỉ có một phòng thì cho phép bỏ trống và
   backend tự chọn phòng duy nhất (giữ app cũ chạy được tới khi cập nhật).

## Todo

- [ ] Khoá Redis theo phòng + wrapper tương thích ngược
- [ ] `set_node_state` / `list_node_states`
- [ ] `room_aggregation_service` + kiểm thử với ca 0/1/N node
- [ ] `telemetry_handler` chạy comfort theo phòng của node gửi
- [ ] `override` + `last_switch` theo phòng
- [ ] API nhận `room_id`, suy ra được khi hộ có 1 phòng

## Tiêu chí hoàn thành

- Hai phòng, mỗi phòng đặt nhiệt độ khác nhau, giữ độc lập qua nhiều chu kỳ.
- Rút điện 1 trong 4 cảm biến: phòng vẫn chạy bằng 3 node còn lại, log nói rõ.
- Rút hết cảm biến: comfort **bỏ lượt**, không đẩy lệnh dựa trên số cũ.
- Ghi đè phòng A không đụng phòng B.

## Rủi ro

| Rủi ro | Giảm thiểu |
|---|---|
| Một cảm biến hỏng kéo lệch trung bình (đặt cạnh cửa nắng) | Loại giá trị lạc theo trung vị ở bước 3 |
| Số vòng Redis tăng theo số node mỗi tick | Dùng `MGET`/pipeline, không lặp `GET` |
| App cũ gọi API không có `room_id` | Suy ra phòng duy nhất; chỉ lỗi khi hộ có ≥2 phòng |

## Bảo mật

- `room_id` từ client phải kiểm thuộc org của token trước khi đọc/ghi Redis.
- Không cho client tự chọn `device_uuid` khi ghi trạng thái — chỉ worker ghi.

## Tiếp theo

Pha 05 (app/web) cần API pha này. Pha 03/04 (firmware) không phụ thuộc pha này.
