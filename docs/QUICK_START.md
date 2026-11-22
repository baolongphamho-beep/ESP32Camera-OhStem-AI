# Quick start: Camera AI OhStem + YOLO UNO + cân Mi Scale 2

Hướng dẫn từng bước để chạy được hệ thống từ đầu. Muốn biết mỗi file làm gì thì xem `docs/CAU_TRUC_FILE.md`; chi tiết kỹ thuật thì xem `README.md`.

Cập nhật: 18/11/2026. **Plan 2 hiện là mặc định** (`yolo_uno_servo_sweep`); xem [hướng dẫn Plan 2](HUONG_DAN_PLAN2.md). Bản AI chạy trực tiếp trên UNO đã bổ sung mã nguồn nhưng cần ESP-IDF để build: [native README](../firmware/edge_yolo/README.md). Những phần **chưa thử trên phần cứng thật** được ghi rõ trong bài.

---

## 0. Cần chuẩn bị

**Phần cứng**

| Thiết bị | Ghi chú |
| :-- | :-- |
| YOLO UNO (ESP32-S3) + cáp USB-C có truyền dữ liệu | Cáp chỉ sạc thì máy tính không thấy bo |
| Camera AI OhStem + cáp USB-C cấp nguồn + dây Grove 4 chân | Camera là tài sản lab: **không nạp firmware lên camera** |
| Servo SG92R | Chỉ cần cho chế độ đo chiều cao bằng servo |
| LCD 16×2 có module I2C (PCF8574) | |
| Cân Xiaomi Mi Scale 2 có pin | MAC của cân đã điền trong `lib/Config/config.h` |
| Module relay hoặc MOSFET (tùy chọn) | Để YOLO UNO bật/tắt nguồn cân. Chưa thử trên mạch thật |

**Phần mềm trên laptop (Windows)**

- VS Code + extension **PlatformIO IDE** (biên dịch và nạp code cho YOLO UNO).
- Python 3.11 hoặc mới hơn (máy nhóm đã chạy được với 3.14).

---

## 1. Cài đặt (chỉ làm một lần)

**1.1. Lấy code.** Clone repo của nhóm hoặc chép cả folder `ESP32Camera-OhStem-AI-main`, rồi mở file `ESP32Camera-OhStem-AI.code-workspace` trong VS Code.

**1.2. Môi trường Python.** Mở PowerShell tại thư mục gốc repo, rồi chạy:

```bash
py -m venv .venv-camera
```

```bash
.\.venv-camera\Scripts\python.exe -m pip install -r camera\requirements.txt
```

Lệnh thứ hai tải khoảng 1 GB (có PyTorch), mất vài phút. Kiểm tra cài xong:

```bash
.\.venv-camera\Scripts\python.exe -m unittest discover -s tests -p "test_*.py"
```

Thấy `OK` ở cuối là được.

**1.3. PlatformIO.** Khi mở workspace, PlatformIO tự nhận project vì có `platformio.ini`. Lần build đầu nó tự tải trình biên dịch và thư viện, nên mất vài phút.

> Nếu extension báo "Failed to install PlatformIO IDE" (trên máy nhóm từng bị, nghi do Windows Smart App Control), vẫn dùng được bằng dòng lệnh. Ở mọi bước bên dưới, thay "bấm Build/Upload" bằng các lệnh này:
> ```bash
> & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e <tên env> -t upload
> ```
> ```bash
> & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -b 115200
> ```

**Cách nạp một env trong VS Code:** biểu tượng PlatformIO (đầu con kiến) ở thanh trái → **Project Tasks** → chọn env → **Upload**, rồi **Monitor**. Mỗi env là một chương trình; nạp env mới sẽ thay chương trình cũ trên bo.

---

## 2. Nối dây

YOLO UNO đánh số chân theo **GPIO**, khác với số D in trên bo (ví dụ cổng Grove D5/D6 là GPIO8/GPIO9).

| Nối | Vào YOLO UNO | Ghi chú |
| :-- | :-- | :-- |
| Camera, dây Grove | Cổng **D5–D6** (camera TX → GPIO9, camera RX ← GPIO8) | Dùng cho `yolo_uno_camera_lcd` và `yolo_uno_servo_sweep`. Bản servo cũ (`yolo_uno_height_servo*`) mặc định GPIO5/6, xem mục 7 |
| Camera, nguồn | Cáp USB-C riêng | |
| LCD SDA / SCL | **GPIO11 / GPIO12** | VCC theo module (thường 5 V), GND chung |
| Servo, dây cam/vàng | **GPIO10** (D7) | Plan 2 (`yolo_uno_servo_sweep`, `yolo_uno_servo_test`). Bản servo cũ dùng GPIO2 (A1) |
| Servo, dây đỏ / nâu | **5V riêng / GND** | Plan 2 khuyên nguồn 5 V 2 A riêng + tụ 470–1000 µF, **nối chung GND** với UNO |
| Relay, chân IN (tùy chọn) | **GPIO10** (D7) | Chỉ cho `yolo_uno_camera_lcd` (relay cắt dây (+) pin cân, README mục 8). **Không** dùng cùng `yolo_uno_servo_sweep` vì GPIO10 là servo |
| Cân Mi Scale 2 | Không nối dây | Giao tiếp qua Bluetooth |

---

## 3. Kết nối camera với laptop

1. Bật camera. Camera phát một mạng Wi-Fi riêng: nối laptop vào mạng đó, khi ấy camera có địa chỉ **`192.168.4.1`**.
2. Mở trình duyệt vào `http://192.168.4.1`. Thấy trang của camera là laptop đã nói chuyện được với camera.
3. (Tùy chọn) Cho camera vào Wi-Fi của lab qua trang cấu hình Wi-Fi của camera, rồi nối laptop vào cùng mạng đó. Camera sẽ có IP mới (nhóm từng dùng `10.127.7.68`). Ở các lệnh dưới, thay `192.168.4.1` bằng IP này.
4. Kiểm tra lấy ảnh: mở `http://<IP camera>/capture` phải thấy một tấm ảnh.

---

## 4. P5 theo Plan 2: servo quét tìm toàn thân + cân + BMI (hướng chính)

Bước lên cân → hệ thống thức dậy → servo nghiêng camera từng bước từ dưới lên → dừng khi thấy cả đầu và chân → tính chiều cao từ góc servo → chờ cân chốt → BMI → giữ kết quả trên LCD → về chế độ nghỉ.

> **Trạng thái:** logic đã test trên PC (mô phỏng), firmware đã build, script laptop đã chạy với YOLO thật trên webcam. **Chưa chạy với bo + servo + camera thật.** Thông số đang là mặc định.

**Bước 1: Lắp đặt.** Camera trên servo, ống kính cao khoảng **1,0–1,2 m** so với mặt cân, cách tâm cân **2,8–3,2 m** (đứng gần hơn thì người cao không lọt khung, LCD sẽ báo `Step back!`). Đo chính xác độ cao ống kính → `height` (cm).

**Bước 2: Hiệu chuẩn servo** (mỗi lần lắp lại camera)

1. Nạp **`yolo_uno_servo_test`** (servo ở GPIO10), mở Monitor.
2. Gõ góc (ví dụ `90`) tới khi camera **nằm ngang** (thước thủy / app đo góc) → `servo_level`.
3. Gõ một góc nhỏ hơn `servo_level`: camera cúi xuống thì `servo_dir=1`, ngẩng lên thì `servo_dir=-1`.
4. Tìm khoảng góc cần quét (từ lúc thấy chân tới lúc thấy đầu, cộng dư) → `servo_bottom` / `servo_top`. Khoảng càng hẹp quét càng nhanh (0→180 mất ~10 s).

**Bước 3: Nạp firmware.** Nạp **`yolo_uno_servo_sweep`**, mở Monitor. Phải thấy `[BOOT] Plan 2 servo sweep + scale + BMI`, dòng `[CFG] …` và `[SYSTEM] Ready (STANDBY: step on the scale)`. LCD tắt đèn nền, hiện `Standby` / `Step on scale`.

**Bước 4: Nhập thông số** (đóng Monitor trước, vì viewer dùng cùng cổng COM):

```bash
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py --port COM9 --configure
```

Trả lời "n" cho phần lắp đặt, "y" cho phần servo, rồi nhập `servo_level`, `servo_dir`, `servo_bottom`, `servo_top`… (Enter = giữ nguyên). `height` và `focal` sửa bằng `--set`, ví dụ:

```bash
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py --port COM9 --set height=112 --set focal=1.33
```

Viewer in `Da luu cau hinh.` là UNO đã lưu (giữ cả khi rút điện). Ý nghĩa từng khóa: README mục 10.

**Bước 5: Chạy script laptop**

```bash
.\.venv-camera\Scripts\python.exe vision\body_tracker.py --ip 192.168.4.1
```

- Cửa sổ hiện khung người (xanh) và vùng an toàn (viền mép 3%). `q` thoát, `p` tạm dừng gửi.
- Mỗi 10 giây có dòng `[Timing] … set servo_settle >= …`. Nếu lớn hơn `servo_settle` hiện tại (mặc định 400 ms) thì đặt lại: `usb_viewer.py --port COM9 --set servo_settle=<số đó>`.

**Bước 6: Đo**

1. Đứng lên cân, đứng thẳng, nhìn về camera.
2. LCD: `Waking up...` → `Aligning cam...` (servo nhích dần) → `Measuring...` → `H:170.2cm` / `Wait scale lock` → `H:170.2 W:65.3` / `BMI:22.5 D:2.8m`.
3. Monitor in khung `HEIGHT / WEIGHT / BMI` kèm góc servo.
4. Bước xuống: kết quả vẫn hiện thêm `hold_sec` giây (mặc định 12), rồi LCD về `Standby` và tắt đèn nền. Người khác bước lên trong lúc đó thì đo lại ngay.

**Bước 7: Kiểm tra sai số.** Đo 3–5 người đã biết chiều cao (thấp, trung bình, cao). Lệch đều một khoảng: kiểm tra `height`. Lệch tăng theo chiều cao hoặc theo khoảng cách: kiểm tra `focal` và `servo_level`.

> Thử không cần laptop: gõ vào Monitor `BOX_0.40_0.10_0.60_0.90_90` (một người đủ khung) hoặc `NOBODY`, `MULTI`. Lệnh khác: `status`, `config`, `reset`.

---

## 5. P3: Phát hiện người đi bộ trước đầu xe

Laptop chạy YOLO, vẽ bounding box, ước lượng kích thước, khoảng cách, và cảnh báo khi người gần hơn 2,5 m.

1. Nạp env **`yolo_uno_camera_lcd`** cho YOLO UNO (camera cắm Grove D5–D6, LCD SDA11/SCL12).
2. Chạy, thay IP bằng IP camera của bạn:
   ```bash
   .\.venv-camera\Scripts\python.exe pedestrian_distance.py 192.168.4.1
   ```
3. Cửa sổ laptop hiện khung người gần nhất: **xanh** là an toàn, **đỏ** là gần hơn 2,5 m. Bấm `q` để thoát.
4. LCD hiện:

   | Tình huống | Dòng 1 | Dòng 2 |
   | :-- | :-- | :-- |
   | Người gần hơn 2,5 m | `Pedestrian 1.80m` | `!! TOO CLOSE !!` |
   | Người xa hơn | `Pedestrian 3.20m` | `Safe distance` |
   | Không có ai | `No people` | `detected` |

   Monitor in dòng `[Camera] Pedestrian 1.80 m WARNING`. Quá 2 giây không có dữ liệu mới thì LCD quay về màn hình thường.

- Đổi ngưỡng cảnh báo: sửa `SAFE_DISTANCE` ở đầu `pedestrian_distance.py`, không cần nạp lại bo.
- Khoảng cách tính từ chiều cao người trung bình 1,65 m và `FOCAL_LENGTH = 600` px, nên chỉ là ước lượng. Muốn chính xác hơn thì đo `FOCAL_LENGTH` cho đúng camera: đặt người cao H ở khoảng cách d, đo chiều cao box h (pixel), rồi lấy `FOCAL_LENGTH = h × d / H`.
- Phần này đã test trên PC (giải mã nhãn, màn hình LCD) nhưng **chưa thử với camera + LCD thật**.

---

## 6. P5 bản đơn giản: camera đặt cố định (không servo)

1. Nạp env **`yolo_uno_camera_lcd`**.
2. Chạy lần đầu với `-Configure` để nhập thông số lắp đặt:
   ```bash
   .\camera\start_smartscale.ps1 -CameraIP 192.168.4.1 -Configure
   ```
3. Những lần sau bỏ `-Configure`. LCD luân phiên hiện chiều cao/cân nặng và khoảng cách/BMI.

Lưu ý: `camera/settings.json` đang có `"focal_px": 3.6`. Đây là tiêu cự tính bằng mm, không phải pixel. Hãy để trống khi `-Configure` hỏi, nếu không chiều cao sẽ sai. Chi tiết ở `camera/README.md`.

---

## 7. Các chương trình khác

| Muốn | Nạp env | Chạy trên laptop |
| :-- | :-- | :-- |
| Servo bản cũ: dò chân rồi đầu, tự động | `yolo_uno_height_servo_bmi` (có cân + BMI) hoặc `yolo_uno_height_servo` | `.\.venv-camera\Scripts\python.exe pedestrian_detector.py` |
| Servo bản cũ, điều khiển bằng phím W/S/F/H/R | như trên | `.\.venv-camera\Scripts\python.exe manual_height_test.py` |
| Tìm MAC của cân | `yolo_uno_ble_scan` | Bước lên cân, đọc dòng `[BLE] MAC=...` trong Monitor. Env Plan 2 (`yolo_uno_servo_sweep`): nhập MAC qua `usb_viewer.py --configure`. Các env khác: chép vào `SCALE_MAC` trong `lib/Config/config.h` |
| Giao diện web (hoa quả + sức khỏe) | | `py .\integration\app.py`, mở http://127.0.0.1:8765 |

Bản servo cũ mặc định đọc camera ở **GPIO5/6**, không phải cổng Grove D5–D6 (GPIO8/9). Nếu camera cắm D5–D6, sửa `-DHEIGHT_CAMERA_RX_PIN` và `-DHEIGHT_CAMERA_TX_PIN` trong env đó.

---

## 8. Lỗi thường gặp

| Hiện tượng | Kiểm tra |
| :-- | :-- |
| Upload báo không tìm thấy cổng | Cáp USB có truyền dữ liệu chưa. Giữ **BOOT**, bấm **RESET**, thả BOOT rồi Upload lại. Cắm nhiều thiết bị thì thêm `--upload-port COMx` |
| Monitor toàn ký tự lạ | Đặt baud **115200** |
| LCD trống | Xem `lcd=` trong dòng `[ALIVE]`: `not-found` là chưa thấy LCD trên SDA11/SCL12 (kiểm tra dây). `ready` mà vẫn không có chữ thì vặn biến trở tương phản sau LCD |
| LCD hiện `Camera error` / `Check laptop/IP` | Script laptop chưa chạy, sai IP, hoặc dây Grove/UART sai chân. Monitor có dòng `[CAMERA] Ignored: ...` là UNO có nhận nhưng không hiểu bản tin; chép dòng đó để sửa bộ đọc |
| Bước lên cân mà vẫn `Standby` | Cân phải gửi được số ≥ 5 kg qua BLE: pin cân, MAC trong `config.h`. Gõ `status` trong Monitor |
| Không có cân nặng (`W:--`) | Pin cân, MAC trong `config.h`, đứng lên cân để cân thức dậy. `scale=` trong `[ALIVE]` cho biết trạng thái BLE |
| LCD báo `Step back!` mãi | Đứng xa camera hơn (2,8–3,2 m), hoặc hạ thấp/đưa camera ra xa |
| LCD báo `No full body` | Người không nằm trong khoảng quét: nới `servo_bottom`/`servo_top`, kiểm tra `servo_dir` |
| Servo quét lố, chiều cao lệch | Tăng `servo_settle` theo dòng `[Timing]` (mục 4, bước 5) |
| Servo kêu hoặc rung ở đầu hành trình | Thu hẹp `servo_bottom` / `servo_top` |
| Bo bị reset khi servo quay | Cấp nguồn servo riêng, nối chung GND (mục 2) |
| Viewer báo `Cau hinh bi tu choi` | Một giá trị ngoài khoảng (Monitor in `[CFG] Rejected (...)`); không có gì bị thay đổi |
| Hay báo `MULTI` sai | Tăng `--multi-ratio`. Có người ở xa phía sau thì đặt `--min-area-px` |
| Script không lấy được ảnh | Mở `http://<IP>/capture` bằng trình duyệt; laptop và camera phải cùng mạng |

---

## 9. Lưu ý

- **Không nạp firmware lên camera OhStem.** Mọi chương trình ở đây chỉ nạp vào YOLO UNO. Nếu lỡ nạp, khôi phục từ `backup_original_camera.bin` bằng esptool (hỏi người phụ trách lab trước).
- Chiều cao, khoảng cách và BMI là ước lượng cho đồ án, cần đối chiếu với thước thật. Đây không phải thiết bị y tế.
- Không đưa lên GitHub: `.venv-camera/`, `.pio/`, `camera/settings.json`, `camera_secrets.txt`, `.edge-deps/`.
