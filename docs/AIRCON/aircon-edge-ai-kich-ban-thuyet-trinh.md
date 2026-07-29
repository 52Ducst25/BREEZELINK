# Kịch bản thuyết trình — Aircon × Edge AI

**Hồ sơ dự thi Qualcomm Future Makers 2026** · Smart Living · Edge–Cloud AI.
Đi kèm `aircon-x-ai.pptx` (18 slide) — mỗi đoạn ứng đúng một slide, giọng khẳng định.
Nguồn số: `docs/dinh-vi-canh-tranh.md` · `docs/uno-q-edge-ai-concept-va-payload.md`.

> **Nộp bài:** hạn **30/07/2026**, đội **3–5 người**, **video ≤ 5 phút**.

---

**[1 · Bìa]**

Nhóm em giới thiệu **Aircon** — điều khiển máy lạnh theo tiện nghi thích ứng, đưa AI
chạy tại biên trên Arduino UNO Q. Em nói ba phần: ý tưởng và mức độ đã chạy, mình khác
thị trường ở đâu, và ghép UNO Q theo concept nào.

**[2 · Bối cảnh]**

Điểm phải nói rõ đầu tiên: **Aircon không điều khiển theo ngưỡng.** Nó chạy adaptive
comfort theo ASHRAE RP-884 — khảo sát thực địa hai mươi nghìn mẫu, 160 toà nhà, bốn
châu lục. Cốt lõi: nhiệt độ dễ chịu của con người không cố định, nó trôi theo khí hậu
ngoài trời.

Một con số cho thấy vì sao. Cảm giác thật của người bám nhiệt độ da má **0,67**, bám
nhiệt độ phòng chỉ **0,35** — mà nhiệt độ phòng lại là thứ duy nhất máy lạnh thường đo.

**[3 · Hiện trạng]**

Ý tưởng này không nằm trên giấy — **nhóm em đã dựng và chạy thử tại nhà.** Node trong
nhà gộp bốn vai trò: đo, phát hồng ngoại, làm chủ ESP-NOW, màn cảm ứng. Node ngoài trời
gửi nhiệt ẩm về. Phía sau là EMQX, FastAPI, PostgreSQL, web quản trị và app Flutter.

Dải số dưới cùng là **số đo thật từ hệ đang chạy**: nhịp telemetry 15 giây, 120 mẫu
liên tục, nhiệt độ 24,3 đến 25,6 độ.

**[4 · Thị trường]**

Thị trường này đã có người. **BenKon** — 500 cửa hàng, có bằng sáng chế, gọi vốn năm
trăm nghìn đô, nhưng **chỉ làm doanh nghiệp**, và sản phẩm lõi là khoá máy theo lịch
chứ không tính nhiệt độ dễ chịu.

**Ambi Climate** là bên duy nhất thật sự làm AI tiện nghi, học sở thích qua phản hồi
người dùng. **Sensibo** thì Climate React chỉ là luật ngưỡng nếu–thì, và phải trả thuê
bao mới có geofencing. **Cielo Breez** không có mô hình tiện nghi nào.

**[5 · Ba điểm mạnh]**

**Một — mình đo ngoài trời bằng phần cứng thật, cả thị trường thì tra API.** Biến `T_rm`
trong công thức ASHRAE **chính là** nhiệt độ ngoài trời trung bình trượt. Ambi, Sensibo,
Cielo đều lấy số của cả thành phố từ API; Aircon đo ở **đúng bức tường căn nhà đó**. Nhà
hướng tây lúc ba giờ chiều lệch vài độ so với API, và lệch đó đi thẳng vào công thức.
Đối thủ **không vá được bằng phần mềm** — họ thiếu phần cứng.

**Hai — chạy tiêu chuẩn, không đoán sở thích.** Ambi phải học nên máy mới mua chưa biết
gì. Aircon chạy công thức đã bình duyệt trên hai mươi nghìn mẫu, **đúng từ lần bật đầu
tiên**.

**Ba — mọi quyết định giải thích được bằng số**, nhờ `comfort_log` ghi đủ biến trung
gian mỗi chu kỳ.

Khoảng trống còn lại là **hộ gia đình** — đúng chỗ Edge AI phát huy trọn vẹn nhất.

**[6 · Vì sao đặt suy luận tại biên]**

Bốn lý do, không lý do nào vì công nghệ mới. Độ trễ: hiện phải đi node, broker, worker
rồi mới quay về node. Mất mạng: rớt Internet là chuyện thường ở nhà dân, mà điều hoà
không được phép dừng. Riêng tư: nhiệt độ và giờ giấc vẽ ra chân dung một gia đình. Và
chi phí: mười lăm giây một mẫu, nhân số node, nhân số khách.

**[7 · Arduino UNO Q]**

Một bo hai bộ não. Linux để suy nghĩ — chạy mô hình, giữ lịch sử vài ngày. Vi điều
khiển để phản xạ — đọc cảm biến đúng nhịp, không bị hệ điều hành xen ngang. Trước phải
ghép Raspberry Pi với một vi điều khiển rời; nay bớt một điểm hỏng.

**[8–9 · Kiến trúc và ánh xạ]**

Ba tầng, ba nhịp: cảm biến theo mili giây, biên theo phút, đám mây theo ngày.

Điểm quan trọng nhất: **giữ nguyên công thức.** Hằng số hồi quy không đụng tới, người
vận hành vẫn chỉnh tay đè lên mô hình được. Thay đổi duy nhất là thay `T_rm` quá khứ
bằng `T_rm` dự báo, và chuyển quyết định xuống biên.

**[10 · Concept và payload]**

Ràng buộc số một: **không phá hợp đồng dây hiện tại** — đổi tên topic là các node đang
chạy im lặng biến mất khỏi hệ mà không báo lỗi gì. Nên UNO Q vào theo hướng **mở rộng**:
thêm ba gói tin, giữ nguyên bốn gói cũ. **Mỗi gói trả lời đúng một câu hỏi.**

**`forecast` — sắp tới trời nóng cỡ nào?** Ba mươi phút nữa ngoài trời 31,4 độ, sai số
0,5. Điểm hay là **sai số quan trọng ngang con số dự báo**: sai số lớn quá thì hệ tự bỏ
dự báo, quay về cách tính cũ. Mô hình biết lúc nào nó đang đoán mò.

**`presence` — trong phòng có người không?** HC-SR501 báo có người hay phòng trống, kèm
mức tin cậy. Gói này **giữ lại trên broker**, nên hệ khởi động lại là biết ngay, không
chờ ai cử động.

**`model` — có bản mô hình mới chưa?** Đám mây đẩy xuống kèm mã kiểm tra và số hiệu bản
cũ để quay lui. Nạp lỗi thì tự về bản trước — **không có chuyện một bản hỏng làm chết cả
đàn thiết bị**.

**[11 · Mô hình dự báo]**

Tập huấn luyện đã tự tích luỹ từ ngày đầu — `comfort_log` ghi một dòng mỗi chu kỳ quyết
định, kèm đủ biến trung gian. Về lượng dữ liệu: học tăng cường kiểu thường cần hàng trăm
ngày, quá lâu. Nhóm em chọn hướng CLUE dùng Gaussian Process — mô hình **biết chỗ nào nó
không chắc**, tính chất quý hơn hiệu năng đỉnh khi dữ liệu thưa. Nghiên cứu công bố rút
yêu cầu xuống **bảy ngày**.

**[12 · Tiết kiệm đến từ giờ KHÔNG chạy]**

Đây là chỗ tiền thật sự nằm. Tiết kiệm **không** đến từ bớt nửa độ — tám tiếng làm lạnh
một phòng trống là tám tiếng điện đổ đi.

Ba việc, và quan hệ giữa chúng mới là mấu chốt. Phòng trống thì tắt, khoản lớn nhất.
Bật trước khi về tới — đây là thứ **cho phép** tắt mạnh tay, vì không có nó thì không ai
dám tắt.

Cách hệ biết có người là **HC-SR501** gắn trên node ESP-NOW — vài chục nghìn, ESP32 còn
thừa chân nên cắm thẳng. Vắng chuyển động quá mười lăm phút thì nâng nhiệt độ đặt hoặc
tắt máy. Hệ AeroSense làm đúng vậy và công bố tiết kiệm mười lăm phần trăm — **mà mình
đang dùng đúng công nghệ đó**: cùng ESP32, cùng ESP-NOW, cùng DHT22.

**[13 · Thổi đúng chỗ người ngồi]**

Đây là con số đẹp nhất trong tài liệu nhóm em đọc. Khảo sát phòng ngủ: **29 độ kèm gió
nhẹ 0,4 mét trên giây** cho giấc ngủ tốt hơn **26 độ không quạt**, đồng thời **cắt 25,3
phần trăm điện**. Ba độ, một phần tư hoá đơn.

Hai mảnh ghép đều nằm sẵn trong máy: đã có tham số cộng thêm ban đêm, đã có hạ tầng học
mã tốc độ quạt qua hồng ngoại — chỉ là chưa nối vào nhau. **Không mua thêm gì.**

Biết người ngồi đâu thì dùng radar mmWave chứ không camera: đặt camera trong phòng ngủ
khách hàng thì nhóm em không làm.

**[14 · Học thói quen · Phát hiện hỏng hóc]**

Cùng một chuỗi dữ liệu, hai câu hỏi khác hẳn nhau. Thứ nhất: phòng này thường cần gì,
vào giờ nào. Thứ hai là thứ không đối thủ tiêu dùng nào làm — **máy đang yếu đi mà chủ
nhà không biết.** Cùng một chênh lệch nhiệt độ mà thời gian kéo phòng xuống mục tiêu dài
dần ra qua từng tuần, đó là dàn bẩn, thiếu gas, hoặc máy nén xuống sức. Đây là đường mở
sang dịch vụ bảo trì dự đoán.

**[15 · Vòng điều khiển]**

Sáu bước: đo mỗi mười lăm giây, gom, dự báo mỗi phút, chạy thuật toán comfort với `T_rm`
**đã dự báo**, phát mã hồng ngoại, rồi học lại. Năm bước đầu chạy hoàn toàn trong nhà —
mất Internet thì chỉ bước học dừng, máy lạnh vẫn chạy đúng.

**[16 · Phân bổ]**

Đặt sai chỗ thì hoặc chậm, hoặc hỏng khi mất mạng, hoặc tốn tiền. ESP32 giữ việc nó làm
tốt: đọc cảm biến, phát hồng ngoại, giao diện. UNO Q nhận phần suy luận. Đám mây giữ
phần huấn luyện lại.

**[17 · Lộ trình]**

Bốn bước, mỗi bước tự đứng được. Một: gắn HC-SR501 và luật vắng người, làm ngay trên
phần cứng hiện tại. Hai: UNO Q chạy **song song, chỉ quan sát** và ghi dự báo. Ba: đối
chiếu dự báo với số đo thật rồi mới trao quyền xuống biên — **không trao quyền cho một
mô hình chưa ai chấm điểm.** Bốn: kênh cập nhật mô hình và radar cho hướng gió.

**[18 · Kết]**

Bốn chỉ tiêu nghiệm thu khi Edge AI chạy trên UNO Q, trong đó chỉ tiêu em coi trọng
nhất là giữ **một trăm phần trăm** khả năng ra quyết định khi mất Internet.

Cảm biến, đường truyền, thuật toán và giao diện đã chạy. Thứ duy nhất còn thiếu là một
bộ não đủ sức chạy mô hình ngay trong nhà — và đó chính xác là chỗ **Arduino UNO Q** lấp
vào.

Em xin hết.
