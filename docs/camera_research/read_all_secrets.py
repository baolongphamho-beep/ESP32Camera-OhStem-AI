import re

# 1. Đọc tiếp file camera_secrets.txt (loại bỏ các dòng log lỗi wifi hệ thống)
with open("camera_secrets.txt", "r", encoding="utf-8") as f:
    lines = f.readlines()

print("=== CÁC CẤU HÌNH ỨNG DỤNG, UART & AI MODEL TRONG CAMERA ===")
app_lines = [line.strip() for line in lines[18:] if "wifi stop" not in line and "create wifi task" not in line]
for idx, line in enumerate(app_lines, 1):
    print(f"[{idx:03d}] {line[:130]}")

# 2. Trích xuất toàn bộ mã nguồn trang Web (HTML/JS) nhúng trong Firmware
with open("backup_original_camera.bin", "rb") as f:
    data = f.read(4 * 1024 * 1024)

html_pages = re.findall(b"<html.*?</html>", data, re.DOTALL | re.IGNORECASE)
for i, page in enumerate(html_pages, 1):
    fname = f"camera_webui_{i}.html"
    with open(fname, "wb") as out:
        out.write(page)
    print(f"\n--> Đã trích xuất thành công giao diện Web ẩn số {i} ra file: {fname} ({len(page)} bytes)")
