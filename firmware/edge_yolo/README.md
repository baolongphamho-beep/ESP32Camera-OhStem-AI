# SmartScale chạy YOLO26n trên YOLO UNO

Firmware ESP-IDF riêng cho ESP32-S3 có **Flash 16 MB, PSRAM octal 8 MB**, đúng kết quả probe của bo hiện tại. AI, tính chiều cao và BMI đều chạy trên UNO. Camera OhStem cung cấp JPEG qua Wi-Fi; LCD nối I2C trực tiếp vào UNO. Laptop chỉ cấu hình và xem ảnh qua Type-C.

**Đã kiểm tra trên bo COM9 ngày07/11/2026:** nạp thành công và xác minh hash; chạy suy luận với tensor0 mất711ms, còn4,876,212byte PSRAM và84,959byte internal RAM trước khi cấp JPEG buffer. Kết quả được bo gửi qua USB (`build/device_probe.json`). Chưa nhập Wi-Fi nên chưa kiểm tra ảnh người thật, bản tin cân và LCD trong firmware native này. Tốc độ711ms là self-test, không phải tốc độ toàn bộ pipeline ảnh.

Bản sao lưu Flash16MB trước khi nạp ở `firmware/backups/yolo_uno_before_edge_20261107.bin` tính từ gốc repo, được bỏ qua bởi Git. Không công khai bản sao lưu vì có thể chứa cấu hình cũ.

```text
OhStem /capture -- Wi-Fi JPEG --> YOLO UNO -- I2C --> LCD 16x2
Mi Scale 2 -------- BLE ------->     |
                                    +-- USB JPEG + kết quả --> laptop (tùy chọn)
```

Không chạy `camera/smartscale_camera.py` hoặc launcher cũ trong chế độ này. Không cần UART kết quả phân loại Face/No Face. Hai bo vẫn cần đường truyền ảnh Wi-Fi và nguồn điện riêng/phù hợp.

## Quy trình Setup chuẩn từ đầu (Đấu nối, Camera, Nạp Firmware & Chạy)

1. **Đấu nối mạch theo Netlist:**
   - **LCD 16×2 I2C** cắm vào cổng I2C đỏ trên YOLO UNO: **`SDA = GPIO 11`**, **`SCL = GPIO 12`**, `VCC = 5V`, `GND = GND` (Firmware tự dò địa chỉ PCF8574 tại `0x20..0x27` và `0x38..0x3F`).
   - **Camera AI OhStem**: cấp nguồn 5V qua cổng Type-C.
   - **Cân Mi Scale 2**: lắp pin, phát quảng bá BLE service `0x181D` (MAC `70:87:9e:ea:3a:e0`).
   - **YOLO UNO**: cắm cáp USB Type-C (có truyền dữ liệu) vào máy tính.
2. **Vào chế độ `Stream video` trên Camera AI & Setup lại Wi-Fi cho Camera:**
   - Bấm nút trên Camera AI mở menu màn hình OLED sau lưng camera, chọn mục **`6. Stream video`** (bắt buộc để mở endpoint `/capture`).
   - Nếu camera chưa nối Wi-Fi (hoặc đổi Wi-Fi mới): kết nối điện thoại/laptop vào Wi-Fi do camera phát ra (`ohstemcam_...`), mở trình duyệt vào **`http://192.168.4.1`**, chọn mạng Wi-Fi 2.4 GHz và nhập mật khẩu.
   - Khi kết nối thành công, màn hình OLED của camera sẽ hiện **địa chỉ IP** (ví dụ `10.127.7.68`). Đóng mọi tab trình duyệt đang mở trang camera để tránh lỗi `{"busy":1}`.
3. **(Song song) Biên dịch và nạp Firmware xuống YOLO UNO:**
   - Xem cổng COM của YOLO UNO trong Device Manager (ví dụ `COM13` hoặc `COM9`), sau đó chạy lệnh bên dưới.
4. **Chạy chương trình kèm `--configure` rồi vận hành:**
   - Chạy `python camera/usb_viewer.py --port COM13 --configure`, nhập SSID/Password Wi-Fi và IP camera rồi bắt đầu đo. Lần sau chỉ cần chạy `python camera/usb_viewer.py --port COM13`.

## Biên dịch và nạp

Đã chuẩn bị model `models/yolo26n_224_s8_s3.espdl` (~3.08 MB), ESP-DL pinned revision `512ed0b0dc5f5320f3d9ca5a5aea189803f87f31`, cùng môi trường ESP-IDF 6.1 trên máy này.

Từ thư mục gốc project:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -Flash -Port COM9
```

Lệnh thứ hai thay chương trình trên UNO bằng firmware này, có ghi bootloader/bảng phân vùng mới. Script không tự xóa NVS. Firmware Arduino/PlatformIO vẫn nằm nguyên trong project và có thể nạp lại bằng environment cũ.

Các file sau biên dịch ở `build/`: `smartscale_edge_yolo.bin`, `bootloader/bootloader.bin`, `partition_table/partition-table.bin`. Không nạp riêng app `.bin` vào địa chỉ 0; dùng script để đúng địa chỉ. File model đã được nhúng vào app.

`.edge-deps`, môi trường Python và các model sinh ra không đưa vào Git. Khi clone trên máy mới cần chuẩn bị lại dependencies/model; xem phần tái tạo bên dưới.

## Nhập cấu hình và xem ảnh

### Kiểm tra riêng cân BLE và LCD (không cần camera)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -ScaleOnly -Flash -Port COM11
```

Thay COM11 bằng cổng thực tế. Bản chỉ cân có Smart Standby:

- Lúc khởi động/nghỉ: ghi `Standby / Step on scale`, tắt đèn nền LCD, quét BLE thụ động 60 ms mỗi 600 ms (10%).
- Gói từ đúng `scaleMac`, cân nặng hợp lệ **trên 5 kg**: chuyển `ACTIVE`, quét 100 ms mỗi 100 ms (100%), bật đèn nền. Không cần chờ cờ ổn định. LCD hiện `W:xx.xx kg / Measuring...` hoặc `Stable: LOCK`.
- Bước xuống/gói về 0, trọng lượng còn không quá 5 kg, hoặc dữ liệu hết hạn 30 giây: trở về `STANDBY`. Bản thử này chưa giữ kết quả sau khi bước xuống.
- USB gửi gói SSV1 `kind=scale` kèm `phase`, `ble_duty_percent` (0 nếu chưa bắt đầu quét thành công), `weight_kg`, `weight_stable` mỗi giây và khi đổi trạng thái.

Quản lý nguồn đã bật DFS và Automatic Light Sleep (Tickless Idle). `ACTIVE` đặt dải CPU 80–240 MHz, giữ khóa CPU 240 MHz và khóa ngăn ngủ; `STANDBY` nhả hai khóa đó, giới hạn CPU ở 80 MHz và cho phép ngủ khi không còn khóa driver. Giới hạn tần số lúc nghỉ tránh việc các task/telemetry nâng CPU trở lại 240 MHz qua khóa RTOS. BLE dùng Modem Sleep và giữ main XTAL để duy trì lịch quét trong Light Sleep. Không vào Deep Sleep. Chưa có số liệu dòng tiêu thụ; duty BLE không phải mức giảm điện năng toàn bo. Chế độ chỉ cân không tải AI, không lấy ảnh hay tính chiều cao/BMI.

Wi-Fi dùng `WIFI_PS_MAX_MODEM` khi nghỉ và `WIFI_PS_NONE` khi hoạt động; STA listen interval là 3. Nếu chưa cấu hình SSID thì Wi-Fi chưa start: đọc lại được chế độ PS chỉ chứng minh cấu hình, chưa kiểm chứng tiết kiệm điện hoặc tốc độ lấy ảnh qua Wi-Fi thật.

USB máy tính có SOF sẽ giữ khóa `edge_usb` ngăn Light Sleep để bảo toàn cấu hình/viewer. Khóa này tự nhả khi dùng nguồn USB không có host (sạc/pin dự phòng), khi đó các khóa driver khác vẫn có quyền ngăn ngủ. Không bỏ khóa của driver để ép ngủ. Khi chưa có nguồn độc lập, chỉ kiểm chứng DFS/chuyển trạng thái và cấu hình PS; chưa kết luận chip thực sự vào Light Sleep hoặc tiết kiệm bao nhiêu mA.

Gói `kind=scale` có thêm `pm_enabled`, `cpu_mhz` (tần số tại thời điểm lấy mẫu), `cpu_min_mhz`, `cpu_max_mhz`, `light_sleep_enabled` (đã cấu hình), `usb_sleep_blocked`, `no_ls_locks` (tổng số khóa ngăn ngủ đang giữ), `wifi_ps` và `wifi_started`. `light_sleep_enabled=true` không có nghĩa chip đã ngủ. Khi cắm máy tính, dự kiến `usb_sleep_blocked=true` và `no_ls_locks>=1`.

Đóng Monitor/viewer trước khi chạy công cụ chỉ đọc sau (Python cần `pyserial`, không cần OpenCV):

```powershell
python camera/power_monitor.py --port COM11 --seconds 30
```

Để trống cân → bước lên → bước xuống; kiểm tra CPU 80 → 240 → 80 MHz và Wi-Fi PS `MAX_MODEM → NONE → MAX_MODEM`. Công cụ chỉ in khi trạng thái nguồn thay đổi; Ctrl+C đóng monitor, không dừng firmware. Muốn kiểm chứng dòng tiêu thụ/Light Sleep độc lập, cần nguồn ngoài USB host và dụng cụ đo; chưa có thì ghi rõ phần này chưa kiểm chứng.

`sdkconfig.defaults` bật PM/Tickless/BLE modem sleep; `build.ps1` cũng chuyển đúng các key PM cần thiết trong `sdkconfig` cũ sang `y` trước khi build, giữ nguyên các cấu hình khác. Có thể phải biên dịch lại toàn bộ ESP-IDF sau thay đổi PM.

Kiểm tra tại chỗ: để trống cân xem đèn nền tắt, bước lên xem LCD sáng và chốt đúng kg, bước xuống xem đèn nền tắt lại; lặp ít nhất 10 lượt. Nếu bỏ lỡ gói bước xuống, chờ tối đa 30 giây cho timeout. Khi nhận `ACTIVE`, số `ble_duty_percent` phải là 100; khi nghỉ là 10. Chưa kết luận đạt mục tiêu thức dậy dưới 1 giây nếu chưa đo.

TV1 có thể gọi `setPowerMode(bool active)` từ một task ứng dụng khi tích hợp FSM. Hàm điều khiển BLE scan, LCD backlight, khóa CPU/ngủ và Wi-Fi PS; gọi lặp lại cùng trạng thái không tăng bộ đếm khóa. `bleSetLowPowerScan(bool standby)` và `lcdSetBacklight(bool on)` cũng dùng riêng được; không gọi từ ISR/callback BLE. Không có `-ScaleOnly` thì chạy camera + cân với điều phối Standby; thêm `-CameraContinuous` để kiểm chuẩn pipeline liên tục. Xem [hướng dẫn ghép và kiểm thử](../../docs/CAMERA_POWER_INTEGRATION.md). Bản có camera quét BLE ACTIVE 12.5% để dành radio cho Wi-Fi, không tắt hoàn toàn BLE trong HTTP; bản chỉ cân vẫn ACTIVE 100% như mô tả ở trên.

### Thử độ trễ đánh thức

Thử chẩn đoán độ trễ đánh thức: giữ LCD tắt lúc nghỉ nhưng quét BLE liên tục:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File firmware/edge_yolo/build.ps1 -ScaleOnly -StandbyScanPercent 100 -Flash -Port COM11
```

`-StandbyScanPercent` nhận 10, 25, 50 hoặc 100; mặc định 10. Chu kỳ Standby vẫn là 600 ms, chỉ thay cửa sổ quét. Gói USB phải báo `phase=STANDBY` và `ble_duty_percent=100` cho phép thử này; đèn nền vẫn tắt. So sánh ít nhất 5 lượt đứng hẳn lên cân (>5 kg), không chờ LOCK mới tính thời gian LCD sáng. Nếu nhanh hơn ở 100%, thử 25/50% để cân bằng; nếu vẫn trễ, kiểm tra thời điểm cân phát gói trọng lượng. 100% chỉ là cấu hình chẩn đoán, chưa phải phương án tiết kiệm điện cuối cùng. Muốn trở lại 10%, build/nạp lại với `-StandbyScanPercent 10`.

### Bản tích hợp camera

Đã cài dependencies USB viewer trong `.venv-camera` trên máy này. Máy mới có thể tạo venv rồi cài `camera/requirements-usb.txt`.

```powershell
.\.venv-camera\Scripts\python.exe camera/usb_viewer.py --port COM9 --configure
```

Nhập SSID, mật khẩu Wi-Fi (hiện khi nhập), URL ảnh thật của camera như `http://10.127.7.68/capture`, và thông số lắp đặt. Wi-Fi được lưu trong NVS của UNO, không ghi mật khẩu ra log hoặc file trong repo. IP mặc định chỉ là giá trị ban đầu, phải thay nếu camera đổi IP.

- **fixed**: camera đặt ngang, biết khoảng cách tới vị trí đứng. Nhập khoảng cách (m), chiều cao thật người chuẩn (cm). Nhập `fy/H = 0` để hiệu chuẩn tự động ngay box toàn thân đầu tiên. Người đầu tiên trong khung phải chính là người chuẩn, đứng thẳng và thấy cả đỉnh đầu lẫn bàn chân. Sau đó UNO lưu tiêu cự để đo người tiếp theo ở cùng vị trí.
- **ground**: nhập độ cao tâm camera tính từ **mặt trên bàn cân**, góc nghiêng xuống (độ) và `fy/H` đã hiệu chuẩn. Khoảng cách được tính từ tia chiếu chân cắt mặt phẳng bàn cân. `fy/H` bắt buộc khác 0.
- `fy/H` là tiêu cự theo pixel chia chiều cao ảnh. Ví dụ fy=600px trên ảnh cao480px thì nhập1.25. **Không nhập tiêu cự 3.6mm.** Chuẩn hóa này chỉ giữ đúng khi cùng góc nhìn và tỷ lệ ảnh; đổi crop, xoay, zoom hoặc lens thì hiệu chuẩn lại.
- Ở `fixed`, khoảng cách là thông số đã nhập, không phải một phép đo độc lập. Từ một box đơn lẻ không thể suy ra đồng thời chiều cao và khoảng cách khi thiếu thông tin chuẩn.

Cấu hình lưu trong UNO. Lần sau chỉ xem:

```powershell
.\.venv-camera\Scripts\python.exe camera/usb_viewer.py --port COM9
```

Nhấn Q hoặc Esc để đóng. Laptop không tải/chạy model. Sau cấu hình, UNO hoạt động với nguồn USB/nguồn phù hợp mà không cần laptop chạy viewer. Lưu ý nguồn qua cổng USB laptop sẽ mất nếu rút cáp, nên cần cấp nguồn khác để chạy độc lập.

## LCD và trạng thái dữ liệu

- Không có người: hai dòng `No people` / `detected`.
- Một người đủ khung và có hiệu chuẩn: luân phiên `H:...cm`, `W:...kg LOCK` với `D:...m`, `BMI:...`.
- Cân chưa chốt: W vẫn cập nhật 2 chữ số thập phân, có `...`; BMI đợi bit ổn định của cân.
- Nhiều người: yêu cầu một người mỗi lần. Box chạm biên ảnh bị từ chối vì có thể thiếu đầu/chân.
- Wi-Fi/camera mất kết nối: LCD báo lỗi kết nối thay vì giữ số đo trước.
- Dữ liệu camera hết hạn sau 10 giây, cân sau30giây. BMI chỉ có khi chiều cao mới hợp lệ và cân đã chốt. USB viewer không giữ ảnh cũ khi có thông báo camera offline.

Nhận diện bằng bounding box **không bảo đảm** đỉnh hộp trùng đỉnh đầu và đáy hộp trùng chân. Chiều cao là ước lượng, cần đối chiếu thước thật với nhiều người sau khi cố định camera. Input224 và lượng tử hóa có thể giảm khả năng phát hiện người ở xa.

## Tái tạo model

Các script ở `tools/` thực hiện export theo tutorial Espressif, cần Python3.11, môi trường `.venv-edge`, PyTorch CPU và `requirements.txt`. Dùng `tools/prepare_sources.ps1` để lấy runtime pinned; `tools/fetch_quant_sources.py` lấy notebook/scripts lượng tử hóa. Chuẩn bị bộ COCO8 train tại `.edge-deps/datasets/coco8` và model gốc `../ESP32Camera-OhStem-AI/yolo26n.pt`, rồi:

```powershell
.\.venv-edge\Scripts\python.exe firmware/edge_yolo/tools/quantize.py --size 224
```

Đây là bản bring-up: calibration COCO8, 8 bước calibration và 8 bước TQT. File `.build.json` lưu kích thước, SHA256, revision và **accuracy_validated=false**. Chưa được coi là mô hình đã kiểm định độ chính xác chiều cao. Không chạy lượng tử hóa trên bo; chỉ firmware suy luận chạy trên bo.

## Kiểm tra và chẩn đoán

Khi khởi động, UNO chạy một lần suy luận với tensor 0 để kiểm tra cấp phát và đường chạy mô hình, trước khi chờ Wi-Fi. USB viewer nhận bản tin `boot` nếu đã kết nối lúc đó; phép thử này không phải kiểm tra độ chính xác nhận diện.

- `WiFi not ready`: chạy lại viewer `--configure`, kiểm tra Wi-Fi2.4GHz.
- `Camera capture failed`: kiểm tra URL/IP, camera đang bật và hai bo cùng mạng có thể truy cập; giảm độ phân giải.
- `Model allocation failed`/`JPEG allocation failed`: chụp lại thông báo, cần điều chỉnh kích thước model/bộ nhớ, không dùng kết quả đo.
- COM9 Access denied: đóng monitor, viewer và ứng dụng khác đang giữ cổng.
- Có chiều cao nhưng không có W: đánh thức cân, đứng lên, kiểm tra MAC và bản tin quảng bá0x181D. Không có BMI cho tới khi W chốt.

Giao thức USB dùng header `SSV1`, độ dài JSON/JPEG và CRC32, cho phép đồng bộ lại khi đọc thiếu byte. UART0 dành log kỹ thuật; cổng USB dùng viewer nhị phân, không dùng Serial Monitor để xem ảnh.
