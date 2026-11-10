# YOLO26n trực tiếp trên YOLO UNO

Phần triển khai hiện tại nằm tại [firmware/edge_yolo/README.md](../firmware/edge_yolo/README.md).

- Phần cứng đã probe: Flash 16 MB, PSRAM 8 MB.
- Model YOLO26n đã chuyển sang ESP-DL INT8, input224, khoảng3.08MB.
- Firmware ESP-IDF đã biên dịch: nhận JPEG từ OhStem qua Wi-Fi, chạy AI và hình học trên UNO, đọc Mi Scale2 qua BLE, hiển thị LCD.
- Trình xem `camera/usb_viewer.py` chỉ nhận ảnh/kết quả từ UNO qua Type-C, không chạy AI.
- Cấu hình, hiệu chuẩn, lệnh build/nạp và giới hạn độ chính xác được mô tả trong README firmware.

Các environment PlatformIO và chương trình `camera/smartscale_camera.py` vẫn là bản cũ chạy AI trên laptop; không dùng launcher cũ khi đã nạp firmware native.

Mô hình lượng tử hóa mới chỉ dùng COCO8 để bring-up; chưa kiểm định độ chính xác đo chiều cao. Việc biên dịch thành công chưa thay cho thử nghiệm bộ nhớ, tốc độ và đối chiếu thước thật trên bo.
