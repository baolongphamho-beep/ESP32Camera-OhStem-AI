> **Máy LAB đã được thiết lập ngày 18/11/2026:** ESP-IDF 6.1 đã cài, native đã full build/nạp UNO COM5 và kiểm tra STANDBY đạt. Tại gốc dự án chạy `powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -Flash`. Script tự đồng bộ sang `C:\esp\SmartScale`; không cần tự sao chép. [Biên bản kiểm chứng](reports/NATIVE_BUILD_FLASH_20261118.md). Các bước cài bên dưới dành cho máy mới.

> Tài liệu nền nhập từ GitHub commit `c2f71b6`. Để dùng bản Plan 2 đã tích hợp trên máy này, ưu tiên [HUONG_DAN_PLAN2.md](HUONG_DAN_PLAN2.md) và [README native](../firmware/edge_yolo/README.md). Những số liệu bo/đường dẫn máy upstream cần đối chiếu lại.

# Hướng Dẫn Toàn Diện: Thiết Lập & Vận Hành SmartScale Trên Laptop Mới

Tài liệu này được biên soạn dành cho **người mới hoàn toàn**, hướng dẫn từng bước để tải mã nguồn, cài đặt công cụ và vận hành thành công toàn bộ hệ thống **SmartScale (Edge AI trên ESP32-S3 + Cân BLE Mi Scale 2 + Camera AI OhStem + Màn hình LCD 16×2)** trên một máy tính cá nhân hoặc laptop mới tinh.

---

## Mục Lục
1. [Yêu Cầu Chuẩn Bị Trước](#1-yêu-cầu-chuẩn-bị-trước)
2. [Cài Đặt Các Phần Mềm Cốt Lõi Trên Laptop Mới](#2-cài-đặt-các-phần-mềm-cốt-lõi-trên-laptop-mới)
   - [2.1. Cài đặt Git for Windows](#21-cài-đặt-git-for-windows)
   - [2.2. Cài đặt Python (3.9 - 3.11)](#22-cài-đặt-python-39---311)
   - [2.3. Cài ESP-IDF cho firmware native Plan 2](#23-cài-esp-idf-cho-firmware-native-plan-2)
3. [Tải Mã Nguồn Dự Án (Clone Git)](#3-tải-mã-nguồn-dự-án-clone-git)
4. [Thiết Lập Môi Trường Python & Chạy Kiểm Thử](#4-thiết-lập-môi-trường-python--chạy-kiểm-thử)
5. [Sơ Đồ Đấu Nối Phần Cứng](#5-sơ-đồ-đấu-nối-phần-cứng)
6. [Biên Dịch & Nạp Firmware Xuống Bo Mạch YOLO UNO](#6-biên-dịch--nạp-firmware-xuống-bo-mạch-yolo-uno)
7. [Cấu Hình Wi-Fi & Camera Qua Cáp USB (USB Viewer)](#7-cấu-hình-wi-fi--camera-qua-cáp-usb-usb-viewer)
8. [Vận Hành Độc Lập Không Cần Laptop](#8-vận-hành-độc-lập-không-cần-laptop)
9. [Bảng Xử Lý Sự Cố Thường Gặp (Troubleshooting / FAQ)](#9-bảng-xử-lý-sự-cố-thường-gặp-troubleshooting--faq)

---

## 1. Yêu Cầu Chuẩn Bị Trước

### Thiết bị phần cứng
1. **Bo mạch điều khiển:** YOLO UNO (trang bị chip vi xử lý **ESP32-S3**, 16MB Flash, 8MB Octal PSRAM).
2. **Camera AI:** Camera OhStem AI (cảm biến OV2640, phát luồng Wi-Fi JPEG `/capture`).
3. **Cân thông minh:** Xiaomi Mi Body Composition Scale 2 (Mi Scale 2, kết nối BLE).
4. **Màn hình hiển thị:** LCD 16×2 tích hợp mạch chuyển đổi giao tiếp **I2C PCF8574**.
5. **Dây cáp & Nguồn:**
   - Cáp USB Type-C truyền dữ liệu (chú ý: phải dùng cáp có truyền data, không dùng cáp chỉ có chức năng sạc).
   - Nguồn cấp 5V (củ sạc điện thoại hoặc pin dự phòng) cho Camera OhStem và bo YOLO UNO.
6. **Mạng Wi-Fi:** Mạng Wi-Fi băng tần **2.4 GHz** (ESP32-S3 và Camera OhStem không hỗ trợ băng tần 5GHz).

### Hệ điều hành Laptop
- Windows 10 hoặc Windows 11 (64-bit).
- Dung lượng ổ cứng trống tối thiểu: **10 GB** (cho Git, Python, ESP-IDF Compiler).

---

## 2. Cài Đặt Các Phần Mềm Cốt Lõi Trên Laptop Mới

### 2.1. Cài đặt Git for Windows
1. Tải bộ cài Git tại: [https://git-scm.com/download/win](https://git-scm.com/download/win)
2. Chạy file `.exe` cài đặt. Nhấn **Next** theo mặc định cho đến khi hoàn tất.
3. Mở cửa sổ **PowerShell** hoặc **Command Prompt** và gõ lệnh sau để kiểm tra:
   ```powershell
   git --version
   ```
   *(Nếu hiện `git version 2.x.x` là thành công).*

---

### 2.2. Cài đặt Python (3.9 - 3.11)
> [!IMPORTANT]
> Khuyên dùng **Python 3.9, 3.10 hoặc 3.11** để tương thích tối đa với bộ thư viện PyTorch, OpenCV và ESP-IDF.
1. Tải Python 3.10 hoặc 3.11 (64-bit Windows Installer) từ trang chủ: [https://www.python.org/downloads/](https://www.python.org/downloads/)
2. Khi mở file cài đặt, **BẮT BUỘC TÍCH VÀO Ô:**
   - ☑ **"Add python.exe to PATH"** (ở góc dưới cùng màn hình cài đặt đầu tiên).
3. Chọn **Install Now**.
4. Kiểm tra trong PowerShell:
   ```powershell
   python --version
   pip --version
   ```

---

### 2.3. Cài ESP-IDF cho firmware native Plan 2

`pip install -r requirements.txt` chỉ chuẩn bị Python cho YOLO/viewer, không cài SDK để biên dịch firmware. Firmware `firmware/edge_yolo` cần ESP-IDF và toolchain riêng; `build.ps1` không tự cài SDK.

1. Cài **Espressif Installation Manager (EIM)** theo [hướng dẫn Windows chính thức cho ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/windows-setup.html).
2. Dùng **Custom Installation**, chọn **ESP-IDF v6.1** để khớp baseline dự án, cùng công cụ cho **ESP32-S3**. Chọn đường dẫn cài ngắn, không dấu cách.
3. Khi cài xong, mở **Open IDF Terminal** trong dashboard EIM, chạy `idf.py --version` để xác nhận. Python `.venv` của ứng dụng không thay cho môi trường IDF này.
4. Ghi lại thư mục SDK có `tools/idf.py` và `export.ps1`, dùng cho tham số `-IDFPath`. Ví dụ `C:\Espressif\v6.1\esp-idf` chỉ là minh họa; thay bằng vị trí cài thật.

**Đường dẫn dự án:** [ESP-IDF không hỗ trợ dấu cách trong đường dẫn SDK hoặc dự án](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/windows-start-project.html#start-a-project). Thư mục OneDrive hiện tại chứa `Mira's Laptop`, script `build.ps1` tự tạo/cập nhật bản sao build ở `C:\esp\SmartScale` (đổi bằng `-BuildRoot` nếu cần). Chạy lệnh tại bản gốc trong LAB; tiếp tục chỉnh sửa tại đây. Đổi riêng `-IDFPath` không giải quyết dấu cách trong đường dẫn dự án.

---

## 3. Tải Mã Nguồn Dự Án (Clone Git)

Mở **PowerShell**, chuyển vào thư mục bạn muốn chứa dự án (ví dụ `D:\PROJECT` hoặc `C:\Projects`) và chạy lệnh clone nhánh `smartscale-edge-ai`:

```powershell
# Di chuyển vào thư mục bạn muốn lưu
cd D:\PROJECT

# Tải đúng nhánh chính của dự án SmartScale
git clone -b smartscale-edge-ai https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI.git SmartScale

# Di chuyển vào thư mục dự án
cd SmartScale
```

---

## 4. Thiết Lập Môi Trường Python & Chạy Kiểm Thử

Hệ thống có file `requirements.txt` chuẩn hóa để bạn cài đặt toàn bộ thư viện cần thiết chỉ trong một bước.

### Bước 4.1: Tạo môi trường ảo (Virtual Environment)
```powershell
# Tạo môi trường ảo tên .venv
python -m venv .venv

# Kích hoạt môi trường ảo trên PowerShell
.\.venv\Scripts\Activate.ps1
```
*(Nếu gặp lỗi PowerShell chặn script kích hoạt, chạy trước lệnh: `Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass` rồi kích hoạt lại).*

### Bước 4.2: Cài đặt các thư viện phụ thuộc
```powershell
pip install --upgrade pip
pip install -r requirements.txt
```

### Bước 4.3: Chạy bộ kiểm thử tự động (Unit Tests)
Kiểm tra xem toàn bộ thuật toán hình học tính chiều cao, bộ giải mã giao thức USB và CRC32 có hoạt động hoàn hảo trên máy tính của bạn không:
```powershell
python -m unittest discover -s tests -p "test_*.py" -v
```
**Kết quả mong đợi:** Toàn bộ 15 tests đều hiện `ok` (`Ran 15 tests ... OK`).

---

## 5. Sơ Đồ Đấu Nối Phần Cứng (Netlist) & Cài Đặt Camera AI

### 5.1. Đấu nối phần cứng theo Netlist
Trước khi cấp điện, hãy nối dây các module theo sơ đồ chuẩn dưới đây:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                             SƠ ĐỒ KẾT NỐI HỆ THỐNG                          │
└─────────────────────────────────────────────────────────────────────────────┘

       [ Cân Mi Scale 2 ]                    [ Camera OhStem AI ]
        (Đặt trên sàn)                       (Chế độ: 6. Stream video)
              │                                      │
              │ Sóng Bluetooth BLE (0x181D)          │ Phát luồng Wi-Fi JPEG (/capture)
              ▼                                      ▼
       ┌──────────────────────────────────────────────────────┐
       │               Bo Mạch YOLO UNO (ESP32-S3)            │
       │                                                      │
       │   - Xử lý BLE đọc cân nặng                           │
       │   - Thu ảnh Wi-Fi từ Camera (Core 0 song song)       │
       │   - Chạy mạng AI YOLO26n INT8 suy luận (Core 1)      │
       │   - Ghép cặp đo và tính chỉ số BMI                   │
       └──────────────────────────┬───────────────────────────┘
                                  │
                                  │ Giao tiếp I2C (Cổng I2C màu đỏ)
                                  ▼
                       [ Màn hình LCD 16×2 ]
                          SDA -> GPIO 11 | SCL -> GPIO 12
                          VCC -> 5V      | GND -> GND
```

#### Bảng chân cắm chi tiết (Netlist) trên bo YOLO UNO

| Linh kiện | Chân linh kiện | Chân trên YOLO UNO | Chức năng | Ghi chú |
|---|---|---|---|---|
| **LCD 16×2 I2C** | **SDA** | **GPIO 11** | Tín hiệu dữ liệu I2C | Dùng cổng I2C màu đỏ trên YOLO UNO |
| | **SCL** | **GPIO 12** | Tín hiệu xung nhịp I2C | Dùng cổng I2C màu đỏ trên YOLO UNO |
| | **VCC** | **5V** | Nguồn nuôi 5V | |
| | **GND** | **GND** | Chân mass chung | Phải chung GND với hệ thống |
| **Camera OhStem** | **Type-C** | Củ sạc 5V riêng | Cấp nguồn cho Camera | Cần nguồn tối thiểu 5V-1A ổn định |
| | **Wi-Fi** | Wi-Fi chung với UNO | Truyền hình ảnh JPEG | Cùng mạng Wi-Fi 2.4GHz |
| | **TX / RX (Grove)** | **GPIO 9 (D6) / GPIO 8 (D5)** | Cổng Grove UART | Chỉ cần nếu chạy Pipeline 2 (UART) |
| **Cân Mi Scale 2** | Pin 4x AAA | Không dây (BLE) | Kết nối BLE tự động | Bo tự quét gói `0x181D` (MAC `70:87:9E:EA:3A:E0`) |
| **YOLO UNO** | **Type-C** | Cổng Type-C Laptop | Nạp firmware & Cấu hình | Dùng cáp Type-C có truyền dữ liệu |

---

### 5.2. Chuyển Camera AI vào chế độ "Stream video" & Setup lại Wi-Fi cho Camera
> [!IMPORTANT]
> Endpoint lấy ảnh `/capture` của Camera AI OhStem **chỉ hoạt động khi camera đang ở chế độ `Stream video`**. Bạn có thể thực hiện bước này **song song** trong lúc chờ máy tính biên dịch và nạp firmware cho YOLO UNO ở Mục 6.

1. **Vào chế độ Stream video:**
   - Cấp nguồn 5V cho Camera AI OhStem.
   - Quan sát màn hình OLED 2.0 inch phía sau lưng camera, dùng nút bấm vật lý trên camera để di chuyển trong menu (`SELECT AI MODEL / CHỌN CHỨC NĂNG`) và chọn mục **`6. Stream video`**.
2. **Setup lại Wi-Fi cho Camera (khi đổi mạng Wi-Fi hoặc dùng lần đầu):**
   - Nếu camera chưa kết nối được Wi-Fi đã lưu, camera sẽ tự phát một mạng Wi-Fi riêng (Access Point) có tên dạng **`ohstemcam_...`** hoặc **`ohstem-ai-camera-xxxxx`**.
   - Dùng điện thoại hoặc laptop kết nối vào mạng Wi-Fi `ohstemcam_...` đó.
   - Mở trình duyệt web và truy cập địa chỉ: **`http://192.168.4.1`** (trang *Cấu hình Wi-Fi*).
   - Chọn tên mạng **Wi-Fi 2.4 GHz** (cùng mạng sẽ dùng cho YOLO UNO), nhập mật khẩu và nhấn **KẾT NỐI**.
3. **Kiểm tra địa chỉ IP trên màn hình Camera:**
   - Sau khi kết nối Wi-Fi thành công, màn hình OLED phía sau camera sẽ hiển thị **địa chỉ IP** được cấp (ví dụ: `10.127.7.68` hoặc `192.168.1.150`) kèm dòng nhắc *Open this on PC or phone same WiFi*.
   - **Lưu ý quan trọng:** Hãy **đóng tất cả các tab trình duyệt** đang mở địa chỉ IP của camera trước khi chạy YOLO UNO, vì web server trên camera chỉ phục vụ 1 kết nối tại một thời điểm (nếu mở trình duyệt song song, camera sẽ trả về `{"busy":1}`).

---

## 6. (Làm Song Song) Biên Dịch & Nạp Firmware Xuống Bo Mạch YOLO UNO

Dùng bản native Plan 2 sau khi hoàn thành mục 2.3. Model `yolo26n_224_s8_s3.espdl` có trên máy đang làm việc nhưng được gitignore; máy mới chạy `fetch_model.ps1`. ESP-DL được chuẩn bị bằng `prepare_sources.ps1`. `build.ps1` kiểm tra SDK/model, tự đồng bộ nguồn sang đường dẫn build không dấu cách và tải ESP-DL pinned nếu thiếu; không tự cài SDK hoặc xóa cấu hình người dùng.

### Bước 6.1: Xác định cổng COM của bo mạch YOLO UNO
1. Cắm cáp USB Type-C từ bo YOLO UNO vào laptop.
2. Mở **Device Manager** trên Windows (nhấn `Win + X` $\rightarrow$ chọn `Device Manager`).
3. Mở rộng mục **Ports (COM & LPT)**, tìm số cổng COM của YOLO UNO (ví dụ `USB Serial Device (COM13)` hoặc `COM9`).

### Bước 6.2: Chuẩn bị thư viện và build trước khi nạp

Mở PowerShell tại gốc dự án LAB. Script tự đồng bộ các file cần build sang thư mục không dấu cách. Ví dụ dưới giả sử đã cài SDK tại `C:\Espressif\v6.1\esp-idf`; thay bằng đường dẫn cài thật:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/tools/fetch_model.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -IDFPath "C:\Espressif\v6.1\esp-idf"
```

Nếu gặp `ESP-IDF SDK not found`, chưa đến bước nạp hay mở cổng COM: cài SDK hoặc sửa `-IDFPath`. Cài thêm thư viện pip không sửa lỗi này. Máy LAB đã full build/link native bằng ESP-IDF 6.1; máy mới vẫn phải cài SDK trước.

### Bước 6.3: Nạp sau khi build thành công

Xác nhận bo phù hợp partition flash 16 MB của native; đóng Serial Monitor/viewer đang dùng cổng COM. Sau đó chạy:

```powershell
# Thay đường dẫn SDK bằng thông tin thực tế. Cổng COM được tự tìm; nhiều bo thì thêm -Port COMx.
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -IDFPath "C:\Espressif\v6.1\esp-idf" -Flash
```

Lệnh này nạp bootloader, partition và app lên **YOLO UNO**. Camera OhStem giữ firmware gốc.

---

## 7. Chạy Chương Trình, Configure Lại & Vận Hành (USB Viewer)

Sau khi đã có IP trên màn hình Camera AI (Mục 5.2) và nạp xong firmware cho YOLO UNO (Mục 6), bạn tiến hành cấu hình và chạy chương trình:

### Bước 7.1: Chạy chương trình kèm `--configure` để thiết lập thông số
Vẫn giữ cáp USB nối với máy tính, kích hoạt môi trường `.venv` và chạy lệnh:

```powershell
python camera/usb_viewer.py --configure
```
*(Cổng được tự tìm; nếu báo nhiều bo hoặc không thấy bo, thêm `--port COMx` theo Device Manager).*

### Bước 7.2: Nhập thông số mạng & lắp đặt
Chương trình sẽ hỏi tương tác từng mục:
1. **WiFi SSID:** Nhập tên mạng Wi-Fi 2.4GHz (cùng mạng với Camera AI, ví dụ `HCMUT-MEETING`).
2. **WiFi password:** Nhập mật khẩu Wi-Fi.
3. **URL hoặc IP camera:** Nhập địa chỉ IP hiện trên màn hình OLED của Camera AI (ví dụ gõ `10.127.7.68` hoặc `http://10.127.7.68/capture` — chương trình sẽ tự động thêm `/capture`).
4. **Chế độ `fixed` / `ground`:**
   - Chọn `fixed` (mặc định): Nhập khoảng cách từ camera đến vị trí đứng trên cân (m), chiều cao thật của người chuẩn đầu tiên (cm), và nhập `fy/H = 0` để mạch tự hiệu chuẩn tiêu cự ngay khung hình toàn thân đầu tiên.
   - Hoặc chọn `ground`: Nhập độ cao tâm camera so với mặt bàn cân (cm), góc nghiêng xuống (độ) và hệ số tiêu cự `fy/H`.

> [!TIP]
> Ngay khi hiện **`Da luu cau hinh.`**, toàn bộ thông số được lưu vĩnh viễn vào bộ nhớ **NVS Flash** của YOLO UNO và cửa sổ xem ảnh trực tiếp sẽ mở ra ngay lập tức!

### Bước 7.3: Chạy chương trình ở các lần tiếp theo
Ở những lần sử dụng sau (khi không đổi Wi-Fi hay IP camera), bạn không cần gõ `--configure` nữa mà chỉ cần chạy thẳng:
```powershell
python camera/usb_viewer.py
```
Cửa sổ **SmartScale - YOLO UNO USB viewer** sẽ hiển thị trực tiếp video từ camera, khung xanh nhận diện người, chiều cao, khoảng cách, cân nặng, BMI và thời gian xử lý (`AI: ... ms   Net: ... ms`). Nhấn **`Q`** hoặc **`Esc`** khi muốn thoát.

---

## 8. Vận Hành Độc Lập Không Cần Laptop

Khi đã hoàn thành Bước 6 và Bước 7, bạn có thể **rút hoàn toàn cáp kết nối với laptop**:

1. Cấp nguồn 5V cho bo YOLO UNO bằng củ sạc hoặc pin dự phòng.
2. Bật màn hình LCD. Lúc này màn hình sẽ hiện:
   ```text
   SmartScale Ready
   Step on scale...
   ```
3. Người dùng đứng lên cân **Mi Scale 2**:
   - Màn hình cân LED sáng lên và chốt số đo cân nặng.
   - Bo mạch YOLO UNO bắt sóng BLE, rung chuông hoặc nhấp nháy đèn xác nhận.
   - Dòng 1 màn hình LCD cập nhật: `W: 65.4kg  ...`
4. Camera OhStem tự động ghi hình người đứng:
   - ESP32-S3 nạp ảnh trực tiếp qua Wi-Fi, chạy mô hình YOLO26n INT8 trong ~700ms.
   - Nhận diện đỉnh đầu, tính toán khoảng cách và chiều cao theo mô hình Pinhole Camera.
   - Dòng 1 cập nhật chiều cao: `W: 65.4kg H: 172cm`
   - Dòng 2 tự động tính toán chỉ số BMI: `BMI: 22.1 (Normal)`
5. Toàn bộ chu trình hoạt động hoàn toàn tự động, khép kín trên chip mà không cần máy tính phụ trợ!

---

## 9. Bảng Xử Lý Sự Cố Thường Gặp (Troubleshooting / FAQ)

| Hiện tượng lỗi | Nguyên nhân khả dĩ | Cách khắc phục triệt để |
|---|---|---|
| **PowerShell báo lỗi `ESP-IDF SDK not found`** | Chưa cài SDK hoặc chưa tìm đúng thư mục SDK. | Hoàn thành mục 2.3, truyền `-IDFPath "<thư mục esp-idf thực tế>"`; kiểm tra có `tools/idf.py` và `export.ps1`. Build trước khi thêm `-Flash`. |
| **Không tìm thấy cổng COM trong Device Manager** | Thiếu Driver USB Serial (CH340 hoặc CP210x) hoặc cáp USB là cáp chỉ sạc. | 1. Tải và cài driver CH340 (`CH341SER.EXE`).<br>2. Đổi cáp USB Type-C khác có hỗ trợ truyền dữ liệu. |
| **Màn hình LCD 16×2 sáng nhưng không hiện chữ (chỉ hiện 16 ô vuông đen)** | Chiết áp vi chỉnh độ tương phản (Contrast) ở mặt sau bo I2C chưa được xoay đúng mức. | Lấy tuốc nơ vít nhỏ xoay nhẹ biến trở màu xanh dương ở mặt lưng mạch I2C đến khi chữ hiện rõ nét. |
| **Bo mạch báo `WiFi connect failed`** | Mạng Wi-Fi là băng tần 5GHz hoặc nhập sai mật khẩu. | Đảm bảo bộ phát Wi-Fi có phát sóng băng tần 2.4GHz. Chạy lại `python camera/usb_viewer.py --port COMx --configure` để nhập lại mật khẩu. |
| **Cân Mi Scale 2 không truyền số đo sang bo** | Pin của cân yếu hoặc địa chỉ MAC trong code khác với cân thực tế. | 1. Thay pin mới cho cân.<br>2. Bước chân lên cân cho màn hình cân sáng đèn.<br>3. Kiểm tra địa chỉ MAC trong file [`lib/Config/config.h`](../lib/Config/config.h) (mặc định là `70:87:9E:EA:3A:E0`). |
| **Quá trình nạp báo lỗi `A fatal error occurred: Could not open COMx`** | Cổng COM đang bị một chương trình khác chiếm dụng (như Serial Monitor của Arduino, PuTTY, hoặc script Python đang chạy). | Tắt tất cả các cửa sổ terminal hoặc phần mềm đang kết nối với cổng COM đó rồi chạy lại lệnh nạp. |

---

*Tài liệu thuộc dự án **SmartScale — Hệ Thống Đo Nhân Trắc Tự Động**, phát triển bởi nhóm ACLAB.*
