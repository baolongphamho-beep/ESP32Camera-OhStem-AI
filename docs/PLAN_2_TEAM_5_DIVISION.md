> **Bàn giao mã và tài liệu TV5 — 20/11/2026.** Nhánh đã có viewer/config, ResultHold, định dạng LCD thuần C++, công cụ QA và test TV5. Chưa đưa firmware servo/preview native và model 192 của LAB lên; các lệnh cho firmware đó cần bản LAB tương ứng. Kết quả kiểm thử PC không thay cho nghiệm thu cân/servo thật.

# Kế Hoạch 2: Thảo Luận & Phân Chia Công Việc Cho Nhóm 5 Người

Tài liệu này dùng cho buổi họp nhóm thống nhất kiến trúc và phân chia công việc song song cho **5 thành viên** nhằm nâng cấp hệ thống **SmartScale (`firmware/edge_yolo`)** với 2 tính năng mới:
1. **Chế độ nghỉ tiết kiệm điện & tự động đánh thức từ cân:** YOLO UNO ở trạng thái chờ tiêu thụ thấp, khi người bước lên Mi Scale 2 gửi tín hiệu về thì kích hoạt hệ thống hoạt động.
2. **Trục Servo gắn Camera tự căn khung Full Body:** Camera đặt tại vị trí cố định trên trục servo quay góc ngẩng (pitch) từ dưới lên trên, tự động dừng khi phát hiện toàn thân (Full Body) và tính toán chiều cao chính xác theo góc nghiêng thực tế.

---

## 1. Bức tranh kiến trúc chung để cả nhóm thống nhất

### 1.1. Sơ đồ khối phần cứng mới
```text
                        ┌───────────────────────────────┐
  Mi Scale 2 ──(BLE)───►│                               │◄──(Wi-Fi JPEG /capture)── Camera OhStem
                        │    YOLO UNO (ESP32-S3)        │                           (đặt trên gá Servo)
  Nguồn 5V ngoài ──────►│  - BLE Wake + Power Standby   │                                  ▲
  (riêng cho Servo,     │  - YOLO26n INT8 + Geometry    │────(PWM 50Hz: GPIO 10 / D7)──────┘
   chung GND với UNO)   │  - FSM điều khiển Servo       │
                        └───────────────┬───────────────┘
                                        │ (I2C: SDA=GPIO11, SCL=GPIO12)
                                        ▼
                                  LCD 16×2 I2C
```

### 1.2. Luồng vận hành (Máy trạng thái 5 bước)
1. **`STANDBY` (Nghỉ tiết kiệm điện):** Không gọi camera Wi-Fi, không chạy `YOLO26n`, tắt đèn nền LCD, ngắt xung giữ Servo, BLE quét cân ở chu kỳ thấp (~10% duty) và lọc đúng MAC cân.
2. **`WAKING & HOMING` (Thức dậy):** Nhận tín hiệu có người bước lên cân $\rightarrow$ Bật CPU/LCD backlight, đảm bảo Wi-Fi sẵn sàng, đưa Servo về góc dưới cùng (`Bottom`).
3. **`SERVO_SWEEP` (Quét từng bước từ dưới lên trên):** Thực hiện **Step-Scan** (*nhích $4^\circ\text{–}5^\circ$ $\rightarrow$ chờ ổn định `150–250 ms` $\rightarrow$ lấy frame mới $\rightarrow$ chạy `YOLO26n`*). Nếu thấy người quá gần (`TOO_CLOSE`) thì báo `"Step back!"`; nếu bước xuống giữa chừng thì quay lại `STANDBY`.
4. **`LOCKED_MEASURE` (Dừng & Đo):** Dừng servo ngay khi toàn thân nằm gọn trong vùng an toàn; quy đổi góc servo thành `tiltDeg` thực tế đưa vào `groundHeight()`, lấy trung vị 2–3 frame và chờ cân chốt (`LOCK`) để tính BMI.
5. **`RESULT_HOLD` (Giữ kết quả):** Giữ số đo `H / W / D / BMI` trên LCD thêm `10–15 giây` sau khi người bước xuống để kịp đọc kết quả, sau đó đưa servo về vị trí nghỉ và quay lại `STANDBY`.

---

## 2. Agenda họp nhóm 30 phút (Những điểm PHẢI chốt trước khi code)

Cả nhóm cần thống nhất 5 điểm sau ngay trong buổi họp đầu tiên:

1. **Chốt cơ chế ngủ / đánh thức YOLO UNO:**
   - **Khuyến nghị chọn Phương án A (Smart Standby + Low-duty BLE + ESP-PM Light/Modem Sleep):** Không cần độ chế thêm phần cứng vào cân, phản hồi thức dậy `< 1 giây` (vì Deep Sleep trên ESP32-S3 tắt hoàn toàn BLE/PSRAM và mất 3–5s khởi động lại để nạp model 3 MB + nối Wi-Fi).
   - *(Tùy chọn)* Có lắp thêm công tắc / cảm biến lực FSR dưới chân cân vào **RTC GPIO** (VD `GPIO 6 / D3`) để test Deep Sleep sâu hay không?
2. **Chốt phần cứng Servo & Mạch nguồn:**
   - Chọn **Servo bánh răng kim loại (`MG90S` hoặc `MG996R`)** — không dùng `SG90` nhựa vì dây Type-C cấp nguồn cho Camera OhStem rất cứng, dễ kéo trượt góc hoặc gãy nhông.
   - Thống nhất **dùng nguồn 5V 2A rời + tụ `470–1000 µF` cho Servo, nối chung Mass (GND) với YOLO UNO** để tránh sụt áp reset ESP32-S3.
   - Chốt chân PWM điều khiển Servo: **`GPIO 10 (D7)`**.
3. **Chốt vị trí lắp đặt cố định trong phòng lab:**
   - Do OV2640 (`320×240`, ống kính 3.6 mm) có góc nhìn dọc $\text{VFOV} \approx 41.1^\circ$, cần đặt camera **cách tâm bàn cân $D = 2.5\text{–}3.2\text{ m}$** và **cao $h_{\text{cam}} = 1.0\text{–}1.2\text{ m}$** so với mặt trên của cân. (Nếu đặt gần hơn $2.2\text{ m}$, khung hình dọc không bao giờ chứa đủ cả đầu và chân ở bất kỳ góc servo nào).
4. **Kiểm tra cân Mi Scale 2 thực tế của nhóm:**
   - Xác nhận ngay cân của nhóm có phát số cân trong gói quảng bá BLE `0x181D` không, hay bắt buộc phải kết nối GATT `2A9D` (có sẵn env test [`yolo_uno_ble_scan`](../platformio.ini) trong [`src/ble_scanner.cpp`](../src/ble_scanner.cpp)).
5. **Chốt quy trình Git:**
   - Giữ nhánh chính ổn định; mỗi thành viên làm trên nhánh riêng (`feat/fsm-integration`, `feat/power-ble`, `feat/servo-driver`, `feat/vision-geometry`, `feat/ui-viewer-qa`). Tách module mới (`servo_ctrl.*`) để hạn chế xung đột code.

---

## 3. Hợp đồng giao tiếp dùng chung (Interface Contract)

Đóng băng các quy ước này ngay sau buổi họp để 5 người lập trình độc lập mà vẫn ghép khớp nhau:

1. **Sơ đồ chân (Pinout) trên YOLO UNO ([`variants/yolo_uno/pins_arduino.h`](../variants/yolo_uno/pins_arduino.h)):**
   - LCD 16×2 I2C: `SDA = GPIO 11`, `SCL = GPIO 12`.
   - Servo PWM (50Hz): `GPIO 10 (D7)`.
   - Chân Wakeup phần cứng dự phòng (nếu dùng Deep Sleep): `GPIO 6 (D3)`.
   - Tuyệt đối không dùng: `GPIO 19/20` (USB), `GPIO 26–37` (Flash/PSRAM), `GPIO 43/44` (UART0).
2. **Giao diện Driver Servo (`servo_ctrl.h`):**
   - `bool servoInit(int gpioPin, int minDeg, int maxDeg);`
   - `void servoMoveDeg(float deg, uint32_t settleMs);`
   - `void servoDetach();`
   - `float servoToTiltDeg(float servoDeg, const EdgeConfig& cfg);`
3. **Giao diện Nguồn, BLE & LCD (`edge_io.h`):**
   - `void setPowerMode(bool active);` *(chuyển đổi giữa Standby tiết kiệm điện và Active)*
   - `void bleSetLowPowerScan(bool standby);`
   - `void lcdSetBacklight(bool on);`
   - `void lcdShowPhase(SystemPhase phase, ...);`
4. **Giao diện Framing & Hình học (`edge_geometry.h`):**
   - `enum class FramingStatus { None, FullBody, TopClipped, BottomClipped, TooClose, SideClipped };`
   - `FramingStatus analyzeFraming(const PersonBox& box, int width, int height, float marginPx);`
5. **Tham số NVS mới (`EdgeConfig` & [`usb_protocol.py`](../camera/usb_protocol.py)):**
   - Bổ sung các key: `servo_pin`, `servo_bottom`, `servo_top`, `servo_step`, `servo_level`, `servo_dir`, `servo_settle`, `hold_sec`.
   - Gói JSON `SSV1` bổ sung: `"phase"`, `"servo_deg"`, `"tilt_deg"`, `"framing"`.

---

## 4. Phân chia công việc chi tiết cho 5 thành viên

### Thành viên 1 (TV1): Team Lead — Kiến trúc FSM & Tích hợp hệ thống
- **File sở hữu chính:** [`firmware/edge_yolo/main/app_main.cpp`](../firmware/edge_yolo/main/app_main.cpp)
- **Nhiệm vụ cụ thể:**
  - Thiết kế và lập trình máy trạng thái 5 bước (`STANDBY -> WAKING_HOMING -> SERVO_SWEEP -> LOCKED_MEASURE -> RESULT_HOLD`) trong `app_main.cpp`.
  - Xử lý logic Step-Scan (tránh quay liên tục gây mờ ảnh & lố góc do độ trễ suy luận ~0.8–1.0s/frame), hủy quét khi người bước xuống sớm, báo lỗi khi `Multiple` hoặc `TooClose`.
  - Kết nối các module từ TV2, TV3, TV4, TV5; quản lý merge code và điều phối test tích hợp trên bo thật.
- **Tiêu chí hoàn thành (DoD):** Hệ thống chạy liên hoàn ít nhất 20 lượt không treo, tự hồi phục khi mất Wi-Fi camera hoặc người dùng rời cân giữa chừng.

### Thành viên 2 (TV2): Embedded Power & BLE Mi Scale 2
- **File sở hữu chính:** Phần BLE, Power Management & `sdkconfig.defaults` trong [`firmware/edge_yolo/main/edge_io.cpp`](../firmware/edge_yolo/main/edge_io.cpp), [`firmware/edge_yolo/sdkconfig.defaults`](../firmware/edge_yolo/sdkconfig.defaults), [`src/ble_scanner.cpp`](../src/ble_scanner.cpp).
- **Nhiệm vụ cụ thể:**
  - Dùng `yolo_uno_ble_scan` kiểm chứng gói BLE thực tế của Mi Scale 2 tại lab (`0x181D` broadcast vs GATT `2A9D`).
  - Sửa `bleGapEventHandler()` trong `edge_io.cpp` để **lọc đúng địa chỉ `cfg.scaleMac`** (và bổ sung GATT client fallback nếu cân cần kết nối mới trả số cân).
  - Triển khai chế độ `STANDBY` tiết kiệm điện: giảm BLE scan duty cycle (~10%), kích hoạt `CONFIG_PM_ENABLE` (DFS + Light Sleep), quản lý Wi-Fi Power Save, thêm hàm `lcdSetBacklight(bool on)` để tắt đèn nền LCD khi nghỉ.
  - *(Nếu nhóm chọn thêm Phương án B)* Cấu hình ngắt thức dậy RTC GPIO cho cảm biến bước chân.
  - Đo đạc và lập bảng so sánh dòng tiêu thụ (mA) giữa chế độ `STANDBY` và `ACTIVE` để đưa vào báo cáo.
- **Tiêu chí hoàn thành (DoD):** Cân thật kích hoạt YOLO UNO thức dậy ổn định dưới 1 giây khi bước lên; không nhận nhầm cân lạ; có số liệu đo giảm công suất rõ rệt lúc nghỉ.

### Thành viên 3 (TV3): Cơ khí, Mạch nguồn Servo & Driver PWM
- **File sở hữu chính:** Tạo mới `firmware/edge_yolo/main/servo_ctrl.h` & `servo_ctrl.cpp`, cập nhật [`firmware/edge_yolo/main/CMakeLists.txt`](../firmware/edge_yolo/main/CMakeLists.txt) và phần phần cứng giá đỡ.
- **Nhiệm vụ cụ thể:**
  - Thiết kế / lắp ráp gá xoay 1 trục (tilt/pitch) gắn Camera OhStem chắc chắn lên Servo, cân bằng trọng tâm và chừa dây Type-C mềm để dây không kéo lệch góc servo.
  - Đấu nối mạch cấp nguồn 5V ngoài (2A+) + tụ lọc + nối chung Mass (GND) với YOLO UNO.
  - Viết driver điều khiển Servo bằng ESP-IDF `esp_driver_ledc` (PWM 50 Hz, 14-bit), có ramp chuyển động êm, giới hạn hành trình `[servoMinDeg, servoMaxDeg]` tránh kẹt cơ khí, và `servoDetach()` khi đã dừng.
  - Đo thực nghiệm và lập công thức quy đổi góc PWM servo $\leftrightarrow$ góc nghiêng thật (`servoLevelDeg`, `servoDir`).
- **Tiêu chí hoàn thành (DoD):** Servo quay mượt từ dưới lên trên không rung giật, không làm sập nguồn (brownout reset) YOLO UNO khi chạy chung Wi-Fi/BLE/AI, độ lặp lại góc dừng ổn định.

### Thành viên 4 (TV4): Edge AI, Vision Framing & Hiệu chuẩn hình học
- **File sở hữu chính:** [`firmware/edge_yolo/main/edge_geometry.h`](../firmware/edge_yolo/main/edge_geometry.h), [`tests/edge_geometry_test.cpp`](../tests/edge_geometry_test.cpp), [`firmware/edge_yolo/tools/quantize.py`](../firmware/edge_yolo/tools/quantize.py).
- **Nhiệm vụ cụ thể:**
  - Thay thế hàm `fullBox()` đơn giản bằng `analyzeFraming()` trả về trạng thái chi tiết (`FullBody`, `TopClipped`, `BottomClipped`, `TooClose`, `SideClipped`) với vùng đệm an toàn biên (margin `6–10 px` thay vì `2 px`).
  - Kết nối góc nghiêng động `tiltDeg` từ servo vào `groundHeight()` và bổ sung bộ lọc trung vị (median 2–3 frame) sau khi servo dừng để giảm dao động box.
  - Đánh giá độ nhạy của model `yolo26n_224_s8_s3.espdl` trên ảnh thật của OhStem (`320×240`); nếu tỷ lệ detect chân/đầu ở biên kém, tiến hành tinh chỉnh ngưỡng `conf` hoặc lượng tử hóa lại với `--size 256` bằng `tools/quantize.py`.
  - Thực hiện hiệu chuẩn `fy/H` & `cameraHeightCm`, sau đó đo kiểm chứng đối chiếu thước dây với 3–5 người có chiều cao khác nhau ở nhiều góc servo.
- **Tiêu chí hoàn thành (DoD):** Pass 100% `edge_geometry_test.cpp`; có bảng thống kê sai số thực tế ($\Delta H$ tính bằng cm) trước và sau hiệu chuẩn.

### Thành viên 5 (TV5): UI/UX LCD, USB Config/Viewer, QA Test & Tài liệu
- **File sở hữu chính:** [`lib/Measurements/measurement_state.h`](../lib/Measurements/measurement_state.h), phần hiển thị LCD trong `edge_io.cpp`, [`camera/usb_viewer.py`](../camera/usb_viewer.py), [`camera/usb_protocol.py`](../camera/usb_protocol.py), [`tests/`](../tests/) & [`docs/`](../docs/).
- **Nhiệm vụ cụ thể:**
  - Cập nhật `MeasurementState` thêm trạng thái `ResultHold` (giữ kết quả BMI `10–15s` sau khi người bước xuống khỏi cân để kịp đọc LCD).
  - Thiết kế câu chữ ngắn gọn trên LCD 16×2 cho từng pha (`Standby`, `Aligning cam...`, `Step back!`, `Wait scale lock`, `H/W & D/BMI`).
  - Cập nhật `usb_protocol.py` & `usb_viewer.py` (mở rộng `--configure` để nhập `scaleMac` và các thông số servo; vẽ thêm trạng thái FSM, góc servo hiện tại, `tilt_deg` và vùng biên an toàn lên cửa sổ viewer).
  - Bổ sung unit test cho cấu hình mới trong [`tests/test_usb_protocol.py`](../tests/test_usb_protocol.py) và [`tests/measurement_state_test.cpp`](../tests/measurement_state_test.cpp); viết hướng dẫn lắp đặt – hiệu chuẩn – vận hành cho nhóm.
- **Tiêu chí hoàn thành (DoD):** Toàn bộ unit tests Python (`unittest`) và C++ (`g++`) pass 100%; người mới trong nhóm có thể cấu hình toàn bộ hệ thống qua `usb_viewer.py --configure` mà không cần sửa code cứng.

---

## 5. Lộ trình thực hiện theo 4 Mốc (Timeline đề xuất 10–14 ngày)

| Mốc | Thời gian | Hoạt động chính của các thành viên | Đầu ra kiểm tra được |
|---|---|---|---|
| **Mốc 1: Khảo sát & Chốt thiết kế** | Ngày 1–2 | • Cả nhóm họp chốt mục 2 & 3.<br>• TV2 test cân Mi Scale 2 thật.<br>• TV4 chạy thử `edge_yolo` bản gốc đo baseline người thật.<br>• TV3 dựng gá servo & mạch nguồn 5V. | Chốt file Interface Contract; biết rõ loại gói BLE của cân & cự ly đặt camera chuẩn. |
| **Mốc 2: Phát triển song song** | Ngày 3–6 | • TV2 làm BLE MAC filter + Standby/Light Sleep.<br>• TV3 hoàn thiện driver `servo_ctrl` + quay thử.<br>• TV4 viết `analyzeFraming` + test C++.<br>• TV5 làm `ResultHold` + `usb_viewer.py` + test.<br>• TV1 dựng khung FSM trong `app_main.cpp`. | Mỗi người test chạy độc lập thành công module/unit test của mình. |
| **Mốc 3: Tích hợp & Hiệu chuẩn** | Ngày 7–9 | • Merge 5 nhánh lại trên bo YOLO UNO thật.<br>• Tinh chỉnh `servoStepDeg`, `servoSettleMs` (xử lý frame đệm trễ của camera).<br>• Hiệu chuẩn `servoLevelDeg`, `cameraHeightCm`, `fy/H`. | Pipeline chạy thông suốt từ lúc bước lên cân tới lúc hiện BMI và quay về `STANDBY`. |
| **Mốc 4: Stress Test & Báo cáo** | Ngày 10–14 | • Chạy 20–30 ca test thực tế (cao/thấp, bước ra sớm, đứng quá gần, 2 người, mất Wi-Fi).<br>• TV2 chốt số liệu đo dòng; TV4 chốt bảng sai số chiều cao.<br>• TV5 hoàn thiện tài liệu, video demo & slide. | Hệ thống ổn định, đủ số liệu định lượng cho báo cáo đồ án. |

---

## 6. Checklist kiểm thử tích hợp trước khi Demo (QA Sign-off)
- [ ] **Test 1 — Đánh thức từ nghỉ:** Hệ thống ở `STANDBY` (đèn nền LCD tắt, servo nằm yên) $\rightarrow$ bước lên cân $\rightarrow$ UNO thức dậy và bắt đầu đưa servo về góc dưới trong `< 1.5s`.
- [ ] **Test 2 — Tự căn chỉnh nhiều chiều cao:** Thử liên tiếp 1 người thấp và 1 người cao hơn (hoặc ngồi/đứng) $\rightarrow$ servo dừng ở 2 góc khác nhau nhưng đều lấy trọn đầu và chân, tính `H` sai số trong ngưỡng chấp nhận được.
- [ ] **Test 3 — Đứng quá gần / nhiều người:** Đứng sát camera $< 2\text{ m}$ $\rightarrow$ không kẹt vô hạn mà báo `"Step back!"`; 2 người vào khung $\rightarrow$ báo `"One at a time"`.
- [ ] **Test 4 — Thoát sớm giữa chừng:** Bước lên cân cho servo đang quét rồi bước xuống ngay $\rightarrow$ servo dừng/trở về `STANDBY`, không treo trạng thái.
- [ ] **Test 5 — Giữ kết quả đọc BMI:** Chốt xong H + W + BMI, bước xuống khỏi cân $\rightarrow$ LCD vẫn giữ kết quả 10–15 giây để đọc rồi mới tắt đèn nền về `STANDBY`.
- [ ] **Test 6 — Chống sụt áp:** Chạy liên tục 15 chu kỳ nối tiếp với Wi-Fi + BLE + Servo + `YOLO26n` không xảy ra Brownout Reset.
