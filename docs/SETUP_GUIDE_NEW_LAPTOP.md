# Hướng Dẫn Toàn Diện: Thiết Lập & Vận Hành SmartScale Trên Laptop Mới

Tài liệu này được biên soạn dành cho **người mới hoàn toàn**, hướng dẫn từng bước để tải mã nguồn, cài đặt công cụ và vận hành thành công toàn bộ hệ thống **SmartScale (Edge AI trên ESP32-S3 + Cân BLE Mi Scale 2 + Camera AI OhStem + Màn hình LCD 16×2)** trên một máy tính cá nhân hoặc laptop mới tinh.

---

## Mục Lục
1. [Yêu Cầu Chuẩn Bị Trước](#1-yêu-cầu-chuẩn-bị-trước)
2. [Cài Đặt Các Phần Mềm Cốt Lõi Trên Laptop Mới](#2-cài-đặt-các-phần-mềm-cốt-lõi-trên-laptop-mới)
   - [2.1. Cài đặt Git for Windows](#21-cài-đặt-git-for-windows)
   - [2.2. Cài đặt Python (3.9 - 3.11)](#22-cài-đặt-python-39---311)
   - [2.3. Cài đặt ESP-IDF Offline Installer (v5.5.x)](#23-cài-đặt-esp-idf-offline-installer-v55x)
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

### 2.3. Cài đặt ESP-IDF Offline Installer (v5.5.x)
Đây là bộ công cụ biên dịch mã nguồn C++ (GCC Compiler, CMake, Ninja, ESP-IDF SDK) cho chip ESP32-S3.

1. Tải bản **ESP-IDF v5.5.x Offline Installer** từ Espressif:
   - Link tải trực tiếp: [https://dl.espressif.com/dl/esp-idf/](https://dl.espressif.com/dl/esp-idf/) (chọn file `esp-idf-tools-setup-offline-5.5.x.exe`).
2. Mở trình cài đặt và làm theo hướng dẫn:
   - **Màn hình Version:** Chọn `v5.5.6 (release version - .zip archive download)` hoặc bản v5.5 tương đương.
   - **Màn hình chọn thư mục (Directory):**
     - Mặc định chọn `C:\Espressif` hoặc `D:\Workspace\Espressif`.
     - *Lưu ý:* Nếu hiện cảnh báo *"Directory already exists and is not empty"*, bạn hãy chọn một thư mục trống mới hoặc xóa thư mục cũ bị gián đoạn trước đó.
   - **Màn hình Select Components (Chọn thành phần):**
     - Chọn **Full installation** hoặc giữ nguyên các mục mặc định.
     - *Mẹo tiết kiệm dung lượng:* Bạn có thể **bỏ chọn** các mục không dùng tới như *Espressif-IDE*, *Rust language support*, *Amazon Corretto JDK*. Chỉ cần giữ lại **Frameworks** và các công cụ Python/GCC.
   - Nhấn **Install** và chờ thanh tiến trình chạy hoàn tất 100%.

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

## 5. Sơ Đồ Đấu Nối Phần Cứng

Trước khi cấp điện, hãy nối dây các module theo sơ đồ chuẩn dưới đây:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                             SƠ ĐỒ KẾT NỐI HỆ THỐNG                          │
└─────────────────────────────────────────────────────────────────────────────┘

       [ Cân Mi Scale 2 ]                    [ Camera OhStem AI ]
        (Đặt trên sàn)                       (Gắn thẳng góc người đứng)
              │                                      │
              │ Sóng Bluetooth BLE (0x181D)          │ Phát luồng Wi-Fi JPEG
              ▼                                      ▼
       ┌──────────────────────────────────────────────────────┐
       │               Bo Mạch YOLO UNO (ESP32-S3)            │
       │                                                      │
       │   - Xử lý BLE đọc cân nặng                           │
       │   - Thu ảnh Wi-Fi từ Camera                          │
       │   - Chạy mạng AI YOLO26n INT8 suy luận chiều cao     │
       │   - Ghép cặp đo và tính chỉ số BMI                   │
       └──────────────────────────┬───────────────────────────┘
                                  │
                                  │ Giao tiếp I2C (Cổng I2C màu đỏ)
                                  ▼
                       [ Màn hình LCD 16×2 ]
                          Dòng 1: Cân nặng & Chiều cao
                          Dòng 2: Trạng thái & Chỉ số BMI
```

### Bảng chân cắm chi tiết trên bo YOLO UNO

| Linh kiện | Chân linh kiện | Chân trên YOLO UNO | Chức năng | Ghi chú |
|---|---|---|---|---|
| **LCD 16×2 I2C** | **SDA** | **GPIO 11** | Tín hiệu dữ liệu I2C | Dùng cổng I2C màu đỏ trên YOLO UNO |
| | **SCL** | **GPIO 12** | Tín hiệu xung nhịp I2C | Dùng cổng I2C màu đỏ trên YOLO UNO |
| | **VCC** | **5V** | Nguồn nuôi 5V | |
| | **GND** | **GND** | Chân mass chung | Phải chung GND với hệ thống |
| **Camera OhStem** | **Type-C** | Củ sạc 5V riêng | Cấp nguồn cho Camera | Cần nguồn tối thiểu 5V-1A riêng |
| | **Wi-Fi** | Wi-Fi chung với UNO | Truyền hình ảnh JPEG | Cùng mạng Wi-Fi 2.4GHz |
| **Cân Mi Scale 2** | Pin 4x AAA | Không dây (BLE) | Kết nối BLE tự động | Bo tự quét MAC cân (`70:87:9E:EA:3A:E0`) |
| **YOLO UNO** | **Type-C** | Cổng Type-C Laptop | Nạp firmware & Cấu hình | Cắm vào laptop khi nạp / cấu hình |

---

## 6. Biên Dịch & Nạp Firmware Xuống Bo Mạch YOLO UNO

Dự án đã tích hợp sẵn mô hình lượng tử hóa **`yolo26n_224_s8_s3.espdl`** trực tiếp trong Git, bạn **không cần** phải huấn luyện hay lượng tử hóa lại từ đầu!

### Bước 6.1: Xác định cổng COM của bo mạch
1. Cắm cáp USB Type-C từ bo YOLO UNO vào laptop.
2. Mở **Device Manager** trên Windows (nhấn `Win + X` $\rightarrow$ chọn `Device Manager`).
3. Mở rộng mục **Ports (COM & LPT)**, tìm tên cổng kết nối, ví dụ:
   - `USB-Enhanced-SERIAL CH340 (COM9)` $\rightarrow$ Cổng của bạn là `COM9`.

### Bước 6.2: Chạy script tự động biên dịch và nạp (1 chạm)
Mở PowerShell tại thư mục dự án và chạy:

```powershell
# Thay COM9 bằng cổng COM thực tế trên máy bạn
powershell -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -Flash -Port COM9
```

> [!NOTE]
> - Script `build.ps1` sẽ tự động tìm kiếm bộ cài đặt ESP-IDF tại `C:\Espressif` hoặc `D:\Workspace\Espressif`.
> - Quá trình biên dịch lần đầu tiên mất khoảng 2 - 4 phút.
> - Sau khi biên dịch xong, script sẽ tự động nạp bootloader, bảng phân vùng và nạp file model AI vào phân vùng `model` (địa chỉ `0x810000`).
> - Khi hoàn tất, màn hình sẽ thông báo `Hash of data verified. Leaving... Staying in monitor or reset`.

---

## 7. Cấu Hình Wi-Fi & Camera Qua Cáp USB (USB Viewer)

Sau khi nạp firmware, bạn cần cung cấp thông tin Wi-Fi và địa chỉ Camera để bo mạch kết nối.

### Bước 7.1: Mở công cụ cấu hình
Vẫn giữ cáp USB nối với máy tính, chạy lệnh:

```powershell
python camera/usb_viewer.py --port COM9 --configure
```

### Bước 7.2: Nhập thông số mạng
Chương trình sẽ hỏi tương tác qua giao diện dòng lệnh:
1. **Wi-Fi SSID:** Nhập tên mạng Wi-Fi 2.4GHz của bạn (ví dụ: `Lab_Robotics`).
2. **Wi-Fi Password:** Nhập mật khẩu Wi-Fi.
3. **Camera URL:** Nhập đường dẫn lấy ảnh của Camera OhStem (ví dụ: `http://192.168.1.150/capture` hoặc `http://10.127.7.68/capture`).

> [!TIP]
> Các thông số trên sẽ được lưu vĩnh viễn vào bộ nhớ **NVS Flash** của chip ESP32-S3. Lần sau bật nguồn, bạn không cần phải cấu hình lại!

### Bước 7.3: Mở màn hình quan sát ảnh thời gian thực (Tùy chọn)
Nếu bạn muốn vừa xem camera vừa kiểm tra các bounding box AI mà chip ESP32-S3 đang xử lý, chạy lệnh:
```powershell
python camera/usb_viewer.py --port COM9
```
Màn hình OpenCV sẽ mở ra, hiển thị hình ảnh từ camera được truyền ngược về qua giao thức nhị phân USB bảo vệ bởi mã kiểm tra CRC32.

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
| **PowerShell báo lỗi `ESP-IDF not found` khi chạy `build.ps1`** | ESP-IDF được cài ở ổ đĩa khác đường dẫn mặc định. | Truyền tham số đường dẫn trực tiếp: `powershell -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -EspressifRoot "C:\Espressif" -Flash -Port COMx`. |
| **Không tìm thấy cổng COM trong Device Manager** | Thiếu Driver USB Serial (CH340 hoặc CP210x) hoặc cáp USB là cáp chỉ sạc. | 1. Tải và cài driver CH340 (`CH341SER.EXE`).<br>2. Đổi cáp USB Type-C khác có hỗ trợ truyền dữ liệu. |
| **Màn hình LCD 16×2 sáng nhưng không hiện chữ (chỉ hiện 16 ô vuông đen)** | Chiết áp vi chỉnh độ tương phản (Contrast) ở mặt sau bo I2C chưa được xoay đúng mức. | Lấy tuốc nơ vít nhỏ xoay nhẹ biến trở màu xanh dương ở mặt lưng mạch I2C đến khi chữ hiện rõ nét. |
| **Bo mạch báo `WiFi connect failed`** | Mạng Wi-Fi là băng tần 5GHz hoặc nhập sai mật khẩu. | Đảm bảo bộ phát Wi-Fi có phát sóng băng tần 2.4GHz. Chạy lại `python camera/usb_viewer.py --port COMx --configure` để nhập lại mật khẩu. |
| **Cân Mi Scale 2 không truyền số đo sang bo** | Pin của cân yếu hoặc địa chỉ MAC trong code khác với cân thực tế. | 1. Thay pin mới cho cân.<br>2. Bước chân lên cân cho màn hình cân sáng đèn.<br>3. Kiểm tra địa chỉ MAC trong file [`lib/Config/config.h`](../lib/Config/config.h) (mặc định là `70:87:9E:EA:3A:E0`). |
| **Quá trình nạp báo lỗi `A fatal error occurred: Could not open COMx`** | Cổng COM đang bị một chương trình khác chiếm dụng (như Serial Monitor của Arduino, PuTTY, hoặc script Python đang chạy). | Tắt tất cả các cửa sổ terminal hoặc phần mềm đang kết nối với cổng COM đó rồi chạy lại lệnh nạp. |

---

*Tài liệu thuộc dự án **SmartScale — Hệ Thống Đo Nhân Trắc Tự Động**, phát triển bởi nhóm ACLAB.*
