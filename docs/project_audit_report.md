# Báo Cáo Rà Soát Toàn Bộ Dự Án — `NimBLE_SmartScale_Cp`

> [!NOTE]
> **Cập nhật trạng thái (sau commit `5533edb` & hoàn thiện Phase 1 + Phase 2):** Toàn bộ các lỗi Critical/High và các mục tối ưu ưu tiên 1–2 của **Pipeline 1 (`firmware/edge_yolo/`)** trong báo cáo này (song song hóa Core 0/Core 1, tick `1000 Hz`, LCD resync không busy-wait, I2C pull-up & cache dòng LCD, tắt đèn nền LCD ở `STANDBY`, Wi-Fi reconnect dùng `esp_timer`, BLE lọc MAC + `BleScanRetry` + mặc định standby `10%`, đồng bộ `hold_sec` mặc định `10s`, `HeightMedianFilter`, `AutoCalibCollector`, event-driven sleep kể cả trong `lcdTask`, RAII `CaptureLease`, tối ưu Internal SRAM khi boot bằng cách đẩy `dl::Model`/`HttpCaptureClient`/buffer JSON sang PSRAM và đặt `ALWAYSINTERNAL=512`, driver Servo LEDC 50 Hz 14-bit dùng `ledc_set_duty`/`ledc_update_duty`, `analyzeFraming()`, ROM CRC32, `usb_viewer.py`, `power_monitor.py`, `servo_bench.py` & `quantize.py`) **đã được xử lý xong**. Các mục thuộc **Pipeline 2 cũ** (`src/`, `lib/BLE/`, `lib/Scale/`, `lib/calib_fsm/`, `boards/`, `camera/start_smartscale.ps1`) được giữ nguyên theo phạm vi dự án để không ảnh hưởng nhánh di sản.

> **Phạm vi kiểm tra ban đầu:** Toàn bộ mã nguồn C++ (ESP-IDF `firmware/edge_yolo/`, Arduino/PlatformIO `src/` & `lib/`), công cụ Python & PowerShell (`camera/`, `tools/`), cấu hình build (`sdkconfig*`, `platformio.ini`, `boards/`), unit tests (`tests/`) và tài liệu (`docs/`, `README.md`).  
> **Trạng thái kiểm thử tự động hiện tại:** Kiểm tra lệnh thực tế tại [CAMERA_POWER_INTEGRATION.md](./CAMERA_POWER_INTEGRATION.md) (29 Python unit tests & 11 C++ host unit tests đều PASS).

---

## 1. Tổng Quan Ngữ Cảnh & Kiến Trúc Dự Án

Dự án gồm **2 pipeline** cùng hướng đến bài toán đo chiều cao + cân nặng + BMI tự động trên bo **YOLO UNO (ESP32-S3, 16MB Flash, 8MB Octal PSRAM)**:

1. **Pipeline 1 — Edge AI Native (`firmware/edge_yolo/`, ESP-IDF v5.5.6 + `esp-dl v3.3.15`) [Nhánh chính]:**
   - ESP32-S3 trực tiếp tải JPEG từ Camera AI OhStem (`http://<IP>/capture`) qua Wi-Fi, chạy **YOLO26n INT8 (`224×224`)** trên chip, tính chiều cao theo hình học pinhole (`fixed` / `ground`), nghe quảng bá BLE `0x181D` từ cân **Mi Scale 2**, hiển thị lên **LCD 16×2 I2C** và stream frame + JSON qua USB-Serial/JTAG bằng giao thức nhị phân `SSV1 + CRC32` cho [`usb_viewer.py`](../camera/usb_viewer.py).
   - Theo [`PLAN_1_AI_CO_IMPLEMENTATION.md`](./PLAN_1_AI_CO_IMPLEMENTATION.md) và [`PLAN_2_TEAM_5_DIVISION.md`](./PLAN_2_TEAM_5_DIVISION.md), giai đoạn tiếp theo sẽ nâng cấp thêm chế độ nghỉ tiết kiệm điện (`STANDBY` đánh thức bằng cân) và trục Servo xoay camera theo góc nghiêng.
2. **Pipeline 2 — Laptop AI + PlatformIO/Arduino (`src/`, `lib/`, `camera/smartscale_camera.py`) [Nhánh cũ / dự phòng]:**
   - Laptop chạy `yolo26n.pt` (Ultralytics), gửi nhãn qua HTTP `/control?serial=...` tới camera để camera chuyển tiếp qua UART Grove sang YOLO UNO; YOLO UNO dùng `NimBLE-Arduino` vừa làm Client kết nối GATT cân Mi Scale 2 vừa làm Server phát cho TabletApp, kèm mã nguồn cũ cho cảm biến lực FSR và máy quét QR GM65.

---

## 2. Các Lỗi Đã Phát Hiện (Phân Theo Mức Độ)

### 🔴 Mức Nghiêm Trọng (Critical — Gây sai hành vi cốt lõi, treo hoặc crash)

#### 1. Pipeline Dual-Core của `edge_yolo` thực tế đang chạy **Đơn Nhân trên Core 0**
- **Vị trí:** [`sdkconfig.defaults`](../firmware/edge_yolo/sdkconfig.defaults#L1-L33), [`sdkconfig`](../firmware/edge_yolo/sdkconfig#L1647-L1651), [`app_main.cpp`](../firmware/edge_yolo/main/app_main.cpp#L129-L130) & [L236-L238](../firmware/edge_yolo/main/app_main.cpp#L236-L238).
- **Nguyên nhân:** `app_main.cpp` pin `captureWorkerTask` (priority 4) lên Core 0 với giả định `app_main()` chạy YOLO26n trên **Core 1**. Tuy nhiên `sdkconfig.defaults` **không cấu hình `CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1=y`**, nên trong `sdkconfig` sinh ra vẫn là `CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0=y`.
- **Hậu quả:** Toàn bộ Wi-Fi (`CORE_0`), NimBLE (`CORE_0`), `captureWorkerTask` (`CORE_0`, prio 4) và suy luận `YOLO26n` (`app_main`, `CORE_0`, prio 1) **tranh giành cùng Core 0**, trong khi **Core 1 gần như bỏ trống**. Không đạt được song song hóa tải ảnh & suy luận, đồng thời dễ gây đói `IDLE0` dẫn tới kích hoạt Task Watchdog (`5s`).
- **Cách sửa:** Thêm `CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1=y` vào `sdkconfig.defaults` (và bổ sung kiểm tra cờ này trong [`build.ps1`](../firmware/edge_yolo/build.ps1#L89-L97)), hoặc tạo task suy luận riêng bằng `xTaskCreatePinnedToCore(..., 1)`.

#### 2. Delay khởi tạo LCD HD44780 bị rút gọn về **0 ms** do `CONFIG_FREERTOS_HZ=100`
- **Vị trí:** [`edge_io.cpp`](../firmware/edge_yolo/main/edge_io.cpp#L471-L483) và [`sdkconfig`](../firmware/edge_yolo/sdkconfig#L1849).
- **Nguyên nhân:** Tick FreeRTOS đang là `100 Hz` (`1 tick = 10 ms`). Do chia nguyên, `pdMS_TO_TICKS(5)` và `pdMS_TO_TICKS(1)` đều cho kết quả **`0` tick**:
  - `vTaskDelay(pdMS_TO_TICKS(5))` sau lệnh `0x30` đầu tiên → **chờ 0 ms** (datasheet HD44780 yêu cầu ≥ 4.1 ms).
  - `vTaskDelay(pdMS_TO_TICKS(1))` sau lệnh `0x30` thứ hai → **chờ 0 ms** (yêu cầu ≥ 100 µs).
  - `vTaskDelay(pdMS_TO_TICKS(5))` sau lệnh Clear Display `0x01` → **chờ 0 ms** (yêu cầu ≥ 1.52 ms).
- **Hậu quả:** LCD rất dễ khởi tạo hỏng, loạn ký tự hoặc hiện ô vuông đen khi mới cấp nguồn. Ngoài ra nhánh `USE_I2C_MASTER_NEW` ([`edge_io.cpp`](../firmware/edge_yolo/main/edge_io.cpp#L425-L431)) **quên bật `bus_cfg.flags.enable_internal_pullup = true`** (trong khi nhánh legacy có bật), và [`edge_io.cpp`](../firmware/edge_yolo/main/edge_io.cpp#L434) chỉ quét 5 địa chỉ `{0x21, 0x27, 0x20, 0x3F, 0x38}` thay vì toàn bộ dải `0x20..0x27` và `0x38..0x3F` như tài liệu mô tả.
- **Cách sửa:** Dùng `esp_rom_delay_us()` cho các khoảng chờ chính xác của HD44780 (`5000`, `200`, `2000` µs) và/hoặc đặt `CONFIG_FREERTOS_HZ=1000`; bật `enable_internal_pullup = true`; quét đầy đủ `0x20..0x27` và `0x38..0x3F`.

#### 3. Gọi `vTaskDelay(2000)` trực tiếp trong Event Loop hệ thống (`wifiEventHandler`)
- **Vị trí:** [`edge_io.cpp`](../firmware/edge_yolo/main/edge_io.cpp#L178-L186) & [`applyWifiConfig`](../firmware/edge_yolo/main/edge_io.cpp#L205-L227).
- **Nguyên nhân:** `wifiEventHandler` chạy trên task `sys_evt` (Default Event Loop). Khi mất Wi-Fi, gọi `vTaskDelay(pdMS_TO_TICKS(2000))` làm **treo toàn bộ Event Loop trong 2 giây**. Khi người dùng đổi Wi-Fi qua USB, `applyWifiConfig` gọi `esp_wifi_disconnect()` rồi `esp_wifi_connect()`, ngay sau đó event `DISCONNECTED` cũ chạy trong `sys_evt`, ngủ 2s rồi lại gọi `esp_wifi_connect()` đè lên kết nối mới.
- **Lỗi đi kèm tại `applyWifiConfig`:**
  - Ép cứng `wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK` ([L215](../firmware/edge_yolo/main/edge_io.cpp#L215)) → **không thể kết nối Wi-Fi Open (không mật khẩu)** hoặc AP WPA1.
  - `sizeof(wifi_cfg.sta.ssid) - 1` ([L213-L214](../firmware/edge_yolo/main/edge_io.cpp#L213-L214)) cắt mất 1 ký tự cuối nếu SSID dài đúng 32 byte hoặc mật khẩu dài đúng 64 byte.
- **Cách sửa:** Dùng `esp_timer` một lần (one-shot) để hẹn gọi lại `esp_wifi_connect()` sau 2s thay vì block `sys_evt`; đặt `authmode = WIFI_AUTH_OPEN` khi `password` rỗng và `WIFI_AUTH_WPA_WPA2_PSK` khi có mật khẩu.

#### 4. Con trỏ chưa khởi tạo gây Undefined Behavior / Crash trong `BLE::init()` (Pipeline 2)
- **Vị trí:** [`lib/BLE/ble.h`](../lib/BLE/ble.h#L19-L20), [`lib/BLE/ble.cpp`](../lib/BLE/ble.cpp#L95-L99), [`lib/Scale/scale.cpp`](../lib/Scale/scale.cpp#L51-L53).
- **Nguyên nhân:** `_ble_client` và `_ble_server` không được khởi tạo `= nullptr`. Khi `ble_node = new BLE()` cấp phát vùng heap chứa rác khác 0, `BLE::init()` kiểm tra `if (_ble_server != nullptr) return 0;` và thoát ngay không gọi `NimBLEDevice::init()`, sau đó `createService()` / `advertise()` truy cập con trỏ rác → **crash (LoadProhibited)**.
- **Lỗi đi kèm trong `lib/BLE/`:**
  - [`ble.h`](../lib/BLE/ble.h) thiếu `#pragma once` / include guard.
  - [`ble.cpp`](../lib/BLE/ble.cpp#L355-L358): `uuidStr.length() <= 4` **luôn sai trên NimBLE-Arduino v2.3.6**, vì `NimBLEUUID("181d").toString()` trả về `"0x181d"` (độ dài **6**)! Hậu quả là **không có Service UUID nào được thêm vào gói quảng bá BLE cho TabletApp** (phải dùng `getUUID().bitSize() == 16`).
  - [`ble.cpp`](../lib/BLE/ble.cpp#L67): Lưu con trỏ thô `_target_advertise_device = advertisedDevice` qua nhiều vòng reconnect thay vì sao chép `NimBLEAddress` (rủi ro dangling pointer).
  - [`ble.h`](../lib/BLE/ble.h#L90-L91): `setValue<uint8_t*>` không kiểm tra `size >= 0`, nếu gọi với mặc định `size = -1` sẽ ép kiểu thành `size_t` cực lớn.

#### 5. Vòng lặp treo vô hạn & sai chu kỳ lấy mẫu 1000× trong `readForceSensors` (Pipeline 2)
- **Vị trí:** [`lib/Scale/scale.cpp`](../lib/Scale/scale.cpp#L232-L234), [L259](../lib/Scale/scale.cpp#L259), [L289](../lib/Scale/scale.cpp#L289) và [`config.h`](../lib/Config/config.h#L35).
- **Nguyên nhân:**
  1. `while (master_serial.available() == 0) { vTaskDelay(pdMS_TO_TICKS(10)); }` **không có timeout** → nếu ESP Slave mất kết nối hoặc rớt byte `0xA5`, task treo vĩnh viễn và vòng `retry < 3` không bao giờ chạy.
  2. `SCALE_FORCE_SENSOR_SAMPLE_FREQUENCY` có đơn vị là **Hz (`1`)**, nhưng lại truyền thẳng vào `pdMS_TO_TICKS(SCALE_FORCE_SENSOR_SAMPLE_FREQUENCY)` → delay **1 ms (1000 Hz)** thay vì `1000 / freq` = **1000 ms (1 Hz)**.
  3. Tương tự ở [L319](../lib/Scale/scale.cpp#L319), so sánh `TickType_t` trực tiếp với `1000.0 / SCALE_FORCE_SENSOR_SAMPLE_FREQUENCY` mà thiếu `pdMS_TO_TICKS()`.

#### 6. Module `lib/calib_fsm/calib_fsr.cpp` không thể biên dịch
- **Vị trí:** [`lib/calib_fsm/calib_fsr.cpp`](../lib/calib_fsm/calib_fsr.cpp#L15-L18).
- **Nguyên nhân:** Tham chiếu `PIN_1..PIN_8` không tồn tại trong [`lib/Config/config.h`](../lib/Config/config.h#L65-L85) (đã đổi thành `PIN_L1..PIN_L10`). Hiện chưa gây lỗi build chỉ vì `src/main.cpp` không `#include "calib_fsr.h"`.

---

### 🟠 Mức Cao (High — Lỗi logic, Race Condition, mất dữ liệu đo hoặc hỏng script trên máy mới)

#### 7. BLE trong `edge_yolo`: Không lọc MAC, Race Condition làm chết scan, thiếu fallback GATT/`0x181B`
- **Vị trí:** [`edge_io.cpp`](../firmware/edge_yolo/main/edge_io.cpp#L556-L620).
- **Chi tiết:**
  - `bleGapEventHandler()` **không kiểm tra `event->disc.addr` với `s_config.scaleMac`**, dù NVS và giao thức cấu hình có lưu `mac` (cân lạ ở gần có thể ghi đè cân nặng).
  - `s_ble_synced`, `s_ble_paused` (và `s_wifi_ready` ở [L169](../firmware/edge_yolo/main/edge_io.cpp#L169)) là biến `static bool` thường (không `std::atomic`) được đọc/ghi đồng thời từ nhiều task/core dưới cờ tối ưu `-O2`.
  - `bleStartScan()` **bỏ qua mã lỗi trả về của `ble_gap_disc()`** ([L607](../firmware/edge_yolo/main/edge_io.cpp#L607)). Nếu `bleSetCapturePriority(false)` chạy đúng lúc `ble_gap_disc_cancel()` chưa hoàn tất hoặc Wi-Fi Coex báo `BLE_HS_EBUSY`, `ble_gap_disc()` thất bại âm thầm và BLE có thể ngừng quét cho tới lần capture kế tiếp (hoặc tắt hẳn nếu `DISC_COMPLETE` đã bắn trước đó).
  - Khi camera lỗi/timeout (2.2s), `bleSetCapturePriority(true)` **tắt BLE scan suốt 2.2s** mỗi lượt thử lại.
  - Chỉ đọc quảng bá thụ động `0x181D`, chưa xử lý `0x181B` (Mi Body Composition Scale 2) hay kết nối GATT `0x2A9D` nếu cân của lab không phát weight trong broadcast.

#### 8. Nguy cơ ghi `NaN` / `Inf` vĩnh viễn vào NVS & Race khi tự động hiệu chuẩn tiêu cự (`focalPx`)
- **Vị trí:** [`edge_io.cpp`](../firmware/edge_yolo/main/edge_io.cpp#L151-L164), [L327-L331](../firmware/edge_yolo/main/edge_io.cpp#L327-L331) và [`app_main.cpp`](../firmware/edge_yolo/main/app_main.cpp#L329-L333).
- **Chi tiết:**
  - `processConfigLine()` gọi `strtof()` mà **không kiểm tra `std::isfinite` hay miền giá trị** (`distance > 0`, `reference > 0`, `focal >= 0`). Nếu `referenceCm == 0` hoặc `NaN`, dòng `focal = (box.y2 - box.y1) * cfg.distanceM / (cfg.referenceCm / 100)` sinh `Inf`/`NaN` và `saveFocalIfUnchanged()` **lưu thẳng `Inf`/`NaN` vào NVS**, làm hỏng hoàn toàn chế độ đo cho đến khi người dùng ghi đè lại `focal`.
  - `saveFocalIfUnchanged()` chỉ so sánh `focalPx` mà không kiểm tra `groundMode`, `distanceM`, `referenceCm` có vừa bị đổi qua USB trong lúc suy luận (~711ms) hay không.
  - Hơn nữa, tự hiệu chuẩn lấy ngay **khung hình đơn đầu tiên** vượt qua `fullBox()` (dù người mới bước vào mép khung, hơi cúi hoặc box bị nhiễu) rồi khóa vĩnh viễn vào NVS mà không chờ ổn định 3–5 frame.

#### 9. Mất cân nặng & BMI ngay khi YOLO rớt 1 khung hình hoặc khi người dùng bước xuống cân
- **Vị trí:** [`measurement_state.h`](../lib/Measurements/measurement_state.h#L29-L30) & [`edge_io.cpp`](../firmware/edge_yolo/main/edge_io.cpp#L577-L579).
- **Chi tiết:**
  - Chỉ cần model INT8 `224×224` bỏ sót người trong **1 frame duy nhất** (ví dụ `score = 0.49`), `cameraState(PersonStatus::None)` gọi ngay `clearWeight()` → **xóa sạch cân nặng và BMI** dù người vẫn đang đứng yên trên cân (Mi Scale 2 sau khi chốt sẽ ít/ngừng gửi gói mới).
  - Khi người bước xuống để đi tới đọc màn hình LCD, `load_removed` xóa cân nặng ngay lập tức làm biến mất kết quả BMI trước khi người đo kịp xem.

#### 10. Lỗi đồng bộ luồng (Data Race) & dùng sai API ISR trong `lib/Scale/scale.cpp` và `lib/GM65/` (Pipeline 2)
- **Vị trí:** [`lib/Scale/scale.cpp`](../lib/Scale/scale.cpp#L20-L22), [L148-L169](../lib/Scale/scale.cpp#L148-L169), [L410-L428](../lib/Scale/scale.cpp#L410-L428); [`GM65_scanner.cpp`](../lib/GM65/GM65_scanner.cpp#L7-L11) & [L82-L91](../lib/GM65/GM65_scanner.cpp#L82-L91).
- **Chi tiết:**
  - `parseScaleData()` (chạy trong NimBLE host task, **không phải ISR**) gọi `xTaskGetTickCountFromISR()` ở [L169](../lib/Scale/scale.cpp#L169).
  - `scale_value`, `timestamp`, `stable_weight_logged` bị đọc/ghi đồng thời giữa NimBLE task, `updateValuesService` và `reconnectDevicesService` không có mutex/atomic.
  - `scanner_data = raw_data;` ở [L417](../lib/Scale/scale.cpp#L417) ghi vào `String` **bên ngoài** `is_qr_updated_mutex` trong khi `updateValuesService` đọc bên trong mutex (rủi ro hỏng heap).
  - `qrScannerService` **không bao giờ gọi `scanner.begin()`**; `vTaskDelay(50)` thiếu `pdMS_TO_TICKS`.
  - `xSemaphoreCreateMutex()` được gọi ở khởi tạo biến tĩnh toàn cục (trước `setup()`). Biến `is_scale_updated` và `is_scale_updated_mutex` là code chết (chỉ ghi `true`, không bao giờ đọc).
  - [`GM65_scanner.cpp`](../lib/GM65/GM65_scanner.cpp#L7-L11): `_tx_buf[1]` không được khởi tạo (`0x00`), gửi khung lệnh hỏng nếu đối tượng không nằm trong vùng `.bss`.
  - [`GM65_scanner.cpp`](../lib/GM65/GM65_scanner.cpp#L82-L91): Heuristic `if (b2 >= 0x80)` giải mã sai các ký tự tiếng Việt có byte thấp `< 0x80` như `Đ` (`0x0110`), `đ` (`0x0111`), `Ă` (`0x0102`), `ă` (`0x0103`), `Ĩ/ĩ`, `Ũ/ũ`.
  - [`scale.cpp`](../lib/Scale/scale.cpp#L403): `verifyCitizenID()` chỉ kiểm tra `index_of_vertical_bar[0] == 12`, bỏ sót trường hợp `== 10` như comment mô tả.

#### 11. Script `tools/quantize.py` và `camera/start_smartscale.ps1` hỏng đường dẫn trên máy mới
- **Vị trí:** [`firmware/edge_yolo/tools/quantize.py`](../firmware/edge_yolo/tools/quantize.py#L13-L14), [L32-L35](../firmware/edge_yolo/tools/quantize.py#L32-L35) và [`camera/start_smartscale.ps1`](../camera/start_smartscale.ps1#L20-L29).
- **Chi tiết:**
  - `prepare_sources.ps1` clone `esp-dl` vào `<repo>/.edge-deps/esp-dl`, nhưng `quantize.py` chỉ tìm ở `WORKSPACE_ROOT / ".edge-deps/esp-dl/..."` (cao hơn repo 2 cấp) rồi fallback sang `<repo>/.edge-deps/quantize_yolo26` → **chạy trên laptop mới sẽ báo lỗi `"Run prepare_sources.ps1 first"` dù đã chạy rồi**. Hơn nữa, nếu clone tại `C:\SmartScale`, `ROOT.parents[1]` văng **`IndexError`** ngay lúc khởi chạy!
  - `start_smartscale.ps1` chỉ tìm `.venv` ở thư mục cao hơn repo **2 cấp** (`D:\.venv`) hoặc `.venv-camera`, **không kiểm tra `<repo>\.venv\Scripts\python.exe`** mà [`README.md`](../README.md#L146) hướng dẫn tạo.

---

### 🟡 Mức Trung Bình & Nhỏ (Medium / Low — Edge cases, UX, Cấu hình & Tài liệu lệch code)

| # | Vị trí | Vấn đề |
|---|---|---|
| 12 | [`app_main.cpp`](../firmware/edge_yolo/main/app_main.cpp#L242-L247) | Khi Wi-Fi mất kết nối, frame cũ vẫn có thể nằm lại trong `s_ready_idx` và `s_capture_sem`, khiến frame đầu tiên sau khi nối lại là ảnh cũ trước lúc rớt mạng. |
| 13 | [`edge_io.cpp`](../firmware/edge_yolo/main/edge_io.cpp#L504) | `if (clockMs() < s_lcd_override_until)` bị lỗi tràn số nguyên không dấu sau ~49.7 ngày (nên so sánh hiệu có dấu `int32_t(s_lcd_override_until - clockMs()) > 0`). |
| 14 | [`edge_io.cpp`](../firmware/edge_yolo/main/edge_io.cpp#L518-L524) | Khi trạng thái là `Partial` hoặc `NeedsCalibration`, LCD của `edge_yolo` chỉ hiện thông báo lỗi camera mà **ẩn luôn cân nặng `W:`** (khác với [`lib/Display/display.cpp`](../lib/Display/display.cpp#L127-L136) vẫn luân phiên hiển thị cân nặng). |
| 15 | [`camera/usb_viewer.py`](../camera/usb_viewer.py#L46-L78), [L102-L138](../camera/usb_viewer.py#L102-L138) | (a) Không cho nhập `mac` cân khi `--configure` dù firmware hỗ trợ; (b) Nhập sai URL/mode hoặc rút cáp USB giữa chừng gây crash traceback thay vì xử lý mềm; (c) Hiển thị chuỗi thô `"Height: None cm"`, `"BMI: None"`; (d) Bỏ qua `confidence` khi vẽ bounding box; (e) Nhập password Wi-Fi dạng plaintext (trái ngược mô tả ở `docs/camera_research/README.md`). |
| 16 | [`edge_geometry.h`](../firmware/edge_yolo/main/edge_geometry.h#L10-L16) & [`smartscale_camera.py`](../camera/smartscale_camera.py#L190-L204) | `originalBox()` không bảo vệ chia cho 0 nếu ảnh tỷ lệ cực dị làm `activeW == 0` hoặc `activeH == 0`. Ngưỡng kiểm tra giữa 2 pipeline lệch nhau (`edge_yolo`: `conf=0.5`, biên `2px`, cao ≥ 20% ảnh; laptop: `conf=0.6`, biên `0.5px`, không giới hạn chiều cao tối thiểu). |
| 17 | [`src/main.cpp`](../src/main.cpp#L39) | `TaskLEDControl` chỉ cấp `1024` bytes stack — quá sát ngưỡng tràn stack trên ESP32-S3 (nên tối thiểu `2048`). |
| 18 | [`platformio.ini`](../platformio.ini#L29-L44) & [`boards/yolo_uno.json`](../boards/yolo_uno.json#L40-L42) | `yolo_uno_camera` (RX=8, TX=9) và `yolo_uno_camera_lcd` (RX=9, TX=8) bị đảo ngược chân UART; `yolo_uno.json` khai báo Flash `8MB` trong khi bo thực tế có `16MB Flash + 8MB PSRAM`. |
| 19 | [`dependencies.lock`](../firmware/edge_yolo/dependencies.lock#L27) & [`yolo26n_224_s8_s3.json`](../firmware/edge_yolo/models/yolo26n_224_s8_s3.json) | `dependencies.lock` chứa đường dẫn cứng `D:\PROJECT\.edge-deps\esp-dl\esp-dl` (khi `build.ps1` xóa đi sẽ làm bẩn `git status` trên mọi máy khác); file dump lượng tử hóa `yolo26n_224_s8_s3.json` (710 KB, 19.928 dòng) bị commit thừa vào Git. |
| 20 | [`SETUP_GUIDE_NEW_LAPTOP.md`](./SETUP_GUIDE_NEW_LAPTOP.md#L259-L272), [`firmware/edge_yolo/README.md`](../firmware/edge_yolo/README.md#L35-L86), [`docs/camera_research/README.md`](./camera_research/README.md#L15-L28) | Tài liệu lệch code thực tế: `SETUP_GUIDE` mô tả LCD hiện `SmartScale Ready`, `BMI: 22.1 (Normal)` và có chuông/đèn báo BLE (thực tế firmware không có); `edge_yolo/README.md` ghi ESP-IDF 6.1 và dẫn script không tồn tại `tools/fetch_quant_sources.py`; `docs/camera_research/README.md` có các link cũ đã hỏng (`smartscale/...`, `releases/...`). |

---

## 3. Các Điểm Cần Tối Ưu (Theo Tác Động Thực Tế)

### ⚡ 1. Tối ưu tốc độ suy luận & Pipeline Edge AI trên ESP32-S3 (Tác động lớn nhất)
1. **Bật chạy song song Dual-Core thực sự (`CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1=y`):** Tách hẳn `YOLO26n` sang Core 1 trong khi Wi-Fi/lwIP/BLE/HTTP capture chạy trên Core 0. Thời gian vòng lặp tổng thể sẽ tiệm cận `max(AI_ms, Net_ms)` thay vì `AI_ms + Net_ms`.
2. **Tận dụng Internal SRAM cho Tensor trung gian của ESP-DL:**
   - Hiện tại [`app_main.cpp`](../firmware/edge_yolo/main/app_main.cpp#L200-L201) truyền `max_internal_size = 0`, đẩy toàn bộ activation tensor ra Octal PSRAM (80 MHz) dù RAM nội còn ~85 KB (và có thể giải phóng thêm nếu bật `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`).
   - Thử nghiệm truyền `max_internal_size` (ví dụ `64*1024`–`96*1024`) kết hợp bật **`CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y`** và **`CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB=y`** trong `sdkconfig.defaults` để tăng tốc các lớp tích chập trên Octal PSRAM (thường cải thiện **15–35%** thời gian suy luận).
3. **Tái sử dụng kết nối HTTP Keep-Alive thay vì tạo mới TCP mỗi frame:**
   - [`capture()`](../firmware/edge_yolo/main/app_main.cpp#L44-L95) đang `esp_http_client_init` → bắt tay TCP 3 bước → `close` → `cleanup` mỗi khung hình, gây trễ mạng, phân mảnh heap và tích tụ socket `TIME_WAIT`. Giữ 1 handle persistent với `keep_alive_enable = true` (chỉ mở lại khi lỗi hoặc đổi URL) sẽ giảm đáng kể `capture_ms`.
4. **Dùng ROM CRC32 & bỏ qua USB framing khi chạy độc lập:**
   - Thay vòng lặp CRC32 từng bit bằng `esp_rom_crc32_le()` (có sẵn trong ROM ESP32-S3, nhanh hơn ~10 lần).
   - Kiểm tra `usb_serial_jtag_is_connected()` ở đầu [`usbFrame()`](../firmware/edge_yolo/main/edge_io.cpp#L280-L301) để không tốn CPU tính CRC32 và không chờ timeout 25 ms trên TX ringbuffer khi chạy bằng nguồn 5V không cắm laptop.
   - Đẩy việc truyền USB (hoặc ít nhất không chặn inference) ra khỏi đường găng nếu cần tăng FPS khi xem trên PC.

### 📏 2. Tối ưu độ chính xác đo chiều cao, BMI & chất lượng model AI
1. **Lọc nhiễu theo thời gian (Temporal Smoothing / Median Filter) & Chống rớt 1 frame (Debounce):**
   - Với đầu vào `224×224`, sai lệch 1 pixel box trên ảnh gốc `320×240` đã làm chiều cao nhảy ~1.5–2.5 cm. Nên dùng **trung vị (median) 3–5 frame hợp lệ liên tiếp** cho `heightCm` và yêu cầu **2–3 frame `None` liên tiếp** trước khi xóa cân nặng.
   - Với tự hiệu chuẩn `fixed`, chỉ lưu `focalPx` vào NVS sau khi nhận ≥ 3 frame `fullBox` liên tiếp có độ biến thiên chiều cao box nhỏ (ví dụ `< 3%`).
2. **Giữ kết quả sau khi bước xuống (`ResultHold`):**
   - Giữ hiển thị `H / W / D / BMI` thêm 10–15 giây sau khi cân gửi cờ `load_removed` để người đo tự bước xuống xem màn hình LCD được (khớp kế hoạch trong `PLAN_1`).
3. **Lượng tử hóa lại model bằng dữ liệu ảnh thật từ Camera OhStem:**
   - Model hiện tại (`yolo26n_224_s8_s3.espdl`) mới chỉ hiệu chuẩn lượng tử bằng **8 ảnh COCO8** (`accuracy_validated: false`) và vẫn giữ đầu ra 80 class trong khi bài toán chỉ cần duy nhất class `0 (person)`. Calib lại bằng 100–300 ảnh người đứng tại phòng lab (320×240 từ OV2640) — hoặc fine-tune 1 class `person` — sẽ cải thiện rõ rệt độ ổn định tọa độ đỉnh đầu & bàn chân.
4. **Lưu ý độ trễ Double-Buffering khi tích hợp Servo Step-Scan:**
   - Hiện `captureWorkerTask` tải sẵn frame `N+1` ngay sau khi gửi frame `N`, khiến frame `N+1` nằm chờ ~550 ms trước khi được suy luận. Ở chế độ cố định điều này tăng FPS nhưng tăng độ trễ; khi tích hợp Servo (theo `PLAN_1`), **bắt buộc tạm dừng/xả buffer `s_ready_idx`** mỗi khi quay servo để tránh suy luận nhầm ảnh ở góc quay cũ.

### 🔋 3. Tối ưu I2C LCD, Công suất & Trải nghiệm vận hành
1. **Cache nội dung LCD trong `edge_io.cpp`:**
   - [`lcdTask()`](../firmware/edge_yolo/main/edge_io.cpp#L498-L549) đang gửi lại toàn bộ 32 ký tự qua I2C mỗi 250 ms ngay cả khi màn hình không đổi. Thêm mảng `prev_line[2]` (như `lib/Display/display.cpp`) sẽ giảm ~99% lưu lượng I2C và tránh chớp màn hình; đồng thời thêm cơ chế tự dò lại LCD nếu cắm muộn sau khi boot.
2. **Chế độ Standby tiết kiệm điện & Điều khiển đèn nền LCD:**
   - Hiện YOLO UNO gọi `/capture` và chạy YOLO26n liên tục 24/7 làm nóng chip và tốn điện; `pcfWriteNibble()` luôn ép đèn nền bật (`0x08`). Cần bổ sung biến điều khiển backlight + chế độ `STANDBY` (chỉ bật AI/Wi-Fi khi cân BLE phát hiện người bước lên, theo đúng định hướng `PLAN_1`).
3. **Tối ưu `camera/usb_viewer.py`:**
   - Khi nhận nhiều gói frame trong 1 lần `feed()`, chỉ cần giải mã JPEG và vẽ **frame mới nhất** thay vì giải mã lần lượt toàn bộ hàng đợi.
   - Bổ sung nhập `mac` cân, format `--` thay cho `None`, vẽ điểm `confidence` lên khung xanh và bắt ngoại lệ `SerialException` khi mất cáp USB.

---

## 4. Đề Xuất Thứ Tự Ưu Tiên Xử Lý Ngay

1. **Ưu tiên 1 (Sửa nhanh, lợi ích tức thì cho `edge_yolo`):**
   - Thêm `CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1=y`, `CONFIG_FREERTOS_HZ=1000` (hoặc `esp_rom_delay_us` cho LCD), `CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y` vào `sdkconfig.defaults`.
   - Sửa `edge_io.cpp`: bỏ `vTaskDelay` trong `wifiEventHandler`, sửa `authmode` cho Wi-Fi Open, bật I2C pull-up + quét đủ địa chỉ + cache dòng LCD, lọc MAC BLE + kiểm tra lỗi `ble_gap_disc()`, dùng `std::atomic` và `esp_rom_crc32_le()` + `usb_serial_jtag_is_connected()`, chặn `NaN/0` trước khi ghi NVS.
2. **Ưu tiên 2 (Ổn định chất lượng đo thực tế):**
   - Thêm debounce `None` + median filter 3–5 frame + cơ chế `ResultHold` trong `measurement_state.h` & `app_main.cpp`.
   - Tối ưu HTTP keep-alive & thử nghiệm `max_internal_size` cho `dl::Model`.
3. **Ưu tiên 3 (Sửa lỗi công cụ & Pipeline 2 / vệ sinh repo):**
   - Sửa đường dẫn trong `tools/quantize.py`, `camera/start_smartscale.ps1`, cải thiện `camera/usb_viewer.py`.
   - Sửa các bug crash/treo ở `lib/BLE/ble.h`, `lib/BLE/ble.cpp`, `lib/Scale/scale.cpp`, `lib/GM65/GM65_scanner.cpp` nếu nhóm vẫn còn duy trì Pipeline 2 (hoặc đánh dấu/dọn bỏ code chết `calib_fsm`).
   - Đồng bộ lại tài liệu và loại `yolo26n_224_s8_s3.json` / đường dẫn cục bộ trong `dependencies.lock` khỏi Git.
