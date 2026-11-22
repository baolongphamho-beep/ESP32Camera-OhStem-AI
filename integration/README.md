# Ghép Project 3 và Project 5

Đây là ứng dụng chạy trên máy tính, ghép ảnh camera với cân nhận qua Yolo UNO. Không phải firmware mới và không tự nạp gì vào thiết bị.

## Mở và chạy

Mở `ESP32Camera-OhStem-AI.code-workspace` ở thư mục gốc repo bằng VS Code. Mở Terminal > New Terminal tại thư mục gốc repo (hoặc dùng Terminal > Run Task > `P3+P5: chạy giao diện web`):

```powershell
py .\integration\app.py
```

Nếu launcher `py` báo không tìm thấy Python, dùng đường dẫn Python có sẵn bằng script:

```powershell
powershell -NoProfile -File .\integration\start.ps1
```

Mở http://127.0.0.1:8765 trong Chrome hoặc Edge. Giữ terminal đang chạy; Ctrl+C để dừng. Không mở index.html trực tiếp vì cần backend API.

## Thử không cần thiết bị

1. Chọn ảnh trên máy.
2. Chọn nguồn cân “Nhập tay để thử”.
3. Hoa quả: nhập nhãn và đơn giá, hoặc nạp link model ảnh Teachable Machine đã huấn luyện, rồi bấm Nhận diện ảnh. Nạp model cần Internet; suy luận bằng TensorFlow.js trong trình duyệt. Chưa kèm model, chưa kiểm thử với model thực tế của nhóm. Chỉ áp dụng một loại quả mỗi lượt.
4. Sức khỏe: bấm đỉnh đầu trên ảnh và điền h, D, alpha, fy, cy. Bản đầu dùng chọn điểm bằng tay, chưa tự tìm đỉnh đầu. Chỉ xác nhận hiệu chuẩn khi thông số đúng với ảnh đang dùng.
5. Bấm Ghép kết quả. JSON tải xuống có ghi nguồn cân nhập tay/Serial và cách chọn đỉnh đầu.

## Camera và cân thật

Cài pyserial vào đúng Python dùng chạy ứng dụng:

```powershell
py -m pip install -r .\integration\requirements.txt
py .\integration\app.py --camera-url http://192.168.1.20/capture --serial-port COM4 --legacy-unit kg
```

IP và COM4 chỉ là ví dụ: thay bằng camera và Yolo UNO thực tế. Camera phải truy cập được từ máy tính. Endpoint phải trả một ảnh JPEG/PNG; `/capture` là endpoint đã thấy trong HTML firmware gốc. Đóng Serial Monitor khác trước khi mở cùng cổng COM. Mở cổng Serial có thể làm bo reset tùy phần cứng.

`--legacy-unit kg` đọc đúng log `[MiScale2] Weight received: ...` của repo gốc. Chỉ dùng sau khi xác minh cân đang trả kg bằng màn hình cân. Nếu là pound, chọn `lb`; ứng dụng đổi sang kg. Không tự suy ra đơn vị từ dòng log vì log gốc thiếu đơn vị đáng tin cậy.

Nếu sửa firmware của nhóm, giao thức khuyến nghị là mỗi dòng một JSON:

```json
{"type":"weight","weight_kg":60.5}
```

Khi firmware đã đổi khối lượng về kg, có thể xuất:

```cpp
Serial.printf("{\"type\":\"weight\",\"weight_kg\":%.4f}\n", weightKg);
```

Đặt sau khi kiểm tra gói BLE và chuyển đơn vị. Khi nhận JSON, bỏ `--legacy-unit`. Không dùng cùng lúc dữ liệu JSON đã đổi kg và log có đơn vị cấu hình khác. Dữ liệu cũ hơn 5 giây không được ghép. Chưa xác minh bit cân ổn định, nên người vận hành phải đối chiếu số đã ổn định và ghép đúng người trong ảnh.

## Hình học đo chiều cao

`h' = h + D * tan(alpha + atan((cy - v_top) / fy))`.

- h tính từ mặt cân nơi chân người đứng đến tâm camera; D là khoảng cách ngang đến mặt phẳng người.
- alpha dương khi ngẩng camera lên. v tăng từ trên xuống dưới ảnh.
- fy, cy phải lấy từ hiệu chuẩn, đúng độ phân giải ảnh đã khử méo. Ứng dụng chưa tự khử méo.
- Camera không nghiêng ngang; người đứng thẳng gần mặt phẳng dọc trung tâm, đúng vạch khoảng cách. Đỉnh đầu không phải tâm khuôn mặt.
- Bộ số kiểm tra toán học: h=1, D=2, alpha=0, fy=600, cy=240, v_top=30 -> 1.70 m. Đây không phải thông số camera của bạn.
- BMI chỉ là phép tính kg/m²; ứng dụng không phân loại sức khỏe, chưa được kiểm chứng như thiết bị y tế.

## Cấu trúc và nguồn

- `integration/`: giao diện dùng chung, backend camera/Serial, kiểm tra công thức.
- `../camera_firmware/web/`: HTML trích từ firmware camera gốc, để tham khảo.
- `../lib/`, `../src/`: firmware cân SmartScale đầy đủ (bản mới hơn `scale_reference/` cũ của Mini Project, vốn lấy từ commit `88fe8bf` của https://github.com/KhiemNC/NimBLE_SmartScale_Cp).
- `../backup_original_camera.bin`, `../flash_header.bin`, `../camera_firmware/partitions/`: bản nhị phân firmware camera.

Mã cân ghi MI SCALE2, trong khi đề có hình S400: cần kiểm tra service, characteristic và payload của cân thật. Bản ghép không giải quyết sự khác nhau giao thức bằng cách đổi tên model. Repo còn có QR/cảm biến lực; cần giản lược và sửa parser trước khi phát triển firmware chính thức.

## Kiểm tra

```powershell
py -m unittest discover -s .\integration -p "test_*.py"
```

Đã kiểm tra công thức với kết quả biết trước, dữ liệu không hợp lệ, JSON cân và log kg/lb. Chưa kiểm tra BLE, camera thật, độ chính xác đo hoặc build PlatformIO. Chưa có điều khiển servo/nguồn cân hay tự nhận diện đỉnh đầu.
