# Camera OhStem → chiều cao, khoảng cách, cân nặng và BMI trên LCD

## 1. Luồng mới

Laptop lấy ảnh `/capture`, chạy YOLO, vẽ box và tính ngay ở **box toàn thân đầu tiên** khi đã có đủ thông số. Không phải bấm C và không chờ 15 khung hình. Laptop gửi nhãn qua `/control?serial=...`; camera chuyển qua Grove/UART về YOLO UNO. UNO nhận cân BLE và tính BMI, hiển thị LCD 16×2.

AI vẫn chạy trên laptop. Đóng tab trình duyệt đang chạy Face/No Face để tránh hai chương trình cùng gửi UART hoặc tranh lấy ảnh.

## 2. Nối dây và upload

Chọn **`yolo_uno_camera_lcd` → Build → Upload → Monitor 115200** trong PlatformIO.

| Kết nối | Chân |
|---|---|
| Camera TX → UNO RX | GPIO9 / D6 |
| Camera RX ← UNO TX | GPIO8 / D5 |
| LCD SDA | GPIO11 |
| LCD SCL | GPIO12 |
| LCD VCC/GND | Đấu theo điện áp yêu cầu module; chung GND với UNO |
| Cân Mi Scale 2 | BLE, MAC trong `lib/Config/config.h` |
| Camera và laptop | Cùng Wi-Fi; Type-C cấp nguồn camera |

Đây là chiều UART trong file `AI_Vision_camera.ino` người dùng gửi. Profile `yolo_uno_camera` cũ vẫn là RX8/TX9: nếu dây hiện tại đã chạy theo cấu hình cũ, chỉnh hai build flag `CAMERA_RX_PIN` và `CAMERA_TX_PIN` trong profile LCD cho khớp dây, hoặc đấu lại theo bảng. Không đổi dây chỉ dựa vào màu; kiểm tra nhãn TX/RX.

LCD ưu tiên **bus SDA11/SCL12**, chờ nguồn ổn định rồi quét các địa chỉ I2C. Tự nhận các địa chỉ backpack PCF8574/PCF8574A từ 0x20–0x27 và 0x38–0x3f, thay vì chỉ 0x27/0x3f. Nếu chưa thấy, thử cặp GPIO6/7 trong sketch cũ; luôn bỏ qua GPIO8/9 khi camera dùng UART ở đó. Trước khi đổi bus có gọi Wire.end()/setPins(), vì Wire.begin() trên bus đã chạy không đổi chân.

Nếu có nhiều backpack trên một bus, chọn đúng `LCD_I2C_ADDRESS` trong `config.h`; không mặc định mọi thiết bị ACK đều là LCD. Không tìm được thì thử lại mỗi 5 giây. Log `[LCD SCAN]` cho biết cặp chân đang dò, `[I2C] ACK` cho biết địa chỉ, `[LCD] Address=... SDA=... SCL=...` cho biết cấu hình được chọn. `lcd=ready` là đã gửi lệnh hiển thị, vẫn cần xác minh chữ trên màn hình thật.

Board PlatformIO dùng pinout riêng tại `variants/yolo_uno/pins_arduino.h`: SDA11/SCL12 theo sơ đồ YOLO UNO, không dùng mặc định M5Stack ATOMS3. Bốn cổng I2C trên board dùng chung SDA11/SCL12.

Không nạp sketch `.ino` riêng lên UNO sau bản tích hợp vì sẽ thay chương trình cân. Không cần nạp lại firmware camera.

## 3. Nhập thông số khi đổi cách lắp

Tại thư mục gốc `NimBLE_SmartScale_Cp`, chạy:

```powershell
.\.venv-camera\Scripts\python.exe camera\smartscale_camera.py --ip 10.127.7.68 --configure --send-uart
```

Thay IP bằng IP hiện tại. Chương trình hỏi mode và các số đo, lưu vào `camera/settings.json`. Lần sau bỏ `--configure` để dùng lại; mỗi lần thay vị trí/góc lắp có thể chạy lại `--configure` mà không sửa code hoặc nạp lại bo.

Hoặc dùng launcher:

```powershell
.\camera\start_smartscale.ps1 -CameraIP 10.127.7.68 -Configure
```

Các thông số có thể ghi đè trực tiếp bằng `--distance-m`, `--reference-cm`, `--focal-px`, `--camera-height-cm`, `--tilt-deg`, `--mode`. Tham số trực tiếp chỉ có hiệu lực lần chạy đó, trừ khi dùng `--configure` để lưu. Xem mẫu `settings.example.json` để chỉnh file bằng tay.

## 4. Chọn cách đo

### Mode `fixed`: camera ngang, vị trí đứng cố định

Phù hợp để hiệu chuẩn ban đầu, hoặc đo người trên cân cố định. Giữ camera ngang, không nghiêng/roll, người gần trục giữa ảnh. Đo khoảng cách **theo trục trước camera đến vị trí đứng** bằng thước, nhập `distance_m`. Nếu chưa biết tiêu cự pixel, để `focal_px` trống và nhập `reference_cm`: chiều cao thật của người mẫu đầu tiên.

Ví dụ minh họa (thay 2.5 m và 170 cm bằng số thật):

```powershell
.\.venv-camera\Scripts\python.exe camera\smartscale_camera.py --ip 10.127.7.68 --mode fixed --distance-m 2.5 --reference-cm 170 --recalibrate --send-uart
```

Người mẫu phải là người xuất hiện **đầu tiên**, thấy đủ đầu và chân. Ngay box đó, chương trình tự tính và lưu `camera/calibration.json`:

```text
fy_pixel = box_height_pixel × distance_m / reference_height_m
height_m = box_height_pixel × distance_m / fy_pixel
```

Kết quả người mẫu đầu tiên bằng chiều cao đã nhập, dùng để hiệu chuẩn; đó không phải phép kiểm chứng độc lập. Sau đó mới đo người khác và so với thước. Khoảng cách D trong mode này là **số đã nhập**, không phải khoảng cách tự đo từ ảnh.

Tiêu cự được dùng lại theo đúng độ phân giải; lần sau không cần nhập reference. `--recalibrate` bỏ hiệu chuẩn đã lưu để lấy lại từ box đầu tiên. File hiệu chuẩn cũ dùng `cm_per_pixel` không áp dụng cho bản mới, cần làm lại một lần.

### Mode `ground`: camera có độ cao/góc biết trước, tự ước lượng D và H

Cần:

- `focal_px`: fy đã hiệu chuẩn theo **pixel** ở đúng độ phân giải, hoặc để trống dùng file hiệu chuẩn ở bước fixed.
- `camera_height_cm`: độ cao tâm ống kính tính từ **mặt bàn cân**.
- `tilt_deg`: góc nghiêng xuống so với phương ngang, xuống là dương, lên là âm. Camera không bị nghiêng xoay trái/phải (roll).

Chạy `--configure`, chọn `ground`, nhập các số thật. Hoặc ghi đè `--mode ground --camera-height-cm SO_DO --tilt-deg GOC_DO`; nếu chưa có file hiệu chuẩn thì cần thêm `--focal-px TIEU_CU_PIXEL`.

Chương trình giao tia qua chân với mặt phẳng bàn cân để tính khoảng cách ngang, rồi dùng tia qua đỉnh đầu và giả thiết người đứng thẳng để tính chiều cao. Đây là cách không dùng chiều cao trung bình để suy ra lại chính chiều cao người.

**Không có một con số fy pixel dùng chung chỉ vì cảm biến là OV2640.** fy phụ thuộc ống kính, độ phân giải, crop/zoom. Không lấy tiêu cự mm để điền thẳng vào `focal_px`. Chuyển vị trí/góc thì cập nhật thông số lắp; thay ống kính, crop/zoom hoặc độ phân giải thì hiệu chuẩn tiêu cự lại. Bản hiện tại giả định fx=fy, tâm ảnh ở giữa và chưa bù méo ống kính; các số là ước lượng cho đồ án, cần đo sai số thật.

Tham chiếu mô hình và đơn vị tiêu cự: [OpenCV Camera Calibration](https://docs.opencv.org/4.13.0/d9/d0c/group__calib3d.html).

## 5. LCD hiển thị gì?

Không có box, chưa có dữ liệu hoặc ảnh mất quá 3 giây:

```text
No people
detected
```

LCD chỉ có 16 ký tự mỗi dòng nên câu “No people detected” được tách làm hai dòng.

Có số đo, màn hình đổi trang mỗi 2,5 giây, ví dụ:

```text
H:170.0cm
W:65.00kg LOCK
```

```text
D:2.50m
BMI:22.49
```

- `...` sau cân nặng: cân đang thay đổi; `LOCK`: cân đã báo ổn định.
- BMI = cân nặng kg / (chiều cao m)². Chỉ tính khi có H hợp lệ và cân nặng đã chốt; không có kết luận sức khỏe.
- Thiếu hiệu chuẩn: `Person detected / Need calibration`; box bị cắt: `Full body needed`; nhiều người: `One at a time`.
- Không chờ số đo chiều cao ổn định: box hợp lệ đầu tiên đã có kết quả. Vì vậy kết quả có thể dao động theo box; không xem là thiết bị đo chuẩn.
- Dữ liệu cân hết hạn sau 30 giây không có gói mới; báo bỏ tải/về 0, mất người hoặc nhiều người sẽ xóa cân nặng dùng cho BMI. Cân mới nhận tối đa 2 giây trước box đầu có thể dùng cho lượt đến đó.
- Chỉ một người đứng trên cân, người trước phải ra khỏi khung trước lượt tiếp theo. Camera không xác minh danh tính hoặc tự bảo đảm người trong box chính là người trên cân.

## 6. Giao thức và chẩn đoán

Laptop gửi nhãn `MEAS_H170.0_D2.500`, `NO_PEOPLE`, `PERSON_UNCALIBRATED`, `PERSON_PARTIAL`, `MULTIPLE_PEOPLE` kèm độ tin cậy nhận dạng người. Nhãn không chứa dấu phẩy/chấm phẩy để tương thích firmware camera.

UNO đọc cả `label;probability` và `class:label,percent` ở 115200 8N1. HTTP thành công chưa chứng minh UART/LCD đã nhận: xem `[Camera RX]`, `[Camera] Height=... Distance=...` và `[LCD]` trong Monitor. Không có kết quả mới thì LCD không giữ số cũ mãi.

Lệnh HTTP dùng dấu `;` trực tiếp theo giao diện camera gốc: `/control?serial=LABEL;0.950;stop`. Không dùng `requests params` cho trường này vì nó biến dấu phân cách thành `%3B`; [ESP-IDF không tự giải mã URL khi đọc query](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html). `camera/protocol.py` xây URL đúng định dạng và có kiểm tra hồi quy. Sau khi cập nhật cần thoát và chạy lại Python, không cần nạp lại UNO chỉ vì thay đổi URL này.

Trên cửa sổ ảnh phải thấy `UART HTTP: ON`; nếu `UART: OFF`, thêm `--send-uart` hoặc chạy bằng launcher. Trạng thái ON chỉ xác nhận đã bật gửi, chưa xác nhận UNO nhận.

Không nhận ảnh: kiểm tra `http://IP_CAMERA/capture`. Chạy Python lỗi model: dùng `--model DUONG_DAN_PT`; mặc định lấy `../ESP32Camera-OhStem-AI/yolo26n.pt`.

Nếu lúc nhận HTTP lúc timeout, chỉ để một chương trình lấy ảnh/gửi UART; đóng tab Face/No Face và chương trình camera cũ. Bản mới giới hạn mặc định 3 yêu cầu ảnh/giây và 1 lệnh UART/giây, tăng thời gian chờ HTTP và thử lại với nhịp 1–3 giây khi lỗi. Không gửi thêm lệnh mỗi lần lấy ảnh thất bại. Khi ảnh lỗi, cửa sổ báo mất kết nối thay vì giữ số cũ; UNO vẫn hết hạn kết quả sau 3 giây, không kéo dài dữ liệu cũ để che lỗi mạng.

Có thể giảm tải thêm bằng `--fps 1 --send-interval 1`; thời gian chờ chỉnh bằng `--capture-timeout` và `--control-timeout`. Tăng timeout không sửa được IP sai hoặc mất kết nối Wi-Fi. Sau sửa Python chỉ cần thoát và chạy lại, không upload UNO.

Cửa sổ ảnh có thể báo `Setup needed: ...` nếu thiếu/sai thông số. Có box chưa có thông số thật thì không thể tự sinh chiều cao và khoảng cách đúng.

## 7. Kiểm tra phần mềm

```powershell
.\.venv-camera\Scripts\python.exe -m unittest discover -s tests -p 'test_camera_*.py' -v
pio run -e yolo_uno_camera_lcd
```

Kiểm tra trạng thái BMI/giao thức native (nếu có g++):

```powershell
g++ -std=c++17 -I lib/Measurements tests/measurement_state_test.cpp -o .pio/measurement_state_test.exe
.\.pio\measurement_state_test.exe
```

Kiểm tra dò LCD bằng bus I2C mô phỏng (chạy code Display thật, không cần bo):

```powershell
g++ -std=c++17 -DLCD_ENABLED=1 -DCAMERA_ENABLED=1 -DSCALE_ENABLE_FORCE=0 -DSCALE_ENABLE_QR=0 -DARDUINO_YOLO_UNO -DCAMERA_RX_PIN=9 -DCAMERA_TX_PIN=8 -I tests/lcd_stubs -I variants/yolo_uno -I lib/Config -I lib/Measurements tests/display_probe_test.cpp -o .pio/display_probe_test.exe
.\.pio\display_probe_test.exe
```

Các trường hợp gồm địa chỉ 0x20/0x3e, đổi bus khỏi chân sai, chọn GPIO6/7 khi cần, không chiếm UART8/9, LCD cấp nguồn trễ, nhiều địa chỉ và thiết bị I2C không phải LCD. Kiểm tra mô phỏng không thay cho xác minh trên màn hình thật.
