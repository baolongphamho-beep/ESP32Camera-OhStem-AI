# Hướng dẫn lắp đặt – hiệu chuẩn – vận hành (Plan 2)

**Cập nhật sau gộp GitHub 18/11/2026:** có hai cách chạy dùng chung logic Plan 2:

**Theo hai dấu ✓ trong ảnh workflow:** xem [Reset/kết nối Wi-Fi camera → configure/thông số cấu hình](WORKFLOW_WIFI_CONFIGURE.md). Đây là hướng dẫn cho **native đã nạp COM5**; bảng cấu hình ở mục 4 bên dưới mô tả bản Arduino dùng laptop chạy YOLO.

| Cách chạy | AI chạy ở đâu | Công cụ build | Chương trình laptop |
|---|---|---|---|
| `yolo_uno_servo_sweep` — mặc định, đã build | Laptop | PlatformIO | `vision/body_tracker.py` + USB viewer tùy chọn |
| `firmware/edge_yolo` — bản native vừa tích hợp | YOLO UNO | ESP-IDF 6.1 | Chỉ `camera/usb_viewer.py` |

Phần hướng dẫn bên dưới mô tả cách PlatformIO đã có. Để chạy native, xem [README native](../firmware/edge_yolo/README.md): camera cấp JPEG trực tiếp qua Wi-Fi, không cần UART kết quả từ laptop. Chọn **servo** khi `--configure`; cấu hình `height`, `focal`, `servo_*`, `hold_sec`, `scaleMac` cho cả hai bản. NVS của Arduino (`sweep_cfg`) và native (`edge_cfg`) riêng nhau: cấu hình lại sau khi đổi firmware.

Native **đã full build bằng ESP-IDF 6.1 và nạp vào UNO COM5**, nhận 20 gói STANDBY hợp lệ trong 20 s. [Biên bản nạp](reports/NATIVE_BUILD_FLASH_20261118.md). Chưa kiểm thử đo người thật hoặc toàn bộ camera/cân/servo. Cân native phải quảng bá `0x181D`; xác minh bằng env BLE scanner trước khi chuyển. GPIO10 dành cho servo, không gắn relay nguồn cân vào chân này.

Tài liệu cho **thành viên mới trong nhóm**. Đọc từ đầu đến cuối là tự lắp và chạy được hệ thống servo quét toàn thân + cân Mi Scale 2 + BMI (env `yolo_uno_servo_sweep`). **Không cần sửa code**: mọi thông số đều cấu hình qua USB bằng `usb_viewer.py`.

Thời gian dự kiến: lắp đặt 30 phút, hiệu chuẩn 20 phút, kiểm thử theo [QA_CHECKLIST.md](QA_CHECKLIST.md).

> **Quy tắc của lab:** chỉ nạp firmware vào **YOLO UNO**. **Không nạp gì vào camera OhStem** (tài sản của lab).

---

## 1. Hệ thống gồm những gì

```
 Mi Scale 2 ──BLE──► YOLO UNO (ESP32-S3) ──PWM GPIO10──► servo ──► camera OhStem
                         ▲      │                                     │ Wi-Fi
              UART GPIO9/8│      └──I2C GPIO11/12──► LCD 16×2           ▼
                         └──────── camera ◄── /control ── laptop (YOLO: vision/body_tracker.py)
```

1. Người bước lên cân. Cân gửi số cân qua BLE, UNO thức dậy, LCD sáng.
2. Servo đưa camera về góc dưới (`servo_bottom`), rồi nhích dần lên từng bước `servo_step`.
3. Laptop lấy ảnh từ camera, chạy YOLO tìm người, rồi gửi `BOX_…` về UNO qua UART của camera.
4. Khi người nằm trọn trong khung, servo dừng. Chiều cao được tính từ **góc servo** và **khung người**, lấy trung vị 3 khung hình.
5. Cân chốt số, UNO tính BMI. LCD hiện `H:… W:…` / `BMI:… D:…m`, giữ thêm `hold_sec` giây sau khi bước xuống, rồi về nghỉ.

| File chính | Vai trò |
|---|---|
| [src/app_main.cpp](../src/app_main.cpp) | Chương trình chính trên UNO |
| [lib/EdgeSweep/](../lib/EdgeSweep/) | `edge_config.h` (thông số), `edge_geometry.h` (hình học), `edge_fsm.h` (máy trạng thái), `servo_ctrl.*` (servo), `edge_io.*` (LCD) |
| [vision/body_tracker.py](../vision/body_tracker.py) | Laptop chạy YOLO tìm người |
| [camera/usb_viewer.py](../camera/usb_viewer.py) | Cấu hình UNO qua USB và xem trạng thái |

---

## 2. Chuẩn bị

**Phần cứng**
- YOLO UNO, cáp USB-C có truyền dữ liệu.
- Camera OhStem (chạy firmware gốc của lab) và nguồn cho camera.
- Servo. Plan 2 khuyên dùng servo **bánh răng kim loại MG90S hoặc MG996R**. SG92R nhựa vẫn chạy được nhưng dễ bị dây Type-C của camera kéo lệch.
- **Nguồn 5 V riêng ≥ 2 A** cho servo, tụ **470–1000 µF**, dây nối.
- LCD 16×2 có module I2C. Cân Mi Scale 2 có pin.
- Thước dây hoặc thước cuộn ≥ 3 m.

**Phần mềm trên laptop** (cài một lần)
```powershell
# PlatformIO: cài extension "PlatformIO IDE" trong VS Code
py -3 -m venv .venv-camera
.\.venv-camera\Scripts\pip install -r camera\requirements.txt -r camera\requirements-usb.txt
.\.venv-camera\Scripts\pip install ultralytics
```
File model `yolo26n.pt` để ở thư mục gốc project. File này không có trong git, xin bạn trong nhóm.

> Trên máy này lệnh `python` là lối tắt của Microsoft Store, không chạy được. Dùng `.\.venv-camera\Scripts\python.exe` hoặc `py -3`.

---

## 3. Lắp đặt

### 3.1 Đấu dây

| Từ | Đến | Ghi chú |
|---|---|---|
| Servo dây tín hiệu (cam/vàng) | **GPIO10 (D7)** | Chân đã chốt trong Interface Contract |
| Servo dây đỏ | **+5 V nguồn riêng** | **Không lấy 5 V từ UNO**, dễ bị sụt áp và tự khởi động lại (brownout) |
| Servo dây nâu/đen | GND nguồn riêng **và** GND của UNO | **Bắt buộc nối chung GND** |
| Tụ 470–1000 µF | Hai cực nguồn 5 V của servo, sát servo | Chân dài (+) nối 5 V |
| Camera TX | **GPIO9** (RX của UNO) | UART 115200 |
| Camera RX | **GPIO8** (TX của UNO) | |
| LCD SDA / SCL | **GPIO11 / GPIO12** | Địa chỉ I2C firmware tự dò |
| LCD VCC / GND | 5 V / GND | |

Không dùng các chân GPIO19/20 (USB), 26–37 (flash/PSRAM), 43/44 (UART0). Nếu nhập các chân này khi cấu hình, `usb_viewer.py` sẽ từ chối.

### 3.2 Đặt camera
- Gắn camera lên trục servo. Servo quay **gật lên/xuống** (pitch), không quay ngang. Để dây Type-C chùng để dây không kéo lệch góc.
- **Chiều cao:** trục servo / tâm ống kính cao hơn **mặt trên của cân** khoảng **1,0–1,2 m**. Ghi lại số đo này (cm), đây là thông số `height`.
- **Khoảng cách:** camera cách **tâm bàn cân** khoảng **2,8–3,2 m**. Nếu gần hơn 2,2 m, không góc servo nào thấy được cả đầu lẫn chân. Người 190 cm cần ≥ 2,8 m.
- Camera nhìn thẳng vào cân. Phía sau người đứng nên là tường trơn, tránh có người khác đi qua.

### 3.3 Nạp firmware vào UNO
```powershell
pio run -e yolo_uno_servo_sweep -t upload
pio device monitor -e yolo_uno_servo_sweep
```
Lúc khởi động phải thấy các dòng `[BOOT] Plan 2 servo sweep + scale + BMI`, `[CFG] servo_pin=10 …` và `[SYSTEM] Ready`. LCD hiện `Standby` / `Step on scale`, đèn nền tắt.

Lệnh `pio` nằm ở `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`. Có thể dùng nút ✓ / → trên thanh PlatformIO của VS Code thay cho lệnh.

---

## 4. Cấu hình (không cần sửa code)

Đóng Serial Monitor trước (vì một cổng COM chỉ một chương trình mở được), rồi chạy:
```powershell
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py --configure
```
(Số COM xem trong Device Manager hoặc trong thanh trạng thái của PlatformIO.) Trả lời từng câu hỏi như bảng dưới:

| Câu hỏi | Trả lời cho Plan 2 |
|---|---|
| WiFi SSID / URL ảnh camera | Enter (env này không dùng) |
| MAC cân Mi Scale 2 | MAC của cân nhóm, ví dụ `70:87:9e:ea:3a:e0` (cách tìm ở mục 5.1). Khi MAC đổi, UNO **tự khởi động lại** để quét lại |
| Nhập thông số lắp đặt? | Enter (= có) |
| Chế độ fixed / ground / servo | **`servo`** |
| Trục servo cao hơn mặt cân (cm) | Số đo ở mục 3.2 |
| fy/H | `1.33` nếu chưa hiệu chuẩn (cách làm ở mục 5.4) |
| Nhập thông số servo? | `y` |
| `servo_pin` … `hold_sec` | Theo bảng ở mục 4.1. Enter = giữ nguyên giá trị đang lưu |

Viewer gửi lệnh `BEGIN / CFG … / APPLY`. UNO kiểm tra **toàn bộ** giá trị: chỉ cần một giá trị sai là UNO không đổi gì cả. Nếu tất cả hợp lệ, UNO lưu vào NVS (rút điện hay nạp lại firmware vẫn còn) và in `[CFG] Applied and saved`.

Đổi nhanh một giá trị mà không phải trả lời hết các câu hỏi:
```powershell
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py --set servo_level=92 --set hold_sec=12
```

### 4.1 Ý nghĩa từng thông số

| Khóa | Mặc định | Khoảng | Ý nghĩa |
|---|---|---|---|
| `servo_pin` | 10 | 0–48 | Chân tín hiệu servo |
| `servo_bottom` | 45 | 45–135 | Góc lệnh lúc **bắt đầu quét** (camera nhìn thấp nhất) |
| `servo_top` | 135 | 45–135 | Góc lệnh lúc **kết thúc quét**. Thu hẹp khoảng bottom–top thì quét nhanh hơn |
| `servo_step` | 5 | 0,5–20 | Mỗi lần nhích bao nhiêu độ (TV3: 5; FSM tự chia đôi nếu nhảy qua vùng vừa khung) |
| `servo_level` | 90 | 45–135 | Góc lệnh khi camera **nằm ngang** (hiệu chuẩn ở mục 5.2) |
| `servo_dir` | −1 | 1 / −1 | Quy ước TV3 (22/11): `−1` nếu tăng góc lệnh làm camera **ngẩng lên**, `1` nếu làm camera **cúi xuống** |
| `servo_settle` | 200 | 0–3000 ms | Chờ bao lâu sau khi servo **dừng hẳn** (mục 5.5) |
| `servo_delay` | 500 | 0–3000 ms | Ảnh camera trả về cũ khoảng bấy nhiêu ms (TV3 ước lượng) |
| `servo_dps` | 4 | 1–60 °/s | Tốc độ quét liên tục của driver native (FSM bước không dùng) |
| `hold_sec` | 12 | 0–120 s | Giữ kết quả trên LCD sau khi bước xuống (Plan 2: 10–15 s) |
| `height` | 110 | 1–400 cm | Trục servo cao hơn mặt cân |
| `focal` | 1,33 | 0,01–20 | fy/H của camera (mục 5.4) |
| `mac` | `SCALE_MAC` | `AA:BB:CC:DD:EE:FF` | MAC của cân |

Gõ `config` trong Serial Monitor để xem các giá trị đang dùng. Gõ `config defaults` để quay về mặc định (MAC trở về mặc định sau lần khởi động lại tiếp theo).

---

## 5. Hiệu chuẩn (làm một lần mỗi khi dời vị trí)

### 5.1 Tìm MAC của cân
1. `pio run -e yolo_uno_ble_scan -t upload`, rồi mở Monitor.
2. Bước lên cân, chép MAC trong dòng `[BLE] MAC=…`.
3. Nạp lại `yolo_uno_servo_sweep` và nhập MAC qua `--configure` (mục 4).

### 5.2 Tìm `servo_level` và `servo_dir`
**Hành trình servo giới hạn cứng 45°..135°** (22/11). **Firmware native (22/11):** dùng `py -3 tools\servo_jog.py` (lệnh `level`, `up`/`down`, `bottom`, `top`, `save`), xem [workflow](WORKFLOW_WIFI_CONFIGURE.md). Các bước dưới đây dành cho bản Arduino.
1. `pio run -e yolo_uno_servo_test -t upload` (servo trên GPIO10), mở Monitor (line ending "Newline").
2. Gõ một số góc, ví dụ `90`, `95`, `85`. Đặt điện thoại có app đo độ nghiêng (level/bubble) lên lưng camera, tìm góc mà camera **nằm ngang**. Đó là `servo_level`.
3. Gõ `servo_level + 20`: camera **ngẩng lên** thì `servo_dir = −1`, cúi xuống thì `servo_dir = 1` (quy ước TV3 từ 22/11, ngược với bản trước).
4. Tìm hai góc giới hạn mà dây chưa căng và gá chưa chạm: góc nhìn thấp nhất (thấy được chân trên cân) là `servo_bottom`, góc nhìn cao nhất là `servo_top`. Ví dụ `60` và `130`.
5. Gõ `detach`, nạp lại `yolo_uno_servo_sweep`, rồi gửi các giá trị bằng `--set` hoặc `--configure`. Giá trị cũ trong NVS vẫn còn sau khi nạp lại.

### 5.3 Đo `height`
Đo bằng thước từ **mặt trên của cân** (không phải sàn) đến **trục servo / tâm ống kính**. Sai 1 cm ở `height` thì chiều cao tính ra sai gần 1 cm.

### 5.4 Hiệu chuẩn `focal` (fy/H)
Cách nhanh, dùng một người đã biết chiều cao thật `H` (cm) đứng trên cân, cách camera `D` (m):
1. Đặt camera nằm ngang: chạy `servo_test`, gõ góc `servo_level`.
2. Trên laptop chạy:
   ```powershell
   .\.venv-camera\Scripts\python.exe vision\body_tracker.py --ip <IP camera> --dry-run
   ```
   Chép một dòng `[Body] BOX_x1_y1_x2_y2_conf` khi khung trùm **trọn** người (cả đỉnh đầu lẫn chân). y1, y2 là mép trên và mép dưới của khung, tính theo tỉ lệ 0–1 của chiều cao ảnh.
3. Tính `focal = (y2 − y1) × D × 100 / H`. Ví dụ `y1 = 0.30`, `y2 = 0.85`, `D = 3.0`, `H = 170` cho `focal ≈ 0,97`.
4. Đo thêm 2–3 lần rồi lấy trung bình, gửi bằng `--set focal=…`.

Nếu ở góc nằm ngang không thấy trọn người, cho người đứng xa hơn hoặc ngồi xuống (miễn là biết chiều cao của phần cơ thể nằm trong khung).

**Kiểm chứng:** đo 3–5 người có chiều cao khác nhau rồi so với thước dây (ca F01–F14 trong [QA_FIELD_LOG.csv](QA_FIELD_LOG.csv)):
- Nếu tất cả bị **lệch đều** một số cm thì kiểm tra lại `height` và `servo_level`.
- Nếu sai lệch **tăng theo chiều cao** thì chỉnh `focal`.
- TV4 chốt bảng sai số trước và sau hiệu chuẩn.

### 5.5 Chọn `servo_settle`
Chạy `body_tracker.py` khoảng 30 giây. Cứ 10 giây laptop in một dòng `[Timing] 90% of frames: capture … + detect … + control … = … ms -> set servo_settle >= N`. Gửi số N đó bằng `--set servo_settle=N`. Nếu đặt nhỏ quá, servo dừng sai góc vì dùng phải ảnh cũ; lớn quá thì quét chậm.

---

## 6. Vận hành hằng ngày

1. Cắm nguồn servo, sau đó cắm UNO (USB), bật camera.
2. Laptop và camera vào cùng mạng Wi-Fi. Chạy:
   ```powershell
   .\.venv-camera\Scripts\python.exe vision\body_tracker.py --ip <IP camera>
   ```
3. (Tùy chọn) xem trạng thái: `usb_viewer.py`. Viewer hiện phase, góc servo, tilt, framing và 2 dòng LCD.
4. Người dùng bước lên cân, **đứng yên, nhìn về phía camera**, chờ LCD hiện kết quả.

### Màn hình LCD
| LCD | Nghĩa | Người dùng làm gì |
|---|---|---|
| (tối) `Standby` / `Step on scale` | Đang nghỉ | Bước lên cân |
| `Waking up...` / `Please wait` | Servo về góc dưới | Đứng yên |
| `Aligning cam...` / `Stand still` | Servo đang quét | Đứng yên |
| `Measuring...` / `H:170.2cm` | Đang đo, chờ cân chốt số | Đứng yên |
| `H:170.2 W:65.3` / `BMI:22.5 D:2.9m` | Kết quả | Đọc kết quả, rồi bước xuống |
| `Step back!` / `Too close to cam` | Đứng quá gần | Lùi ra xa camera |
| `One at a time` / `2+ people seen` | Có hơn 1 người trong khung | Người thứ hai ra khỏi khung |
| `No full body` / `Stand straight` | Quét hết mà không thấy trọn người | Đứng thẳng, vào giữa khung |
| `Camera error` / `Check laptop/IP` | Không nhận được ảnh / bản tin | Kiểm tra `body_tracker.py`, IP, dây UART |

### Lệnh trong Serial Monitor
| Lệnh | Tác dụng |
|---|---|
| `status` | Phase, góc servo, tilt, framing, lần cuối nhận tin từ camera |
| `config` / `config defaults` | Xem cấu hình / quay về mặc định |
| `reset` | Về STANDBY ngay |
| `BOX_0.40_0.10_0.60_0.95_90`, `NOBODY`, `MULTI` | Giả lập bản tin từ laptop để thử servo khi không có camera |

---

## 7. Kiểm thử và báo lỗi
- Trước khi lên phần cứng: `powershell -ExecutionPolicy Bypass -File tools\run_all_tests.ps1` (phải ra 0 FAIL).
- Kiểm thử trên phần cứng: [QA_CHECKLIST.md](QA_CHECKLIST.md) (6 ca QA + 24 ca thực tế). Ghi log rồi chạy `tools\qa_log_check.py`.
- Bảng lỗi thường gặp: mục 5 của QA_CHECKLIST.

## 8. Giới hạn hiện tại (cần nói rõ khi báo cáo)
- AI chạy trên **laptop**, chưa chạy trên UNO, vì thư mục `firmware/edge_yolo/main/` không có trên máy.
- Chế độ nghỉ chưa tiết kiệm điện thật. UNO vẫn quét BLE liên tục; phần `setPowerMode`, `bleSetLowPowerScan` thuộc TV2.
- Env này **không có relay cắt nguồn cân**: GPIO10 dành cho servo, và cân phải còn điện mới đánh thức được UNO. Yêu cầu của đề "UNO bật/tắt nguồn cân" hiện chỉ có trong env `yolo_uno_camera_lcd`.
- **Chưa chạy trên phần cứng thật:** mới test trên PC (mô phỏng) và build.
