# TV5 — Hướng dẫn hiệu chuẩn, kiểm tra và nghiệm thu (nhánh `feat/ui-viewer-qa`)

Phần việc **TV5 — UI/UX LCD, USB Config/Viewer, QA Test & Tài liệu** theo [Plan 2](PLAN_2_TEAM_5_DIVISION.md). Nhánh này dựa trên `smartscale-edge-ai@4ce7f4e` (FSM Step-Scan, servo và firmware của nhánh chính **giữ nguyên**) và chỉ thêm công cụ/tài liệu/kiểm thử của TV5 cùng hai thay đổi nhỏ trong firmware.

## 1. Nhánh này thêm gì

| Phần | File | Ghi chú |
|---|---|---|
| Lệnh USB `CONFIG?` | `firmware/edge_yolo/main/config_readback.h`, `edge_io.cpp` | Trả gói `config_values` với cấu hình đang dùng (từ NVS). **Không gửi mật khẩu Wi-Fi**, chỉ `password_set` |
| Câu chữ LCD ≤ 16 ký tự | `app_main.cpp` (`Model input err`), `tests/test_lcd_text_length.py` | `Model input error` (17 ký tự) bị LCD cắt; test tự động quét mọi `lcdMessage`/`lcdSetLine` |
| Tự tìm cổng COM | `camera/serial_port.py` | `usb_viewer.py`, `power_monitor.py`, `servo_bench.py`: `--port` mặc định `auto` (USB Espressif 303A). Nhiều bo thì báo và yêu cầu `--port COMx` |
| Đọc lại cấu hình | `camera/usb_viewer.py --show-config`, `camera/usb_protocol.py` | In từng khóa kèm ý nghĩa và dòng sao lưu hiệu chuẩn |
| Đo lấy ảnh camera | `tools/camera_fetch_bench.py` | Thời gian `/capture` theo từng cặp độ phân giải × chất lượng JPEG; trả camera về cài đặt cũ |
| Đo độ trễ ảnh sau khi servo dừng | `tools/servo_delay_probe.py` | Dùng firmware `-ServoTest` (lệnh `SERVO <góc>`); gợi ý thời gian chờ / số khung cần xả |
| Bảng sai số chiều cao | `tools/height_error_report.py`, `docs/QA_FIELD_LOG.csv` | Lệch TB, MAE, RMSE, tỉ lệ ±2/±5 cm theo nhóm ca và khoảng cách |
| Kiểm thử | `tools/run_tv5_tests.py` | Toàn bộ test Python + 13 chương trình test C++ của nhánh |

## 2. Hiệu chuẩn servo (firmware `-ServoTest` của nhánh chính)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -ServoTest -Flash -Port COMx
python camera/servo_bench.py            # cổng tự tìm
```

Trong `servo_bench.py`: `SERVO 90`, `SERVO 95`… để tìm góc camera **nằm ngang** (đặt app đo độ nghiêng lên camera) → `servo_level`. Thử `SERVO <level + 10>`: camera **cúi xuống** thì `servo_dir = 1`, ngẩng lên thì `-1`. `SERVO BOTTOM`/`SERVO TOP`/`SERVO SWEEP` để xem hành trình quét. Ghi các giá trị rồi nạp lại firmware thường và nhập bằng `--configure`.

Đo độ trễ ảnh (camera bật, `servo_bench.py` đóng):

```powershell
python tools/servo_delay_probe.py --camera http://<IP camera> --a 85 --b 95 --cycles 6 --json servo-delay.json
```

## 3. Cấu hình và kiểm tra lại

```powershell
python camera/usb_viewer.py --configure      # Wi-Fi, URL camera, MAC cân, lắp đặt, servo
python camera/usb_viewer.py --show-config    # đọc lại giá trị UNO đang dùng (sau khi rút điện vẫn còn)
python tools/camera_fetch_bench.py --camera http://<IP camera> --framesize 4 5 6 --quality 15 12 10
```

Lưu ý `camera_fetch_bench.py`: trên camera OhStem của lab, `framesize=5` cho 320×240 và `8` cho 640×480; chạy với `--framesize 4 5` để biết `4` (giá trị nhánh chính đặt) cho ảnh bao nhiêu điểm ảnh.

## 4. Nghiệm thu trên phần cứng (QA Sign-off của Plan 2)

| Ca | Cách làm | Đạt khi |
|---|---|---|
| QA1 Đánh thức | Hệ thống `STANDBY` (LCD tắt nền) → bước lên cân | Thức và về góc dưới trong < 1,5 s |
| QA2 Nhiều chiều cao | 3–5 người khác chiều cao | Servo dừng ở góc khác nhau, lấy trọn đầu–chân; ghi `H_thuoc_cm`, `H_he_thong_cm` vào `QA_FIELD_LOG.csv` |
| QA3 Quá gần / nhiều người | Đứng < 2 m; 2 người trong khung | LCD `Too close! / Step back!`; `Multiple people / One at a time`, không kẹt |
| QA4 Bước xuống giữa chừng | Bước xuống khi đang `SERVO_SWEEP` | Về `STANDBY`, không treo |
| QA5 Giữ kết quả | Có H/W/BMI rồi bước xuống | LCD giữ kết quả `hold_sec` (mặc định 10 s) rồi tắt nền |
| QA6 Chống sụt áp | 15 chu kỳ liên tục Wi-Fi + BLE + servo + YOLO | Không reset (theo dõi bằng viewer / `power_monitor.py`) |

Sau buổi đo:

```powershell
python tools/height_error_report.py docs/QA_FIELD_LOG.csv --md height-errors.md
```

## 5. Kiểm thử trên PC

```powershell
python tools/run_tv5_tests.py      # hoặc --cxx C:\msys64\ucrt64\bin\g++.exe
```

## 6. Đã thử trên phần cứng

- Trên firmware Plan 2 của LAB trước đó (cùng LCD, cùng YOLO UNO): LCD 16×2 hiển thị đúng các pha, đánh thức bằng cân Mi Scale 2 qua BLE, Wi-Fi kết nối, cấu hình USB + `CONFIG?`.
- **Bản của nhánh này (firmware nhánh chính + `CONFIG?`) chưa nạp lên bo**; cần thử lại `--show-config` và LCD sau khi nạp.
