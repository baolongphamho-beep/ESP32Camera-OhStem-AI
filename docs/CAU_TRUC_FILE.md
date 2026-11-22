# Bản đồ file của project

File này giải thích **mỗi file dùng để làm gì** và **các file nối với nhau thế nào**, để cả nhóm dễ hình dung trước khi đọc code. Muốn biết cách chạy từng phần thì xem `README.md`.

Cập nhật bổ sung: 19/11/2026.

**Ghép từ GitHub 22/11:** `firmware/edge_yolo/main/http_capture_client.h` + `http_capture_parser.h` (lấy ảnh camera bằng socket, hạn thời gian từng bước), `wifi_reconnect.h`, `wifi_credentials.h` (Wi-Fi nối lại sau 2 s, WPA/WPA2/Open), `ble_scan_retry.h` (quét BLE thử lại); test tương ứng trong `tests/`. Xem mục 13 [báo cáo 22/11](reports/TV5_GITHUB_SYNC_20261122.md).

**TV5 22/11 (đọc lại cấu hình, đo, báo cáo):** firmware native trả lời lệnh USB `CONFIG?` bằng gói `config_values` (không có mật khẩu); `usb_viewer.py --show-config` và lệnh `config` trong `servo_jog.py` dùng nó. `tools/servo_delay_probe.py` đo độ trễ ảnh camera sau khi servo dừng để chọn `servo_delay`. `tools/height_error_report.py` tổng hợp sai số chiều cao từ `docs/QA_FIELD_LOG.csv`. Test: `tests/test_config_readback.py`, `tests/test_servo_delay_probe.py`, `tests/test_height_error_report.py`.

**TV5 22/11 (servo):** `tools/servo_jog.py` quay servo bằng tay qua USB để hiệu chuẩn `servo_level`/`servo_dir`/`servo_bottom`/`servo_top` rồi `save` (firmware native, lệnh `SERVO <góc>`/`SERVO OFF`, logic lease ở `firmware/edge_yolo/main/servo_jog.h`; test `tests/test_servo_jog.py`, `tests/edge_native_plan2_test.cpp`). `camera/serial_port.py` tự tìm cổng YOLO UNO cho mọi công cụ.

**TV5 22/11:** `tools/camera_fetch_bench.py` đo thời gian lấy ảnh `/capture` theo từng cặp độ phân giải × chất lượng JPEG từ laptop, rồi trả camera về cài đặt cũ (test `tests/test_camera_fetch_bench.py` dùng camera giả trên localhost). Đối chiếu GitHub mới nhất: [báo cáo 22/11](reports/TV5_GITHUB_SYNC_20261122.md).

**TV5 độc lập:** `camera/telemetry.py` dùng chung định dạng chẩn đoán nguồn và `PacketRecorder` ghi metadata-only JSONL. `camera/usb_viewer.py`, `camera/power_monitor.py`, `tools/native_probe.py` nhận `--log`; `tools/native_qa.py` đọc log native JSONL/SSV1, xuất báo cáo JSON/CSV, không suy ra PASS phần cứng khi thiếu dữ liệu. `tests/test_native_qa.py` và các ca mới của `tests/test_usb_viewer.py` chạy không mở GUI/COM. Mẫu `tests/fixtures/native_qa_sample.jsonl` là dữ liệu giả có nhãn. Xem [hướng dẫn sử dụng](TV5_INDEPENDENT_WORK.md).

**Cập nhật cân TV2:** `camera/power_monitor.py` đọc gói `kind=scale` của nhánh tích hợp upstream (native LAB chưa phát loại gói này). `camera/usb_protocol.py::SessionFilter` và viewer xử lý session/telemetry mới, vẫn đọc firmware cũ; `lib/Measurements/scale_presence.h` là helper cân độc lập đã test, chưa nối vào native. `tests/scale_presence_test.cpp` kiểm TTL/đổi lượt và API weight-only. Xem [đối chiếu 19/11](reports/GITHUB_TV2_REVIEW_20261119.md).

**Bổ sung native 18/11/2026:** `firmware/edge_yolo/main/` chạy YOLO trực tiếp trên UNO bằng ESP-IDF; luồng laptop chạy AI bên dưới mô tả bản Arduino. Native lấy JPEG từ camera qua Wi-Fi rồi gửi ảnh/kết quả về `camera/usb_viewer.py` qua USB. Thêm `--preview` để thử riêng camera, không cần cân/servo; lease RAM trong `main/camera_test.h` tự hết hạn sau 10 s mất heartbeat. `tools/native_probe.py --preview` kiểm tra ảnh/trạng thái không mở GUI. Xem [workflow Wi-Fi/configure](WORKFLOW_WIFI_CONFIGURE.md).

---

## 1. Nhìn tổng thể: ba "bộ não"

Project có ba nơi chạy code:

| Ở đâu | Viết bằng | Làm gì |
| :-- | :-- | :-- |
| **Laptop** | Python | "Nhìn": lấy ảnh từ camera, chạy AI (YOLO tìm người), gửi kết quả dạng chữ cho camera |
| **Camera AI OhStem** | Firmware gốc của OhStem (**không sửa, không nạp lại**) | Phát Wi-Fi, cho lấy ảnh qua `/capture`, nhận chữ qua `/control` rồi chuyển chuỗi đó ra cổng UART |
| **YOLO UNO** (ESP32-S3) | C++ (PlatformIO) | "Quyết định và hiển thị": đọc UART, quay servo, đọc cân BLE, tính chiều cao + BMI, bật/tắt nguồn cân, in lên LCD |

Đường đi của dữ liệu (giống nhau ở mọi chế độ):

```
Laptop (Python) ──HTTP /capture──> lấy ảnh
Laptop ──HTTP /control?serial=NHAN;0.93;stop──> Camera OhStem ──UART 115200──> YOLO UNO ──> LCD
                                                                  Cân Mi Scale 2 ──BLE──┘
```

---

## 2. Bắt đầu từ đâu? (theo việc muốn làm)

| Muốn làm | File chính trên laptop | Env PlatformIO nạp cho UNO | File chính trên UNO |
| :-- | :-- | :-- | :-- |
| **P5 theo Plan 2: servo quét tìm toàn thân** (hướng nhóm chốt) | `vision/body_tracker.py` (+ `camera/usb_viewer.py` để cấu hình) | `yolo_uno_servo_sweep` | `src/app_main.cpp` + `lib/EdgeSweep/` |
| P5 servo, dò chân rồi dò đầu (bản cũ) | `pedestrian_detector.py` (tự động) / `manual_height_test.py` (bấm phím) | `yolo_uno_height_servo` hoặc `yolo_uno_height_servo_bmi` | `src/height_servo_main.cpp` / `src/height_servo_bmi_main.cpp` + `lib/HeightCamera/` |
| P5 camera cố định (không servo) | `camera/smartscale_camera.py` (chạy qua `camera/start_smartscale.ps1`) | `yolo_uno_camera_lcd` | `src/main.cpp` |
| P3 phát hiện người đi bộ + khoảng cách | `pedestrian_distance.py` | `yolo_uno_camera_lcd` | `src/main.cpp` |
| Thử servo, hiệu chuẩn góc | (không cần) | `yolo_uno_servo_test` | `src/servo_test.cpp` |
| Tìm địa chỉ MAC của cân | (không cần) | `yolo_uno_ble_scan` | `src/ble_scanner.cpp` |
| Giao diện web (hoa quả + sức khỏe) | `integration/app.py` | (đọc cân qua cổng Serial) | |

---

## 3. Thư mục gốc

| File | Công dụng |
| :-- | :-- |
| `README.md` | Hướng dẫn chính cho nhóm: đối chiếu yêu cầu đề, bảng env, lệnh chạy, nối dây relay và servo, Plan 2 servo quét (mục 10) |
| `CLAUDE.md` | Ghi chú cho Claude Code (bối cảnh, quy tắc, lệnh test). Người cũng đọc được, rất ngắn gọn |
| `platformio.ini` | Khai báo mọi **env** của firmware YOLO UNO. Mỗi env chọn một file trong `src/` + các cờ (chân, thông số). Giá trị mặc định lần đầu của Plan 2 (`SWEEP_*`) nằm trong env `yolo_uno_servo_sweep`; sau đó đổi bằng `usb_viewer.py` |
| `boards/yolo_uno.json`, `variants/yolo_uno/pins_arduino.h` | Mô tả bo YOLO UNO cho PlatformIO, kèm bảng chân (D5/D6 = GPIO8/9, A1 = GPIO2…) |
| `yolo_uno_8MB.csv` | Bảng phân vùng flash 8 MB của UNO |
| `ESP32Camera-OhStem-AI.code-workspace` | Mở project trong VS Code, có sẵn task chạy script Python |
| `ESP32Camera-OhStem-AI-main.code-workspace` | Workspace do nhóm tạo, mở kèm folder `../HeightMeasure_YoloUNO` |
| `.vscode/` | Cấu hình VS Code (cmake trỏ tới `firmware/edge_yolo`, debug, IntelliSense) |
| `yolo26n.pt` | Model YOLO dùng chung cho mọi script Python (phát hiện người) |
| `pedestrian_distance.py` | **P3:** phát hiện người đi bộ (chọn người gần nhất), vẽ bounding box, ước lượng kích thước + khoảng cách, cảnh báo khi < 2,5 m, gửi `PED_WARN_D…` / `PED_SAFE_D…` / `NO_PEOPLE` về UNO để hiện lên LCD (env `yolo_uno_camera_lcd`) |
| `pedestrian_detector.py` | **P5 bản cũ, tự động:** YOLO tìm người, gửi `FOOT_ABOVE/BELOW/CENTER` rồi `HEAD_…` để servo dò chân rồi dò đỉnh đầu |
| `manual_height_test.py` | **P5 bản cũ, thủ công:** bấm W/S để quay servo, F/H để đánh dấu chân/đầu, R để làm lại |
| `inspect_camera.py`, `extract_details.py`, `find_all_uart.py`, `find_uart_format.py`, `read_all_secrets.py`, `read_partitions.py` | Công cụ phân tích **file backup** firmware camera (chỉ đọc file, không đụng tới camera). Nhóm dùng để tìm ra camera gửi gì qua UART. `extract_details.py` tạo `camera_secrets.txt` (đã gitignore vì có thể chứa mật khẩu Wi-Fi) |
| `camera_webui_1/2/3.html` | Trang web trích từ firmware camera (đang kết nối Wi-Fi, cài Wi-Fi, trang AI chính) |
| `backup_original_camera.bin` (16 MB), `flash_header.bin`, `spiffs_dump.bin` | Bản sao firmware gốc của camera. **Không sửa, không xóa.** Dùng để khôi phục camera nếu lỡ nạp sai |

---

## 4. `src/`: chương trình chính của YOLO UNO

Mỗi file là **một chương trình riêng**, chỉ được biên dịch trong env của nó. Các file khác bị loại ra bằng `build_src_filter` hoặc bọc trong `#ifdef`.

| File | Env | Chức năng |
| :-- | :-- | :-- |
| `main.cpp` | `yolo_uno_camera_lcd`, `yolo_uno_camera` | Cân BLE + nhận nhãn camera qua UART (chiều cao tính sẵn trên laptop) + LCD + BMI + bật/tắt nguồn cân |
| `app_main.cpp` | `yolo_uno_servo_sweep` | **Plan 2** (trùng tên với `firmware/edge_yolo/main/app_main.cpp` của TV1): cân ≥ 5 kg đánh thức hệ thống, servo quét từ `servo_bottom` lên `servo_top`, dừng khi khung người `FullBody`, chiều cao = trung vị 3 khung, cân → BMI, LCD theo phase, tắt đèn nền khi nghỉ, giữ kết quả `hold_sec`. Nhận cấu hình `BEGIN/CFG/APPLY` từ `usb_viewer.py` (lưu NVS), gửi gói trạng thái SSV1 mỗi giây. Gõ `BOX_…` vào Serial Monitor để thử không cần laptop |
| `height_servo_main.cpp` | `yolo_uno_height_servo` | Bản servo cũ: chỉ đo chiều cao (dò chân + đầu) |
| `height_servo_bmi_main.cpp` | `yolo_uno_height_servo_bmi` | Bản servo cũ + cân + BMI + LCD + nguồn cân |
| `servo_test.cpp` | `yolo_uno_servo_test` | Gõ góc 0–180 vào Serial Monitor để thử servo (GPIO10) và tìm góc nằm ngang (`servo_level`) khi hiệu chuẩn |
| `ble_scanner.cpp` | `yolo_uno_ble_scan` | In mọi thiết bị BLE xung quanh, dùng để tìm MAC của cân |
| `edge_probe.cpp` | `yolo_uno_edge_probe` | Kiểm tra flash/PSRAM trước khi thử chạy AI ngay trên UNO |

---

## 5. `lib/`: thư viện firmware dùng chung

| Thư mục / file | Chức năng |
| :-- | :-- |
| `Config/config.h` | **Mọi thông số chung:** MAC cân, UUID BLE, chân camera, chân LCD, chân relay nguồn cân, chân camera UART + xung servo của Plan 2 (`SWEEP_*`) |
| `BLE/ble.cpp/.h` | Lớp thấp: quét, kết nối, nhận notify qua NimBLE |
| `Scale/scale.cpp/.h` | Đọc gói cân Mi Scale 2: tách cân nặng, cờ "đã ổn định", cờ "đã bước xuống". Task `runScaleService` chạy song song với `loop()` |
| `Measurements/measurement_state.h` | Logic thuần (test được trên PC): gộp chiều cao + cân nặng, hết hạn dữ liệu cũ, tính BMI. **ResultHold** (Plan 2, TV5): `setResultHoldMs(hold_sec*1000)` + `heldResult(now)` giữ H/W/BMI sau khi người bước xuống; mặc định tắt |
| `Measurements/measurements.cpp/.h` | Bọc `measurement_state.h` để dùng an toàn từ nhiều task |
| `Measurements/camera_packet.h` | Giải mã các nhãn camera của `main.cpp` (`MEAS_H…_D…`, `NO_PEOPLE`…) |
| `Measurements/pedestrian_state.h` | **P3:** giải mã `PED_WARN_D…` / `PED_SAFE_D…`, giữ khoảng cách người đi bộ trong 2 s cho màn LCD |
| `Camera/camera.cpp/.h` | Đọc UART camera cho `main.cpp` (dạng `nhãn;xác suất`) |
| `Display/display.cpp/.h` | LCD 16×2 I2C: tự dò địa chỉ, chọn màn hình theo chế độ (thường / servo BMI / Plan 2), chỉ ghi lại dòng nào đổi. Plan 2: `setDisplayLines()` + `setDisplayBacklight()` |
| `ScalePower/scale_power_state.h` | Logic thuần bật/tắt nguồn cân (bật khi có người, tắt sau 2 phút, chống bật tắt liên tục) |
| `ScalePower/scale_power.cpp/.h` | Điều khiển chân relay GPIO10 + lệnh Serial `power on/off/auto/status` |
| `HeightCamera/` | Servo + UART cho **bản servo cũ** (dò chân rồi đầu, `H = D × (tan θ_đầu − tan θ_chân)`) |
| **`EdgeSweep/edge_config.h`** | Thông số Plan 2 (`servo_pin`, `servo_bottom`, `servo_top`, `servo_step`, `servo_level`, `servo_dir`, `servo_settle`, `hold_sec`, `height`, `focal`) + kiểm tra khoảng giá trị (giống `usb_protocol.py`) |
| **`EdgeSweep/edge_geometry.h`** | `FramingStatus` + `analyzeFraming()` (đúng Interface Contract), góc nghiêng từ servo, chiều cao + khoảng cách từ khung người, trung vị |
| **`EdgeSweep/edge_message.h`** | Đọc bản tin `BOX_x1_y1_x2_y2_conf`, `NOBODY`, `MULTI` (chịu được đuôi `;0.90`) |
| **`EdgeSweep/servo_motion.h`** | (TV3, ghép 22/11) Đổi góc ↔ xung ↔ duty, góc nghiêng (`tilt = dir × (góc − level)`), bộ tạo chuyển động tăng/giảm tốc (60°/s, 240°/s²), lịch sử góc 5 s, `servoFrameFresh`, `servoTravelMs` cho FSM. Dùng chung cho driver Arduino `EdgeSweep/servo_ctrl.cpp`, native `firmware/edge_yolo/main/servo_ctrl.cpp` và `edge_fsm.h`. Test: `tests/servo_motion_test.cpp` |
| **`EdgeSweep/edge_fsm.h`** | Máy trạng thái 5 phase STANDBY → WAKING_HOMING → SERVO_SWEEP → LOCKED_MEASURE → RESULT_HOLD, có Step back / One at a time / No full body / Camera error |
| **`EdgeSweep/edge_io.h/.cpp`** | Interface Contract 3.3, phần LCD (TV5): `lcdSetBacklight(bool)`, `lcdShowPhase(phase, notice, data)`. `lcdTextFor()` (**TV5-2**) tạo chữ LCD 16×2 cho từng phase và cảnh báo, mỗi dòng đúng 16 ký tự. Phần nguồn/BLE (`setPowerMode`, `bleSetLowPowerScan`) là của TV2, chưa có trong bản PlatformIO |
| **`EdgeSweep/servo_ctrl.h/.cpp`** | Interface Contract 3.2 (TV3): `servoInit`, `servoMoveDeg` (không chặn), `servoDetach`, `servoToTiltDeg(servoDeg, EdgeConfig)`. Bản Arduino dùng ESP32Servo, xung 500–2400 µs |
| **`EdgeSweep/usb_packet.h`** | Đóng gói SSV1 (CRC32) để UNO trả lời `usb_viewer.py` |
| `GM65/`, `calib_fsm/` | Máy quét QR GM65 và hiệu chuẩn cảm biến lực, thuộc cấu hình cũ `yolo_uno_s3`. Không dùng trong P3/P5 |

Các file `.h` thuần (không gọi Arduino) đều có test chạy trên laptop trong `tests/`.

---

## 6. `vision/`: laptop cho Plan 2 (servo quét)

| File | Chức năng |
| :-- | :-- |
| `body_tracker.py` | **Script chạy chính.** Mỗi khung: lấy ảnh `/capture` → YOLO (`yolo26n.pt`) tìm người → gửi `BOX_x1_y1_x2_y2_conf` / `NOBODY` / `MULTI` qua `/control` (tối đa 5 lần/s). Cửa sổ xem trước vẽ khung người + vùng an toàn (q thoát, p tạm dừng), `--dry-run` chỉ in, `--source webcam:0` hoặc folder ảnh để thử. In `[Timing]` để chọn `servo_settle` |
| `body_protocol.py` | Logic thuần: chọn người lớn nhất, `MULTI` nếu có người thứ hai to cỡ người đầu, chuẩn hóa khung về 0..1, tạo nhãn. Test ở `tests/test_body_protocol.py` |

Laptop **không** tính chiều cao. Nó chỉ báo người đang ở đâu trong ảnh; YOLO UNO biết góc servo, tự xét đủ khung chưa và tự tính. Bản dò khuôn mặt cũ đã bỏ (lưu ở `..\_archive_face_tracking_20261118.zip`).

---

## 7. `camera/`: laptop cho chế độ camera cố định

| File | Chức năng |
| :-- | :-- |
| `smartscale_camera.py` | YOLO trên laptop đo chiều cao người khi camera đặt cố định (mode `fixed` hoặc `ground`), gửi `MEAS_H…_D…` về UNO |
| `start_smartscale.ps1` | Chạy script trên với tham số gọn (`-CameraIP`, `-Configure`) |
| `geometry.py` | Công thức máy ảnh lỗ kim cho mode `fixed`/`ground` |
| `protocol.py` | Tạo URL `/control?serial=NHAN;0.930;stop` đúng kiểu camera cần (không mã hóa dấu `;`). **Mọi script gửi lệnh nên dùng hàm này.** `ultralytics_model_path()`: đường dẫn model cho YOLO (tránh lỗi dấu `'` trong `Mira's Laptop` làm Ultralytics tưởng thiếu file rồi tự tải về) |
| `usb_protocol.py`, `usb_viewer.py` | Cấu hình (`--configure`, `--set key=value`) và xem trạng thái/ảnh qua USB: cho `firmware/edge_yolo` và cho env `yolo_uno_servo_sweep` (hiện phase, góc servo, tilt, framing) |
| `settings.json` / `settings.example.json` | Thông số lắp đặt cho `smartscale_camera.py`. Lưu ý: `"focal_px": 3.6` hiện đang sai (đó là mm, không phải pixel) |
| `requirements*.txt` | Thư viện Python cần cài (`opencv-python<5`, `ultralytics`, `requests`) |
| `README.md` | Hướng dẫn hiệu chuẩn chế độ cố định |

---

## 8. Các thư mục còn lại

| Thư mục | Nội dung |
| :-- | :-- |
| `integration/` | Web app trên laptop (`app.py`, `index.html`, `start.ps1`): nhận diện hoa quả (Project 6, giữ làm ví dụ cho công cụ huấn luyện P3) + đo sức khỏe, đọc cân qua cổng Serial. Mở http://127.0.0.1:8765 |
| `firmware/edge_yolo/` | YOLO26n **ngay trên YOLO UNO** bằng ESP-IDF, đã khôi phục `main/` và `tools/`, tích hợp Plan 2. Đã full build ESP-IDF 6.1, nạp COM5 và kiểm tra STANDBY đạt |
| `.edge-deps/` | Notebook lượng tử hóa YOLO26 (`quantize_yolo26/`) + backup flash UNO. Chỉ để trên máy, không đưa lên GitHub |
| `camera_firmware/` | Firmware camera đã tách: `web/` (các trang HTML), `partitions/` (từng phân vùng; `00_nvs*` có thể chứa mật khẩu Wi-Fi) |
| `tools/inspect_flash.py` | Đọc bảng phân vùng của file backup và tách ra từng phần (không ghi gì vào thiết bị) |
| `tools/run_all_tests.ps1` | **TV5-5.** Một lệnh chạy hết test C++ (g++) + Python + mô phỏng QA, in bảng PASS/FAIL/SKIP. Thêm `-Firmware` để build mọi env |
| `tools/qa_log_check.py` | **TV5-5.** Đọc log Serial của `yolo_uno_servo_sweep`, tách từng lượt đo (H, W, BMI, góc dừng, thời gian), chấm 6 ca QA của Plan 2, xuất CSV |
| `docs/HUONG_DAN_PLAN2.md` | **TV5-6.** Hướng dẫn cho người mới: lắp đặt, đấu dây, nạp firmware, cấu hình qua `usb_viewer.py --configure` (chế độ `servo`), hiệu chuẩn `servo_level`/`servo_dir`/`height`/`focal`/`servo_settle`/MAC, vận hành, ý nghĩa LCD |
| `docs/QA_CHECKLIST.md` | Cách test 6 ca QA trước demo + 24 ca thực tế + bảng lỗi + bảng ký xác nhận |
| `docs/QA_FIELD_LOG.csv` | Bảng trống để ghi 24 ca test thực tế (ΔH, W, BMI, …) |
| `docs/` | Tài liệu: xem mục 9 |
| `tests/` | Test chạy trên laptop: xem mục 10 |
| `include/` | Mặc định của PlatformIO, chưa dùng |
| `.pio/` | PlatformIO tự tạo khi build (thư viện tải về, file `.bin`). Xóa được, sẽ tự tạo lại |

---

## 9. `docs/`

| File | Nội dung |
| :-- | :-- |
| `QUICK_START.md` | **Hướng dẫn chạy từng bước**: cài đặt, nối dây, kết nối camera, chạy P5 Plan 2 (servo quét), P3, lỗi thường gặp |
| `CAU_TRUC_FILE.md` | File này |
| `DE_BAI_MINI_PROJECT.md` | Đề bài P3, P5 (và P6 để tham khảo) + ý tưởng "đầu robot" của thầy |
| `HANDOFF_CHAT.md` | Lịch sử việc đã làm, lỗi đã biết, phần việc TV5 theo Plan 2, danh sách việc tiếp theo |
| `SMARTSCALE_README.md` | Chi tiết cân BLE: nối dây, tìm MAC, LCD, lỗi thường gặp |
| `MINI_PROJECT_README.md`, `reports/` | Phân tích firmware camera gốc, review `pedestrian_detector.py` |
| `ON_DEVICE_INFERENCE.md`, `SMARTSCALE_CAMERA_COPY_NOTES.md` | Ghi chú về chạy AI trên UNO và nguồn gốc file |

---

## 10. `tests/`: kiểm tra trên laptop (không cần phần cứng)

| Test | Kiểm tra gì |
| :-- | :-- |
| `test_body_protocol.py` | Nhãn `BOX_…` / `NOBODY` / `MULTI` của `vision/` khớp bộ đọc firmware |
| `test_camera_protocol.py`, `test_camera_geometry.py` | URL `/control`, nhãn P3, đường dẫn model YOLO, công thức chế độ cố định |
| `test_usb_protocol.py`, `test_usb_viewer.py` | Gói SSV1, 8 khóa cấu hình Plan 2 + `scaleMac`, viewer chịu được gói thiếu/sai, `--configure` / `--set` |
| `edge_sweep_logic_test.cpp` | Plan 2: khóa cấu hình, hình học chiều cao, framing, bản tin `BOX_`, **chữ LCD (TV5-2) luôn 16 ký tự**, CRC SSV1 |
| `edge_sweep_sim.h` | Bộ mô phỏng dùng chung (cân + servo + camera trễ) cho `edge_sweep_fsm_test.cpp` và `qa_log_sim.cpp` |
| `qa_log_sim.cpp` | In log giống firmware cho 17 lượt đo giả lập (đủ 6 ca QA) để thử `tools/qa_log_check.py` |
| `test_qa_log_check.py` | Test công cụ chấm QA: thời gian, brownout/khởi động lại, giữ kết quả, file UTF-16, định dạng log khớp firmware |
| `edge_sweep_fsm_test.cpp` | Mô phỏng cân + servo + camera trễ 250 ms: người 120–190 cm ở 2,8–3,2 m, đứng quá gần, 2 người, không thấy toàn thân, mất camera, cân không chốt, bước xuống giữa chừng, giữ kết quả, người mới, lắp ngược, `millis()` tràn số |
| `measurement_state_test.cpp`, `scale_power_test.cpp` | Gộp chiều cao + cân, BMI, **ResultHold (TV5-1)**; logic bật/tắt nguồn cân |
| `pedestrian_state_test.cpp` | Nhãn P3 `PED_…`, hết hạn 2 s, không lẫn với nhãn P5 |
| `display_probe_test.cpp` (+ `lcd_stubs/`) | Chạy driver LCD thật trên một bus I2C giả |
| `edge_geometry_test.cpp` | Cho `firmware/edge_yolo`. đã khôi phục `main/`, test hình học đã chạy đạt |

Lệnh chạy các test có ở `CLAUDE.md`, mục "Lệnh kiểm tra".


## Bổ sung từ GitHub cho Plan 2 (18/11/2026)

`firmware/edge_yolo/main/` đã khôi phục: `app_main.cpp` (FSM + capture/inference native), `edge_io.cpp` (BLE MAC, Wi-Fi, USB, LCD, PM), `edge_device_config.h` (cấu hình native), `edge_scale.h` (decoder BLE thuần), `capture_gate.h` (loại ảnh sai phiên/góc), `servo_ctrl.*` (LEDC, driver TV3: tick 20 ms `esp_timer`, `servoStop`/`servoSweepStart`/`servoDegAt`, không chặn vòng lặp), `camera_control.h` (22/11: dựng URL `/control`, khóa `cam_framesize`/`cam_quality` để UNO tự đặt độ phân giải/chất lượng JPEG cho camera). Logic `EdgeFsm`/framing/hình học/LCD tái dùng từ `lib/EdgeSweep`, không sao chép sang bộ khác. `tools/fetch_model.ps1` tải model pinned có kiểm tra hash; `tests/edge_native_plan2_test.cpp` kiểm tra cấu hình, BLE, wake và ảnh cũ. `docs/PLAN_2_TEAM_5_DIVISION.md` là bản kế hoạch đã mang vào repo; `docs/reports/PLAN2_GITHUB_MERGE.md` ghi quyết định gộp và kết quả kiểm chứng. Native đã full build ESP-IDF 6.1/nạp COM5/kiểm tra STANDBY đạt; xem `docs/reports/NATIVE_BUILD_FLASH_20261118.md`.


Cập nhật môi trường native: SDK `C:\Espressif\v6.1\esp-idf`, `IDF_TOOLS_PATH=C:\Espressif`, Python IDF `C:\Espressif\python_env\idf6.1_py3.11_env\Scripts\python.exe`. `build.ps1` tự đồng bộ các file native/lib cần thiết sang `C:\esp\SmartScale` bằng `tools/stage_build.ps1` (marker nhận diện nguồn, không xóa cây thư mục). `prepare_sources.ps1 -DestinationRoot <root> -SkipDataset` chỉ tải ESP-DL khi build, không cần dataset. `tools/native_probe.py` đọc SSV1 để kiểm tra bo, không đổi cấu hình. Không ghi đè hoặc xóa firmware backup.
