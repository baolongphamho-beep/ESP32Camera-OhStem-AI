import re
import os

def inspect_binary(filename, label):
    print(f"\n================ KIỂM TRA {label} ({filename}) ================")
    if not os.path.exists(filename):
        print(f"LỖI: Chưa tìm thấy file '{filename}'. Hãy kiểm tra lại đường dẫn hoặc lệnh backup!")
        return

    file_size = os.path.getsize(filename)
    print(f"Dung lượng file: {file_size / (1024*1024):.2f} MB ({file_size / 1024:.0f} KB)")

    with open(filename, "rb") as f:
        data = f.read()

    # Đo dữ liệu thực tế (loại bỏ vùng nhớ trống toàn 0xFF)
    non_ff = len(data) - data.count(b"\xff")
    print(f"Dữ liệu thực tế: {non_ff / 1024:.1f} KB ({(non_ff / len(data)) * 100:.1f}%)")

    # Tìm các từ khóa kỹ thuật AI, format file và giao tiếp UART
    keywords = [
        b"TFL3", b"tflite", b"espdl", b"model", b"yolo", b"fomo", 
        b"person", b"face", b"color", b".bin", b".json", b".txt", 
        b"UART", b"Grove", b"baud", b"frame", b"camera"
    ]
    found_kw = [kw.decode("utf-8", errors="ignore") for kw in keywords if kw.lower() in data.lower()]
    print("Từ khóa phát hiện:", ", ".join(found_kw) if found_kw else "Không tìm thấy từ khóa đặc trưng")

    # Bóc tách các chuỗi ASCII/UTF-8 có nghĩa (đường dẫn, tên model, hàm nhận diện)
    strings = re.findall(b"[a-zA-Z0-9_./:-]{5,50}", data)
    interesting = []
    for s in strings:
        s_dec = s.decode("utf-8", errors="ignore")
        if any(k in s_dec.lower() for k in ["model", "tflite", "dl", "yolo", "detect", "class", "uart", "spiffs", ".json", ".bin", ".txt", "ohstem", "vision", "grove"]):
            if s_dec not in interesting:
                interesting.append(s_dec)

    print(f"Các chuỗi/tên file quan trọng tìm thấy (Top {min(35, len(interesting))}):")
    for item in interesting[:35]:
        print("  ->", item)

# Kiểm tra phân vùng SPIFFS và File backup toàn bộ Flash
inspect_binary("spiffs_dump.bin", "PHÂN VÙNG SPIFFS (896 KB)")
inspect_binary("backup_original_camera.bin", "TOÀN BỘ FIRMWARE GỐC (16 MB)")
