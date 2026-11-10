# SmartScale — Hệ Thống Đo Nhân Trắc Tự Động (AI + Cân BLE + LCD)

> [!TIP]
> 📖 **Dành cho người mới bắt đầu hoặc thiết lập trên laptop mới:**  
> Vui lòng xem tài liệu hướng dẫn từng bước chi tiết tại: [**docs/SETUP_GUIDE_NEW_LAPTOP.md**](docs/SETUP_GUIDE_NEW_LAPTOP.md).  
> Tài liệu bao gồm cách cài đặt Git, Python, ESP-IDF, sơ đồ đấu nối, nạp firmware 1-chạm và vận hành độc lập.

Hệ thống thông minh kết hợp bo mạch vi điều khiển **YOLO UNO (ESP32-S3)**, **Cân Bluetooth Mi Scale 2**, **Camera AI OhStem** và **Màn hình LCD 16×2 I2C** để tự động đo chiều cao, cân nặng và tính chỉ số khối cơ thể (BMI) theo thời gian thực.

---

## 1. Hai Luồng Hoạt Động (Architecture Pipelines)

Dự án hỗ trợ 2 luồng hoạt động linh hoạt:

### 🌟 Pipeline 1: Edge AI Native (Khuyên dùng — Độc lập 100% trên YOLO UNO)
> Toàn bộ mạng nơ-ron YOLO26n được lượng tử hóa INT8 và nhúng thẳng vào chip ESP32-S3. Không cần laptop duy trì chạy AI.

```text
OhStem Camera /capture ── (Wi-Fi JPEG) ──► YOLO UNO (ESP32-S3) ── (I2C) ──► LCD 16×2
                                                  ▲
Mi Scale 2 ────────────── (BLE 0x181D) ───────────┘
                                                  │
                                        (USB-Serial / Tùy chọn)
                                                  ▼
                                      Laptop cấu hình / xem ảnh
```
- **Firmware:** ESP-IDF v5.5 + `esp-dl v3.3.15` nằm tại [`firmware/edge_yolo`](firmware/edge_yolo/README.md).
- **Bộ nhớ:** Flash 16MB QIO, Octal PSRAM 8MB (thời gian suy luận ~711ms).
- **Độc lập:** Sau khi nạp code và cấu hình Wi-Fi một lần qua USB, rút máy tính ra hệ thống vẫn tự hoạt động bình thường.

---

### 💻 Pipeline 2: Laptop AI (Chế độ phụ / Kiểm thử)
> AI YOLO chạy trên máy tính Windows, lấy ảnh từ camera qua HTTP và đẩy kết quả đo sang YOLO UNO qua USB/UART.

```text
Mi Scale 2 ── BLE ──► YOLO UNO ── USB Type-C ──► Serial Monitor / LCD
                         ▲
                         │ UART qua dây Grove
                    Camera OhStem ◄── HTTP gửi nhãn ── Laptop chạy YOLO (.venv)
                         │                                    ▲
                         └──────── ảnh qua Wi-Fi ─────────────┘
```

---

## 2. Cấu Trúc Thư Mục Dự Án (Project Structure)

Dự án được sắp xếp phân lớp rõ ràng, sạch sẽ:

```text
NimBLE_SmartScale_Cp/
├── firmware/                   # [PIPELINE 1] MÃ NGUỒN FIRMWARE CHO YOLO UNO
│   ├── edge_yolo/              # Firmware Edge AI Native (ESP-IDF C++)
│   │   ├── main/               # Mã nguồn C++ (app_main.cpp, edge_io.cpp)
│   │   ├── models/             # File model AI (.espdl, .build.json)
│   │   ├── tools/              # Công cụ lượng tử hóa (quantize.py, prepare_sources.ps1)
│   │   ├── build/              # Thư mục chứa firmware biên dịch (.bin)
│   │   └── build.ps1           # Script một chạm biên dịch và nạp firmware
│   └── backups/                # Bản sao lưu gốc bộ nhớ Flash 16MB trước khi nạp
│
├── camera/                     # [PIPELINE 2 & TOOLS] CÔNG CỤ PYTHON CHO CAMERA & USB
│   ├── smartscale_camera.py    # Pipeline chạy YOLO trên laptop
│   ├── usb_viewer.py           # Công cụ cấu hình Wi-Fi & xem ảnh USB từ YOLO UNO
│   ├── geometry.py             # Công thức toán học hình học Pinhole ước lượng D & H
│   ├── protocol.py             # Định dạng chuỗi giao thức gửi nhãn
│   ├── usb_protocol.py         # Giao thức truyền tin USB nhị phân SSV1 + CRC32
│   └── start_smartscale.ps1    # Script khởi chạy camera launcher tự động
│
├── lib/                        # CÁC THƯ VIỆN C++ DÙNG CHUNG
│   ├── Measurements/           # Quản lý trạng thái đo, ghép dữ liệu cân + chiều cao, tính BMI
│   ├── Config/                 # Cấu hình chân GPIO, địa chỉ MAC cân, UUID BLE
│   ├── Display/                # Trình điều khiển màn hình LCD 16×2 PCF8574 I2C
│   ├── BLE/                    # Quét và kết nối thiết bị Bluetooth Low Energy
│   └── Scale/                  # Giải mã gói tin cân nặng Mi Scale 2
│
├── docs/                       # TÀI LIỆU KỸ THUẬT VÀ HƯỚNG DẪN
│   ├── SETUP_GUIDE_NEW_LAPTOP.md   # Hướng dẫn chi tiết thiết lập trên laptop mới từ A-Z
│   ├── camera_hardware_context.md  # Bối cảnh phần cứng, IP, UART và sensor OV2640
│   ├── ON_DEVICE_INFERENCE.md      # Hướng dẫn chi tiết kỹ thuật suy luận trên chip
│   └── camera_research/            # Tài liệu & script dịch ngược Camera OhStem
│
├── boards/                     # CẤU HÌNH PHẦN CỨNG & BẢNG PHÂN VÙNG
│   ├── yolo_uno.json           # Khai báo bo mạch YOLO UNO (ESP32-S3)
│   └── yolo_uno_8MB.csv        # Bảng phân vùng phụ 8MB
│
├── src/                        # Code Arduino PlatformIO (cho Pipeline 2)
│   └── main.cpp                # Luồng nhận kết quả UART và đẩy LCD
│
├── tests/                      # KIỂM THỬ TỰ ĐỘNG (UNIT TESTS)
│   ├── test_camera_geometry.py # Kiểm thử công thức hình học
│   ├── test_camera_protocol.py # Kiểm thử chuỗi giao thức camera
│   ├── test_usb_protocol.py    # Kiểm thử đồng bộ khung truyền tin USB CRC32
│   └── edge_geometry_test.cpp  # Kiểm thử C++ logic hình học
│
├── requirements.txt            # Thư viện Python chuẩn hóa (opencv, ultralytics, pyserial)
├── yolo26n.pt                  # Mô hình gốc YOLO26n PyTorch (5.5MB)
├── platformio.ini              # Cấu hình môi trường PlatformIO
├── .gitignore                  # Cấu hình bỏ qua các file build/cache
└── README.md                   # Tài liệu hướng dẫn tổng quan dự án
```

---

## 3. Sơ Đồ Đấu Nối Phần Cứng (Hardware Wiring)

| Thiết bị | Chân trên Thiết bị | Chân trên YOLO UNO | Chú thích |
|---|---|---|---|
| **LCD 16×2 I2C** | SDA | **GPIO11** | Cổng I2C màu đỏ (mặc định) |
| | SCL | **GPIO12** | Cổng I2C màu đỏ |
| | VCC / GND | 5V / GND | Chung mass GND với bo |
| **Camera OhStem** | TX | **GPIO9 (D6)** | Camera truyền kết quả sang UNO (Pipeline 2) |
| | RX | **GPIO8 (D5)** | UNO truyền cấu hình sang Camera |
| | Cấp nguồn | Cổng Type-C riêng | Cần nguồn ổn định cho Wi-Fi & cảm biến |
| **Cân Mi Scale 2** | Không dây (BLE) | Bluetooth tích hợp | Đọc qua MAC `70:87:9E:EA:3A:E0` |
| **Laptop** | USB Type-C | Cổng Type-C trên UNO | Cấp nguồn, nạp code, xem monitor / cấu hình |

---

## 4. Hướng Dẫn Vận Hành Nhanh

### Bước 1: Môi trường làm việc dùng chung
- **Môi trường ảo Python:** `D:\PROJECT\.venv` (đầy đủ `ultralytics`, `torch`, `opencv`, `esptool`, `esp-ppq`).
- **ESP-IDF:** `D:\Workspace\Espressif` (đã cài đặt sẵn GCC 14 cho ESP32-S3, CMake, Ninja).
- **Thư viện AI ESP-DL:** `D:\PROJECT\.edge-deps\esp-dl` (dùng chung cho mọi dự án).

### Bước 2: Nạp Firmware Edge AI (Pipeline 1) xuống bo YOLO UNO
Cắm bo YOLO UNO vào máy tính (ví dụ cổng `COM9`):
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -Flash -Port COM9
```

### Bước 3: Cấu hình Wi-Fi & Camera cho bo qua cáp USB
```powershell
D:\PROJECT\.venv\Scripts\python.exe camera/usb_viewer.py --port COM9 --configure
```
Nhập SSID, mật khẩu Wi-Fi của phòng thí nghiệm và URL camera (ví dụ `http://10.127.7.68/capture`). Thông số sẽ được lưu vĩnh viễn vào bộ nhớ Flash (NVS) của bo.

### Bước 4: Chạy kiểm thử đơn vị (Unit Tests)
Chạy bộ kiểm tra tự động trước khi release:
```powershell
D:\PROJECT\.venv\Scripts\python.exe -m unittest discover -s tests -p "test_*.py" -v
```
