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

## Phần cứng

- LCD: cổng I2C đỏ trên YOLO UNO, **SDA=GPIO11, SCL=GPIO12**. Firmware dò PCF8574 tại 0x20..0x27 và 0x38..0x3f, thử thêm GPIO6/7; không dò GPIO8/9. Dò lại sau 5 giây nếu LCD chưa sẵn sàng.
- Cân: MAC `70:87:9e:ea:3a:e0`. Firmware mới đọc bản tin quảng bá BLE service 0x181D; không kết nối GATT như firmware Arduino cũ. Cần đánh thức cân và kiểm tra cân có phát bản tin cân nặng khi đứng lên. Nếu cân không phát quảng bá, cần bổ sung đường nhận GATT trước khi dùng thực tế.
- Camera: bật chế độ lấy ảnh `/capture`, ưu tiên **QVGA 320x240**, camera và UNO cùng Wi-Fi 2.4 GHz có thể truy cập nhau. Tắt xử lý Face/No Face trên giao diện camera để giảm tải nếu có thể. Không cần mở stream trên trình duyệt khi đo.
- Type-C dùng truyền dữ liệu. Đóng Serial Monitor trước khi nạp hoặc mở USB viewer.

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

Nhập SSID, mật khẩu Wi-Fi (ẩn trên màn hình), URL ảnh thật của camera như `http://10.127.7.68/capture`, và thông số lắp đặt. Wi-Fi được lưu trong NVS của UNO, không ghi mật khẩu ra log hoặc file trong repo. IP mặc định chỉ là giá trị ban đầu, phải thay nếu camera đổi IP.

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
