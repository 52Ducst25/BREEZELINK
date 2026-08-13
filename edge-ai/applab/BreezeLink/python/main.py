"""Điểm vào của App Lab. Mọi thứ thật nằm trong gói ``edge_ai`` cạnh file này.

Gói đó KHÔNG được sửa ở đây — nó là bản sao do ``edge-ai/deploy/build-applab-app.py``
đổ vào, cùng một mã nguồn với bản chạy bằng systemd. Sửa ở đây thì lần dựng lại
kế tiếp xoá mất, và tệ hơn là hai bản triển khai lặng lẽ khác nhau.

CẤU HÌNH đọc từ ``python/.env`` (xem ``_load_dotenv`` trong ``edge_ai/main.py``).
File đó KHÔNG nằm trong git và không do script dựng tạo ra — nó chứa ORG_ID của
hộ. Thiếu nó thì dịch vụ dừng ngay với một dòng nói rõ thiếu biến nào, chứ không
chạy tiếp bằng giá trị đoán: ORG_ID sai nghĩa là mọi lệnh gửi xuống đều bị
gateway lặng lẽ từ chối vì lệch ``link_key``.
"""

import sys

# Log của App Lab đọc qua stdout của container, mà container không phải tty nên
# Python đệm theo KHỐI. Không có dòng này thì log hiện thành từng cục 4KB một —
# lúc gỡ lỗi trông y hệt như dịch vụ bị treo.
sys.stdout.reconfigure(line_buffering=True)
sys.stderr.reconfigure(line_buffering=True)

from edge_ai.main import main  # noqa: E402  (phải đứng sau dòng chỉnh đệm)

sys.exit(main())
