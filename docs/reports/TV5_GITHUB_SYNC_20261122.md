# Đối chiếu GitHub mới nhất và phần việc TV5 — 22/11/2026

Đã clone [repo nhóm](https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI) vào thư mục tạm, chỉ đọc. Không merge, commit, push, nạp bo hay đổi gì trên GitHub. Mã LAB chưa commit được chép nguyên vào worktree `claude/tv5-smartscale-plan-2-488f21` để làm mốc. Thư mục LAB gốc không bị sửa.

## 1. Trạng thái các nhánh (22/11)

| Nhánh / PR | Commit mới nhất | Nội dung chính |
|---|---|---|
| `smartscale-edge-ai` (mặc định) | `5533edb` 21/11 (BaoLong) | Native viết lại HTTP capture bằng socket (`http_capture_parser.h`, timeout tách connect/header/body), standby + fast wake, sửa LCD 4-bit (`esp_rom_delay_us`), tự đặt framesize camera, phần BLE/Wi-Fi retry của TV2. Model **224**. **Không có servo, không có FSM Plan 2, không có `lib/EdgeSweep`** |
| `feat/camera-power-integration` | `dad24d5` 21/11 (dathuhu) | BLE scan retry, Wi-Fi reconnect, `src/servo_mg995_test.cpp` (thử MG995). Chưa gộp vào nhánh mặc định |
| `main` | `902321a` 07/11 | Bản cũ |
| [PR #1](https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI/pull/1) | `f048c3e`, **Draft, open**, đích `feat/camera-power-integration` | Mã TV5 (viewer, ResultHold, công cụ QA, test). Chưa ai merge; chưa có firmware native LAB |

## 2. Phần TV5: có ở đâu

| Hạng mục | LAB | PR #1 | GitHub mặc định |
|---|---|---|---|
| `ResultHold` + `scale_presence.h` | Có + test | Có | Có bản riêng của BaoLong (khác API, 130 dòng so với 175) |
| Câu chữ LCD 16×2 (`lib/EdgeSweep/edge_io.h`) | Có + test | Có | Không (LCD native tự viết trong `edge_io.cpp`) |
| `usb_viewer.py` / `usb_protocol.py` | 423/179 dòng, có Plan 2, preview, session, log | Có | Bản khác (297/112 dòng), không có khóa servo |
| `telemetry.py`, `native_probe.py`, `native_qa.py`, `qa_log_check.py`, `run_tv5_tests.py` | Có + test | Có | Không có |
| Firmware native Plan 2 (servo FSM, model 192, preview, đo từng công đoạn) | Có, đã nạp COM5 | Không | Không |
| **Mới 22/11 (chỉ trong worktree này):** servo ramp, khóa `cam_framesize`/`cam_quality`, `camera_fetch_bench.py`, RSSI, sửa delay LCD | Có + test | Không | Không |

Kết luận: GitHub và LAB đã tách thành **hai bản native khác nhau**. Bản GitHub có HTTP capture và LCD tốt hơn. Bản LAB có servo/FSM Plan 2, model 192 và công cụ TV5. Ghép hai bản là việc tích hợp của TV1; TV5 không tự chép đè.

## 3. Điểm lệch cần nhóm chốt (không tự sửa)

1. **Chân và xung servo.** `src/servo_mg995_test.cpp` dùng **MG995 ở D11 = GPIO38**, xung 1000–2000 µs. Interface Contract và driver Plan 2 dùng **GPIO10**, 500–2400 µs. Nếu gá thật nối D11, cấu hình bằng `--set servo_pin=38` (GPIO38 không thuộc chân cấm). Nếu servo chỉ chịu 1000–2000 µs, đổi build flag `SWEEP_SERVO_MIN_US`/`SWEEP_SERVO_MAX_US` rồi hiệu chuẩn lại `servo_level`, `servo_dir`. Cần TV3 xác nhận.
2. **Mã framesize của camera.** GitHub đặt `framesize=4` và ghi chú là QVGA. Trên camera LAB, `framesize=5` cho 320×240, còn `/status` gốc báo `framesize=8` = 640×480. Hai số này khớp bảng esp32-camera **có** 240×240, nên trên camera LAB `4` nhiều khả năng là **240×240**. Chưa kiểm trên camera; chạy `tools/camera_fetch_bench.py --framesize 4 5` sẽ cho biết ngay độ phân giải thật.
3. **Model.** GitHub nhúng 224; bản đã nạp là 192. Từ 22/11, `build.ps1`/CMake trong LAB mặc định 192.

## 4. Đã làm trong worktree (22/11)

| Thay đổi | File | Kiểm chứng |
|---|---|---|
| Servo chạy êm 240°/s, nhớ góc cuối qua `servoDetach()`, settle tính sau khi servo tới nơi | `lib/EdgeSweep/servo_ramp.h` (mới), `lib/EdgeSweep/servo_ctrl.*`, `firmware/edge_yolo/main/servo_ctrl.cpp`, `app_main.cpp` (`servoUpdate()`) | `tests/servo_ramp_test.cpp` PASS; `pio run -e yolo_uno_servo_sweep` SUCCESS |
| UNO tự đặt độ phân giải/chất lượng JPEG cho camera qua `/control` (khóa `cam_framesize`, `cam_quality`, mặc định `-1` = không đổi, lưu NVS) | `firmware/edge_yolo/main/camera_control.h` (mới), `edge_device_config.h`, `edge_io.cpp`, `app_main.cpp`, `lib/EdgeSweep/edge_config.h` (Arduino bỏ qua khóa), `camera/usb_protocol.py`, `camera/usb_viewer.py` | `tests/edge_native_plan2_test.cpp`, `tests/test_usb_protocol.py`, `tests/test_usb_viewer.py` PASS |
| Đo thời gian lấy ảnh theo framesize × quality từ laptop, tự trả cài đặt cũ | `tools/camera_fetch_bench.py` (mới) | `tests/test_camera_fetch_bench.py` (camera giả localhost) PASS |
| Viewer: phóng ảnh bicubic + làm nét nhẹ, khung vẽ sau khi phóng; dòng fetch có độ phân giải và RSSI | `camera/usb_viewer.py`, `camera/telemetry.py` | test viewer PASS |
| Firmware gửi `wifi_rssi`; log cảnh báo nếu `esp_wifi_set_ps(NONE)` bị từ chối | `edge_io.cpp/.h`, `app_main.cpp` | build native (xem mục 5) |
| LCD native: bỏ `vTaskDelay(pdMS_TO_TICKS(1..5))` (= 0 ms ở 100 Hz), thêm pull-up I2C, khởi tạo lại HD44780 mỗi lần thức dậy để chữa lệch nibble | `firmware/edge_yolo/main/edge_io.cpp` | build native (xem mục 5); **chưa thử trên LCD thật** |
| Mặc định build model 192 | `firmware/edge_yolo/build.ps1`, `main/CMakeLists.txt`, `README.md` | — |

## 5. Phạm vi kiểm chứng

- **Test PC:** `tools/run_all_tests.ps1` → **14 PASS, 0 FAIL, 0 SKIP** (11 chương trình C++, 97 test Python + 3 integration, mô phỏng QA). Mốc trước khi sửa: 13 PASS.
- **Build:** `yolo_uno_servo_sweep` (Arduino) SUCCESS. Native ESP-IDF 6.1 build ở thư mục stage riêng `C:\esp\SmartScaleTV5` (không đụng `C:\esp\SmartScale` của LAB). Kết quả ghi ở phần trả lời cuối buổi.
- **Chưa thử trên phần cứng:** không nạp UNO, không mở COM, không gọi camera thật. Ramp servo, đặt framesize/quality, RSSI và sửa LCD đều **chưa chạy trên bo**.

## 6. Ảnh nét hơn mà không trễ nhiều: hướng đề xuất

Số đo đã có trên UNO (QVGA 320×240, quality 15): giải mã JPEG ~12 ms, chuẩn bị ~6 ms, AI ~300 ms. Thời gian lấy ảnh qua Wi-Fi dao động 200 ms tới hơn 6 s, lớn hơn mọi chi phí xử lý ảnh. Vì vậy:

1. **Giảm `cam_quality` từ 15 xuống 12 hoặc 10, giữ 320×240.** JPEG lớn hơn khoảng vài KB. Ở QVGA, thời gian giải mã chỉ tăng vài ms. Đây là cách rẻ nhất để ảnh bớt vỡ khối.
2. **Phóng ảnh đẹp trên PC** (đã làm, mặc định bật): không tốn thời gian trên UNO.
3. **Tăng lên 400×296 hoặc 480×320 chỉ khi bench cho thấy fetch không chậm hơn rõ.** Model vẫn nhận 192×192, nên độ phân giải cao chủ yếu làm ảnh xem đẹp hơn, AI không chính xác hơn bao nhiêu, còn giải mã/tiền xử lý tăng gần theo số điểm ảnh.
4. **Trước khi chỉnh model tiếp, tìm nguyên nhân fetch chậm:** so `camera_fetch_bench.py` (laptop) với `capture_ms` + `wifi_rssi` của UNO. Laptop cũng chậm thì do camera/AP. Laptop nhanh mà UNO chậm thì do Wi-Fi của UNO (RSSI yếu, modem sleep khi chạy cùng BLE).

Lệnh thử khi có camera (UNO ở STANDBY, đóng viewer và trang web camera):

```powershell
py -3 tools\camera_fetch_bench.py --camera http://10.127.7.68 --framesize 4 5 6 7 --quality 15 12 10 --json .edge-deps\qa\camera-bench.json
```

Sau khi nạp firmware mới (cần hỏi trước khi nạp):

```powershell
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py --preview --set cam_framesize=5 --set cam_quality=12 --log .edge-deps\qa\camera-q12.jsonl
.\.venv-camera\Scripts\python.exe tools\native_qa.py .edge-deps\qa\camera-q12.jsonl --json .edge-deps\qa\camera-q12-report.json
```

## 7. Còn chờ phần cứng hoặc thành viên khác

- TV3: xác nhận servo (SG92R/MG995), chân (GPIO10 hay GPIO38), dải xung; thử ramp trên gá thật, đo sụt áp.
- TV1: ghép native GitHub (`5533edb`: HTTP capture, standby/fast wake) với native LAB (servo FSM, model 192, preview, ramp, `cam_*`). Điểm ghép: `app_main.cpp` (worker capture + `CaptureGate`), `edge_io.cpp` (LCD, Wi-Fi, BLE).
- TV2: BLE GATT fallback, đo dòng standby/active.
- TV4: đánh giá độ chính xác model 192, hiệu chuẩn `height`/`focal`/`servo_level`.
- TV5 (phần cứng): QA1–QA6 trong [QA_CHECKLIST](../QA_CHECKLIST.md), kiểm LCD thật, chọn `cam_quality` bằng bench + preview.

## 8. Ghép driver servo TV3 (`servo_ctrl.zip`) vào LAB — 22/11 (lần 2)

Zip dựng trên GitHub `c2f71b6`, nên không chép nguyên file `app_main.cpp`/`edge_io.cpp`/`CMakeLists.txt` của zip (sẽ mất FSM Plan 2, model 192, preview). Chỉ lấy phần servo, theo quyết định của nhóm:

| Nội dung | Làm theo | File |
|---|---|---|
| Bộ tạo chuyển động tăng/giảm tốc (60°/s, 240°/s²), lịch sử góc, `servoFrameFresh`, `servoNextStep` | Zip (nguyên văn) + `servoTravelMs()` cho FSM | `lib/EdgeSweep/servo_motion.h` (thay `servo_ramp.h`), `tests/servo_motion_test.cpp` |
| Dấu `servo_dir`: `tilt = dir × (góc − level)`, `-1` = tăng góc thì ngẩng lên | Zip | `edge_geometry.h`, mặc định `SWEEP_DIR=-1`; NVS cũ tự đảo dấu một lần (native: blob cũ; Arduino: cờ `dir_tv3`) |
| Không chặn chân cấm (chỉ kiểm 0..48) | Zip | `edge_config.h`, `edge_device_config.h`, `usb_protocol.py` (viewer chỉ cảnh báo) |
| Kiểu NVS: `servo_pin`/`servo_dir` i32, `servo_settle`/`servo_delay` u32, góc/tốc độ blob float | Zip | `firmware/edge_yolo/main/edge_io.cpp` (đọc được blob cũ), `src/app_main.cpp` (Preferences vốn cùng kiểu) |
| Khóa mới `servo_delay` (500 ms), `servo_dps` (4°/s); mặc định 45..135°, bước 5°, settle 200 ms | Zip | `edge_config.h`, `platformio.ini`, viewer `--configure` |
| Driver native: tick 20 ms `esp_timer`, LEDC timer 3/kênh 5, `servoStop`, `servoSweepStart`, `servoDegAt`, `servoStillSinceMs` | Zip | `firmware/edge_yolo/main/servo_ctrl.h/.cpp` |
| Comment ghi phần TV1 (FSM gọi homing/detach) | Zip | `lib/EdgeSweep/servo_ctrl.h` |
| `servoMoveDeg` **không chặn**; nhớ góc qua detach; detach chờ tick đang chạy rồi mới `ledc_stop` | Sửa so với zip (FSM LAB cần vòng lặp chạy liên tục) | `firmware/edge_yolo/main/servo_ctrl.cpp` |
| FSM chờ hết hành trình + settle + delay; **chia đôi** khi một bước nhảy từ cụt đầu sang cụt chân | Mới | `lib/EdgeSweep/edge_fsm.h` |

Lý do chia đôi: với bước 5°, người 190 cm cách 2,8 m chỉ vừa khung trong khoảng ~1,5°. Mô phỏng cũ báo `No full body`, bản mới dừng ở 87,5° và đo đúng 190 cm. Hai góc cách nhau dưới 1° mà vẫn không vừa thì báo `Step back!`.

## 9. Nạp và thử trên YOLO UNO thật (22/11, chưa có camera)

**Bo:** một con YOLO UNO **khác** con ngày 18/11. MAC `80:B5:4E:DF:FD:88` (con cũ `A4:CB:8F:F1:0F:8C`), ESP32-S3 rev 0.2, flash 16 MB, PSRAM 8 MB, nhận ở **COM3**. `--port auto` tìm đúng cổng.

**Trước khi nạp:** bo chạy firmware gốc kiểu OhStem (phân vùng `safeboot`/`app0`/`spiffs`, in `QPC: Reset`). Đã sao lưu đủ 16 MB: `.edge-deps/backups/yolo_uno_80B54EDFFD88_stock_20261122.bin`, SHA256 `3BFC7F5F…11D14`. Lệnh khôi phục ghi trong file `.json` cùng tên.

**Nạp:** `build.ps1 -ModelSize 192 -BuildRoot C:\esp\SmartScaleTV5 -Flash` (tự tìm COM3). Bootloader, bảng phân vùng native và app 5.958.832 byte ghi xong, esptool xác minh hash. Lần đầu báo cổng bận vì PlatformIO Serial Monitor đang mở; đóng Monitor rồi nạp lại thì được.

| Phép thử (không camera, không servo, không LCD đã xác nhận) | Kết quả |
|---|---|
| Gói SSV1 mỗi giây | 25 gói / 25 s |
| Đánh thức bằng cân BLE (MAC mặc định `70:87:9E:EA:3A:E0`) | Nhận 16,35 kg rồi 13,9 kg ổn định → `WAKING_HOMING` → `SERVO_SWEEP`, lệnh servo 45° (mặc định TV3) |
| Không Wi-Fi/camera khi đang quét | Sau vài giây báo `Camera error / Check laptop/IP`, không treo |
| Preview (`--preview`) | Pha `CAMERA_TEST`, `Camera test / WiFi not ready`, `preview=true` |
| Cấu hình USB hợp lệ (giá trị mặc định TV3) | `{"kind":"config","ok":true}` |
| Cấu hình sai (`servo_bottom` = `servo_top`) | `ok:false`, không lưu |
| Reset rồi khởi động lại | Gói `boot`: tự kiểm tra model 310 ms, PSRAM trống 4.999.728 B, RAM nội 81.863 B; về `STANDBY` |

Gói trạng thái có đủ `wifi_rssi` (null khi chưa có Wi-Fi), `servo_deg`, `tilt_deg` theo quy ước mới (45° → tilt 45° xuống với `servo_dir=-1`, level 90).

**Chưa thử:** lấy ảnh/nhận diện (chưa có camera), `cam_quality`/`cam_framesize`, chuyển động servo thật (chưa gắn servo, mới thấy lệnh PWM), LCD thật, BMI. Chưa đọc lại được giá trị đã lưu sau reset vì firmware chưa có lệnh GET cấu hình; mới xác nhận ACK và khởi động lại không lỗi.

## 10. Quay servo bằng tay qua USB để hiệu chuẩn (22/11)

Firmware native thêm lệnh USB `SERVO <0..180>` / `SERVO OFF` (lease 10 s giống preview, `firmware/edge_yolo/main/servo_jog.h`). Trong lúc jog: FSM, capture và đánh thức bằng cân tạm dừng; servo gắn với hành trình 0..180, chuyển động mềm theo `servo_motion.h`; lần đầu sau khi bật nguồn chỉ nhảy tới góc được yêu cầu (`servoAssumeAt`), không về 0°. Công cụ `tools/servo_jog.py` gửi lệnh, nhắc lease mỗi 3 s và lưu `servo_level`/`servo_dir`/`servo_bottom`/`servo_top` bằng BEGIN/CFG/APPLY.

**Trên bo COM3 (servo đã gắn):** nạp lại, hash khớp. Chuỗi `SERVO 90 → 100 → 80 → 90 → OFF`: mỗi lệnh có ACK `servo_jog ok`, gói trạng thái pha `SERVO_JOG`, `servo_deg` đúng lệnh, `tilt_deg` 0 / −10 / +10 / 0 (với `servo_dir=-1`, level 90), LCD `Servo jog 100.0 | Tilt -10.0 deg`; `SERVO OFF` → `STANDBY`. **Chuyển động cơ khí do người dùng quan sát**; firmware không đo được góc thật.


## 11. Giới hạn cứng hành trình servo 45°..135° (22/11)

Theo yêu cầu: servo chỉ quay trong 45°..135°. `SWEEP_LIMIT_MIN_DEG`/`SWEEP_LIMIT_MAX_DEG` (`lib/EdgeSweep/edge_config.h`) áp cho: lệnh USB `SERVO` (ngoài khoảng → `ok:false`), `servoInit()` của cả hai driver (dải gọi vào bị cắt về 45..135), khóa `servo_bottom`/`servo_top`/`servo_level` (firmware và `usb_protocol.py` từ chối), `tools/servo_jog.py` (kẹp góc). NVS cũ ngoài khoảng → cấu hình không hợp lệ → về mặc định 45/135/90.

**Thử trên bo COM3 sau khi nạp lại:** `SERVO 90/45/135` → `ok:true`, LCD `Servo jog 45.0 | Tilt +45.0 deg`, `135.0 | Tilt -45.0`; `SERVO 30` và `SERVO 150` → `ok:false` (`SERVO 45..135 or OFF`), servo giữ nguyên 135°; cấu hình `servo_bottom=30` → `ok:false`; `SERVO OFF` → `STANDBY`.

## 12. Phần TV5 làm thêm (22/11, chiều)

| Phần | File | Kiểm chứng |
|---|---|---|
| Lệnh USB `CONFIG?`: firmware trả gói `config_values` với giá trị đang dùng (từ NVS), cùng tên khóa với BEGIN/CFG/APPLY; mật khẩu Wi-Fi không gửi, chỉ `password_set` | `firmware/edge_yolo/main/edge_device_config.h` (`deviceConfigJson`), `edge_io.cpp` | `tests/edge_native_plan2_test.cpp` (không lộ mật khẩu, SSID có `"`/`\` không làm hỏng JSON); build native thành công; **chưa nạp/thử trên bo** vì bo bị rút khi nạp |
| `usb_viewer.py --show-config` (in từng giá trị + dòng `--set` để sao lưu hiệu chuẩn); lệnh `config` trong `servo_jog.py` | `camera/usb_protocol.py` (`format_config`, `config_set_args`), `camera/usb_viewer.py`, `tools/servo_jog.py` | `tests/test_config_readback.py` (cổng giả) |
| Đo độ trễ ảnh camera sau khi servo dừng → gợi ý `servo_delay` | `tools/servo_delay_probe.py` | `tests/test_servo_delay_probe.py` (thời gian hành trình khớp `servo_motion.h`, cách tính trên dữ liệu giả); **cần camera để đo thật** |
| Bảng sai số chiều cao cho báo cáo (bias, MAE, RMSE, ±2/±5 cm, theo nhóm/khoảng cách) | `tools/height_error_report.py` | `tests/test_height_error_report.py`; file mẫu hiện chưa có số liệu đo |

Test PC: `tools/run_all_tests.ps1` 14 PASS, 0 FAIL, 0 SKIP.

**Thử trên bo gốc (COM5, MAC `A4:CB:8F:F1:0F:8C`, bản native 18/11 → bản 22/11):** nạp thành công (tự tìm COM5). `usb_viewer.py --show-config` đọc được đủ giá trị: Wi-Fi (SSID, có mật khẩu), URL camera, MAC cân được **giữ nguyên** qua lần nạp; các khóa servo về mặc định TV3 (`servo_level=90`, `servo_dir=-1`, 45..135, bước 5, settle 200, delay 500) vì giá trị servo cũ trên bo nằm ngoài giới hạn cứng 45..135 nên firmware khôi phục mặc định đúng như thiết kế. Cần hiệu chuẩn lại servo bằng `tools/servo_jog.py`. Mật khẩu Wi-Fi không xuất hiện trong gói trả về.

## 13. Ghép phần "lấy ảnh HTTP ổn định + chờ/đánh thức nhanh" từ GitHub (22/11)

Nguồn: `smartscale-edge-ai@78548a6` (BaoLong/TV2). Chỉ lấy phần mạng/BLE, giữ nguyên FSM servo, model 192 và công cụ TV5 của LAB.

| Phần | File trong LAB | Ghi chú |
|---|---|---|
| Lấy ảnh bằng socket: connect không chặn (2,5 s), hạn header 4,5 s / body 3 s / tổng 6 s, Content-Length + chunked + EOF, cache địa chỉ, TCP_NODELAY | `firmware/edge_yolo/main/http_capture_client.h` (mới, từ `app_main.cpp` GitHub), `http_capture_parser.h` (nguyên bản) | Thay `esp_http_client`. `applyCameraFramesize` → `applyControl(url, var, value)` để dùng chung cho `cam_framesize`/`cam_quality`. Không lấy `kCameraTargetFramesize = 4` của GitHub (trên camera LAB 4 có thể là 240×240) |
| Wi-Fi qua hàng đợi sự kiện, nối lại sau 2 s, WPA/WPA2/Open, SSID 32 byte / khóa 64 hex | `edge_io.cpp`, `wifi_reconnect.h`, `wifi_credentials.h` (nguyên bản) | Trước đây USB APPLY gọi thẳng `esp_wifi_*` và nối lại liên tục; nay mọi thay đổi đi qua event loop |
| Yêu cầu quét BLE gửi về task NimBLE, thử lại 100 ms → 2 s | `edge_io.cpp`, `ble_scan_retry.h` (nguyên bản) | Trước đây task chính/USB gọi GAP trực tiếp |
| Đánh thức nhanh: chờ quét BLE mỗi 100 ms (10 %), chạy 12,5 %; `ESP_COEX_PREFER_WIFI` | `edge_io.cpp` | Giữ nguyên tỉ lệ thời gian nghe lúc chờ |
| Không lấy | `standby_schedule.h`, khóa PM 80/240 MHz, `servo_ctrl`/`servo_math` GitHub | Lịch vòng điều khiển theo kiến trúc GitHub; LAB thức bằng FSM vòng 20 ms. Điện năng là phần TV2; servo đã có bản TV3 + LAB |

Test PC: thêm `http_capture_parser_test`, `wifi_reconnect_test`, `wifi_credentials_test`, `ble_scan_retry_test` (nguyên bản GitHub) → `tools/run_all_tests.ps1` **18 PASS, 0 FAIL, 0 SKIP**. Build native ESP-IDF 6.1 thành công (app 0x599b20, nhỏ hơn ~87 KB vì bỏ `esp_http_client`). **Chưa nạp, chưa thử với camera thật.**

**Nạp và thử trên bo gốc COM5 (22/11, sau khi ghép):** nạp thành công (tự tìm COM5, app 5.865.472 byte). Khởi động: tự kiểm tra model 313 ms, PSRAM trống 4.999.808 B, RAM nội 75.071 B (giảm ~6 KB do bộ đệm 4 KiB của client socket); `CONFIG?` giữ nguyên Wi-Fi/camera/servo; về `STANDBY`. Preview 20 s: `CAMERA_TEST`, `WiFi not ready`, 27 gói, không treo. **Chưa kiểm được lấy ảnh/nối lại Wi-Fi** vì mạng Wi-Fi đã cấu hình (điểm phát điện thoại) và camera chưa bật lúc thử.
