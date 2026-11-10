with open("backup_original_camera.bin", "rb") as f:
    data = f.read(2 * 1024 * 1024)

import re
for kw in [b"class:%s,%d", b"face:%d", b"/control", b"/capture"]:
    pos = data.find(kw)
    if pos != -1:
        snippet = data[max(0, pos-300):min(len(data), pos+300)]
        strings = re.findall(b"[\x20-\x7E]{3,100}", snippet)
        print(f"\n=== VÙNG NHỚ XUNG QUANH '{kw.decode()}' (Offset: 0x{pos:X}) ===")
        for s in strings:
            print("  ", s.decode("utf-8", errors="ignore"))
