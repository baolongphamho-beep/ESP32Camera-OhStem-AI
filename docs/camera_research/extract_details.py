import re

with open("backup_original_camera.bin", "rb") as f:
    # Chỉ cần đọc 4MB đầu tiên (vùng chứa toàn bộ 1MB dữ liệu thực tế)
    data = f.read(4 * 1024 * 1024)

# Tìm tất cả các chuỗi văn bản có độ dài từ 6 ký tự trở lên
strings = re.findall(b"[\x20-\x7E]{6,200}", data)
decoded_strings = [s.decode("utf-8", errors="ignore") for s in strings]

# Lọc các đoạn chứa thông tin về Model, Wi-Fi, Web UI, Menu màn hình và UART
targets = ["model", "teachable", "ohstem", "uart", "baud", "detect", "class", "wifi", "http", "ws:", "wss:", "json", "grove", "serial", "320", "240"]

output_lines = []
for i, s in enumerate(decoded_strings):
    if any(t in s.lower() for t in targets):
        # Lấy kèm ngữ cảnh trước và sau chuỗi tìm thấy
        context = " | ".join(decoded_strings[max(0, i-1):min(len(decoded_strings), i+2)])
        if context not in output_lines:
            output_lines.append(context)

with open("camera_secrets.txt", "w", encoding="utf-8") as out:
    out.write("\n".join(output_lines))

print(f"Đã trích xuất {len(output_lines)} đoạn thông tin vào file 'camera_secrets.txt'!\n")
print("=== 40 ĐOẠN CẤU HÌNH QUAN TRỌNG NHẤT TRONG CAMERA ===")
for line in output_lines[:40]:
    print(" ->", line[:120])
