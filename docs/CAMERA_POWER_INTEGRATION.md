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

## Luồng trạng thái tích hợp (Step-Scan FSM 5 trạng thái)

1. Khởi tạo model YOLO26n một lần trong Octal PSRAM và khởi tạo kênh LEDC Servo (ở trạng thái detached) theo cấu hình NVS.
2. `STANDBY`: không tải ảnh HTTP, không chạy YOLO; PWM Servo tắt (`servoDetach()`), CPU `80 MHz`, Wi-Fi `WIFI_PS_MIN_MODEM`, đèn nền LCD **tắt** (`LCD=OFF`), BLE quét theo `StandbyScanPercent` (mặc định `10%`). Các tác vụ ứng dụng ngủ theo cơ chế event-driven (`portMAX_DELAY` khi rút USB) để tối đa hóa thời gian Auto Light Sleep.
3. Đúng MAC cân đã cấu hình (hoặc nhận mọi cân khi đặt `00:00:00:00:00:00`), tải hợp lệ `> 5 kg`: đánh thức tức thời sang `WAKING_HOMING` (hoặc `ACTIVE` nếu `servo_pin=-1`), nâng CPU `240 MHz`, Wi-Fi `WIFI_PS_NONE`, bật LCD (`LCD=ON`), đưa Servo về góc dưới (`servo_bottom`, mặc định `112°`) và chờ ổn định.
4. `SERVO_SWEEP`: Core 0 và Core 1 đồng bộ qua `ServoCaptureSync` (`poseEpoch` + xả 1 frame đệm camera sau mỗi bước nhích để tránh khung hình mờ/lệch góc). Nếu `TopClipped` / `None`, Step-Scan nhích từng bước `servo_step` lên `servo_top` (`85°`); nếu `BottomClipped` sau khi bước lên, lùi nửa bước 1 lần (tránh dao động); `TooClose` / `SideClipped` / `Multiple` dừng giữ góc và báo nhắc trên LCD.
5. `LOCKED_MEASURE`: khi `analyzeFraming()` trả về `FULL_BODY`, khóa góc quét hiện tại, tính `tiltDeg` động từ góc servo và `groundHeight()`, lấy trung vị 3 mẫu liên tiếp rồi **chốt chiều cao & nhả PWM Servo** (`servoDetach()`), chờ cân chốt `LOCK` → tính BMI (cảnh báo timeout nếu cân chưa chốt sau 15 giây).
6. Bước xuống hoặc dữ liệu cân hết hạn: chuyển sang `DRAINING`, ngừng cấp việc mới qua `CaptureGate`, nhả Servo an toàn; khi cả worker & servo đã rảnh (`s_gate.canSleep() && s_servo_sync.canSleep()`), hạ CPU về `80 MHz`. Nếu lượt vừa rồi có BMI hợp lệ và `hold_sec > 0` thì vào `RESULT_HOLD`, **giữ nguyên đèn nền LCD sáng liền mạch** hiển thị kết quả trong `hold_sec` giây (mặc định `10 giây`) trước khi tắt đèn nền về `STANDBY`.

Các gói JSON `SSV1` (`frame` & `scale`) bổ sung trường `phase`, `servo_deg`, `tilt_deg`, `framing` (cho gói `frame`) và `servo_attached` để hiển thị trực tiếp trên HUD của `usb_viewer.py` và `power_monitor.py`.

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
- `tests/step_scan_fsm_test.cpp` (kiểm thử `StepScanFsm` & `ServoCaptureSync` trong `step_scan_fsm.h`)
