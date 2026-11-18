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
