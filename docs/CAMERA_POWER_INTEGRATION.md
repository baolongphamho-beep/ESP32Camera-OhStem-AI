# Camera AI + TV2 BLE/Power Integration

Tài liệu tích hợp kiến trúc quản lý nguồn / Standby theo cân Mi Scale 2 vào Pipeline 1 (`firmware/edge_yolo/`).

## Bốn chế độ build

Chạy tại thư mục gốc dự án; thay `COM16` bằng cổng thực tế của YOLO UNO. Đóng mọi Serial Monitor / viewer trước khi nạp.

```powershell
# 1. Cân + LCD + nguồn, không camera (kiểm tra riêng BLE + Power; mặc định StandbyScanPercent = 10).
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -ScaleOnly -Flash -Port COM16

# 2. Camera + AI + cân chạy liên tục (kiểm chuẩn camera/chiều cao không cần bước lên cân trước).
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -CameraContinuous -Flash -Port COM16

# 3. Kiểm thử độc lập driver Servo PWM trên bàn lab (lệnh SERVO <deg>, SERVO SWEEP, SERVO DETACH).
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -ServoTest -Flash -Port COM16

# 4. Chế độ tích hợp mặc định: tự đánh thức camera khi bước lên cân (> 5 kg) và tự về nghỉ (BLE standby 10%).
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -Flash -Port COM16
```

`-ScaleOnly`, `-CameraContinuous` và `-ServoTest` loại trừ nhau (`-ServoTest` dùng để test độc lập driver Servo PWM trên bàn lab qua `camera/servo_bench.py --port COM16`, hỗ trợ `SERVO <deg>`, `SERVO SWEEP`, `SERVO DETACH` và `set servo_*=...`). Không truyền cờ nào thì chọn chế độ tích hợp mặc định. `-StandbyScanPercent` hỗ trợ `10` (mặc định), `25`, `50`, `100`. Bản có camera duy trì BLE passive scan `25 ms / 200 ms` (`12.5%`) liên tục khi `ACTIVE` thay vì cancel/start lại discovery trên mỗi lần tải HTTP.

## Luồng trạng thái tích hợp

1. Khởi tạo model YOLO26n một lần trong Octal PSRAM và giữ tài nguyên cho các lượt đo sau.
2. `STANDBY`: không tải ảnh HTTP, không chạy YOLO; CPU `80 MHz`, Wi-Fi `WIFI_PS_MIN_MODEM` (thức mỗi DTIM1 để frame đầu tiên sau khi đánh thức không bị trễ DTIM3), đèn nền LCD **tắt** (`LCD=OFF`), BLE quét theo `StandbyScanPercent` (mặc định `10%`, `10 ms / 100 ms`). Các tác vụ ứng dụng ngủ theo cơ chế event-driven (`portMAX_DELAY` khi rút USB) để tối đa hóa thời gian Auto Light Sleep.
3. Đúng MAC cân đã cấu hình (hoặc nhận mọi cân khi đặt `00:00:00:00:00:00`), tải hợp lệ `> 5 kg`: đánh thức tức thời sang `ACTIVE`, CPU `240 MHz`, Wi-Fi `WIFI_PS_NONE`, đèn nền LCD bật sáng (`LCD=ON`); Core 0 tải ảnh `/capture` (tự động đặt camera về `QVGA 320×240`), Core 1 chạy suy luận YOLO26n.
4. Chiều cao hợp lệ và cân `LOCK` cùng lượt: hiển thị `H/W/D/BMI`, lưu snapshot kết quả hợp lệ cuối cùng.
5. Bước xuống hoặc dữ liệu cân hết hạn: chuyển sang `DRAINING`, ngừng cấp việc mới qua `CaptureGate`. Tác vụ HTTP và suy luận đang chạy dở được hoàn tất tự nhiên (bảo vệ bằng `CaptureLease` / `InferenceLease`) rồi đánh thức ngay task điều phối; kết quả của generation cũ bị loại bỏ.
6. Khi cả hai worker đã nhả tài nguyên (`s_gate.canSleep()`): hạ CPU về `80 MHz`. Nếu lượt vừa rồi có BMI hợp lệ và `hold_sec > 0` thì vào `RESULT_HOLD`, **giữ nguyên đèn nền LCD sáng liền mạch** hiển thị kết quả trong `hold_sec` giây (mặc định `10 giây`, cấu hình `0..60 giây` qua `--configure`) trước khi tắt đèn nền về `STANDBY`. Nếu có người mới bước lên cân thì hủy hold và bắt đầu ngay lượt mới.

Công cụ `D:\PROJECT\.venv\Scripts\python.exe camera/power_monitor.py --port COM16` đọc telemetry `SSV1` và in trạng thái đèn nền `LCD=ON/OFF` cùng `H/BMI/HOLD` khi ở pha `RESULT_HOLD`.

## Kiểm chứng phần mềm (Host Unit Tests)

```powershell
python -m unittest discover -s tests -p "test_*.py" -v
```

Các bài kiểm tra C++ (`g++ -std=c++17 -Wall -Wextra -Werror`):
- `tests/measurement_state_test.cpp`
- `tests/edge_geometry_test.cpp`
- `tests/display_probe_test.cpp`
- `tests/http_capture_parser_test.cpp`
- `tests/ble_scan_retry_test.cpp`
- `tests/camera_power_integration_test.cpp`
- `tests/wifi_credentials_test.cpp`
- `tests/wifi_reconnect_test.cpp`
- `tests/standby_schedule_test.cpp`
- `tests/servo_ctrl_test.cpp`
- `tests/servo_driver_test.cpp` (biên dịch cùng `firmware/edge_yolo/main/servo_ctrl.cpp` và `tests/servo_stubs/`)
