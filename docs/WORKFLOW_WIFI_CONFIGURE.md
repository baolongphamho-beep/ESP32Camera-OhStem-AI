# Workflow Plan 2 — hai mục đánh dấu trong ảnh

Ghi nhận từ ảnh ghi chú của người dùng ngày **18/11/2026**. Hai dấu ✓ tương ứng với **Reset/kết nối Wi-Fi camera** và **chạy configure/nhập thông số**. Hướng dẫn dưới đây áp dụng cho **firmware native Plan 2 đã nạp vào YOLO UNO ở COM5**.

Luồng trong ảnh: **nối mạch → chọn Stream video trên camera → Reset/setup Wi-Fi → nạp firmware UNO → configure → nhận dạng/đo**. Bước nạp firmware UNO đã thực hiện; không cần nạp lại chỉ để đổi Wi-Fi hoặc thông số.

**Nếu hiện tại chỉ thử UNO + camera, chưa dùng cân và servo:** sau khi cấu hình Wi-Fi/URL camera, mở bằng lệnh sau thay cho viewer thông thường:

```powershell
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py --preview
```

Màn hình hiện `CAMERA_TEST` và ảnh do UNO xử lý. Không phải bước lên cân, không quay servo, không cần nhập thông số chiều cao/góc để thử ảnh. Nhấn Q để quay về luồng Plan 2. Viewer thông thường hiện `STANDBY / Step on scale` vì cố ý chưa lấy ảnh khi chưa có tín hiệu cân.

Bản tối ưu mới nhất trên COM5 dùng model 192×192 và camera 320×240, đo xử lý 320–327 ms. Bản đầu trước đó khoảng 0,6 s/ảnh. Camera/mạng vẫn có thể chậm hoặc truyền thiếu ảnh; đây chưa phải tổng độ trễ video 300 ms. Firmware chờ HTTP 6 s cho mỗi thao tác socket. Đóng các trang/script đang xem camera, đặt camera và UNO gần điểm Wi-Fi, hướng ống kính ra vùng sáng để thử nhận diện người. Model nhỏ/QVGA cần kiểm tra lại nhận người ở xa và sai số đo trước khi dùng Plan 2.

Độ phân giải camera được đổi bằng API cảm biến, không nạp firmware camera. Ở camera hiện tại đã xác minh `framesize=5` trả JPEG 320×240, còn cấu hình trước thử là `framesize=8`, quality=15 (bản ghi riêng máy `.edge-deps/camera-sensor-before-fast.json`). Nếu camera reset làm độ phân giải thay đổi, kiểm tra lại ảnh thực tế; không lấy số lựa chọn từ file HTML cũ làm chuẩn. Có thể trả về cấu hình cũ bằng `http://<IP-camera>/control?var=framesize&val=8`.

## ✓ 1. Nhấn Reset và kết nối Wi-Fi cho camera

1. Cấp nguồn cho camera OhStem. Trên menu camera, chọn **`6. Stream video`**.
2. Nhấn **Reset/RST trên camera** để khởi động lại theo bước ghi trong ảnh. Quan sát màn hình và chọn lại `Stream video` nếu camera trở về menu. Reset khởi động lại không đồng nghĩa xóa Wi-Fi đã lưu.
3. Khi camera ở chế độ cấu hình và phát Wi-Fi riêng (`ohstemcam_...` hoặc tên hiển thị trên camera), kết nối laptop/điện thoại vào mạng đó.
4. Mở **`http://192.168.4.1`** để vào trang cấu hình Wi-Fi. Địa chỉ này dùng khi đang nối mạng riêng của camera. Nếu camera đã nối mạng lab, dùng IP hiện trên camera để truy cập; không mặc định IP vẫn là `192.168.4.1`.
5. Chọn **Wi-Fi 2,4 GHz** mà UNO sẽ sử dụng, nhập mật khẩu và nhấn **KẾT NỐI**. Sau đó nối laptop trở lại cùng mạng này.
6. Chép **IP camera được cấp** từ màn hình camera. Ví dụ IP `10.127.7.68` thì URL ảnh đưa vào configure là **`http://10.127.7.68/capture`**. Đây là ví dụ, thay bằng IP đang hiển thị thực tế.
7. Đóng tab trình duyệt hoặc chương trình khác đang lấy ảnh camera trước khi để UNO gọi `/capture`, tránh camera báo bận.

**Kết quả cần ghi nhận:** tên Wi-Fi được chọn, IP camera hiện tại và URL `/capture`. Wi-Fi trên camera và Wi-Fi trên UNO được cấu hình riêng; bước 2 bên dưới nhập lại mạng tương ứng cho UNO.

## ✓ 2. Chạy configure và nhập thông số cấu hình

**Chuẩn bị thư viện cho đúng môi trường Python** (làm một lần, hoặc khi gặp lỗi thiếu module):

```powershell
.\.venv-camera\Scripts\python.exe -m pip install -r camera\requirements-usb.txt
```

Lệnh này cài OpenCV, NumPy và **pyserial** vào chính `.venv-camera` được dùng bên dưới. Cài bằng `pip` trong `.venv` khác không bổ sung thư viện cho `.venv-camera`. Nếu báo `ModuleNotFoundError: No module named 'serial'`, gói cần cài là `pyserial` trong file requirements trên.

Đóng Serial Monitor/viewer khác đang chiếm cổng UNO. Từ thư mục gốc dự án LAB, chạy:

```powershell
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py --configure
```

Số cổng COM có thể đổi mỗi lần cắm lại (lần 18/11 là COM5). Từ 22/11 các công cụ **tự tìm** cổng của YOLO UNO (USB Espressif 303A); xem cổng bằng `py -3 camera\serial_port.py`. Nếu có nhiều bo, thêm `--port COMx`. Tham số `--configure` gửi cấu hình qua USB và lưu vào UNO, không nạp lại firmware.

Khi chạy đúng sẽ hiện `Tu tim thay YOLO UNO o COMx.`, `Da mo COMx.` rồi `WiFi SSID (Enter = giu nguyen):`. Lúc này chương trình đang chờ bạn nhập ở terminal; các câu hỏi cấu hình xuất hiện trước cửa sổ xem ảnh. Đã kiểm tra tới câu hỏi này trên máy LAB sau khi bổ sung pyserial ngày 18/11; chưa nhập hoặc thay cấu hình UNO trong lần kiểm tra.

### Nhập theo thứ tự câu hỏi của chương trình

| Câu hỏi trên terminal | Cần nhập cho native Plan 2 |
|---|---|
| `WiFi SSID` | Tên Wi-Fi 2,4 GHz đã chọn cho camera ở mục 1; UNO cần truy cập được camera trên mạng này. Enter chỉ giữ cấu hình cũ, không lấy tự động từ camera |
| `WiFi password` | Mật khẩu của mạng đó; chương trình hỏi khi nhập SSID mới và không hiện mật khẩu khi gõ |
| `URL anh camera` | URL đầy đủ **`http://<IP-camera>/capture`**, dùng IP vừa ghi nhận ở mục 1 |
| `MAC can Mi Scale 2 (scaleMac)` | MAC của cân thực tế. Ví dụ định dạng `70:87:9E:EA:3A:E0`; Enter giữ MAC đã lưu |
| `Nhap thong so lap dat ...?` | **`y`** hoặc Enter (mặc định có) |
| `Che do fixed / ground / servo` | Gõ **`servo`** rồi Enter; không để Enter chọn mặc định `fixed` |
| `Truc servo/tam camera cao hon mat can (cm)` | Đo độ cao tâm ống kính so với **mặt trên bàn cân**, nhập theo cm; `110` chỉ là giá trị mặc định tham khảo |
| `fy/H` | Tiêu cự theo pixel chia chiều cao ảnh. `1.33` là giá trị khởi đầu tham khảo, cần hiệu chuẩn; không nhập tiêu cự ống kính `3.6 mm` |
| `Nhap thong so servo va thoi gian giu ket qua ...?` | Gõ **`y`** để mở phần nhập servo; Enter ở câu hỏi này mặc định bỏ qua |

### Các khóa servo và thời gian giữ kết quả

| Khóa | Ý nghĩa | Mặc định firmware / cách chọn |
|---|---|---|
| `servo_pin` | GPIO tín hiệu servo | **`10` (D7)** theo Plan 2 |
| `servo_bottom` | Góc bắt đầu quét, camera nhìn thấp | `45°` (TV3); thay bằng giới hạn đã kiểm tra trên gá thực tế |
| `servo_top` | Góc kết thúc quét, camera nhìn cao | `135°` (TV3); thay bằng giới hạn đã kiểm tra. Hai đầu phải khác nhau |
| `servo_step` | Bước góc mỗi lần quét | **`5°`** (TV3). Nếu một bước nhảy từ cụt đầu sang cụt chân, FSM tự chia đôi khoảng đó |
| `servo_level` | Góc lệnh khi camera nằm ngang | `90°` là mặc định; phải tìm lại theo cách lắp servo |
| `servo_dir` | Chiều quay so với góc nghiêng (quy ước TV3 từ 22/11) | **`-1`** nếu tăng góc lệnh làm camera **ngẩng lên** (mặc định); **`1`** nếu làm camera **cúi xuống** |
| `servo_settle` | Chờ sau khi servo **dừng hẳn** | **`200 ms`** (TV3) |
| `servo_delay` | Độ trễ ảnh của camera: chỉ tin ảnh chụp sau dừng + settle + delay | **`500 ms`** (TV3 ước lượng, chưa đo) |
| `servo_dps` | Tốc độ quét liên tục `servoSweepStart()` (native, FSM bước không dùng) | `4°/s` |
| `hold_sec` | Thời gian giữ kết quả sau khi bước xuống cân | **`12 s`** |

Servo (driver TV3, ghép 22/11): mỗi lần đổi góc servo tăng/giảm tốc mềm (tối đa 60°/s, 240°/s²), bắt đầu từ góc cuối đã ra lệnh; lần đầu sau khi bật nguồn coi servo đang ở `servo_bottom`. FSM chờ servo đi hết hành trình + `servo_settle` + `servo_delay` rồi mới dùng ảnh. **Không còn chặn chân cấm**: gửi nhầm `servo_pin=19/20` sẽ làm mất cổng USB của UNO (viewer chỉ cảnh báo). Nếu servo nối ở chân khác (ví dụ MG995 ở D11 = GPIO38 như `src/servo_mg995_test.cpp`), đặt `--set servo_pin=38`. **Đã đổi dấu `servo_dir`**: giá trị cũ lưu trong NVS được firmware tự đảo một lần; nếu nhập tay theo tài liệu cũ thì đảo dấu.

### Hiệu chuẩn servo bằng `servo_jog.py` (native, từ 22/11)

Không cần nạp env Arduino `yolo_uno_servo_test`. **Servo chỉ quay trong 45°..135°** (giới hạn cứng `SWEEP_LIMIT_MIN_DEG`/`SWEEP_LIMIT_MAX_DEG` trong `lib/EdgeSweep/edge_config.h`): lệnh quay tay, quét và các khóa `servo_bottom`/`servo_top`/`servo_level` ngoài khoảng này đều bị từ chối. Firmware native nhận lệnh USB `SERVO <góc>`; servo quay mềm tới góc đó và giữ, LCD hiện `Servo jog` + góc nghiêng. Trong lúc này cân/camera tạm dừng. Thoát (`q`) hoặc mất USB quá 10 s thì servo được nhả và hệ thống về Plan 2. Đóng Serial Monitor/viewer trước khi chạy.

```powershell
py -3 tools\servo_jog.py --start 90
```

1. Gõ số góc hoặc `+`/`-` (1°), `+5`/`-5`. Đặt app đo độ nghiêng lên lưng camera, tìm góc camera **nằm ngang** rồi gõ `level`.
2. Gõ `+10`: camera **ngẩng lên** thì gõ `up` (`servo_dir=-1`), **cúi xuống** thì gõ `down` (`servo_dir=1`).
3. Quay tới góc thấp nhất vẫn thấy chân trên mặt cân, gõ `bottom`; góc cao nhất cần dùng (dây chưa căng, gá chưa chạm), gõ `top`.
4. Gõ `show` để xem, `save` để lưu xuống UNO (chờ `[UNO] Da luu cau hinh.`), gõ `config` để **đọc lại giá trị UNO đang lưu**, rồi `q`.

Đọc lại cấu hình bất cứ lúc nào (kể cả sau khi rút điện), không mở cửa sổ; mật khẩu Wi-Fi không bao giờ được gửi ra:

```powershell
py -3 camera\usb_viewer.py --show-config
```

Cuối kết quả có dòng `--set servo_level=… --set servo_dir=… …` để sao lưu hiệu chuẩn; chạy lại dòng đó nếu cần nạp lại bo.

**Đo `servo_delay` thật** (cần camera trên Wi-Fi + servo đã gắn; đóng viewer):

```powershell
.\.venv-camera\Scripts\python.exe tools\servo_delay_probe.py --camera http://<IP camera> --a 85 --b 95 --cycles 6 --json .edge-deps\qa\servo-delay.json
```

Công cụ cho servo đi qua lại giữa hai góc, lấy ảnh liên tục và in độ trễ ảnh sau khi servo dừng, kèm gợi ý `--set servo_delay=…`. Hướng camera vào vùng có chi tiết (không phải tường trơn).

`--start` nên là góc an toàn giữa hành trình: lần quay đầu tiên sau khi bật nguồn servo nhảy thẳng tới góc này.

### Độ phân giải và chất lượng ảnh camera (native, tùy chọn)

Câu hỏi cuối của `--configure` (mặc định **n**). UNO tự gửi `/control?var=framesize` và `/control?var=quality` sang camera khi giá trị đổi, khi đổi URL hoặc sau một lần lấy ảnh lỗi. Không nạp firmware camera.

| Khóa | Ý nghĩa | Gợi ý |
|---|---|---|
| `cam_framesize` | Độ phân giải camera, `-1` = không đổi (mặc định) | **`5` = 320×240** (đã thử trên camera LAB). `8` = 640×480 là mặc định gốc của camera. `6`/`7` (400×296 / 480×320) chưa thử |
| `cam_quality` | Chất lượng JPEG 4..63, `-1` = không đổi | Số **nhỏ hơn = ảnh nét hơn, file lớn hơn**. Đang dùng 15; thử **12 hoặc 10** |

Ví dụ thử nhanh khi xem preview (lưu luôn vào NVS):

```powershell
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py --preview --set cam_framesize=5 --set cam_quality=12 --log .edge-deps\qa\camera-q12.jsonl
```

Đo trước bằng laptop để chọn cặp giá trị (UNO đang ở STANDBY, đóng viewer và trang web camera):

```powershell
py -3 tools\camera_fetch_bench.py --camera http://10.127.7.68 --framesize 5 6 7 --quality 15 12 10 --frames 10 --json .edge-deps\qa\camera-bench.json
```

Công cụ đọc cài đặt cũ qua `/status`, thử từng cặp, in bảng thời gian lấy ảnh và trả camera về cài đặt cũ. Model trên UNO chỉ nhận 192×192, nên tăng độ phân giải camera chủ yếu làm ảnh xem trên PC đẹp hơn, không làm AI chính xác hơn bao nhiêu. Giảm `cam_quality` tốn rất ít thời gian. Viewer cũng tự phóng ảnh bằng bicubic và làm nét nhẹ trên PC (tắt bằng `--raw-view`), không làm UNO chậm hơn.

Tại từng câu hỏi `servo_*`/`hold_sec`, **Enter giữ nguyên giá trị đã lưu**, không tự điền lại giá trị mặc định trong bảng. Các giá trị hình học/góc trên đây chưa phải bộ thông số đã hiệu chuẩn cho gá của nhóm.

### Xác nhận lưu và chạy

1. Chờ terminal báo **`Da luu cau hinh.`**. Nếu báo **`Cau hinh bi tu choi.`**, kiểm tra dữ liệu vừa nhập và gửi lại.
2. Native lưu cấu hình trong NVS `edge_cfg`. APPLY thành công bắt đầu lại phiên đo; không cần nạp firmware hoặc bấm Reset UNO chỉ để lưu cấu hình.
3. `STANDBY` / `Step on scale` và LCD tắt nền là trạng thái chờ bình thường. Bước lên cân đúng MAC để bắt đầu luồng đo; khi nghỉ, native không lấy ảnh liên tục.
4. Những lần sau chỉ cần xem trạng thái/ảnh:

```powershell
.\.venv-camera\Scripts\python.exe camera\usb_viewer.py
```

**Thông tin còn cần ghi theo bộ lắp thực tế:** SSID, IP/URL camera, MAC cân, `height`, `focal`, `servo_bottom`, `servo_top`, `servo_level`, `servo_dir` và giá trị settle đã thử. Ảnh ghi chú chưa cung cấp các giá trị này; mật khẩu Wi-Fi chỉ nhập trong terminal, không ghi vào tài liệu chia sẻ.

Chi tiết: [hướng dẫn Plan 2](HUONG_DAN_PLAN2.md), [README native](../firmware/edge_yolo/README.md), [biên bản nạp UNO](reports/NATIVE_BUILD_FLASH_20261118.md).
