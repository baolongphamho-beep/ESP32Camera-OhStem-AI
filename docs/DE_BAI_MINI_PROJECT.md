# Đề bài MINI PROJECT – ACLAB (trích phần liên quan)

Nguồn: file `MINI_PROJECT.docx` (Google Drive của lab). Nhóm làm bài **ghép Project 3 + Project 5**. Project 6 được chép kèm để tham khảo, vì phần nhận diện hoa quả + tính tiền trong `integration/` thuộc đề này.

## Project 3: Xây dựng công cụ huấn luyện và deploy bộ não AI lên ESP32Camera OhStem

Mục tiêu:
- Tìm hiểu và sử dụng Camera từ hệ sinh thái hỗ trợ sẵn bởi OhStem, gửi kết quả qua đường UART để hiển thị kết quả trên YoloUNO (kết nối với màn hình LCD).
- Xây dựng công cụ để huấn luyện việc nhận dạng object trên camera AI cho từng ứng dụng cụ thể: nhận diện trái cây, nhận diện ô tô, nhận diện con người, v.v.
- Xây dựng ứng dụng phát hiện người trước đầu xe ô tô, vẽ bounding box, ước lượng kích thước và khoảng cách tới người đi bộ.
- Viết tài liệu hướng dẫn sử dụng (Quick start) và seminar chia sẻ kinh nghiệm hiện thực dự án.
- Viết hướng dẫn sử dụng mã nguồn trên GitHub.

## Project 5: Cân sức khỏe sử dụng XiaoMi và mạch lập trình YoloUNO

Source: https://github.com/KhiemNC/NimBLE_SmartScale_Cp/tree/main — code chạy trên Yolo UNO (ESP32-S3).
Firmware đọc cân nặng qua BLE của cân MI SCALE 2 với `WEIGHT_SERVICE_UUID` và `WEIGHT_CHAR_UUID` (branch `force_sensor_update` > `scale.cpp` > `void parseScaleData(String UUID, unsigned char *data, size_t length)`). Firmware gốc còn gửi FORCE, LEFT, RIGHT, QR qua một kênh BLE khác, nên `config.h` có nhiều characteristic. Nếu chỉ cần cân nặng thì chỉ cần `WEIGHT_CHAR_UUID` và `WEIGHT_SERVICE_UUID`.

Mục tiêu:
- Dùng cân Xiaomi đo sức khỏe; dữ liệu gửi qua Bluetooth, mạch YoloUNO đọc; **bộ phận nguồn thiết kế từ phía YoloUNO để bật/tắt nguồn điện cấp cho cân**.
- Dùng camera đo chiều cao của một người đứng trước cân. Dựa vào **góc nghiêng của camera** (điều khiển bằng RC servo, hoặc đặt góc nghiêng cố định) và **vị trí khuôn mặt trong khung hình** để tính chiều cao.
- Hiển thị kết quả đo chiều cao.
- Viết Quick start + seminar; viết hướng dẫn mã nguồn trên GitHub.

## Project 6 (tham khảo): Cân hoa quả sử dụng XiaoMi và mạch lập trình YoloUNO

Cùng nguồn cân như P5. Mục tiêu khác: dùng camera nhận dạng loại hoa quả, tính thành tiền (khối lượng × đơn giá), hiển thị trên LCD.

## Ý tưởng thầy hướng dẫn bổ sung cho P5 (tin nhắn của thầy)

> Làm một hệ thống chuyển động giống khuôn mặt của một robot: xoay lên xuống để định vị khuôn mặt ở giữa khung hình. Dựa vào góc quay của động cơ, tính ra chiều cao của người trên cân. Vì không thể đặt camera chĩa thẳng vào khuôn mặt của mọi người với nhiều chiều cao khác nhau, nên cần một motor xoay lên xuống, dò dần cho khuôn mặt ở giữa, rồi dựa vào góc quay hiện tại của motor mà tính ra chiều cao.
