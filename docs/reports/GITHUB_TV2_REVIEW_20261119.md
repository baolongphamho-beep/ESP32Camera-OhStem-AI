> **Bàn giao mã và tài liệu TV5 — 20/11/2026.** Nhánh đã có viewer/config, ResultHold, định dạng LCD thuần C++, công cụ QA và test TV5. Chưa đưa firmware servo/preview native và model 192 của LAB lên; các lệnh cho firmware đó cần bản LAB tương ứng. Kết quả kiểm thử PC không thay cho nghiệm thu cân/servo thật.

# Đối chiếu cập nhật cân trên GitHub và công việc TV5 — 19/11/2026

Đã truy vấn GitHub trực tiếp bằng `git ls-remote`, fetch cả ba nhánh vào bản đối chiếu riêng `.edge-deps/upstream-20261119`. Không merge Git, commit, push hoặc nạp bo trong lần rà này. Dự án LAB có thay đổi chưa commit nên chỉ nhập chọn lọc phần tương thích. Sao lưu các file sắp sửa ở `.edge-deps/before-tv2-review-20261119.zip`.

## 1. Đúng nhánh có cập nhật cân

| Nhánh | Commit tại lúc kiểm tra | Kết quả |
|---|---|---|
| `smartscale-edge-ai` (mặc định) | [`302a853`](https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI/commit/302a85390c8c9b134b3f9416474d2e9e278ac8bc) | So với mốc cũ `c2f71b6` chỉ sửa dòng tên nhóm trong hướng dẫn; không thêm BLE/cân |
| `feat/camera-power-integration` | [`6c5a005`](https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI/commit/6c5a0056b0a6eedd5e33fd512edfd42a8d899be9) | Nhánh cần đối chiếu: camera + cân + nguồn, dựa trên TV2 [`0aa415d`](https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI/commit/0aa415de5fd3e5b02bfdb9fda894cf38bdcbc2e6) |
| `main` | [`902321a`](https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI/commit/902321a04be21d28a405dd666107954c3a0afaf5) | Mốc cũ, cấu trúc `smartscale/`, không phải cập nhật cân mới |

Hai commit mới trên nhánh tích hợp có thời gian 18/11/2026 12:46:53 và 13:34:26, UTC+7. Đây là thời gian commit; không suy ra thời điểm push. Nội dung so với mốc `c2f71b6`: 22 file thay đổi, khoảng 1.045 dòng thêm/197 dòng xóa.

## 2. Những gì upstream đã bổ sung

Nguồn: [hướng dẫn tích hợp tại commit cố định](https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI/blob/6c5a0056b0a6eedd5e33fd512edfd42a8d899be9/docs/CAMERA_POWER_INTEGRATION.md) và mã trong cùng commit.

- Build ba chế độ: chỉ cân `-ScaleOnly`, camera liên tục `-CameraContinuous`, hoặc cân tự bật/tắt pipeline; standby BLE duty chọn 10/25/50/100%.
- Đọc quảng bá cân đúng MAC, phân biệt cân ổn định và bỏ tải; thêm chẩn đoán CPU, Wi-Fi power save, BLE duty, USB/PM lock và cấu hình Light Sleep. Không coi cờ Light Sleep là bằng chứng đã ngủ hoặc đã giảm dòng.
- `ScalePresence` tách tải cân khỏi kết quả camera, tăng session khi có lượt lên cân mới, kể cả unload/reload giữa hai lần ứng dụng đọc trạng thái.
- Pipeline dùng generation để bỏ ảnh/kết quả cũ; `DRAINING` đợi HTTP và inference trả tài nguyên trước khi hạ nguồn.
- Snapshot kết quả H/W/BMI, giữ LCD 10 s; viewer nhận `kind=scale` và lọc `session_id`.
- Thêm `camera/power_monitor.py`, test measurement và camera/power integration.

Upstream ghi nhận build/test thành công nhưng **chưa nạp/thử end-to-end bản tích hợp** trong lần chuẩn bị đó. Quan sát wake 10% scan khoảng 3–4 s, 100% dưới 1 s trong tài liệu là số của bản chỉ cân, không phải kết quả bản Plan 2 của LAB.

## 3. Những khác biệt cần giữ khi ghép Plan 2

| Nội dung | Nhánh cập nhật cân | LAB hiện tại / quyết định |
|---|---|---|
| Servo và hình học | Chưa thêm servo; fixed/ground | Giữ FSM servo Plan 2 và thông số `servo_*` |
| Model/tốc độ | Embed 224, HTTP timeout 2,2 s | Giữ model 192, hai nhân, cache 64 byte, preview, timeout 6 s và phép đo từng công đoạn |
| Hold | Snapshot giữ cố định 10 s ở tích hợp | Giữ `hold_sec` cấu hình; không thêm một bộ đếm hold thứ hai cạnh FSM |
| Config/SDK | Build flags, SDK/activation riêng | Không thay script LAB đang stage đường dẫn và kiểm hash model |
| PlatformIO | Rút bớt env trong bản upstream | Giữ các env hiện tại của nhóm |
| LCD | Có câu chờ không phù hợp và vượt chiều rộng | Giữ câu LCD ngắn, trung tính đã có trong TV5 |
| Capture gate | API khác, không có servo settle/preview | Không chép đè; phải kết hợp yêu cầu hủy ảnh theo session và góc servo |

Decoder cân mới vẫn dùng quảng bá `0x181D`; không thấy GATT `2A9D` fallback được triển khai trong native nhánh này. Không nhận định có GATT chỉ từ kế hoạch.

## 4. Phần TV5 đã nhập chọn lọc lần này

| File | Thay đổi | Phạm vi có hiệu lực |
|---|---|---|
| `camera/usb_protocol.py` | `SessionFilter`: loại generation cũ, chặn ảnh sau STANDBY/DRAINING/RESULT_HOLD, xử lý rollover/boot | Tác dụng khi firmware gửi `session_id`; gói legacy không có ID vẫn đọc như trước. Thêm hỗ trợ status của Plan 2 và preview |
| `camera/usb_viewer.py` | Nhận `kind=scale`, hiện CPU/BLE/W/LOCK và H/BMI ở hold, xóa ảnh lúc nghỉ/đổi lượt/boot | Sẵn sàng đọc firmware TV2; giữ cấu hình servo và camera-only preview. Thiếu dữ liệu hiện `--`, không giả lập kết quả |
| `camera/power_monitor.py` | Nhập monitor upstream, sửa thông báo khi không có telemetry để tránh hướng dẫn nạp nhầm | Chỉ đọc `kind=scale`, không dùng đồng thời viewer trên cùng COM. Firmware Plan 2 LAB hiện chưa gửi loại gói này |
| `lib/Measurements/measurement_state.h` | Thêm `weightView()` cho cân độc lập camera, không sinh H/BMI | API bổ sung, không đổi ý nghĩa `view()`/ResultHold hiện tại |
| `lib/Measurements/scale_presence.h` | Helper session độc lập camera, nhập có chỉnh theo API clearWeight(now) local | Đã unit test; **chưa nối vào BLE callback/FSM native LAB**, không coi đã hoàn thành chống trộn lượt trên firmware |
| `tests/`, `tools/run_all_tests.ps1` | Thử session cũ, boot/rollover/preview, hold thiếu dữ liệu; cân độc lập/TTL/unload-reload | Kiểm thử PC, không thay QA cân/servo thật |

Không đưa test `camera_power_integration_test.cpp` của upstream vào bộ test LAB vì nó gọi capture gate API khác. Có test session riêng phù hợp mã đang giữ. Không nhập toàn bộ `edge_io.cpp`, `app_main.cpp`, sdkconfig hoặc build flags upstream.

## 5. TV5 còn cần code gì?

### Phần riêng TV5 có thể làm tiếp

**Tiến độ bổ sung sau rà:** theo yêu cầu làm độc lập, đã triển khai bộ phân tích native metadata/SSV1, UI chẩn đoán nguồn dùng chung, recorder `--log`, kiểm thử mất USB/chạy lại, config reject và preview heartbeat. Xem [kết quả và giới hạn](../TV5_INDEPENDENT_WORK.md). Danh sách dưới đây là backlog tại lúc rà; phần chốt schema firmware/đọc lại NVS và nghiệm thu vật lý vẫn chờ tích hợp, không coi là hoàn thành bởi bộ test PC.

1. **QA log cho native (ưu tiên cao):** hiện `qa_log_check.py` đọc log Arduino `[SCALE]`, `[SWEEP]` có tilt và `[RESULT]`. Native LAB chưa phát đủ định dạng này. Chốt bộ log/SSV1 theo TV1/TV2, bổ sung bộ đọc native và test từ chuỗi gói mô phỏng; tránh ghi PASS cho lượt thiếu dữ liệu.
2. **UI chẩn đoán nguồn chi tiết:** hiện viewer mới hiện CPU/BLE/W; thêm trạng thái Wi-Fi PS, PM locks, USB đang chặn ngủ và phân biệt “đã bật cấu hình sleep” với “đã đo ngủ thật”. Monitor upstream đã có phần văn bản này, có thể tái dùng sau khi firmware cung cấp trường tương ứng.
3. **Mở rộng kiểm thử luồng viewer bằng gói mô phỏng:** đã thêm test vòng lặp thực với serial/OpenCV giả cho đổi người, ảnh đến muộn, DRAINING, RESULT_HOLD và boot. Còn bổ sung ca mất cổng/kết nối lại, config từ chối trong lúc đang đo và preview heartbeat; không mở cửa sổ hoặc đụng phần cứng trong các test này.
4. **Bàn giao cấu hình:** kiểm tra thông số thực sự được firmware chấp nhận/lưu và đọc lại sau reboot. Bản hiện tại có APPLY ACK nhưng chưa có lệnh đọc lại toàn bộ cấu hình; nếu thêm GET config thì phối hợp TV1/TV2 và không gửi mật khẩu ngược ra USB/log.

### Phần cần ghép chung, không coi là TV5 tự làm xong

- **TV1 + TV2:** nối session cân vào callback→snapshot→FSM→capture gate→frame/status; kiểm tra lại session lúc chốt H/W để không ghép cân người mới với ảnh người cũ. Viewer không thể tự tạo session đúng nếu firmware chưa gửi.
- **TV1 + TV2:** điều kiện draining/quiescent của HTTP/inference trước hạ nguồn, nguồn riêng cho `RESULT_HOLD`/LCD, telemetry và lựa chọn BLE scan. API capture gate local và upstream khác nhau; phải giữ servo settle và bỏ ảnh khi đổi góc.
- **TV1 + TV5:** dùng đúng một nguồn `hold_sec` trong FSM, chốt snapshot H/W/BMI trước xóa cân; thử người mới thay kết quả cũ. `MeasurementState::ResultHold` và hold tích hợp không được chồng timer.
- **TV2:** cân thật, tốc độ wake, xác minh dịch vụ BLE/GATT và đo dòng. **TV3:** nguồn/cơ khí/servo. **TV4:** nhận diện toàn thân/hiệu chuẩn và đánh giá model 192. TV5 ghi log, hiển thị và tổng hợp số liệu QA.

LCD, ResultHold, nhập cấu hình MAC/servo và các test nền TV5 đã có; **không cần viết lại từ đầu**. Phần UI mới lần này mới đạt tương thích gói trên PC, không chứng minh đã ghép cân thật trên bản firmware LAB.

## 6. Kiểm chứng

- `tools/run_all_tests.ps1`: **13 PASS, 0 FAIL, 0 SKIP**, gồm 10 chương trình C++, 72 Python chính + 3 integration và QA checker trên log mô phỏng. Sau đó bổ sung một test vòng lặp viewer: 22 test viewer đạt; bộ Python chính chạy lại riêng **73/73 đạt**. Log riêng máy `.edge-deps/tv2-review-tests-20261119.log`.
- Không mở/chiếm COM, không đổi cấu hình cân/camera, không dừng viewer hoặc nạp firmware trong lần rà này.
- `build.ps1 -ModelSize 192`: **native ESP-IDF build/link thành công**, log `.edge-deps/tv2-review-native-build-20261119.log`. Không flash; số liệu phần cứng trước đây chỉ thuộc bản đã nạp ngày 18/11.
