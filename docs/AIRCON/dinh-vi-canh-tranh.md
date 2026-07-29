# Định vị cạnh tranh — Aircon so với thị trường

**Ngày:** 2026-07-29 · Nguồn: trang chủ và báo chí công khai của từng bên (liệt kê cuối file).

Nguyên tắc của file này: **chỉ ghi cái tra được**. Chỗ nào mình chưa hơn thì nói
thẳng — sếp biết BenKon, và một bảng so sánh giấu điểm yếu bị bóc trong ba mươi
giây là mất cả bài.

---

## 1. Bốn đối thủ, ba mô hình kinh doanh khác nhau

### BenKon (Việt Nam) — đối thủ gần nhất về thị trường

| Mục | Nội dung |
|---|---|
| Khách hàng | **Chỉ doanh nghiệp** — 500+ cửa hàng: chuỗi bán lẻ, phòng gym, nhà thuốc, cao ốc, trường đại học |
| Sản phẩm | "AC Lock" — khoá máy lạnh theo lịch, lắp chuyên nghiệp, không phải thay máy |
| Giá | AC Lock **từ 3.000.000 đ/máy** · Energy Health Check **từ 25.000.000 đ/điểm** · Cooling as a Service **từ 2,5 % giá trị tài sản/tháng** |
| Cam kết | **Tiết kiệm 10–20 %**, hoàn tiền 30 ngày |
| Khác | Có bằng sáng chế · tương thích >99 % model máy lạnh · gọi vốn 500.000 USD · quán quân Startup Wheel 2022 · Google for Startups |

**Cái họ chưa làm:** không nhắm hộ gia đình. Cơ chế cốt lõi là **khoá theo lịch và
giám sát**, không phải tính nhiệt độ dễ chịu theo người. Trang chủ **không nêu AI**
như một tính năng.

### Ambi Climate 2 (quốc tế) — đối thủ gần nhất về thuật toán

Giá ~129 USD. Dùng máy học suy ra sở thích của người dùng qua phản hồi thích/không
thích. Đây là bên duy nhất thật sự làm "AI tiện nghi".

**Điểm yếu quyết định — và là chỗ Aircon mạnh nhất:** Ambi **không có cảm biến ngoài
trời**. Nhiệt độ ngoài trời nó lấy từ **dữ liệu thời tiết trực tuyến**. Có nhiều phản
hồi về hoạt động thiếu ổn định.

### Sensibo Sky / Air Pro (quốc tế)

Điều khiển + đo chất lượng không khí (TVOC, CO₂) trên bản Air Pro, nạp vào tính năng
Climate React. **Climate React là luật ngưỡng** — "nếu nhiệt độ vượt X thì làm Y".
**Phải trả thuê bao Sensibo Plus** mới có geofencing, lịch nhiều người, và lịch sử dữ
liệu.

### Cielo Breez (quốc tế)

~130 USD, có nút bấm trên thiết bị, **không thu phí tháng**, quản lý 20 khu vực,
Mode Conflict Control.

---

## 2. Bảng so sánh

| | **Aircon** | BenKon | Ambi | Sensibo | Cielo |
|---|---|---|---|---|---|
| **Cảm biến ngoài trời VẬT LÝ tại công trình** | **✓ node ESP32 riêng** | – | ✗ API thời tiết | ✗ | ✗ |
| Mô hình tiện nghi thích ứng ASHRAE RP-884 | **✓ có trung bình trượt, bù ẩm, lịch đêm, kẹp** | ✗ khoá theo lịch | ~ học sở thích | ✗ luật ngưỡng | ✗ |
| Nhật ký kiểm toán từng quyết định | **✓ `comfort_log` + biến trung gian** | ✗ | ✗ | chỉ bản trả phí | ✗ |
| Màn cảm ứng trên tường, dùng không cần điện thoại | **✓** | ✗ | ✗ | ✗ | ✓ |
| Học mã hồng ngoại tại chỗ | **✓** | ✓ | ✓ | ✓ | ✓ |
| Nhiều tổ chức, có web cho đại lý | **✓ topic tách theo org** | ✓ | ✗ | ✗ | ~ |
| Không thuê bao | **✓** | dịch vụ theo tháng | ✓ | ✗ | ✓ |
| Hộ gia đình | **✓** | ✗ | ✓ | ✓ | ✓ |
| **Đã triển khai thực tế** | **✗ 0** | ✓ 500+ | ✓ | ✓ | ✓ |
| Đo điện năng tại máy | ✓ PZEM | ✓ | ✗ | ✗ | ✗ |

---

## 3. Ba điểm mạnh nói được trước sếp

**Một — mình đo ngoài trời thật, cả thị trường thì tra API.**

Đây không phải chi tiết nhỏ. Biến `T_rm` trong ASHRAE RP-884 **chính là** nhiệt độ
ngoài trời trung bình trượt. Ambi lấy số của cả thành phố từ API thời tiết; mình đo ở
đúng bức tường của căn nhà đó. Nhà hướng tây lúc 3 giờ chiều lệch vài độ so với số
API — mà lệch đó đi thẳng vào công thức. Đối thủ không thể vá bằng phần mềm: **họ
thiếu phần cứng**.

**Hai — mình chạy tiêu chuẩn, không chạy luật ngưỡng cũng không đoán sở thích.**

Sensibo là luật "nếu… thì…". Ambi học thích/không thích, nên khách mới dùng thì máy
chưa biết gì. Aircon chạy công thức đã được bình duyệt trên hai mươi nghìn mẫu khảo
sát — **đúng ngay từ ngày đầu, chưa cần một lần bấm phản hồi nào**, rồi mới học thêm.

**Ba — mọi quyết định đều giải thích được.**

`comfort_log` ghi một dòng mỗi chu kỳ, kèm đủ biến trung gian. Khách hỏi "sao lúc nãy
máy tự đổi sang 27 độ" thì có câu trả lời bằng số. Không đối thủ tiêu dùng nào mở phần
này ra.

---

## 4. Chỗ mình còn thua — nói trước khi bị hỏi

- **Số máy đã lắp: 0.** BenKon có 500+ cửa hàng, có bằng sáng chế, có 500.000 USD, và
  có con số tiết kiệm 10–20 % đo trên thực địa. Mình mới có thuật toán chạy đúng.
- **Tương thích máy lạnh.** BenKon công bố >99 % model. Mình học mã hồng ngoại từng
  máy — chưa có số liệu về tỷ lệ học thành công.
- **Sensibo đã có cảm biến chất lượng không khí** (TVOC, CO₂); mình chưa.
- **Ambi bán ra từ 2017.** Mình chưa bán máy nào.

Cách nói đúng trước sếp: mình **không** cạnh tranh trực diện với BenKon ở chuỗi cửa
hàng. Mình đi vào chỗ họ bỏ trống — **hộ gia đình và cửa hàng nhỏ**, nơi câu hỏi không
phải "khoá máy lúc nào" mà "bao nhiêu độ thì người trong phòng thấy dễ chịu".

---

## Nguồn

- [BenKon — trang chủ](https://benkon.io/)
- [BenKon gọi vốn 500.000 USD — Diễn đàn Doanh nghiệp](https://diendandoanhnghiep.vn/startup-cong-nghe-dieu-hoa-benkon-huy-dong-thanh-cong-500-000-usd-10037616.html)
- [BenKon — TheLEADER](https://theleader.vn/startup-cong-nghe-dieu-hoa-benkon-nhan-von-500000-usd-d9681.html)
- [BenKon — Tin nhanh Chứng khoán](https://www.tinnhanhchungkhoan.vn/truong-minh-dat-dong-sang-lap-benkon-chua-thanh-cong-nhung-da-thanh-nhan-post305234.html)
- [So sánh Sensibo / Cielo / Ambi / Tado](https://www.onesmartcrib.com/sensibo-vs-ceilo-vs-ambi-vs-tado/)
- [Ambi Climate 2 — đánh giá, xác nhận không có cảm biến ngoài trời](https://lesterchan.net/blog/2017/06/02/ambi-climate-2nd-edition-review/)
- [Tổng hợp bộ điều khiển máy lạnh thông minh 2026](https://www.smarthomeexplorer.com/guides/best-smart-ac-controllers-make-any-ac-smart-2026)
