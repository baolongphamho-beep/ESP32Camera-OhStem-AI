# Firmware YOLO UNO — 07/11/2026

Dành cho ESP32-S3 **Flash16MB, PSRAM octal8MB**. Đã chạy self-test YOLO26n input224 trên UNO; chưa kiểm định toàn bộ pipeline với người thật. Model đã nhúng trong app.

| File | Địa chỉ nạp |
|---|---|
| `bootloader.bin` | `0x0` |
| `partition-table.bin` | `0x8000` |
| `smartscale_edge_yolo.bin` | `0x10000` |

`manifest.json` chứa kích thước và SHA256 của từng file. Không có NVS, mật khẩu hoặc bản sao lưu Flash cá nhân trong thư mục này.

## Nạp bằng PowerShell

Tải/clone **cả repo** và đóng Serial Monitor, viewer đang giữ cổng. Lệnh nạp thay chương trình và bảng phân vùng UNO; không tự xóa NVS. Nếu đang chạy đúng bản firmware này thì không cần nạp lại chỉ để đổi Wi-Fi/hiệu chuẩn.

Từ gốc repo, cài công cụ vào venv đã tạo cho viewer:

```powershell
.\smartscale\.venv-camera\Scripts\python.exe -m pip install esptool
powershell -NoProfile -ExecutionPolicy Bypass -File releases/edge_yolo_20261107/flash.ps1 -Port COM9
```

Nếu dùng Python khác, thêm `-Python 'duong-dan/python.exe'`. COM9 là ví dụ; kiểm tra cổng thực tế của UNO. Script kiểm tra SHA256 trước khi ghi.

Sau nạp, mở viewer để nhập Wi-Fi và thông số camera:

```powershell
cd smartscale
.\.venv-camera\Scripts\python.exe camera/usb_viewer.py --port COM9 --configure
```

Camera vẫn cần firmware cung cấp JPEG `/capture`, cùng mạng Wi-Fi có thể truy cập UNO. Xem [README chính](../../README.md).
