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

## 3. Sơ Đồ Đấu Nối Phần Cứng (Netlist)

| Thiết bị | Chân trên Thiết bị | Chân trên YOLO UNO | Chú thích |
|---|---|---|---|
| **LCD 16×2 I2C** | `SDA` | **`GPIO 11`** | Cổng I2C màu đỏ trên YOLO UNO (mặc định) |
| | `SCL` | **`GPIO 12`** | Cổng I2C màu đỏ trên YOLO UNO |
| | `VCC` / `GND` | **`5V`** / **`GND`** | Đấu chung mass `GND` với YOLO UNO |
| **Camera AI OhStem** | Nguồn `Type-C` | Cấp nguồn 5V riêng | Nguồn ổn định ≥ 5V-1A cho Wi-Fi & cảm biến OV2640 |
| | Wi-Fi 2.4 GHz | Cùng mạng Wi-Fi với UNO | Truyền ảnh JPEG qua HTTP `/capture` (Pipeline 1) |
| | `TX` / `RX` (Grove) | `GPIO 9 (D6)` / `GPIO 8 (D5)` | Chỉ dùng ở Pipeline 2 (Grove UART D6-D5) |
| **Cân Mi Scale 2** | Không dây (BLE) | Bluetooth tích hợp | Phát quảng bá BLE `0x181D` (MAC `70:87:9E:EA:3A:E0`) |
| **Laptop / PC** | Cáp USB Type-C | Cổng Type-C trên UNO | Cáp có truyền dữ liệu để nạp firmware & chạy USB Viewer |

---

## 4. Quy Trình Setup & Vận Hành Chuẩn (5 Bước Từ Đầu)

Quy trình chuẩn khi lắp mạch mới, đổi bo YOLO UNO mới hoặc thiết lập trên một máy tính mới:

### Bước 1: Nối mạch theo Netlist
- Cắm màn hình **LCD 16×2 I2C** vào cổng I2C trên YOLO UNO (`SDA -> GPIO 11`, `SCL -> GPIO 12`, `VCC -> 5V`, `GND -> GND`).
- Cấp nguồn 5V qua cổng Type-C cho **Camera AI OhStem**.
- Cắm cáp USB Type-C (loại có truyền dữ liệu) từ **YOLO UNO** vào máy tính và kiểm tra cổng COM trong **Device Manager** (ví dụ `COM13` hoặc `COM9`).
- Lắp pin cho cân **Mi Scale 2**.

### Bước 2: Vào chế độ "Stream video" & Setup lại Wi-Fi trên Camera AI
1. Trên **Camera AI OhStem**, dùng nút bấm vật lý mở menu trên màn hình OLED 2.0 inch phía sau lưng camera và chọn chức năng **`6. Stream video`** *(bắt buộc phải ở chế độ này thì đường dẫn lấy ảnh `/capture` mới mở)*.
2. **Nếu camera chưa nối Wi-Fi (hoặc cần đổi sang mạng Wi-Fi mới):**
   - Khi chưa có mạng, camera tự phát Wi-Fi Access Point tên dạng `ohstemcam_...` (hoặc `ohstem-ai-camera-xxxxx`).
   - Dùng điện thoại hoặc laptop kết nối vào Wi-Fi `ohstemcam_...` đó, mở trình duyệt truy cập **`http://192.168.4.1`**.
   - Chọn mạng **Wi-Fi 2.4 GHz** của phòng lab/nhà bạn, nhập mật khẩu và bấm **KẾT NỐI**.
3. **Lấy địa chỉ IP của Camera:**
   - Sau khi kết nối thành công, màn hình OLED sau lưng camera sẽ hiện **địa chỉ IP** (ví dụ: `10.127.7.68` hoặc `192.168.1.xxx`).
   - **Lưu ý:** Đóng mọi tab trình duyệt đang mở trang web của camera để tránh chiếm luồng khiến camera báo bận (`{"busy":1}`).

### Bước 3: (Làm song song) Cài đặt môi trường & Nạp Firmware cho YOLO UNO
Trong lúc chờ cấu hình camera, trên máy tính mở **PowerShell** tại thư mục gốc dự án:

1. **Tạo môi trường Python (nếu trên PC mới lần đầu chạy):**
   ```powershell
   python -m venv .venv
   .\.venv\Scripts\Activate.ps1
   pip install -r requirements.txt
   ```
2. **Biên dịch và nạp Firmware Edge AI xuống YOLO UNO** (thay `COM13` bằng cổng COM thực tế của bạn):
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -Flash -Port COM13
   ```
   *(Trên PC mới, `build.ps1` sẽ tự động tải thư viện `esp-dl`, tự đồng bộ `dependencies.lock`, biên dịch firmware ở xung nhịp 240 MHz và nạp xuống bo).*

### Bước 4: Chạy chương trình kèm `--configure` để nạp cấu hình cho YOLO UNO
Sau khi nạp firmware xong, chạy lệnh cấu hình qua cáp USB:
```powershell
python camera/usb_viewer.py --port COM13 --configure
```
Nhập lần lượt theo hướng dẫn trên terminal:
- **WiFi SSID:** Tên mạng Wi-Fi 2.4 GHz (cùng mạng với Camera AI).
- **WiFi password:** Mật khẩu mạng Wi-Fi.
- **URL hoặc IP camera:** Gõ địa chỉ IP trên màn hình camera (ví dụ `10.127.7.68` hoặc `http://10.127.7.68/capture` — chương trình tự động thêm `/capture` nếu thiếu).
- **Chế độ đo (`fixed` / `ground`):** Chọn `fixed` (camera đặt ngang, nhập khoảng cách mét và chiều cao cm của người mẫu đầu tiên, nhập `fy/H = 0` để tự hiệu chuẩn) hoặc `ground`.

Khi terminal hiện **`Da luu cau hinh.`**, toàn bộ thông số đã được lưu vào bộ nhớ NVS của YOLO UNO.

### Bước 5: Chạy chương trình & Vận hành
- Ngay sau khi lưu cấu hình ở Bước 4, cửa sổ **SmartScale - YOLO UNO USB viewer** sẽ tự động mở và hiển thị hình ảnh từ camera, khung nhận diện người, chiều cao, khoảng cách, cân nặng, BMI và tốc độ xử lý (`AI: ... ms   Net: ... ms`).
- **Những lần chạy sau** (khi không cần đổi Wi-Fi hay IP camera), chỉ cần chạy thẳng:
  ```powershell
  python camera/usb_viewer.py --port COM13
  ```
- **Chạy độc lập không cần PC:** Rút cáp USB khỏi laptop và cấp nguồn 5V cho YOLO UNO, kết quả đo sẽ tự động hiển thị trực tiếp lên màn hình **LCD 16×2**.

### Kiểm thử đơn vị (Unit Tests)
```powershell
python -m unittest discover -s tests -p "test_*.py" -v
```
