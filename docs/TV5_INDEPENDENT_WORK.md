> **Bàn giao mã và tài liệu TV5 — 20/11/2026.** Nhánh đã có viewer/config, ResultHold, định dạng LCD thuần C++, công cụ QA và test TV5. Chưa đưa firmware servo/preview native và model 192 của LAB lên; các lệnh cho firmware đó cần bản LAB tương ứng. Kết quả kiểm thử PC không thay cho nghiệm thu cân/servo thật.

# Phần TV5 làm độc lập — 19/11/2026

Đã làm phía Python/UI/QA với schema hiện có, không thay firmware BLE/FSM, không liên hệ TV1/TV2, không nạp bo và không mở cổng COM trong lần phát triển này. Giữ nguyên bản native model 192 đã nạp trước đó.

## 1. Nội dung đã hoàn thành

| Phần TV5 | Nội dung | Cách kiểm chứng |
|---|---|---|
| Chẩn đoán nguồn | Dùng chung `camera/telemetry.py` cho viewer và power monitor: Wi-Fi PS, Wi-Fi connected/not started/unknown, PM, Light Sleep, USB và PM locks | Test thiếu/sai trường, USB chặn ngủ, lock >0 và cấu hình sleep chưa đo thực tế |
| Log native | Thêm `--log` vào viewer, native probe và power monitor; JSONL append, đánh dấu ranh giới mỗi lần chạy | Recorder test thời gian, append nhiều lần; không ghi SSID/password/config commands/JPEG/box coordinates |
| Phân tích native | `tools/native_qa.py`: JSONL hoặc binary SSV1, tóm tắt min/median/max từng công đoạn, session filtering, boot count, CSV/JSON | Test CRC lỗi, frame cũ, rollover theo parser/filter, thiếu thời gian/session và đầu ra không ghi đè log đầu vào |
| Mất USB | Viewer dọn cửa sổ/đóng cổng, báo lỗi và hướng dẫn chạy lại; không tự mở lại COM hoặc phát lại cấu hình | Test tháo USB mô phỏng rồi chạy phiên viewer mới, không có lệnh BEGIN/APPLY cũ |
| Preview | Heartbeat mỗi 3 s, Q gửi OFF; mất USB thì lease firmware hết hạn 10 s | Test vòng lặp với clock và cổng giả; không cần cân/servo |
| Cấu hình lỗi | ACK bị từ chối hiện cảnh báo trên viewer và terminal; cảnh báo còn dù có status tiếp theo, không tự retry | Test config reject xen status; firmware success ACK mới xóa cảnh báo |
| Đổi lượt/hold | Giữ các test session/boot/ảnh đến muộn và H/W/BMI trong RESULT_HOLD | Test vòng lặp viewer giả không mở GUI |

Các trường chưa có trong firmware vẫn hiện `--` hoặc `unknown (no telemetry)`. `light_sleep_enabled=true` chỉ là **đã cấu hình**, không hiển thị như đã đo được ngủ thật. `no_ls_locks=-1` được xem là chưa biết. Native LAB hiện chưa gửi telemetry CPU/BLE/PM của TV2; mã hiển thị đã sẵn sàng khi nhận được các trường đó.

## 2. Tự chạy thử công cụ, không cần bo

Chạy từ gốc dự án:

```powershell
.\.venv-camera\Scripts\python.exe tools\native_qa.py tests\fixtures\native_qa_sample.jsonl --json .edge-deps\qa\sample-report.json --csv .edge-deps\qa\sample-packets.csv
```

File mẫu có cờ **SYNTHETIC SAMPLE**. Chương trình sẽ in thống kê hai frame giả 325/327 ms và để các ca QA phần cứng là `N/A`. Đây chỉ là minh họa, không đưa số liệu này vào báo cáo thực nghiệm.

`--processing-max-ms` mặc định 399 ms, là ngân sách kiểm tra các **mẫu đã ghi**, không phải cam kết tốc độ toàn hệ thống. Chọn ngân sách khác khi dùng model 224. `--capture-max-ms` chỉ kiểm khi bạn truyền ngưỡng; mặc định lấy ảnh chỉ có thống kê, không tự đặt một ngưỡng mạng giả định.

## 3. Thu log thực ở lần thử sau

Đóng viewer/Serial Monitor khác trước khi chạy. Các lệnh dưới đây **chưa được chạy trên bo trong lần này**:

```powershell
# Camera-only, xem ảnh đồng thời ghi metadata để phân tích sau
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py --port COM5 --preview --log .edge-deps\qa\camera.jsonl

# Sau khi thoát bằng Q, đọc file vừa ghi
.\.venv-camera\Scripts\python.exe tools\native_qa.py .edge-deps\qa\camera.jsonl --json .edge-deps\qa\camera-report.json --csv .edge-deps\qa\camera-packets.csv

# Hoặc thu headless 30 s, không mở viewer cùng lúc
.\.venv-camera\Scripts\python.exe tools\native_probe.py --port COM5 --preview --seconds 30 --log .edge-deps\qa\probe.jsonl --output .edge-deps\qa\probe-summary.json
```

`probe-summary.json` là tóm tắt cũ, **không** có chuỗi thời gian từng gói nên không đưa vào `native_qa.py`. Input đúng là `probe.jsonl` từ `--log`. Recorder chỉ ghi trường telemetry trong danh sách cho phép; không ghi cấu hình gửi đi, SSID, mật khẩu hoặc JPEG. File ảnh chỉ được ghi nếu người dùng chủ động dùng `--image`/`--person-image` riêng của probe. Đặt log trong `.edge-deps/qa/` để nằm ngoài Git.

Nếu thu gói SSV1 nhị phân bằng một công cụ khác, có thể đọc bằng `--format ssv1`. Chỉ thống kê gói CRC hợp lệ đã nhận; không suy ra số byte/gói đã mất từ số gói hợp lệ. Binary không có thời gian nhận gói thì không suy ra thời gian các pha.

## 4. Ý nghĩa báo cáo native

- `observed_processing`: so sánh các frame đã nhận với ngưỡng, tính riêng thời gian giải mã/chuẩn bị/AI/hậu xử lý khi có trường tương ứng.
- `observed_capture`: thời gian HTTP riêng; không cộng hai median rồi gọi là end-to-end latency. Thời gian exposure, USB và hiển thị không có phép đo đồng bộ trong schema hiện tại.
- `frame_session_isolation`: bỏ ảnh cũ/ảnh bị chặn theo session, báo N/A khi thiếu ID. PASS chỉ nói chuỗi ID quan sát được hợp lệ, không chứng minh callback BLE gắn đúng người.
- `boot_observation`: số boot packet nhìn thấy; không có packet boot không chứng minh không reset. Không kết luận brownout từ một lần khởi động.
- `observed_hold_phase_ms`: khoảng từ gói đầu quan sát `RESULT_HOLD` tới gói đổi pha. Có sai số do lấy mẫu/truyền và không phải thời gian kể từ chân rời cân.
- **QA1–QA6 luôn N/A** trong công cụ này: schema hiện tại thiếu video/tiếp xúc cân, chiều cao đối chiếu, thời điểm nhả tải, nguồn và log reset đầy đủ để nghiệm thu phần cứng. Công cụ không thay `QA_FIELD_LOG.csv` hoặc phép đo thật.

Exit code: `0` không thấy lỗi trong dữ liệu/ngưỡng đã kiểm (có thể vẫn nhiều `N/A`); `1` có FAIL; `2` input/output hoặc định dạng không hợp lệ. Không dùng exit 0 để tuyên bố đạt tất cả QA.

## 5. Phần còn chờ tích hợp

- Firmware LAB gửi `session_id`, gói cân/nguồn đầy đủ và xác thực session lúc ghép H/W: cần module TV1/TV2.
- Draining trước hạ CPU, MAC/duty/GATT/Light Sleep thực: thuộc firmware/TV2, chưa ghép trong lần này.
- Đọc lại cấu hình sau reboot cần lệnh firmware trả cấu hình đã lưu; hiện chỉ kiểm được ACK của APPLY. Chưa thêm lệnh GET hoặc tự đoán giá trị NVS. Không tự phát lại cấu hình khi USB trở lại.
- LCD thật, giữ H/W/BMI, đổi người, stress test, nguồn servo và sai số chiều cao: cần phần cứng và kiểm thử tích hợp. Unit test không được thay bằng dấu PASS thực nghiệm.

## 6. Kiểm chứng đã chạy

`tools/run_all_tests.ps1`: **13 PASS, 0 FAIL, 0 SKIP**; 10 chương trình C++, **83 test Python chính + 3 integration**, và QA checker Arduino trên log mô phỏng. Log `.edge-deps/tv5-independent-tests-20261119.log`. Lệnh mẫu native QA đã xuất JSON/CSV trong `.edge-deps/qa/`; dữ liệu mẫu được gắn nhãn synthetic.

Không sửa thư viện firmware/servo/cân trong bước này; không cần nạp lại UNO chỉ để dùng `--log` và công cụ phân tích mới. Bản `qa_log_check.py` cũ vẫn dành cho Arduino, không đổi schema cũ.

## 7. Mã bàn giao lên GitHub (20/11/2026)

PR TV5 đã được bổ sung source thay cho chỉ có tài liệu. Các nhóm file chính:

- `camera/usb_viewer.py`, `usb_protocol.py`, `telemetry.py`, `power_monitor.py`: viewer/config, session filtering, diagnostics và recorder.
- `lib/Measurements/measurement_state.h`, `scale_presence.h`: ResultHold và cân độc lập camera.
- `lib/EdgeSweep/edge_io.h`: bộ định dạng LCD thuần C++; các header config/FSM/geometry/message/servo interface/USB cùng thư mục là phụ thuộc để biên dịch và mô phỏng. Chưa đưa driver `.cpp` hoặc ứng dụng điều khiển servo vào lần bàn giao này.
- `tools/native_probe.py`, `native_qa.py`, `qa_log_check.py`, `run_tv5_tests.py` và `tests/`: công cụ và kiểm thử chạy trên PC.

Sau khi tải đúng nhánh PR, tạo môi trường Python và cài `camera/requirements-usb.txt`, rồi chạy:

```powershell
python -m pip install -r camera/requirements-usb.txt
python tools/run_tv5_tests.py
# Nếu g++ chưa có trên PATH, truyền vị trí thực tế, ví dụ máy LAB:
python tools/run_tv5_tests.py --cxx C:\msys64\ucrt64\bin\g++.exe
```

Đã chạy kiểm tra ngay trên cây mã chuẩn bị push: **76 test Python, 6 chương trình C++ và QA log mô phỏng đạt**, không có test bị bỏ qua. Số này khác 83 test của toàn bộ LAB ở mục 6 vì PR chỉ mang các phần cần cho TV5 cộng baseline upstream. Runner báo lỗi nếu thiếu compiler hoặc thư viện khiến test bị skip.

Phần `firmware/edge_yolo/main` trong nhánh vẫn là baseline cân/camera upstream, **chưa phải firmware servo/preview/model 192 đang chạy tại LAB**. Vì vậy `--preview`, `-ModelSize 192` và các hướng dẫn nạp bản nhanh cần firmware LAB tương ứng; không suy ra clone nhánh này đã có firmware đó. Không đưa model, mật khẩu, dump flash hoặc môi trường Python vào commit. PR vẫn cần chủ repo xem xét; chưa gộp vào nhánh nhóm.
