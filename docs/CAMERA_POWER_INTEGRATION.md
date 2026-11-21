# Camera AI + TV2 BLE/Power Integration

Tài liệu tích hợp kiến trúc quản lý nguồn / Standby theo cân Mi Scale 2 vào Pipeline 1 (`firmware/edge_yolo/`).

## Ba chế độ build

Chạy tại thư mục gốc dự án; thay `COM13` bằng cổng thực tế của YOLO UNO. Đóng mọi Serial Monitor / viewer trước khi nạp.

```powershell
# 1. Cân + LCD + nguồn, không camera (kiểm tra riêng BLE + Power).
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -ScaleOnly -StandbyScanPercent 10 -Flash -Port COM13

# 2. Camera + AI + cân chạy liên tục (kiểm chuẩn camera/chiều cao không cần bước lên cân trước).
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -CameraContinuous -Flash -Port COM13

# 3. Chế độ tích hợp mặc định: tự đánh thức camera khi bước lên cân (> 5 kg) và tự về nghỉ.
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -StandbyScanPercent 10 -Flash -Port COM13
```

`-ScaleOnly` và `-CameraContinuous` loại trừ nhau. Không truyền cả hai thì chọn chế độ tích hợp mặc định. `-StandbyScanPercent` hỗ trợ `10`, `25`, `50`, `100`. Bản có camera duy trì BLE passive scan `25 ms / 200 ms` (`12.5%`) liên tục khi `ACTIVE` thay vì cancel/start lại discovery trên mỗi lần tải HTTP.

## Luồng trạng thái tích hợp

1. Khởi tạo model YOLO26n một lần trong Octal PSRAM và giữ tài nguyên cho các lượt đo sau.
2. `STANDBY`: không tải ảnh HTTP, không chạy YOLO; CPU `80 MHz`, Wi-Fi `MAX_MODEM`, đèn nền LCD tắt, BLE quét theo `StandbyScanPercent`.
3. Đúng MAC cân đã cấu hình (hoặc nhận mọi cân khi đặt `00:00:00:00:00:00`), tải hợp lệ `> 5 kg`: chuyển sang `ACTIVE`, CPU `240 MHz`, Wi-Fi `WIFI_PS_NONE`, LCD bật sáng; Core 0 tải ảnh `/capture`, Core 1 chạy suy luận YOLO26n.
4. Chiều cao hợp lệ và cân `LOCK` cùng lượt: hiển thị `H/W/D/BMI`, lưu snapshot kết quả hợp lệ cuối cùng.
5. Bước xuống hoặc dữ liệu cân hết hạn: chuyển sang `DRAINING`, ngừng cấp việc mới qua `CaptureGate`. Tác vụ HTTP và suy luận đang chạy dở được hoàn tất tự nhiên, kết quả của generation cũ bị loại bỏ.
6. Khi cả hai worker đã nhả tài nguyên (`s_gate.canSleep()`): hạ CPU về `80 MHz`. Nếu lượt vừa rồi có BMI hợp lệ thì vào `RESULT_HOLD`, giữ LCD sáng hiển thị kết quả trong `10 giây` trước khi tắt đèn nền về `STANDBY`. Nếu có người mới bước lên cân thì hủy hold và bắt đầu ngay lượt mới.

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
