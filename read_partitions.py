import struct

TYPE_MAP = {0x00: "APP", 0x01: "DATA"}
SUBTYPE_DATA_MAP = {
    0x00: "ota", 0x01: "phy", 0x02: "nvs", 0x06: "efuse",
    0x80: "esphttpd", 0x81: "fat (FATFS)", 0x82: "spiffs (SPIFFS)", 0x83: "littlefs (LittleFS)"
}
SUBTYPE_APP_MAP = {0x00: "factory", 0x10: "ota_0", 0x11: "ota_1", 0x20: "test"}

with open("flash_header.bin", "rb") as f:
    data = f.read()

print(f"{'TÊN PHÂN VÙNG':<18} | {'LOẠI':<8} | {'ĐỊNH DẠNG':<18} | {'ĐỊA CHỈ (OFFSET)':<16} | {'DUNG LƯỢNG'}")
print("-" * 85)

found = False
for offset in range(0x8000, len(data) - 32, 32):
    magic, p_type, p_subtype, p_offset, p_size, label_bytes, flags = struct.unpack("<HBBII16sI", data[offset:offset+32])
    if magic == 0x50AA:
        found = True
        label = label_bytes.split(b"\x00")[0].decode("utf-8", errors="ignore")
        type_str = TYPE_MAP.get(p_type, f"0x{p_type:02x}")
        if p_type == 0x00:
            sub_str = SUBTYPE_APP_MAP.get(p_subtype, f"app_0x{p_subtype:02x}")
        elif p_type == 0x01:
            sub_str = SUBTYPE_DATA_MAP.get(p_subtype, f"data_0x{p_subtype:02x}")
        else:
            sub_str = f"custom_0x{p_subtype:02x}"
        
        size_kb = p_size / 1024
        size_str = f"{size_kb/1024:.2f} MB" if size_kb >= 1024 else f"{size_kb:.0f} KB"
        print(f"{label:<18} | {type_str:<8} | {sub_str:<18} | 0x{p_offset:08X}       | {size_str}")

if not found:
    print("CẢNH BÁO: Không tìm thấy chữ ký 0xAA 0x50!")
