# Bổ sung từ repository ESP32Camera-OhStem-AI

Ngày đọc: 07/11/2026. Nguồn: https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI (nhánh main tại thời điểm đọc).

## Đối chiếu firmware

Git blob SHA-1 tính từ file địa phương trùng với metadata GitHub:
- backup_original_camera.bin: 012bd22cde0db5483ae54e413bf2bae1c570deef
- flash_header.bin: eb698f9afcce4d7a583c4def3595b6bc2e07375b
- camera_ai.html sau khi bỏ phần DOCTYPE đầu file trùng camera_webui_3.html: 9ef5a01e98d5cfd351796cf2bd877546f1305d1d.

Repository là bộ firmware backup, HTML và công cụ phân tích/thử nghiệm; danh sách gốc hiện không có dự án C/C++ firmware hoàn chỉnh để build lại.

## Phần mới có thể tận dụng

pedestrian_detector.py (blob c89e27e27062adcf7d17c3d98d8fc8b89680fb4a) dùng OpenCV, requests và Ultralytics. Chương trình tải yolo26n.pt trên máy tính, đọc URL /stream, chọn lớp person, vẽ bounding box và gửi chuỗi DIST_... về /control?serial=...;...;stop.

Đây là phân tích tĩnh: chưa chạy model, chưa kết nối camera. IP trong mã là cấu hình của tác giả, không phải IP được xác nhận của thiết bị người dùng. Đường /stream và cổng phục vụ phải thử thực tế; HTML gốc dùng /capture.

## Vì sao chưa đo chiều cao thật

Mã đặt F=600 pixel, H0=1.65 m, W0=0.45 m. Với chiều cao khung p_h và chiều rộng p_w:

D_est = 0.7 * H0 * F / p_h + 0.3 * W0 * F / p_w
H_est = p_h * D_est / F
      = 1.155 + 0.135 * p_h / p_w (m)

Chiều cao xuất ra phụ thuộc giả định kích thước và tỷ lệ khung, không phải phép đo độc lập. Chiều rộng bounding box toàn người cũng không phải phép đo bề rộng vai.

## Những điểm cần sửa/kiểm chứng

- Thay khoảng cách ước lượng từ chiều cao trung bình bằng D đo độc lập từ hình chiếu camera tới vị trí đứng cố định trên cân.
- Hiệu chuẩn f_y, c_y, méo ảnh và góc ngẩng alpha ở đúng độ phân giải dùng đo.
- Xác định đỉnh đầu; cạnh trên bounding box chỉ là gần đúng, cần kiểm tra/tinh chỉnh hoặc chọn thủ công lúc kiểm chứng.
- Với người gần mặt phẳng giữa ảnh, không roll, ảnh khử méo: beta=atan((c_y-v_top)/f_y); H=h+D*tan(alpha+beta). h đo từ mặt đặt chân tới tâm quang học. Nếu h đo từ sàn, trừ thêm độ cao mặt cân.
- Chỉ chọn người trong vùng cân, bỏ lượt có nhiều người/đầu bị cắt, kiểm tra tư thế và lấy kết quả ổn định qua nhiều ảnh.
- HTML gửi confidence dạng 0..1; Python gửi int(conf*100). Cần xác minh parser firmware và thống nhất đơn vị trước khi nối YoloUNO.
- requests.get trong mã chưa kiểm tra HTTP status, và dòng log gửi thành công không chứng minh UART nhận được. Cần kiểm tra phản hồi HTTP và xác nhận phía nhận.
- find_uart_format.py/find_all_uart.py tìm chuỗi trong binary, chưa giải mã đường xử lý UART hay xác định chân/baud.

## Lộ trình đề xuất

1. Giữ firmware hiện có, thử lấy ảnh từ camera về máy tính.
2. Đặt camera/cân cố định, đo h,D,alpha; hiệu chuẩn camera.
3. Bấm chọn đỉnh đầu thủ công để kiểm chứng công thức bằng vật/thước biết chiều cao.
4. Dùng YOLO tìm vùng người rồi bổ sung định vị đỉnh đầu và kiểm tra tư thế.
5. Ghép chiều cao với khối lượng của cùng lượt cân; gửi kết quả sang YoloUNO/LCD.

Chưa có xác nhận độ chính xác đo thực tế hoặc đủ điều kiện sử dụng y tế.

## Nguồn mã

- https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI/blob/main/pedestrian_detector.py
- https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI/blob/main/camera_webui_3.html
- https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI/blob/main/find_uart_format.py
- https://github.com/baolongphamho-beep/ESP32Camera-OhStem-AI/blob/main/find_all_uart.py
