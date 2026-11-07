# SmartScale: YOLO26n trên YOLO UNO + camera OhStem

YOLO UNO (ESP32-S3) lấy ảnh JPEG từ camera OhStem qua Wi-Fi, chạy YOLO26n trên bo, nhận cân nặng Mi Scale2 qua BLE và hiển thị chiều cao/cân nặng/BMI trên LCD16x2. Laptop qua Type-C chỉ cấu hình và xem ảnh/kết quả.

```text
Camera OhStem -- Wi-Fi JPEG --> YOLO UNO -- I2C --> LCD
Mi Scale 2 -------- BLE ------>    |
                                  +-- Type-C --> USB viewer (không chạy AI trên laptop)
```

## Các phần trong repo

| Thư mục/file | Công dụng |
|---|---|
| [`smartscale/firmware/edge_yolo/`](smartscale/firmware/edge_yolo/README.md) | Firmware ESP-IDF mới chạy YOLO26n trực tiếp trên UNO. |
| [`smartscale/camera/usb_viewer.py`](smartscale/camera/usb_viewer.py) | Xem ảnh và nhập Wi-Fi/thông số hiệu chuẩn qua USB. |
| [`releases/edge_yolo_20261107/`](releases/edge_yolo_20261107/README.md) | Firmware đã biên dịch, script nạp và SHA256; không chứa NVS/cấu hình Wi-Fi. |
| `smartscale/src`, `smartscale/lib`, `smartscale/platformio.ini` | Bản Arduino/PlatformIO trước đây: BLE, LCD và nhận kết quả camera qua UART. |
| `smartscale/camera/smartscale_camera.py` | Bản trước đây chạy YOLO trên laptop. Không chạy cùng firmware native mới. |
| `yolo26n.pt` | Model PyTorch gốc. Bản ESP-DL đã chuyển đổi nằm trong `smartscale/firmware/edge_yolo/models/`. |
| Các file camera ở gốc repo | Firmware/công cụ kiểm tra camera có sẵn từ trước. |

## Chạy lần đầu trên Windows

Phần cứng: UNO có Flash16MB, PSRAM octal8MB; LCD I2C SDA=GPIO11/SCL=GPIO12; camera có endpoint HTTP `/capture`; cân Mi Scale2 MAC `70:87:9e:ea:3a:e0`.

1. Cấp nguồn camera, nối LCD vào cổng I2C đỏ UNO, cắm UNO vào laptop bằng cáp Type-C dữ liệu. Camera và UNO phải truy cập nhau qua Wi-Fi2.4GHz.
2. [Nạp firmware đã biên dịch](releases/edge_yolo_20261107/README.md), hoặc [biên dịch ESP-IDF từ source](smartscale/firmware/edge_yolo/README.md). Không Upload environment PlatformIO cũ khi muốn dùng firmware native.
3. Từ thư mục gốc repo, tạo môi trường cho viewer:

```powershell
cd smartscale
py -3.11 -m venv .venv-camera
.\.venv-camera\Scripts\python.exe -m pip install -r camera/requirements-usb.txt
.\.venv-camera\Scripts\python.exe camera/usb_viewer.py --port COM9 --configure
```

Thay COM9 nếu Windows nhận UNO ở số cổng khác. Đóng Serial Monitor/viewer khác trước khi chạy.

4. Nhập tên/mật khẩu Wi-Fi, URL thật `http://IP_CAMERA/capture`, mode `fixed`, khoảng cách camera–vị trí đứng (m), chiều cao thật người chuẩn (cm), `fy/H=0`. Mật khẩu không hiện ký tự khi gõ; nhập xong nhấn Enter. Không gửi mật khẩu lên GitHub.
5. Terminal phải báo `Da luu cau hinh.` và viewer hiện ảnh. Người chuẩn phải là người đầu tiên đứng đủ đầu–chân trong khung; box toàn thân đầu tiên được dùng để hiệu chuẩn. Camera ở chế độ fixed cần đặt ngang và cố định.
6. Thử người khác tại cùng vị trí; đứng trên cân chờ `LOCK`. LCD luân phiên H/W và D/BMI. BMI cần chiều cao hợp lệ cùng cân nặng ổn định.

Lần sau chỉ xem, bỏ `--configure`:

```powershell
.\.venv-camera\Scripts\python.exe camera/usb_viewer.py --port COM9
```

Ở fixed, khoảng cách D là giá trị đã nhập. Muốn tính khoảng cách theo mặt phẳng bàn cân, dùng ground với độ cao camera, góc nghiêng và tiêu cự đã hiệu chuẩn; xem [hướng dẫn chi tiết](smartscale/firmware/edge_yolo/README.md).

## Trạng thái kiểm tra

Đã nạp firmware và chạy suy luận self-test trên UNO ngày07/11/2026:711ms, còn4,876,212byte PSRAM trước cấp JPEG buffer. Đây là phép thử tensor0, chưa phải phép đo tốc độ toàn bộ pipeline hoặc kiểm định độ chính xác nhận diện/chiều cao. Phần camera người thật, BLE quảng bá và LCD trong firmware native cần đối chiếu trên hệ thống lắp thực tế.

Model ESP-DL input224 được lượng tử hóa bằng COCO8 cho bring-up; metadata ghi `accuracy_validated=false`. Chiều cao từ bounding box chỉ là ước lượng, cần so sánh với thước thật. Xem [các giới hạn và cách chẩn đoán](smartscale/firmware/edge_yolo/README.md).
