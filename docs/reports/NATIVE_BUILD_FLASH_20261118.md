> **Bàn giao mã và tài liệu TV5 — 20/11/2026.** Nhánh đã có viewer/config, ResultHold, định dạng LCD thuần C++, công cụ QA và test TV5. Chưa đưa firmware servo/preview native và model 192 của LAB lên; các lệnh cho firmware đó cần bản LAB tương ứng. Kết quả kiểm thử PC không thay cho nghiệm thu cân/servo thật.

# Build và nạp native Plan 2 trên máy LAB — 18/11/2026

Đã thực hiện theo yêu cầu người dùng: cài SDK, xử lý đường dẫn OneDrive, full build, sao lưu firmware cũ, nạp UNO và kiểm tra trạng thái sau nạp. Người dùng xác nhận thiết bị USB COM5 là **YOLO UNO**; camera OhStem giữ nguyên firmware.

## Môi trường đã cài

| Thành phần | Vị trí/phiên bản |
|---|---|
| ESP-IDF | `C:\Espressif\v6.1\esp-idf`, Git tag `v6.1` |
| IDF_TOOLS_PATH | `C:\Espressif` |
| Python cho SDK | `C:\Espressif\python_env\idf6.1_py3.11_env\Scripts\python.exe`, Python 3.11.15 |
| Compiler | Xtensa GCC 15.2.0, `esp-15.2.0_20251204` |
| Build native | `C:\esp\SmartScale\firmware\edge_yolo\build` |
| ESP-DL | `C:\esp\SmartScale\.edge-deps\esp-dl`, pinned `512ed0b0dc5f5320f3d9ca5a5aea189803f87f31` |

Trình cài EIM v0.21.1 chính thức đã được tải/xác minh SHA256. EIM gặp lỗi giải nén Clang; tiếp tục bằng `tools/idf_tools.py install --targets esp32s3`, cài cmake/ninja/ccache/idf-exe và `install-python-env --features core` của SDK chính thức, tận dụng các gói đã tải. Môi trường legacy `export.ps1` đã kiểm tra hoạt động; không phụ thuộc trạng thái dashboard EIM. SDK không được đặt trong `.venv` của YOLO/viewer.

`build.ps1` đã sửa để tự tìm SDK/Python phù hợp, đồng bộ các file cần thiết từ LAB sang thư mục build không dấu cách và chuẩn bị ESP-DL nếu thiếu. Không cần chuyển dự án gốc khỏi OneDrive. Build mặc định và lệnh PowerShell Windows 5.1 giống README đều chạy thành công. Cảnh báo Kconfig/include/deprecated từ SDK/thư viện không ngăn build; driver có cảnh báo `ledc_channel_config_t::intr_type` đã deprecated.

## Kết quả

- Chip đọc bằng esptool: ESP32-S3 rev 0.2, flash **16 MB**, PSRAM nhúng **8 MB**, USB Serial/JTAG, COM5; MAC `<MAC-UNO>`.
- Backup toàn bộ flash trước nạp: `.edge-deps/backups/yolo_uno_COM5_before_plan2_native_20261118.bin`, **16.777.216 byte**. SHA256 `1fe0f40e6f56a491db161a12407ee25cd71c668c620f5805f4fba12774617c5c`. Backup riêng máy, gitignore, không công khai vì có thể chứa NVS/Wi-Fi.
- Full build/link ESP-IDF 6.1: **SUCCESS**, 1.414 bước build app và bootloader. App **5.951.616 byte** (`0x5ad080`), còn 59% partition app 14 MB. Bootloader 22.560 byte.
- SHA256 file app: `91a71b04d8090e32fa4efc23ea298641fc5a2df0002b42b3de3d4ff91c247678`.
- Nạp bằng `build.ps1 -Flash -Port COM5`: **SUCCESS**, bootloader/app được xác minh hash; partition đã trùng nên esptool bỏ qua ghi lại. Không xóa toàn bộ flash hoặc NVS bằng lệnh erase-flash.
- Sau reset, `tools/native_probe.py` nhận **20 gói SSV1 hợp lệ trong 20 s**, phase `STANDBY`; thông báo `Standby | Step on scale`, PSRAM còn **4.347.540 byte**. Pha này chỉ đạt sau khi model nạp và warmup hoàn tất. Không thu được boot packet vì probe mở sau reset, nên không ghi số thời gian self-test.
- Log probe JSON tại `.edge-deps/native-probe-COM5-20261118.json`; metadata không chứa mật khẩu, không lưu ảnh.

## Lệnh dùng tiếp, chạy tại gốc dự án LAB

```powershell
# Build (không nạp)
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1

# Build rồi nạp UNO; thay COM5 nếu Windows đổi cổng
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -Flash -Port COM5

# Cấu hình Wi-Fi/camera/MAC/hình học/servo và xem kết quả
.\.venv-camera\Scripts\python.exe camera/usb_viewer.py --port COM5 --configure
```

Trong cấu hình chọn **servo** cho Plan 2. LCD tắt nền khi STANDBY là đúng thiết kế. Native dùng BLE để wake; cấu hình đúng MAC cân và xác minh cân quảng bá `0x181D`. Nếu upload env PlatformIO, chương trình Arduino đó sẽ thay native; nó dùng laptop chạy YOLO.

Tại thời điểm nạp bản đầu, chưa kiểm thử ảnh camera thật. Kiểm tra bổ sung camera-only bên dưới đã nhận ảnh thực; các pha servo trên gá, đọc cân thật ở native, độ trễ wake, dòng điện, BMI và sai số chiều cao với người thật vẫn chưa kiểm chứng.

## Bổ sung: thử riêng UNO + camera, chưa dùng cân/servo

Theo yêu cầu người dùng, bổ sung chế độ RAM-only `CAMERA_TEST`: viewer gửi `PREVIEW ON` mỗi 3 s, Q gửi `PREVIEW OFF`; mất heartbeat 10 s tự trở về Plan 2. Khi thử camera, firmware nhả servo, bỏ qua cập nhật FSM theo cân và tạm dừng BLE scan; không xuất H/W/BMI giả. UNO vẫn lấy JPEG qua Wi-Fi và tự chạy YOLO, Python chỉ hiển thị.

```powershell
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py --port COM5 --preview
```

- Đã build/nạp bản bổ sung vào COM5, esptool xác minh hash. App mới **5.952.688 byte**, SHA256 `50b0bd92ad40aa474d87c0c5ad187dfb76076e9b95d310638baf6d738e8cad43`. NVS/Wi-Fi được giữ lại.
- Trước sửa timeout: chỉ 1 ảnh/35 s, lỗi kết nối/chờ HTTP 2,2 s/truyền thiếu JPEG. Laptop gọi cùng camera cũng mất khoảng 3,67–12,78 s/ảnh.
- Tăng HTTP worker timeout lên **6 s**, viewer không xóa ảnh vừa nhận chỉ vì một gói báo lỗi mạng. Đây là timeout mỗi thao tác socket; tổng thời gian lấy một ảnh có thể dài hơn 6 s.
- Probe sau sửa: **60 gói SSV1, 4 ảnh JPEG 640×480 trong 45 s**, phase `CAMERA_TEST`, Wi-Fi ready, servo/tilt/H/W/BMI null. Khung đầu xử lý 614 ms, lấy ảnh 10.113 ms; khung cuối xử lý 609 ms, lấy ảnh 2.555 ms. Có lỗi truyền thiếu ảnh sau 2.872 byte; chưa đạt video mượt.
- Bằng chứng riêng máy: `.edge-deps/camera-test-probe-timeout6.json`, `.edge-deps/camera-test-timeout6.jpg`. Ảnh hiện tối/mờ, chưa có người trong ảnh để xác minh phát hiện người. Không đưa ảnh thử hoặc cấu hình Wi-Fi lên GitHub.
- Bộ kiểm tra dự án trước thay timeout: **12 PASS, 0 FAIL, 0 SKIP**; sau sửa viewer, 19 test `test_usb_viewer.py` đạt. Native build/link và nạp cuối thành công.

Môi trường viewer `.venv-camera` đã cài đúng `camera/requirements-usb.txt` (OpenCV 4.11.0.86 và pyserial 3.5); lỗi `No module named serial` ở Python khác không phản ánh môi trường này. Đóng viewer đang chạy trước khi mở chương trình khác dùng COM5.

## Bổ sung: tối ưu thời gian xử lý, giữ model 224×224

Người dùng muốn giảm số `On-board processing` từ khoảng 615 ms về 300–399 ms. Code cũ gọi `model->run()`; theo [API ESP-DL](https://docs.espressif.com/projects/esp-dl/en/latest/api_reference/model_api.html), mặc định là `RUNTIME_MODE_SINGLE_CORE`. Đã chuyển warmup và suy luận sang `dl::RUNTIME_MODE_MULTI_CORE`, giữ nguyên model, kích thước ảnh và ngưỡng phát hiện. CPU đã ở 240 MHz, PSRAM octal 80 MHz và compiler tối ưu hiệu năng từ trước.

Đã build/nạp COM5 và xác minh hash; app 5.953.008 byte, SHA256 `8b93befc1209560e23b08076141662bf0a2acc0d1222d61605c2b849524ffb5a`. Bản app trước thay được giữ tại `.edge-deps/backups/yolo_uno_app_before_dualcore_20261118.bin` (chỉ app, không phải full flash).

Probe `.edge-deps/camera-test-dualcore.json` nhận 14 ảnh/45 s:

| Công đoạn | Nhỏ nhất | Trung vị | Lớn nhất |
|---|---:|---:|---:|
| Toàn bộ xử lý trên UNO | 550 ms | 557 ms | 569 ms |
| Giải mã JPEG | 51 ms | 51 ms | 52 ms |
| Chuẩn bị ảnh | 11 ms | 11,5 ms | 12 ms |
| Chạy model | 481 ms | 488,5 ms | 499 ms |
| Hậu xử lý | 5 ms | 6 ms | 7 ms |
| HTTP lấy ảnh, tính riêng | 507 ms | 1.338 ms | 7.200 ms |

Chưa đạt 300–399 ms. Số mẫu ít, cảnh/Wi-Fi thay đổi; không dùng chênh lệch số ảnh giữa hai lần probe để kết luận tăng tốc Wi-Fi. Những ảnh trong probe này không phát hiện người; chưa kiểm định độ chính xác theo bộ dữ liệu.

Hướng tiếp theo là xuất/quantize model input 192 hoặc 160 (script `tools/quantize.py` đã có `--size`). Input 160 có khoảng 51% số pixel của 224, nhưng đó chỉ là tỷ lệ kích thước, không phải cam kết latency giảm tương ứng. Cần đo trên UNO và kiểm tra người nhỏ/xa, bounding box đầu/chân, sai số chiều cao trước khi dùng Plan 2. Chưa xuất hoặc thay model nhỏ trong lần này.

Viewer preview nay hiện cả `On-board processing` và `Camera fetch over WiFi`; giảm xử lý AI chưa đồng nghĩa tổng độ trễ hiển thị đạt 300 ms. 19 test viewer đạt; native build/link/nạp thành công. Đã mở lại viewer preview trên COM5.

## Bản nhanh 192×192 + QVGA, đã nạp sau đó

Theo yêu cầu tiếp tục giảm độ trễ, đã thực hiện:

- Cache line dữ liệu 32→64 byte theo [cấu hình ESP-DL mẫu của Espressif](https://github.com/espressif/esp-dl/blob/master/examples/mobilenetv2_cls/sdkconfig.defaults.esp32s3). Đã cập nhật `sdkconfig.defaults` và sdkconfig stage, full build lại; giá trị thực tế `CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE=64`.
- Ưu tiên tối đa 32 KiB SRAM cho model; nếu không cấp phát được thì thử lại PSRAM và giữ fallback weights ở flash. Giữ CPU 240 MHz, PSRAM 80 MHz và hai nhân, không ép xung.
- Đổi cảm biến camera qua HTTP sang **320×240** (`framesize=5`), quality giữ **15**. Trước thay: `framesize=8`, JPEG 640×480. File HTML lưu cũ có số khác, nên đã kiểm kích thước JPEG thật trước khi chốt. Không nạp firmware camera. Bản ghi cũ riêng máy `.edge-deps/camera-sensor-before-fast.json`; trả lại VGA bằng `/control?var=framesize&val=8` trên đúng camera.
- Model 224 + cache mới + QVGA: **32 ảnh/40 s**, xử lý **422–439 ms**; dữ liệu `.edge-deps/camera-test-cache64-qvga.json`. Đây là thay đổi kết hợp, không tách riêng mức tăng tốc từng tối ưu.
- Cài môi trường xuất model riêng **`C:\esp\edge-quant`**, Python 3.11, đúng `tools/requirements.txt`; pip check sạch. Không thay thư viện viewer. Xuất model **192×192** bằng tutorial ESP-DL pinned, COCO8 train, calibration 8 bước + TQT 8 bước. Giữ nguyên bản 224. Chưa cần xuất 160 vì 192 đã vào khoảng 300 ms.
- `build.ps1 -ModelSize 192` kiểm SHA256 và metadata input/target; CMake chọn đúng artifact, `EDGE_MODEL_INPUT` dùng cả embed symbol và đổi tọa độ box. Test hình học cho 160/192/224 cùng ánh xạ về VGA đạt.

Model 192: **3.081.456 byte**, SHA256 `90fdc63553edbd53ac9474f95d74a809f6d4fdd8fb8ddc0efc8d7bdc1c360619`, manifest `firmware/edge_yolo/models/yolo26n_192_s8_s3.build.json`. Giữ `accuracy_validated=false`: test chạy/nhận người không thay cho đánh giá mAP hay sai số chiều cao.

Firmware đang nạp trên COM5: **5.951.808 byte**, SHA256 `516ec1a7b099454f65adcaea2b09180cdec441f752ecbeb3868c615b49752864`; esptool xác minh hash, giữ NVS. Backup app trước cache trong `.edge-deps/backups/yolo_uno_app_before_cache64_20261118.bin`, trước model 192 trong `.edge-deps/backups/yolo_uno_app_cache64_224_qvga_20261118.bin` (các file này chỉ là app, không phải full flash).

Lượt probe đầu bản 192 (`.edge-deps/camera-test-fast192.json`): **5 ảnh/45 s**, xử lý **320–327 ms**, trung vị **324 ms**, model **299–306 ms**. HTTP lại chậm **2.996–11.748 ms**, có timeout; không thể coi 324 ms là độ trễ toàn hệ thống. Ảnh đọc kiểm tra sau đó đang hướng lên trần nhà, nên lượt này chưa chứng minh nhận người. Sau khi mở lại viewer, log đã xác nhận **phát hiện người trên UNO**; chưa định lượng độ chính xác nhận diện/người xa.

```powershell
# Nạp lại đúng bản nhanh (model 192 đã có trên máy LAB)
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -ModelSize 192 -Flash -Port COM5
# Xem ảnh sau khi nạp, không cần cân/servo
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py --port COM5 --preview
```

Mặc định build vẫn chọn 224 để giữ khả năng tải model baseline từ upstream. Artifact 192 chỉ có riêng máy hiện tại, chưa công khai/commit/push. Để tái xuất trên máy LAB, script/weights/dataset được đặt trong cây stage không dấu nháy `C:\esp\SmartScale`; Ultralytics loại dấu nháy trong đường dẫn weights nếu chạy trực tiếp dưới `<user-with-spaces>`. Lệnh đã chạy:

```powershell
& C:\esp\edge-quant\Scripts\python.exe C:\esp\SmartScale\firmware\edge_yolo\tools\quantize.py --size 192
```

Sau xuất, copy artifact `.espdl` và `.build.json` về thư mục model ở dự án LAB. Không chép đè bản 224. Native build/link/nạp, test C++ hình học/capture/lease, 19 test viewer và py_compile các script mới đều đạt.

**Kiểm tra bàn giao:** lượt probe bổ sung 30 s có boot packet và Wi-Fi chưa ready, không nhận ảnh; lưu `.edge-deps/camera-test-fast192-person.json`, không dùng lượt này để tính tốc độ. Sau đó mở lại viewer, log `.edge-deps/viewer-preview-final.log` xác nhận nhận JPEG 320×240, xử lý **325 ms** và **phát hiện người trên UNO**. Viewer đang được giữ mở. Mạng có giai đoạn chưa kết nối/HTTP timeout nên vẫn cần cải thiện điều kiện Wi-Fi; chưa xác định nguyên nhân của boot packet trong lượt probe bổ sung.

## Khôi phục firmware trước nạp nếu cần

Chỉ chạy khi muốn quay về toàn bộ trạng thái flash cũ của **chính UNO này**:

```powershell
& "$env:USERPROFILE\.platformio\packages\tool-esptool\esptool.exe" --port COM5 --baud 460800 write-flash 0 .edge-deps/backups/yolo_uno_COM5_before_plan2_native_20261118.bin
```

Lệnh khôi phục **chưa được chạy**; giữ nguyên bản firmware Plan 2 đang hoạt động. Không commit/push trong phiên này.
