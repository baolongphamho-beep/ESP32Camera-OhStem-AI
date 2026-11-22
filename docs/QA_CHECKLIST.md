# QA Checklist – Plan 2 (servo quét + cân Mi + BMI)

Dành cho cả nhóm trước buổi demo (mục 6 và Mốc 4 của Plan 2). **Các lệnh nạp PlatformIO, UART/laptop chạy YOLO và thu log ở mục 1–2 bên dưới thuộc bản Arduino `yolo_uno_servo_sweep`.** Không dùng các lệnh đó để nạp đè bản native đang thử trên COM5.
TV5 giữ file này. Mỗi lần test thật, ghi kết quả vào [QA_FIELD_LOG.csv](QA_FIELD_LOG.csv).

**Native LAB hiện tại (19/11):** xem [đối chiếu GitHub TV2 và việc còn lại](reports/GITHUB_TV2_REVIEW_20261119.md). Native chạy YOLO trên UNO, không cần laptop `body_tracker.py` hoặc đường UART để chuyển kết quả AI. Build kiểm tra bằng `powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -ModelSize 192`; chỉ thêm `-Flash -Port COM5` khi thực sự cần nạp. Model 192 còn ở máy LAB; `--preview` chỉ thử camera, không nghiệm thu các ca cân/servo.

Native `kind=status` chưa đủ log theo schema của `qa_log_check.py`; công cụ đó chưa được coi là bộ chấm native. Viewer đã đọc được `kind=scale`/session của firmware TV2, nhưng native LAB chưa gửi gói đó và `camera/power_monitor.py` sẽ báo không có telemetry. Mốc cập nhật cân upstream chưa có servo, nên cần ghép FSM trước khi chạy đủ QA1–QA6; các ca chưa thử phải giữ `N/A`.

**Đã có bộ phân tích native riêng:** [TV5_INDEPENDENT_WORK.md](TV5_INDEPENDENT_WORK.md). Thêm `--log .edge-deps/qa/camera.jsonl` vào viewer/probe để ghi metadata có thời gian nhận trên PC; `tools/native_qa.py` đọc file đó, xuất thống kê AI/HTTP và kiểm tra session khi có ID. Schema hiện tại chưa đủ để nghiệm thu QA1–QA6, công cụ luôn giữ các ca đó là N/A. Các dòng PASS về timing/gói tin chỉ áp dụng mẫu đã ghi. Không đưa file `probe-summary.json` vào parser JSONL.

> **PASS chỉ được ghi khi đã chạy thật.** Nếu chưa đủ dữ liệu thì ghi `N/A`, không ghi PASS.

---

## 0. Trước khi lên phần cứng (khoảng 2 phút, không cần bo)

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_all_tests.ps1            # test C++ và Python
powershell -ExecutionPolicy Bypass -File tools\run_all_tests.ps1 -Firmware  # thêm build tất cả env (lâu)
```
Yêu cầu: dòng cuối phải là `0 FAIL`. Source `firmware/edge_yolo/main/` đã có, không bỏ qua `edge_geometry` vì lý do thiếu source cũ. Thiếu compiler/thư viện là thiếu môi trường kiểm chứng, không phải PASS.

Script này còn tự chạy **bộ mô phỏng QA** ([tests/qa_log_sim.cpp](../tests/qa_log_sim.cpp)). Nó sinh ra log giống hệt firmware thật cho 17 lượt đo, rồi cho [tools/qa_log_check.py](../tools/qa_log_check.py) chấm. Nếu bước này PASS thì công cụ chấm log đang chạy đúng.

| Ca QA | Đã có kiểm tra tự động trên PC tại |
|---|---|
| 1. Thức dậy | `edge_sweep_fsm_test` nhóm 1 (Standby → WAKING_HOMING trong < 100 ms sau khi có cân) |
| 2. Nhiều chiều cao | `edge_sweep_fsm_test` nhóm 1 (120/150/170/190 cm, sai số < 1 cm trong mô phỏng) |
| 3. Quá gần / 2 người | `edge_sweep_fsm_test` nhóm 3, 4 |
| 4. Bước xuống sớm | `edge_sweep_fsm_test` nhóm 2 |
| 5. Giữ kết quả | `edge_sweep_fsm_test` nhóm 1, `measurement_state_test` (ResultHold) |
| 6. Sụt áp | **Không giả lập được**, bắt buộc test trên bo thật |

---

## 1. Chuẩn bị phần cứng

- [ ] Servo SG92R dùng **nguồn 5 V riêng** (≥ 1 A), **chung GND** với YOLO UNO. Chân tín hiệu nối GPIO10.
- [ ] Camera OhStem nối UART: camera TX → GPIO9 (RX của UNO), camera RX ← GPIO8. Laptop và camera cùng mạng Wi-Fi.
- [ ] LCD 16×2 I2C sáng khi cấp điện. Cân Mi Scale 2 có pin.
- [ ] Đo bằng thước và ghi lại: **chiều cao trục servo so với sàn** = `height` (cm), **khoảng cách từ camera đến tâm cân** (nên 2,8–3,2 m).
- [ ] Nạp firmware: `pio run -e yolo_uno_servo_sweep -t upload`. **Chỉ nạp vào YOLO UNO, không nạp vào camera OhStem.**
- [ ] Cấu hình: chạy `py camera\usb_viewer.py --configure` (tự tìm cổng; thêm `--port COMx` nếu có nhiều bo), nhập `height`, `servo_level` (góc servo khi camera nhìn ngang)… Xong thì **đóng viewer**, vì viewer giữ cổng COM.
- [ ] Laptop chạy `py vision\body_tracker.py --ip <IP camera>`. Dòng `[Timing]` sẽ gợi ý giá trị `servo_settle`.

## 2. Ghi log cho mỗi buổi test

```powershell
pio device monitor -e yolo_uno_servo_sweep --filter log2file   # tạo file platformio-device-monitor-*.log
# hoặc:  pio device monitor -e yolo_uno_servo_sweep | Tee-Object -FilePath qa_run1.log
py -3 tools\qa_log_check.py qa_run1.log --csv qa_run1.csv
```
Công cụ đọc được cả file UTF-16 do PowerShell tạo. Nó in bảng gồm mỗi lượt một dòng (H, W, BMI, góc servo khi dừng, thời gian từng bước), sau đó in 6 dòng `[PASS]`, `[FAIL]` hoặc `[N/A]`. Exit code là 1 nếu có FAIL.

Firmware in ra các dòng sau (có `ms=` lấy từ `millis()`):
```
[SCALE] on kg=65.20 ms=12345
[SWEEP] -> WAKING_HOMING servo=0.0 tilt=90.0 ms=12350 | LCD "Waking up...    " / "Please wait     "
[RESULT] H=170.2 W=65.20 BMI=22.51 D=2.85 servo=84.0 ms=22000
```

> Thời gian trong log bắt đầu tính khi **bo nhận được gói BLE đầu tiên**. Thời gian từ lúc chân chạm cân đến khi có gói BLE thì phải **quay video hoặc bấm đồng hồ**.

---

## 3. Sáu ca QA trước demo

### QA1 – Đánh thức từ nghỉ
1. Để hệ thống ở STANDBY: đèn nền LCD tắt, servo không giữ lực (lấy tay xoay nhẹ được).
2. Quay video cả cân và LCD, rồi bước lên cân.

**PASS khi:** từ lúc chân chạm cân đến lúc **LCD sáng và hiện `Waking up...`** < **1,5 s** (đếm khung hình trong video), sau đó servo quay về góc dưới. Trong log, cột `wake` ≤ 1500 ms (thường là 0).
**Nếu FAIL:** BLE quét chậm, xem `[MiScale2]` trong log và MAC cân trong `config.h` (việc của TV2).

### QA2 – Tự căn chỉnh với nhiều chiều cao
1. Cho ít nhất 2 người chênh nhau ≥ 15 cm đo lần lượt (hoặc 1 người đứng rồi ngồi ghế đặt trên cân).
2. Đo chiều cao thật bằng thước, ghi vào cột `H_thuoc_cm`.

**PASS khi:** mỗi người đều ra `[RESULT]`; cột `servo` (góc dừng) **khác nhau** giữa 2 người; |H − H_thước| ≤ **ngưỡng nhóm chốt** (đề xuất 3 cm, TV4 chốt sau khi hiệu chuẩn).
**Nếu FAIL:** sai lệch đều một chiều thì chỉnh `servo_level` hoặc `height`; sai tăng theo độ cao thì chỉnh `focal` (TV4).

### QA3 – Đứng quá gần / có nhiều người
1. Đứng cách camera < 2 m (ví dụ 1,2 m).
2. Cho 2 người cùng đứng trong khung hình khi servo đang quét.

**PASS khi:**
- Khi quá gần, LCD hiện `Step back!` / `Too close to cam` khoảng 3 s rồi quét lại từ đầu, không bị kẹt. Lùi ra sau thì đo được bình thường.
- Khi có 2 người, LCD hiện `One at a time` / `2+ people seen`, **servo đứng yên**. Một người bước ra thì quét tiếp.
- Log có notice `TooClose` và `Multiple`.

### QA4 – Bước xuống giữa chừng
1. Bước lên cân. Khi servo đang quét (LCD hiện `Aligning cam...`) thì bước xuống ngay.

**PASS khi:** khoảng 2 s sau (thời gian chờ `leaveGraceMs`) hệ thống về STANDBY, đèn nền tắt, servo nhả. Bước lên lại thì đo bình thường. Log có một lượt `aborted` và không có lượt nào treo ở `open`.

### QA5 – Giữ kết quả BMI để đọc
1. Đo xong, khi LCD hiện `H:… W:…` / `BMI:… D:…m`, bước xuống cân.

**PASS khi:** LCD **vẫn giữ kết quả 10–15 s** (mặc định `hold_sec` = 12) rồi mới tắt đèn về STANDBY. Cột `hold` trong khoảng 10000–15500 ms.
**Ghi chú:** nếu có người khác bước lên trong lúc đang giữ kết quả, hệ thống đo người mới ngay. Lượt trước được đánh dấu "replaced" và không tính vào QA5.

### QA6 – Chống sụt áp (brownout)
1. Bật đủ Wi-Fi, BLE, servo và laptop chạy YOLO. Đo liên tục **15 lượt liền** mà **không tắt monitor**.

**PASS khi:** cả 15 lượt đều kết thúc (`result` hoặc `aborted`), log chỉ có **1 dòng `[BOOT]`**, không có dòng `Brownout`, `rst:0x` hay `Guru Meditation`. Theo dõi thêm `min free heap`: nếu giảm dần qua từng lượt thì có thể bị rò bộ nhớ.
**Nếu FAIL:** servo đang lấy chung nguồn 5 V với UNO. Cần tách nguồn hoặc thêm tụ 470–1000 µF ở chân nguồn servo (việc của TV3).

---

## 4. Ma trận 24 ca thực tế (Mốc 4: 20–30 ca)

Ghi vào [QA_FIELD_LOG.csv](QA_FIELD_LOG.csv), mỗi ca một dòng. Cột `H_he_thong`, `W`, `BMI`, `servo` lấy từ `qa_log_check.py --csv`.

| Nhóm | Ca | Kịch bản |
|---|---|---|
| Chiều cao | F01–F06 | 6 người khác nhau (thấp nhất đến cao nhất có được), đứng 3,0 m |
| Khoảng cách | F07–F10 | Cùng 1 người đứng ở 2,6 / 2,8 / 3,0 / 3,2 m |
| Lặp lại | F11–F14 | Cùng 1 người đo 4 lần liên tiếp (xem độ lặp của H) |
| Quá gần | F15–F16 | 1,2 m và 2,0 m |
| Nhiều người | F17–F18 | Người thứ 2 đứng cạnh / đứng phía sau |
| Bước xuống sớm | F19–F20 | Lúc đang quét / lúc đang đo |
| Giữ kết quả | F21 | Đo xong, bước xuống, bấm giờ đến khi LCD tắt |
| Người kế tiếp | F22 | Người B bước lên trong lúc LCD còn giữ kết quả của A |
| Mất mạng | F23 | Tắt Wi-Fi laptop hoặc dừng `body_tracker.py` khi đang quét → `Camera error` / `Check laptop/IP`; bật lại thì đo tiếp |
| Ánh sáng | F24 | Phòng tối hoặc ngược sáng |

**Tổng hợp cho báo cáo:**
- Sai số trung bình |ΔH| và sai số lớn nhất (từ F01–F14).
- Độ lệch chuẩn của F11–F14.
- Thời gian trung bình từ lúc bước lên đến lúc có kết quả (cột `to_result_ms`).
- Tỉ lệ ca PASS.

---

## 4b. Tổng hợp sai số chiều cao cho báo cáo

Sau khi điền `H_thuoc_cm` và `H_he_thong_cm` vào `docs/QA_FIELD_LOG.csv`:

```powershell
py -3 tools\height_error_report.py docs\QA_FIELD_LOG.csv --md .edge-deps\qa\height-errors.md
```

In bảng lệch trung bình, MAE, RMSE, lệch lớn nhất, tỉ lệ trong ±2/±5 cm theo nhóm ca và khoảng cách, và bảng từng ca. Dòng chưa đủ số liệu bị bỏ qua; số liệu chỉ lấy từ CSV.

## 5. Lỗi thường gặp

| Hiện tượng | Nguyên nhân hay gặp | Cách xử lý |
|---|---|---|
| LCD mãi `Camera error` | Laptop chưa chạy `body_tracker.py`, sai IP, hoặc chưa nối dây UART | Kiểm tra `--ip`, dây camera TX→GPIO9 |
| Quét lên đỉnh rồi báo `No full body` | Đứng gần quá, hoặc `servo_level`/`servo_dir` sai (dấu `servo_dir` đổi theo TV3 từ 22/11) | Lùi ra xa; chạy `status` trên Serial để xem góc tilt |
| Servo rung hoặc giật | Thiếu nguồn | Tách nguồn 5 V, thêm tụ |
| Servo giật mạnh lần quay đầu tiên sau khi bật nguồn | Bình thường: driver coi servo đang ở `servo_bottom`; từ lần sau tăng/giảm tốc mềm (60°/s) | Nếu vẫn giật ở các lần sau, ghi log `[SWEEP]` và báo TV3 |
| LCD hiện ký tự rác sau khi servo quay | Nhiễu/sụt áp làm LCD lệch nibble | Native (22/11) khởi tạo lại LCD mỗi lần thức dậy; nếu lặp lại thì tách nguồn servo |
| `Camera fetch` vài giây | Camera bận, sóng yếu (`RSSI` < -70 dBm trên viewer) hoặc modem sleep | Chạy `py -3 tools\camera_fetch_bench.py --camera http://<IP>` từ laptop để so sánh; đặt UNO gần AP |
| Ảnh viewer vỡ khối | JPEG quality 15 ở 320×240 | `--set cam_quality=12` (hoặc 10); xem [workflow](WORKFLOW_WIFI_CONFIGURE.md) |
| H lệch đều 5–10 cm | `height` hoặc `servo_level` đo sai | Đo lại, rồi gửi `usb_viewer.py --set height=112` |
| W hiện `unlocked` | Cân chưa chốt số (đứng chưa yên) | Đứng yên khoảng 2 s |
| Không ghi được log | `usb_viewer.py` đang giữ cổng COM | Đóng viewer |

---

## 6. Ký xác nhận (QA Sign-off)

| Ca | Kết quả (PASS/FAIL/N/A) | File log | Người test | Ngày |
|---|---|---|---|---|
| QA1 Thức dậy | | | | |
| QA2 Nhiều chiều cao | | | | |
| QA3 Quá gần / 2 người | | | | |
| QA4 Bước xuống sớm | | | | |
| QA5 Giữ kết quả | | | | |
| QA6 Chống sụt áp | | | | |

Chỉ được demo khi cả 6 ca đều PASS, hoặc ca nào chưa đạt đã được ghi rõ lý do và cả nhóm đồng ý.
