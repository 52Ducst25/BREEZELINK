# Pha 06 — Đổi tên `indoor` → room master

## Liên kết

- Tổng quan: [plan.md](plan.md) · Phụ thuộc: pha 01–05 xong hết
- `src/app/utils/mqtt_naming.py`, `src/app/models/enums.py`
- `FirmWare/README.md`, `FirmWare/Interface/README.md`

## Tổng quan

**Ưu tiên:** Thấp — thuần đặt tên. **Trạng thái:** Chưa bắt đầu.

Sau khi có nhiều phòng, chữ `indoor` không còn tả đúng vai trò: node đó là **chủ
của một phòng** (IR + WiFi + gom cảm biến + BLE), còn các node góc phòng cũng nằm
"trong nhà". Bạn đề xuất `room_x_master` — pha này chốt cách gọi.

## Nhận định quan trọng — đọc trước khi động vào

**Có thứ đổi tên được, có thứ TUYỆT ĐỐI KHÔNG.** Ranh giới đã được ghi rõ trong
`mqtt_naming.py:21-27` và đã có một lần suýt hỏng trong phiên làm việc trước:

| Thứ | Đổi được? | Vì sao |
|---|---|---|
| Nhãn giao diện, tài liệu, tên biến trong code | **Có** | Thuần nội bộ |
| `NodeType.indoor` (giá trị enum trong DB) | **Có, nhưng cần migration** | Là dữ liệu, không phải wire |
| Tiền tố topic `bl/` | **KHÔNG** | Hợp đồng dây với firmware **đã nạp trên node đang bán** |
| Client-id `breezelink_{uuid}` | **KHÔNG** | Cùng lý do |
| Phân đoạn topic `{kind}` (`telemetry`/`cmd`/…) | **KHÔNG** | Cùng lý do |

Đổi nhóm "KHÔNG" thì broker **vẫn nhận** topic cũ, **không báo lỗi gì**, và
wildcard của worker chỉ đơn giản thôi khớp — mọi thiết bị đang chạy im lặng mất
traffic. Đây là kiểu hỏng tệ nhất.

**Bài học vừa xảy ra:** trong phiên trước tôi đã đổi tên logger theo hướng ngược
với hướng dự án (`breezelink.*` → `aircon.*`) rồi phải bỏ commit đó khi rebase.
Trước khi đổi bất cứ tên gì, kiểm `origin/master` xem dự án đang đi hướng nào.

**`node_type` KHÔNG nằm trong topic.** `mqtt_naming.py:8-11` ghi rõ: phân đoạn
thứ ba là `device_uuid`, còn `node_type` tra từ hàng `devices`. Nhờ vậy đổi giá
trị enum **không** đụng wire contract. Đây là chỗ thoáng duy nhất.

## Yêu cầu

- Tên mới nhất quán ở: enum DB, code backend, firmware, app, web, tài liệu.
- Không đổi một byte nào của topic/client-id.
- Hộ đang chạy không cần nạp lại firmware vì việc đổi tên này.

## Kiến trúc — chốt cách gọi

| Cũ | Mới | Ghi chú |
|---|---|---|
| `NodeType.indoor` | `NodeType.room_master` | Có migration đổi giá trị enum |
| `NodeType.sensor` | giữ nguyên | Đặt ở pha 01 |
| `NodeType.outdoor` | giữ nguyên | Vẫn đúng nghĩa |
| Thư mục `esp32-indoor/` | `esp32-room-master/` | Đổi bằng `git mv` |
| Nhãn "Trong nhà" trên app/web | "Máy chủ phòng — {tên phòng}" | |

Không dùng `room_x_master` có chỉ số trong tên loại: chỉ số phòng đã nằm ở
`room_id`, nhét vào tên loại là nhân đôi nguồn sự thật.

## File liên quan

**Sửa**
- `src/app/models/enums.py`, `src/app/utils/mqtt_naming.py` — **hai bản sao enum**
- `src/app/alembic/versions/{stamp}_rename_indoor_to_room_master.py`
- Mọi chỗ `NodeType.indoor` / `"indoor"` trong `services/`, `workers/`
- `FirmWare/esp32-indoor/` → `FirmWare/esp32-room-master/`
- `app-flutter/lib/` — nhãn hiển thị
- `README.md`, `FirmWare/README.md`, `FirmWare/Interface/README.md`, `docs/`

## Các bước

1. `grep -rn "indoor"` toàn repo, phân loại từng chỗ theo bảng "đổi được / không".
2. Migration đổi giá trị enum `node_type` trong Postgres (`ALTER TYPE ... RENAME
   VALUE` — Postgres 10+ hỗ trợ, không cần bảng tạm).
3. Đổi hai bản sao enum **cùng một commit** — lệch nhau là worker bỏ gói im lặng.
4. `git mv` thư mục firmware; sửa đường dẫn trong `.github/`, `scripts/`, tài liệu.
5. Cập nhật nhãn app/web; bump build; phát hành OTA.
6. Cập nhật `docs/system-architecture.md` + `docs/codebase-summary.md`.

## Todo

- [ ] Kiểm kê `grep -rn "indoor"` và phân loại
- [ ] Migration `ALTER TYPE node_type RENAME VALUE`
- [ ] Đổi CẢ HAI bản sao enum trong một commit
- [ ] `git mv` thư mục firmware + sửa đường dẫn
- [ ] Nhãn app/web + OTA
- [ ] Cập nhật `docs/`

## Tiêu chí hoàn thành

- `grep -rn "NodeType.indoor"` không còn kết quả.
- `grep -rn "bl/"` và `breezelink_` **không đổi một dòng nào** so với trước pha.
- Hộ đang chạy: node **chưa nạp lại firmware** vẫn gửi/nhận bình thường.
- `alembic downgrade` trả lại giá trị enum cũ.

## Rủi ro

| Rủi ro | Giảm thiểu |
|---|---|
| Đổi nhầm sang wire contract | Bảng phân loại ở trên là bắt buộc đọc; tiêu chí hoàn thành kiểm bằng `grep` |
| Hai bản sao enum lệch nhau | Cùng một commit; chú thích chéo ở cả hai file |
| Đổi tên ngược hướng dự án | Kiểm `origin/master` trước — đã từng mắc |

## Bảo mật

Không có bề mặt mới. Migration không được log `mqtt_token`.

## Tiếp theo

Kết thúc kế hoạch. Sau pha này, cập nhật `docs/development-roadmap.md` và
`docs/project-changelog.md` theo `documentation-management.md`.
