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

## Checklist chạy thật

- Kiểm tra `http://IP_CAMERA/capture` từ laptop, chỉ dùng một client trong lúc đo. Cấu hình SSID/mật khẩu/URL bằng `camera/usb_viewer.py --port COM11 --configure`; không ghi mật khẩu vào Git hoặc chat.
- Thử `-CameraContinuous` trước: ảnh thật, khung người, số cân LOCK và LCD, sau đó hiệu chuẩn và đối chiếu chiều cao. Không dùng số mặc định làm bằng chứng đo chính xác.
- Thử bản tự Standby ít nhất 10 lượt: nghỉ không có HTTP/AI, bước lên lấy ảnh mới, bước xuống dừng cả worker, giữ kết quả đúng 10 giây, lượt sau không hiện số/ảnh người cũ.
- Thử bước xuống giữa lúc tải ảnh/suy luận, unload/reload nhanh, mất Wi-Fi/camera, hai người. Theo dõi free PSRAM và reset khi kết hợp Wi-Fi/BLE/AI.
- Xem nguồn bằng `python camera/power_monitor.py --port COM11 --seconds 30` (không mở đồng thời viewer). Độ trễ quét 10% từng đo khoảng 3–4 giây, 100% dưới 1 giây theo quan sát của người thử trên bản chỉ cân; phải đo lại trên bản tích hợp.

Build/unit test thành công chưa xác nhận end-to-end camera thật. Cần URL, mạng, bố trí và thông số hiệu chuẩn thực tế trước khi nghiệm thu.

Tình trạng kiểm tra phần mềm của lần ghép này: cả ba chế độ build đã thành công; 17 kiểm thử Python và ba chương trình kiểm thử C++ (measurement state, camera power integration, geometry) đã pass. URL được cung cấp là `http://10.127.7.68/capture`, nhưng chưa xác nhận JPEG/kết quả đo từ camera thật. Chưa nạp bản ghép lên bo trong lần chuẩn bị này.
