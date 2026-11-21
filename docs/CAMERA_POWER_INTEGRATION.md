# Camera AI + TV2 BLE/Power integration

Nguồn ghép: camera `origin/smartscale-edge-ai` tại `302a853` (pipeline `c2f71b6`), TV2 tại `0aa415d` trên `feat/tv2-power-ble`. Nhánh làm việc: `feat/camera-power-integration`. Không push tự động.

## Ba chế độ build

Chạy tại thư mục gốc; thay COM11 bằng cổng thật. Đóng mọi Monitor/viewer trước khi nạp.

```powershell
# 1. Cân + LCD + nguồn, không camera (bản kiểm tra TV2).
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -ScaleOnly -StandbyScanPercent 10 -Flash -Port COM11

# 2. Camera + AI + cân chạy liên tục, để kiểm chuẩn trước khi thử Standby.
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -CameraContinuous -Flash -Port COM11

# 3. Tích hợp tự đánh thức từ cân và tự về nghỉ.
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -StandbyScanPercent 10 -Flash -Port COM11
```

`-ScaleOnly` và `-CameraContinuous` loại trừ nhau. Không truyền cả hai thì chọn chế độ tích hợp. `-StandbyScanPercent` hỗ trợ 10/25/50/100; thay đổi mức quét cần build/nạp lại. Bản chỉ cân quét ACTIVE 100%; bản có camera quét ACTIVE 25 ms mỗi 200 ms = 12.5%, liên tục duy trì BLE giữa các lần tải ảnh thay vì dừng discovery suốt HTTP. Cần kiểm tra lại độ tin cậy LOCK/bước xuống và tốc độ camera trên phần cứng; chưa xem mức này là cấu hình RF tối ưu.

Script build xử lý `_OLD_VIRTUAL_PATH` trước khi nạp môi trường EIM để có thể
chạy từ Terminal đang kích hoạt `.venv-camera`. Nếu trước đây CMake báo không
tìm thấy `xtensa-esp32s3-elf-gcc`, chạy lại bằng script đã sửa. Build thất bại
thì chưa có firmware mới được nạp; mở Viewer vẫn có thể nhận dữ liệu từ bản cũ.

## Luồng tích hợp

1. Khởi tạo model một lần và giữ tài nguyên model cho các lượt sau.
2. `STANDBY`: không lấy ảnh, không suy luận; CPU 80 MHz, Wi-Fi MAX_MODEM, LCD tắt, BLE quét theo cấu hình.
3. Đúng MAC cân, tải hợp lệ >5 kg: `ACTIVE`, CPU 240 MHz, Wi-Fi NONE, LCD sáng; Core 0 tải ảnh, luồng suy luận dùng pipeline YOLO hiện có.
4. Chiều cao hợp lệ và cân LOCK cùng lượt: hiển thị H/W/D/BMI, lưu bản kết quả cuối hợp lệ. Multiple people xóa kết quả giữ của lượt đó.
5. Bước xuống/dữ liệu cân hết hạn: `DRAINING`, ngừng cấp việc mới. HTTP và suy luận đang chạy được hoàn thành, ảnh/kết quả cũ bị loại bằng generation. Không suspend task đang giữ mutex hoặc đang dùng HTTP/buffer.
6. Khi cả hai worker đã nhả tài nguyên: CPU về 80 MHz. Có BMI hoàn chỉnh thì `RESULT_HOLD`, LCD giữ kết quả 10 giây; không có thì về `STANDBY` ngay. Có lượt mới thì hủy hold và bắt đầu lượt mới.

Không thêm servo trong lần ghép này. Giữ kết quả hiện là bản snapshot ở tầng tích hợp/LCD, chưa thêm tham số hold vào NVS của TV5. Light Sleep giữ cơ chế khóa USB máy tính; chưa chứng minh dòng tiêu thụ hay ngủ thật khi chưa thử nguồn độc lập.

## Các chỗ tránh ghép nhầm

- `ScalePresence` giữ tải cân và session ID riêng; camera None/Multiple không được xóa nguồn đánh thức. Unload/reload giữa hai lần poll vẫn tạo lượt mới.
- `CaptureGate` kiểm soát capture/inference đang chạy và generation. Khi nghỉ phải tắt cả background capture, không chỉ vòng suy luận.
- Ghép cân/chiều cao kiểm tra lại session dưới cùng critical section. Không đưa cân của người mới vào kết quả AI của lượt cũ.
- Viewer dùng `SessionFilter` để bỏ ảnh của lượt cũ, xóa ảnh khi STANDBY/DRAINING và hiển thị kết quả trong RESULT_HOLD.
- USB có một mutex theo toàn packet, chia khối 512 B với TX buffer 4096 B, giữ CRC32 và timeout; giữ chẩn đoán nguồn TV2.
- Script build giữ profile EIM, PM/Tickless/BLE modem sleep và các thông số Wi-Fi/lwIP của nhánh camera. Không xóa toàn bộ sdkconfig/managed_components để áp dụng mặc định.

## Kiểm chứng phần mềm

```powershell
python -m unittest discover -s tests -p 'test_*.py' -v
g++ -std=c++17 -I lib/Measurements -I firmware/edge_yolo/main tests/camera_power_integration_test.cpp -o .pio/camera_power_integration_test.exe
.\.pio\camera_power_integration_test.exe
g++ -std=c++17 -I lib/Measurements tests/measurement_state_test.cpp -o .pio/measurement_state_test.exe
.\.pio\measurement_state_test.exe
```

Các ca mới gồm: không ngủ khi HTTP/inference chưa kết thúc, bỏ ảnh sau bước xuống, đổi người nhanh giữa hai poll, camera mất người không xóa tải cân, timeout và tràn bộ đếm thời gian, viewer loại ảnh cũ và generation rollover.

### BLE scan retry

Khi bắt đầu scan lỗi tạm thời, `edge_io.cpp` hẹn retry bằng NimBLE callout:
100, 200, 400, 800, 1600 rồi tối đa 2000 ms (cộng một tick để không chạy sớm).
Thành công thì hủy retry và reset backoff. `EINVAL` / `ENOTSUP` báo lỗi cấu hình
và không tự retry. Start/cancel và callback retry chạy trên cùng hàng đợi host;
yêu cầu đổi Active/Standby dùng chế độ mong muốn mới nhất. Không đổi duty cycle.
Nếu cancel khi đổi chế độ thất bại, cũng hẹn xử lý lại thay vì bỏ yêu cầu.

```powershell
g++ -std=c++17 -Wall -Wextra -Werror -I firmware/edge_yolo/main tests/ble_scan_retry_test.cpp -o "$env:TEMP/ble_scan_retry_test.exe"
& "$env:TEMP/ble_scan_retry_test.exe"
```

Test đồng hồ giả kiểm tra deadline/backoff, hai lần thất bại rồi thành công,
hủy/thay deadline cũ và tràn bộ đếm. Test này chưa mô phỏng radio hoặc scheduler
NimBLE. Khi có bo, cần ép lỗi start có kiểm soát để xác nhận tự phục hồi, đồng
thời thử đổi Active/Standby trong lúc chờ retry. Phục hồi BLE host reset chưa
thuộc thay đổi này.

### Wi-Fi reconnect không chặn event loop

`wifiEventHandler()` không delay khi mất mạng. Timer một lần sau 2 giây chỉ
post sự kiện; `esp_wifi_connect()` và cập nhật cấu hình chạy trên default event
loop. STA_CONNECTED/GOT_IP hủy retry. Khi đổi SSID/password, dừng Wi-Fi, bỏ qua
sự kiện cũ trong lúc chờ STA_STOP rồi áp dụng cấu hình mới nhất. SSID rỗng dừng
Wi-Fi và không retry. Timer event cũ phải qua kiểm tra deadline hiện tại, không
được kết nối sớm hoặc tạo thêm một yêu cầu sau khi đã kết nối.

```powershell
g++ -std=c++17 -Wall -Wextra -Werror -I firmware/edge_yolo/main tests/wifi_reconnect_test.cpp -o "$env:TEMP/wifi_reconnect_test.exe"
& "$env:TEMP/wifi_reconnect_test.exe"
```

Test này kiểm tra policy thời gian/cancel, không mô phỏng driver hoặc thứ tự
sự kiện STA_STOP trên bo. Kiểm thử thật: tắt/bật lại AP, đổi SSID/password qua
USB khi retry đang chờ, kiểm tra IP mới và ảnh camera trở lại; đồng thời kiểm
tra cân/LCD vẫn phản hồi.

### Kiểm tra thông tin Wi-Fi

`wifi_credentials.h` kiểm tra thông tin trước khi lưu NVS và trước khi áp dụng
cho driver. Mật khẩu trống chọn `WIFI_AUTH_OPEN`; có mật khẩu giữ ngưỡng tối
thiểu `WIFI_AUTH_WPA2_PSK`. Chưa thêm hỗ trợ mạng chỉ dùng WPA1.
SSID tối đa 32 byte, passphrase 8-63 byte hoặc khóa PSK 64 ký tự hex. Sao chép
đủ các byte vào mảng driver, không cắt ký tự cuối ở độ dài tối đa. Chuỗi ngắn
được đệm zero; không ghi thêm NUL vượt mảng khi chuỗi đầy. USB từ chối cả giao
dịch khi thông tin sai, không ghi NVS hoặc đổi kết nối hiện tại. Không log mật
khẩu. Độ dài tính bằng byte UTF-8, không phải số ký tự hiển thị.

```powershell
g++ -std=c++17 -Wall -Wextra -Werror -I firmware/edge_yolo/main tests/wifi_credentials_test.cpp -o "$env:TEMP/wifi_credentials_test.exe"
& "$env:TEMP/wifi_credentials_test.exe"
```

Test bao gồm mạng mở, passphrase, biên 32/63/64 byte, hex không hợp lệ, UTF-8,
NUL và xóa phần dư khi đổi từ chuỗi dài sang ngắn. Kết nối mạng mở/SSID dài/PSK
hex trên driver thật vẫn cần thử trên bo.

## Checklist chạy thật

- Kiểm tra `http://IP_CAMERA/capture` từ laptop, chỉ dùng một client trong lúc đo. Cấu hình SSID/mật khẩu/URL bằng `camera/usb_viewer.py --port COM11 --configure`; không ghi mật khẩu vào Git hoặc chat.
- Thử `-CameraContinuous` trước: ảnh thật, khung người, số cân LOCK và LCD, sau đó hiệu chuẩn và đối chiếu chiều cao. Không dùng số mặc định làm bằng chứng đo chính xác.
- Thử bản tự Standby ít nhất 10 lượt: nghỉ không có HTTP/AI, bước lên lấy ảnh mới, bước xuống dừng cả worker, giữ kết quả đúng 10 giây, lượt sau không hiện số/ảnh người cũ.
- Thử bước xuống giữa lúc tải ảnh/suy luận, unload/reload nhanh, mất Wi-Fi/camera, hai người. Theo dõi free PSRAM và reset khi kết hợp Wi-Fi/BLE/AI.
- Xem nguồn bằng `python camera/power_monitor.py --port COM11 --seconds 30` (không mở đồng thời viewer). Độ trễ quét 10% từng đo khoảng 3–4 giây, 100% dưới 1 giây theo quan sát của người thử trên bản chỉ cân; phải đo lại trên bản tích hợp.

Build/unit test thành công chưa xác nhận end-to-end camera thật. Cần URL, mạng, bố trí và thông số hiệu chuẩn thực tế trước khi nghiệm thu.

Tình trạng kiểm tra phần mềm của lần ghép này: cả ba chế độ build đã thành công; 17 kiểm thử Python và ba chương trình kiểm thử C++ (measurement state, camera power integration, geometry) đã pass. URL được cung cấp là `http://10.127.7.68/capture`, nhưng chưa xác nhận JPEG/kết quả đo từ camera thật. Chưa nạp bản ghép lên bo trong lần chuẩn bị này.
