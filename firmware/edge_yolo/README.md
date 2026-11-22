# SmartScale Plan 2 — YOLO26n trên YOLO UNO

Mã nền được khôi phục từ nhánh `smartscale-edge-ai`, commit [`c2f71b6`](https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI/commit/c2f71b65c7e8e76567cf5d14f6b8240a50ab70a7), rồi tích hợp Plan 2 ngày 18/11/2026. Luồng: cân BLE → thức dậy → servo quét → khóa toàn thân → trung vị chiều cao → cân chốt/BMI → giữ kết quả → nghỉ.

```text
OhStem /capture ── Wi-Fi JPEG ──► YOLO UNO (YOLO26n + EdgeFsm)
Mi Scale 2 ─────── BLE 0x181D ──►   ├── GPIO10 PWM ──► servo camera
                                  ├── GPIO11/12 ──► LCD 16×2
                                  └── USB SSV1 ──► usb_viewer.py (tùy chọn)
```

**Đã thực hiện trên máy LAB ngày 18/11/2026:** Đã cài ESP-IDF 6.1 tại `C:\Espressif\v6.1\esp-idf`, tools/Python riêng tại `C:\Espressif`. Đã build/nạp native Plan 2 trên **COM5** và bổ sung chế độ `--preview` để thử riêng camera. Probe preview nhận **4 ảnh JPEG 640×480 trong 45 s**, YOLO xử lý khoảng 0,6 s/ảnh. Camera/mạng còn chậm và có lần truyền thiếu ảnh; ảnh thử tối/mờ, chưa kiểm chứng phát hiện người thật. Chưa thử cân và cơ khí servo toàn hệ thống. Xem [biên bản build/nạp](../../docs/reports/NATIVE_BUILD_FLASH_20261118.md).

## Phần cứng và cấu hình

- Native đang dùng partition **flash 16 MB**, PSRAM octal 8 MB theo baseline upstream. Xác minh bo thật trước khi nạp; board Arduino trong `boards/` vẫn cấu hình 8 MB.
- Servo GPIO10 (D7), nguồn 5 V riêng ≥2 A, tụ 470–1000 µF, GND chung; LCD SDA=11/SCL=12. Thu hẹp hành trình bằng `servo_bottom`/`servo_top` sau hiệu chuẩn.
- Camera giữ firmware gốc. Chọn `Stream video` trên camera để có `/capture`, nối cùng Wi-Fi 2,4 GHz với UNO. Đóng các tab/script khác đang lấy ảnh từ camera.
- Cân phải luôn có pin. Lọc đúng MAC; decoder hỗ trợ quảng bá service `0x181D` với bit stable/load-removed. **Chưa có GATT fallback** nếu cân chỉ gửi số qua `2A9D`.

`EdgeDeviceConfig` kế thừa `EdgeConfig` dùng chung; `EdgeFsm` lấy trung vị 3 khung toàn thân và xử lý người rời cân, nhiều người, quá gần, mất ảnh, giữ kết quả. Các thông số servo, `height`, `focal`, MAC lưu trong NVS `edge_cfg`; cấu hình Arduino ở `sweep_cfg` không tự chuyển sang. Các khóa cũ `mode`, `distance`, `reference`, `tilt` không điều khiển native Plan 2: góc nghiêng lấy từ servo.

## Chuẩn bị và build

Máy cần Git for Windows và **ESP-IDF 6.1** cùng toolchain do trình cài chính thức cung cấp. PlatformIO/Arduino hiện có không thay cho môi trường này. Script kiểm tra SDK, model và ESP-DL, báo rõ thành phần còn thiếu; không tự cài SDK hay xóa cache/build.

Cài SDK bằng [EIM theo hướng dẫn Espressif](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/windows-setup.html), chọn v6.1/ESP32-S3, rồi ghi lại đường dẫn thật cho `-IDFPath`. `pip install` không cài ESP-IDF. [Đường dẫn dự án và SDK không được chứa dấu cách](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/windows-start-project.html#start-a-project); với thư mục LAB chứa `Mira's Laptop`, `build.ps1` hiện tự đồng bộ các file cần thiết sang `C:\esp\SmartScale` bằng `tools/stage_build.ps1`. Vẫn chạy lệnh tại gốc dự án LAB; không cần tự chuyển mã nguồn hoặc sửa file trong bản sao build.

Từ gốc dự án:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/tools/fetch_model.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1
# Sau khi build thành công, nạp UNO (tự tìm cổng Espressif; nhiều bo thì thêm -Port COMx):
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -Flash
```

Nếu bản sao build thiếu ESP-DL, `build.ps1` gọi `prepare_sources.ps1 -DestinationRoot <build root> -SkipDataset` để tải đúng nguồn, không tải COCO8 khi chỉ cần build. Chạy riêng `prepare_sources.ps1` để chuẩn bị thêm dataset cho lượng tử hóa. `prepare_sources.ps1` tải ESP-DL pinned `512ed0b0dc5f5320f3d9ca5a5aea189803f87f31` vào `.edge-deps/esp-dl` và COCO8 để tái lượng tử hóa. `fetch_model.ps1` lấy đúng model upstream và xác minh SHA256 `76699d828b929755efc8b854a7cf313fd287a6e0d7ca21fbc82c998688b8955a`; model đã có trên máy này. Muốn tự xuất model, dùng `tools/quantize.py` và `tools/requirements.txt`. Model COCO8 chỉ dùng bring-up; độ chính xác cần kiểm định bằng ảnh thật.

Khi đổi sdkconfig cũ, dùng menuconfig áp dụng `sdkconfig.defaults`: CPU tối đa 240 MHz, task chính Core 1, TCP window 32 KB, PM enable. Không tự xóa sdkconfig cá nhân. HTTP worker ở Core 0; capture gate chỉ cho lấy ảnh sau servo settle và bỏ ảnh cũ khi góc/config/phiên đo đổi. Không prefetch ảnh qua một lần đổi góc.

Sau khi build thành công và xác nhận bo 16 MB, nạp **YOLO UNO** bằng `build.ps1 -IDFPath <đường dẫn> -Flash -Port COMx`. Không nạp camera. Trở về bản Arduino bằng upload env `yolo_uno_servo_sweep`.

## Cấu hình và vận hành

### Chỉ thử UNO và camera (chưa có cân/servo)

```powershell
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py --preview
```

`--preview` bật pha `CAMERA_TEST` tạm thời: UNO lấy JPEG và chạy YOLO mà không chờ người bước lên cân; không gắn hay quay servo, tạm dừng BLE scan, không tính H/W/BMI. Chỉ cần Wi-Fi UNO và URL camera đã cấu hình đúng. Nhấn **Q** để dừng preview; firmware trở về Plan 2. Nếu chương trình đóng đột ngột/mất USB, lease hết hạn sau 10 s và tự khôi phục Plan 2. Chế độ thử không lưu trong NVS. Chỉ mở một viewer trên COM5.

Đổi URL camera mà không nhập lại hình học/servo: thêm `--set camera=http://<IP-camera>/capture` cùng `--preview`. Native mới hỗ trợ lệnh USB `PREVIEW ON`/`PREVIEW OFF`; viewer duy trì heartbeat mỗi 3 s.

### Cấu hình và đo đầy đủ theo Plan 2

**Bản nhanh hiện đang nạp:** input 192×192, hai nhân, cache line 64 byte và ưu tiên tối đa 32 KiB SRAM cho model (có fallback về PSRAM). Camera đã đổi qua API cảm biến sang QVGA 320×240, quality giữ 15; không nạp firmware camera. Probe đầu 5 ảnh: xử lý 320–327 ms, trung vị 324 ms; riêng model 299–306 ms. HTTP ở lần này chậm 2.996–11.748 ms, nên tổng độ trễ vẫn phụ thuộc Wi-Fi. Viewer cuối đã nhận ảnh 325 ms và phát hiện người; chưa đánh giá độ chính xác theo bộ dữ liệu hay sai số chiều cao. Input nhỏ và ảnh QVGA giảm chi tiết người ở xa. Model 160 chưa xuất vì 192 đã vào khoảng 300 ms. Chi tiết trong [biên bản đo](../../docs/reports/NATIVE_BUILD_FLASH_20261118.md).

```powershell
# Giữ bản 192 khi build/nạp lại trên máy LAB (model đã có và kiểm hash)
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -ModelSize 192 -Flash

# Quay về model 224 còn được giữ nguyên
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -ModelSize 224 -Flash
```

`build.ps1` và `main/CMakeLists.txt` mặc định **192** từ 22/11 (khớp bản đang nạp); máy chưa có model 192 thì truyền `-ModelSize 224`. CMake và mã đổi tọa độ dùng chung `EDGE_MODEL_INPUT`, tránh dùng nhầm tỷ lệ 224 cho model 192. Thay đổi cache trong `sdkconfig.defaults` áp dụng cho build mới; build LAB hiện tại đã cập nhật và xác minh `CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE=64`. Model 192 chưa có URL tải công khai: cần xuất bằng `tools/quantize.py --size 192` trong môi trường ở `tools/requirements.txt`, hoặc dùng artifact đã xác minh trên máy này. Với đường dẫn có dấu nháy như `Mira's Laptop`, chạy script/weights/dataset ở cây stage `C:\esp\SmartScale` vì Ultralytics có bước loại dấu nháy trong đường dẫn weights; không đổi tên thư mục người dùng.

Thứ tự thao tác theo hai dấu ✓ trong ảnh ghi chú: **Reset/setup Wi-Fi camera → chạy configure và nhập thông số**. Xem [workflow chi tiết và bảng thông số](../../docs/WORKFLOW_WIFI_CONFIGURE.md).

Cài thư viện USB viewer vào đúng môi trường trước lần chạy đầu (đặc biệt `pyserial`, được import dưới tên `serial`):

```powershell
.\.venv-camera\Scripts\python.exe -m pip install -r camera\requirements-usb.txt
```

```powershell
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py --configure
```

Nhập Wi-Fi, URL `http://<IP-camera>/capture`, MAC cân; chọn **servo** cho hình học, đặt độ cao ống kính trên mặt cân (`height`, cm), `focal = fy / H` (không phải mm), và các khóa servo. Khuyến nghị cự ly 2,8–3,2 m, độ cao camera 1,0–1,2 m. Xem [hướng dẫn hiệu chuẩn](../../docs/HUONG_DAN_PLAN2.md).

```powershell
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py --set servo_bottom=65 --set servo_top=115 --set servo_level=90 --set servo_dir=1 --set servo_settle=400 --set hold_sec=12
```

APPLY không hợp lệ không thay cấu hình RAM. APPLY thành công lưu NVS, bỏ khung ảnh đang xử lý và bắt đầu phiên đo mới; thay MAC có hiệu lực khi quét, không cần reboot. Native gửi JPEG và JSON SSV1 CRC32 với `phase`, `servo_deg`, `tilt_deg`, `framing`, `margin_px`, H/W/D/BMI. Chỉ chạy USB viewer trên laptop, không chạy `body_tracker.py` hoặc `smartscale_camera.py` cùng lúc.

## Chế độ nghỉ và việc cần đo thật

Ở STANDBY: LCD tắt nền, servo nhả PWM, không yêu cầu `/capture`, không suy luận; BLE quét thụ động 20 ms/200 ms (~10%), Wi-Fi modem power save, CPU cho phép DFS 80–240 MHz. Lúc hoạt động giữ CPU ở 240 MHz, BLE 50 ms/100 ms. Không dừng BLE trong lúc HTTP để tránh mất gói bước xuống. Có một self-test model khi khởi động; automatic light sleep chưa bật để giữ USB/BLE hoạt động.

Phải đo dòng, độ trễ thức dậy, brownout, cơ khí/độ lặp góc và sai số 3–5 người, sau đó chạy 20–30 lượt theo [QA checklist](../../docs/QA_CHECKLIST.md). Khi không nhận load-removed, presence hết hạn theo TTL cân 30 s; không cam kết phát hiện bước xuống trong 2 s nếu cân không gửi gói đó. Driver hiện đi theo các bước FSM, chưa có ramp nội suy riêng cho hành trình homing; kiểm tra độ êm trên gá thật trước demo. Bản này chưa đạt toàn bộ DoD phần cứng của Plan 2.
