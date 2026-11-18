# Kế Hoạch 1: Triển Khai Trực Tiếp Pipeline Mới (Cùng AI Tại Đây)

Tài liệu này là lộ trình kỹ thuật chi tiết để **bạn và AI phối hợp trực tiếp** nâng cấp hệ thống **SmartScale (YOLO UNO ESP32-S3 + Mi Scale 2 + Camera OhStem + LCD 16×2)** theo pipeline mới:

> **Luồng mục tiêu:** Hệ thống ở trạng thái nghỉ tiết kiệm điện $\rightarrow$ Người dùng bước lên cân Mi Scale 2 gửi tín hiệu kích hoạt YOLO UNO $\rightarrow$ Trục Servo gắn camera quay từng bước từ dưới lên trên $\rightarrow$ Dừng lại ngay khi nhận diện đủ toàn thân (Full Body) $\rightarrow$ Tính chiều cao động theo góc nghiêng thực tế của servo + chốt cân nặng + tính BMI $\rightarrow$ Giữ kết quả trên LCD trước khi quay về chế độ nghỉ.

---

## 1. Bối cảnh & Điểm xuất phát từ hiện trạng Project

### 1.1. Nền tảng được chọn để nâng cấp
Repo hiện có 2 nhánh kiến trúc:
- **Pipeline 1 — Edge AI Native (`firmware/edge_yolo/`, ESP-IDF v5.5+):** Chạy toàn bộ `YOLO26n` INT8 (`224×224`), giải mã JPEG Wi-Fi, hình học tính chiều cao, nhận BLE Mi Scale 2 (`0x181D`), hiển thị LCD I2C và đẩy frame qua USB (`SSV1`) trực tiếp trên **YOLO UNO (ESP32-S3, 16MB Flash, 8MB Octal PSRAM)**.
- **Pipeline 2 — Laptop AI + Arduino/PlatformIO (`src/`, `lib/`, `camera/smartscale_camera.py`):** Firmware cũ chạy AI trên laptop rồi đẩy nhãn qua camera/UART sang YOLO UNO.

**Quyết định nền tảng:** Nâng cấp trực tiếp trên **Pipeline 1 (`firmware/edge_yolo/`)**, vì toàn bộ suy luận AI và dữ liệu cân hội tụ trực tiếp trên YOLO UNO, cho phép điều khiển Servo và quản lý trạng thái ngủ/thức tức thời mà không phụ thuộc laptop.

### 1.2. Các vấn đề hiện tại cần xử lý song song trong quá trình nâng cấp
Qua rà soát mã nguồn hiện tại:
1. **`firmware/edge_yolo/` chưa kiểm chứng phần cứng end-to-end:** Theo [`firmware/edge_yolo/README.md`](../firmware/edge_yolo/README.md), firmware mới chỉ test nạp + tự suy luận tensor `0` (`711 ms`), chưa kiểm chứng thực tế trên ảnh người thật từ camera, bản tin cân thật và LCD; file [`yolo26n_224_s8_s3.build.json`](../firmware/edge_yolo/models/yolo26n_224_s8_s3.build.json) vẫn ghi `"accuracy_validated": false`.
2. **BLE ở [`edge_io.cpp`](../firmware/edge_yolo/main/edge_io.cpp):**
   - `bleGapEventHandler()` đọc quảng bá `0x181D` nhưng **chưa lọc theo `cfg.scaleMac`** (`70:87:9e:ea:3a:e0`).
   - Mới đọc gói quảng bá bị động `0x181D`, chưa có fallback kết nối GATT `2A9D` (phòng trường hợp cân của lab chỉ gửi weight qua GATT Indication như [`lib/Scale/scale.cpp`](../lib/Scale/scale.cpp)).
   - `bleStartScan()` đang chạy 100% duty cycle (`itvl = 1600, window = 1600`), gây tốn điện và chiếm radio liên tục.
3. **Vòng lặp chính ở [`app_main.cpp`](../firmware/edge_yolo/main/app_main.cpp):** Gọi HTTP `/capture` và chạy `YOLO26n` liên tục 24/7 kể cả khi không có người; `pcfWriteNibble()` trong `edge_io.cpp` luôn ép đèn nền LCD bật (`0x08`).
4. **Mất số đo khi bước xuống ở [`measurement_state.h`](../lib/Measurements/measurement_state.h):** Khi cân gửi cờ `load_removed` (bước xuống), trọng lượng bị xóa ngay lập tức khiến BMI biến mất trước khi người đo kịp bước xuống đọc màn hình LCD.
5. **Tồn đọng khác trong repo:** [`camera/settings.json`](../camera/settings.json) (dùng cho script cũ) đang ghi nhầm `"focal_px": 3.6` (mm thay vì pixel); working tree đang có thay đổi chưa commit ở `camera/usb_viewer.py` và `firmware/edge_yolo/README.md`.

---

## 2. Phân tích & Các quyết định kỹ thuật bắt buộc

### 2.1. Chế độ nghỉ / tiết kiệm điện & Đánh thức từ cân
- **Giới hạn phần cứng ESP32-S3:** Trong **Deep Sleep**, chip tắt hoàn toàn Wi-Fi, BLE baseband và Octal PSRAM nên **không thể lắng nghe gói BLE từ Mi Scale 2**; hơn nữa wake từ Deep Sleep sẽ khởi động lại từ đầu (nạp lại ~3 MB model + kết nối lại Wi-Fi mất 3–5 giây).
- **Lựa chọn mặc định (Phương án A — Smart Standby + BLE Low-Duty Wake + ESP-PM):**
  - Khi ở `STANDBY`: Tắt hoàn toàn HTTP `/capture`, tắt suy luận `YOLO26n`, tắt đèn nền LCD (`0x00`), ngắt PWM Servo (`servoDetach()`), giảm tần số CPU & bật `CONFIG_PM_ENABLE` (DFS / Auto Light Sleep), đưa Wi-Fi vào Modem Sleep (hoặc tạm ngắt nếu ưu tiên pin), và giảm BLE scan xuống ~10% duty cycle (VD: quét `60 ms` mỗi `600 ms`).
  - Khi phát hiện gói cân hợp lệ (`weightKg > 5.0` và `!load_removed`): Đánh thức ngay lập tức (< 1 giây) về full hiệu năng.
- **Lựa chọn mở rộng (Phương án B — True Deep Sleep với Hardware Wakeup):** Nếu sau này gắn thêm công tắc hành trình / cảm biến bước chân / FSR nối vào một **RTC GPIO** (VD `GPIO 6 / D3` hoặc `GPIO 1 / A0`), firmware có thể vào Deep Sleep sâu và wake qua `esp_sleep_enable_ext0_wakeup()`.

### 2.2. Trục Servo quay từ dưới lên trên & Dừng khi Full Body
- **Quang học OV2640 (3.6mm, `320×240`, $F_{\text{pixel}} \approx 320\text{ px}$):** Góc nhìn dọc $\text{VFOV} = 2\arctan(120/320) \approx 41.1^\circ$.
  - Đặt camera cao $h_{\text{cam}} \approx 1.0\text{–}1.2\text{ m}$ (tính từ mặt cân), cách tâm cân $D \approx 2.5\text{–}3.2\text{ m}$: Servo quét từ góc nghiêng xuống (VD `+22°`) lên góc ngang/ngẩng nhẹ (`-5°`) sẽ bao quát tốt từ trẻ em (~1.1m) đến người lớn (~1.9m).
  - **Giới hạn vật lý:** Nếu đứng quá gần ($D < 2.2\text{ m}$), không có góc servo nào thu đủ cả đầu lẫn chân trong 1 khung hình. Thuật toán phải phát hiện khi **cả biên trên lẫn biên dưới cùng bị cắt** (`TOO_CLOSE`) để dừng quét và báo lên LCD `"Step back!"`.
- **Chống trễ / chống lố góc (Step-Scan thay vì quay liên tục):**
  - Một chu kỳ `capture + decode + YOLO26n` trên ESP32-S3 mất **~0.8–1.0 giây**. Nếu quay servo liên tục, lúc YOLO trả kết quả `Full body`, servo đã quay lố $20^\circ\text{–}30^\circ$.
  - Phải chạy **Step-Scan**: Nhích từng bước $\Delta\alpha$ ($4^\circ\text{–}5^\circ$) $\rightarrow$ Chờ rung & làm tươi frame đệm camera (`150–250 ms`, có thể đọc bỏ 1 frame cũ nếu camera buffer bị trễ) $\rightarrow$ Suy luận $\rightarrow$ Đánh giá khung hình $\rightarrow$ Dừng ngay khi `FULL_BODY` nằm gọn trong vùng an toàn (cách mép ảnh $\ge 6\text{–}10\text{ px}$).
- **Tính chiều cao động theo góc Servo:**
  - Hàm `groundHeight()` trong [`edge_geometry.h`](../firmware/edge_yolo/main/edge_geometry.h) **đã hỗ trợ `tiltDeg` tùy ý** (`-80°` đến `+80°`, dương là nghiêng xuống).
  - Khi Servo dừng ở góc $\alpha_{\text{stop}}$, ta quy đổi sang góc nghiêng quang học thực:
    $$\text{tiltDeg} = \text{tilt\_offset} + \text{dir} \times (\alpha_{\text{stop}} - \alpha_{\text{level}})$$
    rồi truyền thẳng vào `groundHeight()`. Không dùng chế độ `fixed` (`tilt = 0`) khi có servo.
- **An toàn nguồn & chân cắm Servo:**
  - Dùng chân PWM trống trên YOLO UNO: **`GPIO 10 (D7)`** (tránh LCD `11/12`, UART cũ `8/9`, USB `19/20`, PSRAM `26–37`).
  - **Cấp nguồn 5V riêng (adapter/buck 5V 2A+) cho Servo**, gắn tụ lọc `470–1000 µF`, **nối chung GND** với YOLO UNO. Tuyệt đối không lấy nguồn động cơ từ chân 3.3V hay qua cổng USB máy tính yếu để tránh brownout reset.

---

## 3. Máy trạng thái (FSM) mục tiêu

| Trạng thái | Hành vi chính | Điều kiện chuyển tiếp |
|---|---|---|
| `0. STANDBY` | AI/HTTP tắt, Servo park & detach PWM, đèn nền LCD tắt, BLE scan chu kỳ thấp (~10%) lọc theo MAC cân | Có gói cân `weightKg > 5.0` & `!load_removed` (hoặc RTC GPIO wake) $\rightarrow$ `WAKING_HOMING` |
| `1. WAKING_HOMING` | Bật CPU 240MHz, bật đèn nền LCD, đảm bảo Wi-Fi sẵn sàng, đưa servo về góc dưới cùng (`servoBottomDeg`) | Wi-Fi ready & Servo về vị trí đầu $\rightarrow$ `SERVO_SWEEP` (người bước xuống $\rightarrow$ `STANDBY`) |
| `2. SERVO_SWEEP` | Step-Scan từ dưới lên trên: nhích $\Delta\alpha$, chờ settle, lấy frame mới, chạy `YOLO26n` + `analyzeFraming()` | • `FULL_BODY` $\rightarrow$ `LOCKED_MEASURE`<br>• Hết góc / `TOO_CLOSE` $\rightarrow$ báo LCD + giữ ngắn $\rightarrow$ `STANDBY`<br>• Bước xuống sớm $\rightarrow$ `STANDBY` |
| `3. LOCKED_MEASURE` | Khóa servo tại $\alpha_{\text{stop}}$, tính `tiltDeg` động, lấy trung vị 2–3 frame `groundHeight()`, chờ cân chốt `LOCK` $\rightarrow$ tính BMI | Có đủ H + W `LOCK` + BMI, hoặc hết timeout cân (`10–15s`) $\rightarrow$ `RESULT_HOLD` |
| `4. RESULT_HOLD` | Giữ nguyên kết quả H, W, D, BMI trên LCD và USB Viewer trong `10–15s` để người đo bước xuống xem được | Hết thời gian giữ (hoặc có người mới bước lên lại) $\rightarrow$ Park servo $\rightarrow$ `STANDBY` |

---

## 4. Lộ trình thực hiện từng bước (Phase 0 $\rightarrow$ Phase 4)

### Phase 0: Kiểm chuẩn phần cứng nền (Hardware Baseline)
*Làm trước khi sửa code mới để khóa toàn bộ ẩn số phần cứng.*
- [ ] **0.1. Kiểm tra trạng thái Git:** Xử lý/stash hoặc giữ lại 2 file đang sửa (`camera/usb_viewer.py`, `firmware/edge_yolo/README.md`) trước khi tạo nhánh mới.
- [ ] **0.2. Xác nhận gói BLE của Mi Scale 2 thật:** Nạp env `yolo_uno_ble_scan` ([`src/ble_scanner.cpp`](../src/ble_scanner.cpp)) hoặc dùng log debug, bước lên cân kiểm tra cân phát `0x181D` kèm weight trong gói quảng bá hay yêu cầu kết nối GATT `2A9D`.
- [ ] **0.3. Chạy thử `firmware/edge_yolo` hiện tại với người thật:** Cấu hình Wi-Fi + URL camera qua `usb_viewer.py --configure`, xác nhận tốc độ `/capture` thực tế, khả năng detect của model `224×224` ở cự ly $2.5\text{–}3.0\text{ m}$ và độ ổn định LCD I2C.

> **Checkpoint 0:** Xác nhận được cân có phát `0x181D` passive không và camera `/capture` + `YOLO26n` detect được người thật ở cự ly đặt cân.

---

### Phase 1: Nâng cấp BLE Scale, Power Standby & State Hold
*Các file tác động:*
- [`firmware/edge_yolo/main/edge_io.h`](../firmware/edge_yolo/main/edge_io.h)
- [`firmware/edge_yolo/main/edge_io.cpp`](../firmware/edge_yolo/main/edge_io.cpp)
- [`firmware/edge_yolo/sdkconfig.defaults`](../firmware/edge_yolo/sdkconfig.defaults)
- [`firmware/edge_yolo/main/CMakeLists.txt`](../firmware/edge_yolo/main/CMakeLists.txt)
- [`lib/Measurements/measurement_state.h`](../lib/Measurements/measurement_state.h)
- [`tests/measurement_state_test.cpp`](../tests/measurement_state_test.cpp)

*Công việc chi tiết:*
1. **Lọc MAC cân & GATT fallback (nếu cần ở Checkpoint 0):** So sánh địa chỉ BLE nhận được với `cfg.scaleMac` trong `bleGapEventHandler()`; bổ sung hàm `bleSetLowPowerScan(bool standby)`.
2. **Điều khiển nguồn & LCD Backlight:**
   - Thêm hàm `lcdSetBacklight(bool on)` (bỏ hardcode `0x08` trong `pcfWriteNibble()`), hỗ trợ hiển thị theo từng `SystemPhase` trên LCD 16×2.
   - Bật `CONFIG_PM_ENABLE` trong `sdkconfig.defaults`, thêm `esp_pm` vào `REQUIRES` của `CMakeLists.txt`, viết API `setPowerMode(bool active)` (chuyển đổi DFS 80/240 MHz + Wi-Fi power-save `WIFI_PS_MAX_MODEM`/`NONE` + tắt/bật task nền hợp lý).
   - *(Tùy chọn)* Thêm cấu hình chân `wakeGpio` trong `EdgeConfig` nếu dùng thêm công tắc/FSR đánh thức từ Deep Sleep.
3. **Mở rộng `MeasurementState`:** Hỗ trợ cơ chế giữ kết quả (`ResultHold`) sau khi nhận `load_removed` trong N giây cấu hình được, không làm mất số đo BMI khi người dùng bước khỏi cân.
4. **Kiểm thử Phase 1:** Cập nhật & chạy `tests/measurement_state_test.cpp` bằng `g++`; test thực tế trên bo: để ở `STANDBY`, bước lên cân $\rightarrow$ xác nhận hệ thống thức dậy trong `< 1s` và đèn nền LCD sáng lên.

---

### Phase 2: Driver Servo PWM & Thuật toán Framing / Geometry động
*Các file tác động / tạo mới:*
- *Tạo mới:* `firmware/edge_yolo/main/servo_ctrl.h` & `firmware/edge_yolo/main/servo_ctrl.cpp`
- [`firmware/edge_yolo/main/CMakeLists.txt`](../firmware/edge_yolo/main/CMakeLists.txt) (thêm `servo_ctrl.cpp` và `esp_driver_ledc`)
- [`firmware/edge_yolo/main/edge_io.h`](../firmware/edge_yolo/main/edge_io.h) & `edge_io.cpp` (lưu/đọc tham số Servo vào NVS `"edge_cfg"`)
- [`firmware/edge_yolo/main/edge_geometry.h`](../firmware/edge_yolo/main/edge_geometry.h)
- [`tests/edge_geometry_test.cpp`](../tests/edge_geometry_test.cpp)

*Công việc chi tiết:*
1. **Driver Servo (`servo_ctrl`):**
   - Khởi tạo `LEDC` 50 Hz (14-bit) trên `cfg.servoPin` (mặc định `10`), quy đổi `deg -> duty_us` (mặc định `500–2500 µs`, có clamp theo `[servoMinDeg, servoMaxDeg]`).
   - Hỗ trợ di chuyển êm (step ramp mỗi 15–20 ms) để camera không giật rung mạnh; cung cấp `servoDetach()` để ngắt xung khi dừng hẳn hoặc khi ở `STANDBY`.
   - Hàm `servoToTiltDeg(float servoDeg, const EdgeConfig& cfg)` quy đổi sang góc nghiêng quang học thực tế.
2. **Nâng cấp `edge_geometry.h`:**
   - Thêm enum `FramingStatus` (`None`, `FullBody`, `TopClipped`, `BottomClipped`, `TooClose`, `SideClipped`) và hàm `analyzeFraming(box, width, height, marginPx)`.
   - Thêm hàm kiểm thử tính chiều cao kết hợp `servoDeg -> tiltDeg -> groundHeight()`.
3. **Kiểm thử Phase 2:** Chạy `tests/edge_geometry_test.cpp` bằng `g++`; test độc lập góc quay servo trên bàn lab (dùng nguồn 5V ngoài + chung GND) xem góc nâng hạ có khớp cơ khí và không gây sụt nguồn UNO.

---

### Phase 3: Tích hợp Pipeline mới vào `app_main.cpp` & USB Viewer
*Các file tác động:*
- [`firmware/edge_yolo/main/app_main.cpp`](../firmware/edge_yolo/main/app_main.cpp)
- [`camera/usb_protocol.py`](../camera/usb_protocol.py)
- [`camera/usb_viewer.py`](../camera/usb_viewer.py)
- [`tests/test_usb_protocol.py`](../tests/test_usb_protocol.py)

*Công việc chi tiết:*
1. **Viết lại vòng lặp chính trong `app_main.cpp` theo FSM 5 trạng thái:**
   - Triển khai Step-Scan từ `servoBottomDeg` lên `servoTopDeg` (bước `servoStepDeg`), dừng settle `servoSettleMs`, bỏ frame đệm cũ nếu cần, chạy `YOLO26n`, phân tích `analyzeFraming()`.
   - Khi nhận `FULL_BODY`: dừng servo ngay, chụp thêm 2–3 frame tại góc dừng lấy trung vị `height_cm` để giảm dao động box, ghép với `weightKg` ổn định để tính BMI, chuyển `RESULT_HOLD`.
   - Xử lý đầy đủ ngoại lệ: người bước xuống giữa chừng, 2 người cùng lúc (`Multiple`), người đứng quá gần (`TooClose` $\rightarrow$ báo `"Step back!"`), hết hành trình servo không thấy full body.
   - Đưa thêm `"phase"`, `"servo_deg"`, `"tilt_deg"`, `"framing"` vào gói JSON `SSV1` gửi qua `usbFrame()`.
2. **Cập nhật `usb_protocol.py` & `usb_viewer.py`:**
   - Mở rộng `config_commands()` cho phép cấu hình `mac`, `servo_pin`, `servo_bottom`, `servo_top`, `servo_step`, `servo_level`, `servo_dir`, `servo_settle`, `hold_sec`.
   - Hỗ trợ hiệu chuẩn góc ngang servo và hiển thị trực quan trạng thái FSM + góc servo lên khung hình OpenCV của `usb_viewer.py`.
3. **Kiểm thử Phase 3:** Chạy `python -m unittest discover -s tests -p "test_*.py" -v` và kiểm tra build ESP-IDF bằng [`firmware/edge_yolo/build.ps1`](../firmware/edge_yolo/build.ps1).

---

### Phase 4: Hiệu chuẩn thực nghiệm & Kiểm định độ chính xác
1. **Hiệu chuẩn cơ khí – quang học tại chỗ:**
   - Cố định giá đỡ camera + servo ở cự ly $2.5\text{–}3.2\text{ m}$, cao $1.0\text{–}1.2\text{ m}$ so với mặt trên bàn cân.
   - Đo thước thật `cameraHeightCm`; dùng thước thủy căn camera nằm ngang $0^\circ$ để ghi lại `servoLevelDeg`; hiệu chuẩn `fy/H` ở góc ngang (nhắc lại: đơn vị là `fy / H` theo tỷ lệ ảnh, **không** nhập `3.6 mm`).
2. **Kiểm thử end-to-end (End-to-End Validation):**
   - Đo đối chiếu bằng thước dây trên ít nhất 3 người có chiều cao khác nhau (thấp/trung bình/cao) tại các góc dừng servo khác nhau.
   - Kiểm tra độ tin cậy chuyển trạng thái: `STANDBY` $\rightarrow$ bước lên $\rightarrow$ quét $\rightarrow$ dừng full body $\rightarrow$ chốt cân & BMI $\rightarrow$ bước xuống xem màn hình `RESULT_HOLD` $\rightarrow$ tự về `STANDBY`.
   - Nếu `yolo26n_224_s8_s3.espdl` bắt khung chân/đầu ở biên kém ổn định, chạy [`tools/quantize.py`](../firmware/edge_yolo/tools/quantize.py) nâng `--size 256` với tập ảnh thực tế tại lab.

---

## 5. Checklist cần bạn xác nhận trước khi chúng ta bắt đầu code
1. Cổng COM của YOLO UNO và trạng thái phần cứng hiện có (đã có Servo + nguồn 5V ngoài chưa, chọn loại servo nào)?
2. Chốt **Phương án A** (BLE Wake + Standby Light/Modem Sleep) hay muốn làm cả **Phương án B** (gắn thêm cảm biến/công tắc chân nối RTC GPIO)?
3. Chốt chân PWM Servo dự kiến (đề xuất **`GPIO 10 / D7`**).
4. Kết quả test nhanh `yolo_uno_ble_scan` với chiếc Mi Scale 2 của nhóm (xác nhận gói `0x181D`).
