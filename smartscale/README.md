# SmartScale — Cân BLE kết hợp camera AI OhStem

**Firmware mới chạy trên bo:** đã nạp firmware ESP-IDF tại [firmware/edge_yolo](firmware/edge_yolo/README.md): UNO tự chạy YOLO26n INT8 input224, nhận ảnh JPEG từ camera qua Wi-Fi, đọc cân BLE và điều khiển LCD; laptop qua USB chỉ xem ảnh/cấu hình. Đã chạy suy luận self-test trên COM9:711ms, còn khoảng4.88MB PSRAM trước JPEG buffer. Đang chờ cấu hình Wi-Fi và thử ảnh người/cân/LCD thực tế; chưa kiểm định độ chính xác đo. Các hướng dẫn Python bên dưới mô tả bản cũ chạy AI trên laptop.

Project dùng **YOLO UNO (ESP32-S3)** nhận cân nặng từ Mi Scale 2 qua BLE và nhận kết quả camera qua UART/Grove. Laptop lấy ảnh từ camera OhStem qua Wi-Fi, chạy YOLO và ước lượng chiều cao sau khi hiệu chuẩn.

**Trạng thái hiện tại:** cân BLE đã được đối chiếu trên phần cứng. Bản tích hợp mới nhận chiều cao/khoảng cách từ laptop, hiển thị LCD 16×2 và tính BMI từ cân nặng đã chốt. Có kiểm tra tự động cho hình học, giao thức và dữ liệu hết hạn; cần kiểm tra LCD và đối chiếu chiều cao/khoảng cách thật sau khi lắp camera. Chưa có lưu lịch sử hoặc gửi chiều cao/BMI sang app BLE.

## 1. Luồng hoạt động

```text
Mi Scale 2 ── BLE ──► YOLO UNO ── USB Type-C ──► Serial Monitor trên laptop
                         ▲
                         │ UART qua dây Grove
                         │
                    Camera OhStem ◄── HTTP gửi kết quả ── Laptop chạy YOLO
                         │                                    ▲
                         └──────── ảnh qua Wi-Fi ──────────────┘
```

Trong bản này, **AI chạy trên laptop**, không chạy trên YOLO UNO. Cần giữ laptop chạy chương trình trong lúc đo chiều cao.

## 2. Thiết bị và phần mềm cần có

- YOLO UNO và dây USB Type-C có truyền dữ liệu (C–C dùng được nếu laptop có cổng USB-C).
- Cân Mi Scale 2, có pin và đã xác định đúng địa chỉ MAC.
- Camera AI OhStem tương thích các endpoint `/capture` và `/control?serial=...`, dây Grove và nguồn cho camera.
- Laptop Windows có VS Code, extension PlatformIO IDE và Python 3.11 để dùng môi trường như bản setup đã kiểm tra.
- Laptop và camera cùng mạng Wi-Fi, truy cập được IP của camera.
- Thước để đo chiều cao thật của người dùng làm mẫu hiệu chuẩn.

Giữ hai project cùng thư mục cha để chương trình tìm được model mặc định:

```text
C:\Users\ASUS\OneDrive\
├── NimBLE_SmartScale_Cp\
└── ESP32Camera-OhStem-AI\
    └── yolo26n.pt
```

## 3. Các folder và file chính

| Đường dẫn | Công dụng |
|---|---|
| `src/main.cpp` | Khởi động chương trình, bật Serial, chạy dịch vụ cân và đọc camera. |
| `lib/Config/config.h` | MAC cân, UUID BLE, cấu hình chân và UART. |
| `lib/BLE/` | Quét, kết nối và trao đổi dữ liệu BLE. |
| `lib/Scale/` | Giải mã cân nặng, gửi dữ liệu sang app, dịch vụ QR/cảm biến lực của cấu hình cũ. |
| `lib/Camera/` | Nhận và giải mã kết quả camera trên YOLO UNO. |
| `lib/Display/` | LCD I2C 16×2, luân phiên chiều cao/cân nặng và khoảng cách/BMI. |
| `lib/Measurements/` | Ghép dữ liệu mới, quản lý trạng thái người/cân và tính BMI; không dùng lại dữ liệu cũ khi mất người. |
| `lib/GM65/` | Code đầu đọc QR GM65. |
| `lib/calib_fsm/` | Code hiệu chuẩn cảm biến lực. |
| `camera/smartscale_camera.py` | Lấy ảnh, chạy YOLO trên laptop, hiệu chuẩn chiều cao và gửi kết quả qua camera. |
| `camera/geometry.py` | Công thức pinhole: tiêu cự pixel, chiều cao, giao tia chân với mặt phẳng bàn cân. |
| `camera/settings.json` | Thông số lắp nhập bằng --configure, có thể nhập lại khi đổi góc/vị trí, không đưa vào Git. |
| `camera/start_smartscale.ps1` | File chạy nhanh chương trình camera từ PowerShell. |
| `camera/requirements.txt` | Các thư viện Python cần cài. |
| `camera/calibration.json` | Được tạo khi nhấn C để lưu hiệu chuẩn, không đưa vào Git. |
| `.venv-camera/` | Môi trường Python riêng; không cần chỉnh file trong đây. |
| `boards/yolo_uno.json` | Mô tả board cho PlatformIO. |
| `variants/yolo_uno/pins_arduino.h` | Pinout YOLO UNO: mặc định I2C SDA11/SCL12 theo sơ đồ mạch; không dùng variant M5Stack. |
| `platformio.ini` | Cấu hình build, upload và Serial Monitor. |
| `.pio/` | PlatformIO tự tạo thư viện tải về và kết quả build. |

## 4. Nối phần cứng

1. Nối **YOLO UNO với laptop bằng dây USB Type-C có dữ liệu**.
2. Cấp nguồn cho camera qua cổng Type-C của camera.
3. Nối dây Grove từ camera vào **D5–D6** trên YOLO UNO.
4. Cân giao tiếp qua BLE, không cần nối dây dữ liệu từ cân vào YOLO UNO.

Cấu hình code hiện tại dùng UART2, baud **115200**, 8N1; RX GPIO8 (D5), TX GPIO9 (D6). Số D in trên board và số GPIO là hai cách đánh số khác nhau; kiểm tra pinout của đúng phiên bản board nếu thay cấu hình.

Hướng dẫn nối camera của OhStem: [Camera AI](https://wiki.ohstem.vn/ai-camera/camera-ai).

## 5. Chọn profile và upload YOLO UNO

| Profile | Sử dụng |
|---|---|
| **`yolo_uno_camera_lcd`** | Bản tích hợp LCD theo `AI_Vision_camera.ino`: camera RX GPIO9, TX GPIO8; LCD SDA11/SCL12. |
| **`yolo_uno_camera`** | Dùng cho cân BLE + camera. Tắt QR và đọc cảm biến lực để tránh trùng UART2/GPIO8–9. |
| `yolo_uno_s3` | Cấu hình cũ có QR/cảm biến lực; chưa bật camera. Profile mặc định hiện là `yolo_uno_camera_lcd`. |

**Khi dùng LCD, chọn `yolo_uno_camera_lcd`.** Cấu hình `yolo_uno_camera` trước đây vẫn dùng RX8/TX9 và không bật LCD. Kiểm tra dây thực tế: hai profile có chiều RX/TX khác nhau.

Trong VS Code:

1. Mở folder `NimBLE_SmartScale_Cp`.
2. Vào PlatformIO → Project Tasks → `yolo_uno_camera_lcd` → General → **Build**.
3. Cắm YOLO UNO, chọn đúng cổng COM → **Upload**.
4. Mở **Monitor** ở baud **115200**, nhấn RESET trên board.

Hoặc chạy trong terminal PlatformIO, tại thư mục gốc project:

```powershell
pio run -e yolo_uno_camera_lcd
pio run -e yolo_uno_camera_lcd -t upload
pio device monitor -b 115200 -p COMx
```

Thay `COMx` bằng cổng USB thực tế của YOLO UNO. Nếu `pio` không được nhận diện, dùng các nút trong Project Tasks hoặc mở terminal từ PlatformIO.

Ví dụ log chương trình đang chạy:

```text
[BOOT] YOLO UNO started; Serial baud = 115200
[Camera] UART2 RX GPIO8 TX GPIO9 baud=115200; waiting for camera
[ALIVE] uptime=... s; scale connected/data status: see BLE logs
```

`[ALIVE]` xuất hiện khoảng mỗi 5 giây, chỉ báo chương trình còn chạy; không chứng minh cân hoặc camera đang gửi dữ liệu.

## 6. Kiểm tra cân trước

### Tìm MAC cân bằng YOLO UNO

Nếu chưa biết MAC (ví dụ iPhone chỉ hiện UUID), dùng profile quét BLE riêng:

1. Trong PlatformIO → Project Tasks → `yolo_uno_ble_scan` → **Upload**.
2. Mở Monitor 115200, nhấn RESET, bước lên cân để bật cân.
3. Tìm các dòng có dạng sau (MAC chỉ là ví dụ):

   ```text
   [BLE] MAC=AA:BB:CC:DD:EE:FF | Name=... | RSSI=-45 dBm
   ```

4. Đối chiếu trước/sau khi bật cân, để cân gần bo, thử lại để xác định đúng thiết bị.
   Tên có thể trống; RSSI mạnh không đủ để khẳng định đó là cân.
5. Chép MAC vào `SCALE_MAC` trong `lib/Config/config.h`.
6. Chọn lại **`yolo_uno_camera_lcd` và Upload** để trở về chương trình cân + camera + LCD (hoặc `yolo_uno_camera` nếu chỉ dùng Serial).

Profile scanner chỉ quét địa chỉ, không kết nối hoặc đọc cân nặng.
Nạp scanner sẽ thay chương trình đang chạy trên bo; mã nguồn cân vẫn giữ nguyên.

### Nhận và đối chiếu cân nặng

1. Mở `lib/Config/config.h`, kiểm tra `SCALE_MAC` khớp địa chỉ cân của nhóm. Nếu sửa MAC thì upload lại.
2. Sau khi khởi tạo BLE xong, task kết nối đợi khoảng **1 giây** rồi bắt đầu quét cân.
3. Bước lên cân để bật cân và giữ yên.
4. Tìm log nhận dữ liệu, ví dụ:

```text
[ĐANG ĐO] Cân nặng: 60.45 kg
[ĐÃ CHỐT] Cân nặng ổn định: 60.50 kg
```

5. So sánh với số và đơn vị trên màn hình cân. Hiện code giải mã cờ kg/lb, nhưng log gửi sang app có chỗ ghi cố định `kg`; nên dùng cân ở đơn vị kg và xác minh số thực tế.
6. Thử ít nhất 3 lượt bước lên/bước xuống; đợi cân tắt rồi bật lại để kiểm tra tự kết nối lại.

Gói chưa ổn định hiện `[ĐANG ĐO]`. Khi cờ ổn định của cân được bật, code in **một dòng `[ĐÃ CHỐT]`**, bỏ log chốt trùng trong cùng lượt. Gói báo bỏ tải/về 0 hoặc lần kết nối lại sẽ cho phép chốt lượt mới. Không tự coi việc số đứng yên vài giây là tín hiệu chốt. Nếu firmware không gửi báo bỏ tải/về 0, cần kết nối lại để đặt lại trạng thái log. Dữ liệu BLE sang app vẫn là luồng cân nặng như trước; log cập nhật app được chuyển xuống mức DEBUG để Monitor dễ đọc.

Tham chiếu định dạng cờ: [Mi Scale 0x181D decode](https://gist.github.com/passcod/2132f8d1ca33232108f00e86f6147e47).

## 7. Chuẩn bị Python cho camera

Máy đang setup đã có `.venv-camera`. Chỉ làm bước cài đặt sau khi dùng máy mới hoặc chưa có môi trường này. Chạy từ thư mục gốc SmartScale:

```powershell
python -m venv .venv-camera
.\.venv-camera\Scripts\python.exe -m pip install -r camera\requirements.txt
```

Không cần kích hoạt môi trường bằng `Activate.ps1`; các lệnh dùng trực tiếp Python trong môi trường riêng.

## 8. Camera, LCD và nhập lại thông số lắp

Bản mới tự hiệu chuẩn ở box toàn thân đầu tiên khi đã nhập chiều cao mẫu và khoảng cách thật, không bấm C và không chờ 15 khung hình. Chọn profile yolo_uno_camera_lcd để dùng LCD; camera UART RX9/TX8 theo sketch đã cung cấp, LCD SDA11/SCL12.

Chạy camera/start_smartscale.ps1 với -CameraIP IP_CAMERA -Configure để nhập và lưu thông số mới. Có mode fixed (camera ngang, khoảng cách đã biết) và mode ground (ước lượng khoảng cách từ độ cao camera, góc nghiêng và tiêu cự pixel đã hiệu chuẩn). Không có tiêu cự pixel chung cho mọi module OV2640.

Xem lệnh chạy, công thức, cấu hình khi đổi góc và hướng dẫn LCD tại [camera/README.md](camera/README.md).

## 9. Kết quả trên LCD

LCD đổi trang mỗi 2,5 giây: chiều cao/cân nặng, rồi khoảng cách/BMI. BMI chỉ có khi chiều cao hợp lệ và cân nặng đã chốt; mất người hoặc dữ liệu hết hạn sẽ bỏ số cũ. Khi chưa thấy người, LCD hiện No people và detected trên hai dòng.

Các giá trị chiều cao/khoảng cách là ước lượng hình học từ bounding box, chưa bù méo ống kính, cần so sánh với thước. Chưa có lưu lịch sử hay gửi chiều cao/BMI sang app BLE.

## 10. Lỗi thường gặp

| Hiện tượng | Kiểm tra |
|---|---|
| Monitor trống | Đúng cổng COM USB, baud 115200, dây có dữ liệu, đã upload đúng profile; mở lại Monitor sau reset nếu USB kết nối lại. |
| Có `[ALIVE]` nhưng chưa có cân nặng | Kiểm tra MAC, pin cân, bước lên để bật cân; đợi quét BLE và đọc log kết nối/đăng ký nhận dữ liệu. |
| Chỉ có `[ALIVE]`, không có bất kỳ log BLE nào | Upload lại cấu hình mới: `build_unflags = -DCORE_DEBUG_LEVEL=0` loại bỏ mức log 0 framework tự thêm, giữ mức INFO 3 của project. |
| Chỉ thấy cổng Bluetooth COM | Kiểm tra YOLO UNO đã cắm vào laptop và nhận thiết bị USB; không chọn nhầm COM Bluetooth. |
| LCD trống | Xem `lcd=` trong `[ALIVE]`: `disabled` là firmware không bật LCD; `not-found` là chưa thấy địa chỉ trên SDA11/SCL12; `ready` là đã thấy ACK và gửi lệnh hiển thị, cần kiểm tra tương phản/loại màn hình nếu vẫn không có chữ. |
| Không có ảnh camera | Kiểm tra nguồn, IP, Wi-Fi và URL `/capture`. |
| Báo không tìm thấy model | Giữ đúng hai folder cùng thư mục cha hoặc chỉ định `--model`. |
| Có ảnh nhưng chưa có chiều cao | Chỉ một người trong ảnh, thấy đủ toàn thân, đã hiệu chuẩn, giữ yên; độ phân giải phải khớp hiệu chuẩn. |
| Laptop gửi HTTP nhưng UNO không có `[Camera RX]` | Kiểm tra dây Grove D5–D6, nguồn, profile `yolo_uno_camera`, chân GPIO và baud; xác minh firmware camera hỗ trợ chuyển tiếp UART. |
| Có raw `[Camera RX]` nhưng không có dòng kết quả | Giữ lại nguyên dòng raw để đối chiếu định dạng firmware. |
| Không xem được log cũ | Cuộn lên terminal còn mở; nếu đã đóng và không lưu file thì không có lịch sử Serial để khôi phục. |

## 11. Các file `.bin` nằm ở đâu?

Các bản sao có sẵn của camera nằm trong project camera:

```text
..\ESP32Camera-OhStem-AI\backup_original_camera.bin
..\ESP32Camera-OhStem-AI\flash_header.bin
..\ESP32Camera-OhStem-AI\spiffs_dump.bin
```

Firmware YOLO UNO được tạo sau khi build profile camera:

```text
.pio\build\yolo_uno_camera\firmware.bin
```

File này dành cho **YOLO UNO**. Thực hiện Upload bằng PlatformIO; quá trình setup hiện tại không cần nạp lại firmware camera hoặc chạy các script đọc flash/secrets trong project camera.

## 12. Checklist một buổi kiểm tra

- [ ] Upload đúng profile `yolo_uno_camera_lcd`, Monitor thấy `[ALIVE]` và `[LCD]`.
- [ ] Cân gửi số khớp màn hình và đúng đơn vị; thử lại ít nhất 3 lượt.
- [ ] Cân tắt/bật lại, YOLO UNO nhận dữ liệu lại được.
- [ ] Laptop mở được ảnh camera.
- [ ] Hiệu chuẩn với người có chiều cao thật đã biết, đứng trên cân.
- [ ] Monitor nhận được `[Camera RX]` và chiều cao ước lượng.
- [ ] Đối chiếu chiều cao với thước và ghi lại sai số.
- [ ] Lưu log/kết quả cần thiết trước khi đóng terminal.
