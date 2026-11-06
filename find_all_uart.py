import re

with open("backup_original_camera.bin", "rb") as f:
    data = f.read(2 * 1024 * 1024)

# Quét toàn bộ vùng .rodata quanh các chuỗi định dạng kết quả UART (0x11000 -> 0x18000)
rodata = data[0x11000:0x18000]
strings = re.findall(b"[\x20-\x7E]{3,120}", rodata)

print("=== CÁC CHUỖI ĐỊNH DẠNG UART & ĐIỀU KHIỂN TRONG FIRMWARE ===")
for s in strings:
    txt = s.decode("utf-8", errors="ignore")
    if any(k in txt.lower() for k in ["%", "serial", "uart", "baud", "class", "face", "color", "line", "qr", "track", "obj", "115200", "9600"]):
        print(" ->", txt)
