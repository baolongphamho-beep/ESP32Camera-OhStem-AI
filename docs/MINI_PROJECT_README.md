# Mini Project - Camera AI và cân hoa quả

## Bản ghép Project 3 + Project 5

> Bản ghi chép gốc của folder `Mini Project`, đã chuyển vào repo `ESP32Camera-OhStem-AI-main`. Đường dẫn bên dưới đã được cập nhật theo vị trí mới.

Mở folder repo trong VS Code. Xem `integration/README.md` để chạy giao diện hoa quả và đo chiều cao dùng chung camera/cân. Mã cân nằm ở `lib/` + `src/` (thư mục gốc repo); bản ghép web chạy trên máy tính, không nạp firmware.

## Nội dung
- `backup_original_camera.bin`, `flash_header.bin` (thư mục gốc repo): hai file gốc, không chỉnh sửa.
- `camera_firmware/partitions/`: năm phân vùng đã tách, bảng phân vùng và manifest.
- `camera_firmware/web/`: ba trang HTML nguyên văn trích từ app0; tên file được đặt để dễ hiểu, không phải tên gốc được xác minh.
- `docs/reports/`: báo cáo hai file và JavaScript đã trích trước đó.
- `tools/inspect_flash.py`: đọc bảng phân vùng và tách phân vùng.

## Các trang HTML
- `wifi_saved.html`: thông báo sau khi lưu cấu hình Wi-Fi.
- `wifi_setup.html`: giao diện cấu hình Wi-Fi.
- `camera_ai.html`: giao diện camera và nhận dạng AI.

HTML giữ nguyên JavaScript/CSS nhúng và đường dẫn thư viện bên ngoài. Mở HTML trên máy chỉ phục vụ xem mã/giao diện; các endpoint camera như /capture, /status, /control cần firmware camera đang hoạt động. Không phải ứng dụng nhận dạng độc lập chạy ngay bằng cách mở file. Chưa thử kết nối camera thực tế.

## Chạy từ terminal tại thư mục gốc repo
```powershell
py .\tools\inspect_flash.py .\backup_original_camera.bin
py -m esptool image-info .\camera_firmware\partitions\02_app0_0x00010000.bin
```

Firmware nhị phân không phải mã nguồn C/C++. Các file HTML được trích đúng byte từ firmware; vị trí và SHA256 ghi trong camera_firmware/web/extraction_manifest.json.
