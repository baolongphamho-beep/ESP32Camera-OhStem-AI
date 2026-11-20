> **Bàn giao tài liệu TV5 — 20/11/2026.** Nội dung mô tả bản phát triển trong LAB. Nhánh này chỉ bổ sung tài liệu, chưa đưa mã TV5/model 192 lên GitHub; các lệnh dùng tính năng mới cần bản mã LAB tương ứng. Kết quả kiểm thử PC không thay cho nghiệm thu cân/servo thật.

> **Bổ sung sau khi cài SDK/nạp bo:** native đã full build ESP-IDF 6.1, nạp UNO COM5 và kiểm tra STANDBY đạt. Xem [biên bản mới](NATIVE_BUILD_FLASH_20261118.md). Báo cáo dưới đây giữ kết quả/thông tin tại thời điểm gộp trước khi cài SDK.

# So sánh và gộp GitHub cho Plan 2 — 18/11/2026

Nguồn: [baolongphamho-beep/ESP32Camera-OhStem-AI](https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI), nhánh mặc định `smartscale-edge-ai`, commit [`c2f71b65c7e8e76567cf5d14f6b8240a50ab70a7`](https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI/commit/c2f71b65c7e8e76567cf5d14f6b8240a50ab70a7) (18/11/2026). Local ban đầu HEAD `547d3d9`; đang có thay đổi chưa commit trong viewer, test viewer, bản đồ file, handoff và hướng dẫn Plan 2 — đã giữ lại.

Đối chiếu 81 file văn bản từ tree cố định: **23 giống, 30 khác, 28 thiếu**. Model `.espdl` lấy riêng: 3.082.720 byte, Git blob `ac64ae9a6f4d92c73d74f03b54e41a8b9f66a542`, SHA256 khớp build provenance. File JSON debug lượng tử hóa (~691 KB) không dùng khi build nên không nhập; giữ file `.build.json` mô tả nguồn/hash.

## Quyết định gộp

| Nhóm | Quyết định | Lý do |
|---|---|---|
| `lib/Measurements`, BLE/Scale, Camera/Display, `src/main.cpp`, USB viewer/protocol, test local | Giữ bản local | Đã bổ sung ResultHold, cấu hình MAC/servo, sửa UART P3, bảo vệ thread và LCD; upstream cũ hơn ở các phần này |
| `platformio.ini` | Giữ mọi env; đổi mặc định sang `yolo_uno_servo_sweep` | Phù hợp hướng nhóm chốt; env relay vẫn chạy riêng |
| Native `main/`, `tools/`, model metadata | Khôi phục rồi tích hợp Plan 2 | Trước gộp thiếu mã nên không thể tái build Edge AI |
| Native capture/USB | Giữ CRC32, chunked write, JPEG checks, worker Core 0/hai buffer; thêm capture gate | Prefetch liên tục qua một lần đổi góc có thể ghép ảnh cũ với góc mới |
| Native BLE/PM | Lọc đúng MAC; raw scale snapshot độc lập camera; standby 10% duty, DFS/Wi-Fi PS, không ngắt BLE trong HTTP | Wake không phụ thuộc camera và tránh mất load-removed |
| Hình học/FSM/UI | Tái dùng header `lib/EdgeSweep` cho native | Framing 3%, tilt từ servo, median, 5 pha, hold và câu chữ LCD khớp bản test |
| Cấu hình native | `EdgeDeviceConfig : EdgeConfig`, BEGIN/CFG/APPLY có validation, NVS | Dùng toàn bộ khóa USB Plan 2, bỏ ảnh đang xử lý sau APPLY; không ghi mật khẩu ra log |
| Tài liệu/kết quả | Nhập kế hoạch, setup/hardware/research còn thiếu; viết lại native README và thêm hướng dẫn hiện tại | Tách rõ baseline upstream với kết quả kiểm chứng mới |

Không merge Git tự động vì local được khởi tạo bằng lịch sử riêng và có thay đổi chưa commit. Không commit, push hay nạp firmware. Backup **101 file** trước khi chỉnh: `.edge-deps/before-plan2-merge.zip`; bản nguồn so sánh: `.edge-deps/upstream-c2f71b6/`; diff đầy đủ trước gộp: `.edge-deps/upstream-to-local.diff`. Các bản này chỉ ở máy và gitignore.

## Kiểm chứng

- `tools/run_all_tests.ps1`: **12 PASS, 0 FAIL, 0 SKIP** (9 chương trình C++, 66 test Python chính + 3 integration; 6 ca QA trên log mô phỏng).
- `pio run -e yolo_uno_servo_sweep`: **SUCCESS**, RAM 38.196/327.680 byte, flash 978.040/3.342.336 byte. Có cảnh báo MCPWM cũ từ thư viện ESP32Servo.
- Driver servo và I/O native: `-fsyntax-only` bằng toolchain Xtensa và header SDK 5.5.5 hiện có: PASS. Đây không phải build/link ESP-IDF native.
- Model tải từ commit cố định và kiểm tra cả Git blob SHA1 lẫn SHA256: PASS.
- Native full build: chưa chạy được vì **máy chưa cài ESP-IDF 6.1**; script build báo rõ SDK thiếu. Không tự cài bộ công cụ hệ thống.

## Còn cần hoàn thiện/đo thực tế theo Plan 2

- Cài ESP-IDF, chuẩn bị ESP-DL pinned, full build native; xác minh flash/PSRAM trên bo trước khi nạp (partition native 16 MB, cấu hình Arduino 8 MB).
- Xác minh cân thật phát `0x181D`; nếu bắt buộc GATT `2A9D`, cần viết fallback. Giữ env scanner/GATT Arduino hiện có.
- Chưa đo dòng, tốc độ wake, sai số chiều cao, brownout hoặc 20–30 chu kỳ trên phần cứng. Automatic light sleep chưa bật, driver chưa có ramp homing riêng.
- Mất gói load-removed có thể khiến presence tồn tại đến TTL 30 s; giới hạn này cần đánh giá bằng cân thật.
- Hiệu chuẩn `servo_level`, `servo_dir`, hành trình cơ khí, `height`, `focal`. GPIO10 chỉ dùng servo trong Plan 2; cân phải luôn có pin để wake BLE.

## Danh mục đối chiếu trước gộp

| File trên GitHub | So với local ban đầu |
|---|---|
| `.gitignore` | Khác — đã rà diff |
| `platformio.ini` | Khác — đã rà diff |
| `README.md` | Khác — đã rà diff |
| `requirements.txt` | Thiếu — đã bổ sung |
| `boards/yolo_uno.json` | Giống |
| `boards/yolo_uno_8MB.csv` | Thiếu — đã bổ sung |
| `camera/geometry.py` | Giống |
| `camera/protocol.py` | Khác — đã rà diff |
| `camera/README.md` | Khác — đã rà diff |
| `camera/requirements-usb.txt` | Giống |
| `camera/requirements.txt` | Giống |
| `camera/settings.example.json` | Giống |
| `camera/smartscale_camera.py` | Khác — đã rà diff |
| `camera/start_smartscale.ps1` | Khác — đã rà diff |
| `camera/usb_protocol.py` | Khác — đã rà diff |
| `camera/usb_viewer.py` | Khác — đã rà diff |
| `docs/camera_hardware_context.md` | Thiếu — đã bổ sung |
| `docs/ON_DEVICE_INFERENCE.md` | Giống |
| `docs/PLAN_1_AI_CO_IMPLEMENTATION.md` | Thiếu — đã bổ sung |
| `docs/PLAN_2_TEAM_5_DIVISION.md` | Thiếu — đã bổ sung |
| `docs/SETUP_GUIDE_NEW_LAPTOP.md` | Thiếu — đã bổ sung |
| `include/README` | Giống |
| `lib/README` | Giống |
| `src/ble_scanner.cpp` | Giống |
| `src/edge_probe.cpp` | Giống |
| `src/main.cpp` | Khác — đã rà diff |
| `tests/display_probe_test.cpp` | Khác — đã rà diff |
| `tests/edge_geometry_test.cpp` | Giống |
| `tests/measurement_state_test.cpp` | Khác — đã rà diff |
| `tests/test_camera_geometry.py` | Giống |
| `tests/test_camera_protocol.py` | Khác — đã rà diff |
| `tests/test_usb_protocol.py` | Khác — đã rà diff |
| `variants/yolo_uno/pins_arduino.h` | Giống |
| `tests/lcd_stubs/Arduino.h` | Giống |
| `tests/lcd_stubs/LiquidCrystal_I2C.h` | Khác — đã rà diff |
| `tests/lcd_stubs/Wire.h` | Giống |
| `lib/BLE/ble.cpp` | Khác — đã rà diff |
| `lib/BLE/ble.h` | Giống |
| `lib/calib_fsm/calib_fsr.cpp` | Giống |
| `lib/calib_fsm/calib_fsr.h` | Giống |
| `lib/Camera/camera.cpp` | Khác — đã rà diff |
| `lib/Camera/camera.h` | Giống |
| `lib/Config/config.h` | Khác — đã rà diff |
| `lib/Display/display.cpp` | Khác — đã rà diff |
| `lib/Display/display.h` | Khác — đã rà diff |
| `lib/GM65/GM65_scanner.cpp` | Giống |
| `lib/GM65/GM65_scanner.h` | Giống |
| `lib/Measurements/camera_packet.h` | Giống |
| `lib/Measurements/measurements.cpp` | Khác — đã rà diff |
| `lib/Measurements/measurements.h` | Khác — đã rà diff |
| `lib/Measurements/measurement_state.h` | Khác — đã rà diff |
| `lib/Scale/scale.cpp` | Khác — đã rà diff |
| `lib/Scale/scale.h` | Khác — đã rà diff |
| `firmware/edge_yolo/build.ps1` | Khác — đã rà diff |
| `firmware/edge_yolo/CMakeLists.txt` | Khác — đã rà diff |
| `firmware/edge_yolo/dependencies.lock` | Khác — đã rà diff |
| `firmware/edge_yolo/partitions.csv` | Giống |
| `firmware/edge_yolo/README.md` | Khác — đã rà diff |
| `firmware/edge_yolo/sdkconfig.defaults` | Khác — đã rà diff |
| `firmware/edge_yolo/main/app_main.cpp` | Thiếu — đã bổ sung |
| `firmware/edge_yolo/main/CMakeLists.txt` | Thiếu — đã bổ sung |
| `firmware/edge_yolo/main/edge_geometry.h` | Thiếu — đã bổ sung |
| `firmware/edge_yolo/main/edge_io.cpp` | Thiếu — đã bổ sung |
| `firmware/edge_yolo/main/edge_io.h` | Thiếu — đã bổ sung |
| `firmware/edge_yolo/models/yolo26n_224_s8_s3.build.json` | Thiếu — đã bổ sung |
| `firmware/edge_yolo/tools/prepare_sources.ps1` | Thiếu — đã bổ sung |
| `firmware/edge_yolo/tools/quantize.py` | Thiếu — đã bổ sung |
| `firmware/edge_yolo/tools/requirements.txt` | Thiếu — đã bổ sung |
| `docs/camera_research/.gitignore` | Thiếu — đã bổ sung |
| `docs/camera_research/camera_webui_1.html` | Thiếu — đã bổ sung |
| `docs/camera_research/camera_webui_2.html` | Thiếu — đã bổ sung |
| `docs/camera_research/camera_webui_3.html` | Thiếu — đã bổ sung |
| `docs/camera_research/extract_details.py` | Thiếu — đã bổ sung |
| `docs/camera_research/find_all_uart.py` | Thiếu — đã bổ sung |
| `docs/camera_research/find_uart_format.py` | Thiếu — đã bổ sung |
| `docs/camera_research/inspect_camera.py` | Thiếu — đã bổ sung |
| `docs/camera_research/pedestrian_detector.py` | Thiếu — đã bổ sung |
| `docs/camera_research/README.md` | Thiếu — đã bổ sung |
| `docs/camera_research/README_SMARTSCALE.md` | Thiếu — đã bổ sung |
| `docs/camera_research/read_all_secrets.py` | Thiếu — đã bổ sung |
| `docs/camera_research/read_partitions.py` | Thiếu — đã bổ sung |
