Dưới đây là toàn bộ thông tin kỹ thuật chi tiết về **phần cứng** và **kiến trúc firmware** của chiếc **Camera AI (AI Vision Camera / ESP32Camera OhStem)** đã được thu thập, trích xuất ngược (reverse-engineer) từ bộ nhớ ROM 16MB và kiểm chứng thực tế tại phòng lab:

## **1\. Thông số kỹ thuật & Kiến trúc Phần cứng Camera AI**

| Hạng mục | Thông số chi tiết thực tế tại Lab |
| :---- | :---- |
| **Vi điều khiển (SoC)** | **ESP32-S3 N16R8** (đóng gói QFN56, revision v0.2, kiến trúc Xtensa 32-bit LX7 Dual-Core 240MHz, thạch anh 40MHz). |
| **Bộ nhớ Flash** | **16MB Quad SPI** (4 đường dữ liệu, điện áp eFuse 3.3V, mã định danh phần cứng Manufacturer: 46, Device: 4018). |
| **Bộ nhớ PSRAM** | **8MB PSRAM** nhúng bên trong chip (Embedded PSRAM 8MB AP\_3v3). |
| **Cảm biến hình ảnh** | **OmniVision OV2640** (2MP), kích thước cảm biến $1/4inch$, kích thước mỗi điểm ảnh \$2.2\\,\\mu\\text{m} \\times 2.2\\,\\mu\\text{m}\$. |
| **Thông số quang học** | Tiêu cự vật lý $f\approx 3.6mm$, khẩu độ $F/2.0$, góc nhìn chéo (DFOV) $6{5}^{\circ }-6{8}^{\circ }$, góc nhìn ngang $\approx 5{3}^{\circ }$. Tiêu cự quy đổi ở độ phân giải $320\times 240$ là ${F}_{pixel}\approx 310-330pixel$. |
| **Màn hình tích hợp** | **OLED 2.0 inch**, độ phân giải **320×240 pixel** gắn phía sau lưng module. |
| **Kết nối không dây** | Wi-Fi chuẩn **2.4 GHz** (không hỗ trợ băng tần 5 GHz) và **Bluetooth 5 (BLE)**; địa chỉ MAC thực tế của thiết bị tại lab là a4:cb:8f:f1:f1:a0. |
| **Nguồn điện & Công suất** | Điện áp hoạt động **3.3V – 5.0V**; dòng tiêu thụ **320mA @ 3.3V** hoặc **230mA @ 5.0V**. |
| **Kích thước & Ngoại hình** | Kích thước **52mm × 44.5mm**, bọc vỏ nhựa bảo vệ màu đen chuẩn hệ sinh thái OhStem. |

### **Hệ thống cổng giao tiếp vật lý & Đấu nối với YoloUNO**

* **Cổng USB Type-C:** Kết nối trực tiếp vào bộ điều khiển USB nội bộ của chip ESP32-S3 theo chế độ **USB-Serial/JTAG** (không qua chip chuyển đổi UART rời), cho tốc độ đọc/ghi Flash từ 400–600 KB/s và nhận cổng **COM3** trên máy tính tại lab.  
* **Cổng Grove 4 chân (UART):** Dùng để cấp nguồn và truyền dữ liệu UART từ Camera AI sang mạch **YoloUNO** (module ESP32-S3-WROOM-1, cổng COM10, MAC 80:b5:4e:de:ed:48).  
* **Sơ đồ ánh xạ chân (Pinout) thực tế trên YoloUNO:** Cáp Grove từ Camera AI được cắm vào cổng Digital **D6 \- D5** (Grove màu trắng) trên YoloUNO. Dù tài liệu tổng quan ghi cổng D6-D5 tương ứng GPIO 5 và GPIO 6, sơ đồ chân phần cứng chi tiết và kết quả kiểm thử thực tế xác nhận chân **D6 nối với GPIO 9** (nhận tín hiệu TX từ Camera AI, khai báo CAM\_RX\_PIN \= 9\) và chân **D5 nối với GPIO 8** (khai báo CAM\_TX\_PIN \= 8).

## **2\. Cấu trúc Bộ nhớ Flash & Bảng phân vùng (Partition Table)**

* **Trạng thái bảo mật phần cứng:** Bộ nhớ Flash 16MB của camera hoàn toàn mở, nhà sản xuất **không khóa eFuse** (eFuse Security Download) và **không bật mã hóa Flash** (Flash Encryption). Toàn bộ 16MB ROM gốc đã được sao lưu nguyên vẹn ra file backup\_original\_camera.bin (và bản 4MB đầu backup\_4MB\_original.bin) bằng công cụ esptool để đảm bảo khôi phục nguyên trạng 100% cho phòng lab.  
* **Đặc điểm phân bổ dung lượng:** Dù chip có 16MB Flash (0x01000000), bảng phân vùng (nằm tại địa chỉ 0x8000, bắt đầu bằng chữ ký magic 0xAA 0x50) chỉ khai báo sử dụng **4MB đầu tiên** (0x00000000 đến 0x00400000), còn **12MB phía sau** hoàn toàn bỏ trống.

| Tên phân vùng (Label) | Loại (Type) | Định dạng (SubType) | Địa chỉ bắt đầu (Offset) | Dung lượng (Size) | Vai trò & Dữ liệu thực tế bên trong |
| :---- | :---- | :---- | :---- | :---- | :---- |
| **nvs** | DATA | nvs (0x02) | 0x00009000 | **20 KB** | Lưu cấu hình hệ thống (Wi-Fi SSID/Password, chế độ chọn trên màn hình, cấu hình UART). |
| **otadata** | DATA | ota (0x00) | 0x0000E000 | **8 KB** | Quản lý trạng thái khởi động của phân vùng ứng dụng. |
| **app0** | APP | ota\_0 (0x10) | 0x00010000 | **3.00 MB** | Chứa toàn bộ Firmware chính, driver OV2640, giao diện OLED, thuật toán AI nội bộ và các trang Web UI nhúng sẵn. Dữ liệu thực tế (khác 0xFF) chỉ chiếm **1029.0 KB** (\~1MB, tức 6.3% tổng Flash 16MB). |
| **spiffs** | DATA | spiffs (0x82) | 0x00310000 | **896 KB** | Phân vùng ổ đĩa ảo nội bộ nhưng thực tế kiểm tra (spiffs\_dump.bin) **hoàn toàn trống (0.0 KB / 0.0%)**, không chứa file mô hình rời. |
| **coredump** | DATA | data\_0x03 | 0x003F0000 | **64 KB** | Lưu nhật ký lỗi hệ thống (Crash log) khi xảy ra sự cố tràn bộ nhớ hoặc lỗi tác vụ. |

## **3\. Kiến trúc Firmware gốc & Các chế độ hoạt động nội bộ**

* **Nền tảng biên dịch:** Firmware gốc được xây dựng trên framework **Arduino-ESP32** kết hợp **ESP-IDF phiên bản v4.4.7-dirty**, biên dịch qua **PlatformIO** (lộ diện qua các đường dẫn C:/Users/ADMIN/.platformio/packages/framework-arduinoespressif32/... trong file nhị phân).  
* **Menu 7 chế độ trên màn hình OLED 2.0 inch (SELECT AI MODEL / CHỌN CHỨC NĂNG):** Quét vùng nhớ .rodata tại Offset 0x119FA xác định 7 tính năng tích hợp sẵn (hỗ trợ cả tiếng Anh và tiếng Việt):  
  1. **Object tracking** (Theo vật thể).  
  2. **Line tracking** (Dò line).  
  3. **Face recognition** (Nhận diện khuôn mặt): Sử dụng thuật toán tương quan chéo chuẩn hóa (NCC \- Normalized Cross-Correlation) với chuỗi log nội bộ \[FACE\] skin=%d \| grid(%d/%d) cand=%d det=%d \| NCC1=%.2f-\>ID%d NCC2=%.2f (acc=%.2f mgn=%.2f), hỗ trợ lưu/xóa khuôn mặt qua nút bấm (PRESS:Save/Del R:Flash, Saved Face:ID%d, Deleted Face:ID%d) và xuất kết quả qua UART theo định dạng **face:%d**.  
  4. **Color recognition** (Nhận diện màu sắc).  
  5. **QR code** (Đọc mã QR).  
  6. **Stream video** (Truyền hình ảnh qua Wi-Fi & chạy mô hình AI tùy chỉnh qua HTTP/Web).  
  7. **Settings** (SYSTEM SETTINGS / Cài đặt hệ thống: chỉnh âm lượng Volume, giao diện Theme, ngôn ngữ lang).

## **4\. Cơ chế Mạng, Giao diện Web ẩn (Web UI) & Các cổng HTTP Endpoints**

### **Cơ chế kết nối Wi-Fi**

* **Chế độ Access Point (Khi chưa có mạng \- No WiFi):** Camera tự phát mạng Wi-Fi riêng (tên dạng ohstemcam\_... hoặc ohstem-ai-camera-xxxxx) và hiển thị hướng dẫn truy cập **\[http\://192.168.4.1\](http\://192.168.4.1)** để mở trang \<title\>Cấu hình WiFi\</title\>.  
* **Chế độ Station (Khi đã nối mạng):** Sau khi cấu hình, camera kết nối vào mạng Wi-Fi của lab hoặc Mobile Hotspot 2.4GHz trên laptop, hiển thị địa chỉ IP được cấp trực tiếp lên màn hình OLED 2.0 inch (tại lab là IP **10.127.7.68**) kèm thông báo Open this on PC or phone same WiFi.

### **Mã nguồn Giao diện Web ẩn nhúng trong Firmware**

Bóc tách trực tiếp từ phân vùng app0 thu được các trang HTML/JS nhúng sẵn (camera\_webui\_1.html, camera\_webui\_2.html, camera\_webui\_3.html) với cơ chế hoạt động như sau:

* **Tích hợp TensorFlow.js & Teachable Machine:** Trang web sử dụng thư viện @tensorflow/tfjs@1.3.1 và @teachablemachine/image@0.8, cho phép người dùng dán đường link mô hình (modelPath lưu trong localStorage.modelPath, ví dụ \[https\://teachablemachine.withgoogle.com/models/\](https\://teachablemachine.withgoogle.com/models/)... chứa bộ file model.json, metadata.json, weights.bin) để phân loại ảnh hoặc ước lượng tư thế (aiModel.estimatePose) ngay trên trình duyệt.  
* **Hỗ trợ MQTT qua WebSocket:** Mã nguồn tích hợp kết nối tới broker của hãng tại địa chỉ wss://mqtt.ohstem.vn:8084/mqtt/.  
* **Script giả lập Webcam (getUserMedia):** Nhúng sẵn một đoạn mã JavaScript tự động hút ảnh từ location.origin \+ "/capture" mỗi 70ms để vẽ lên thẻ \<canvas\> kích thước 320×240 và ghi đè hàm navigator.mediaDevices.getUserMedia trên trình duyệt.

### **Danh sách các HTTP Endpoints nội bộ (Offset 0x172B2 – 0x1743F)**

Khi bật chế độ **Stream video**, web server esp\_httpd trên camera mở các đường dẫn giao tiếp sau:

| Endpoint | Phương thức & Định dạng phản hồi | Chức năng & Lưu ý kỹ thuật thực tế |
| :---- | :---- | :---- |
| **/capture** | HTTP GET $\rightarrow$ Content-Type: image/jpeg (Access-Control-Allow-Origin: \*). | Trả về 1 khung hình tĩnh JPEG độ phân giải **320×240** (\~18.8 KB). Hoạt động ổn định nhất khi gọi từ Python (requests / curl), tránh được lỗi timeout của FFmpeg. |
| **/stream** | HTTP GET $\rightarrow$ multipart/x-mixed-replace;boundary=123456789000000000000987654321. | Phát luồng video MJPEG liên tục. Khi dùng cv2.VideoCapture mặc định dễ gặp lỗi Stream timeout triggered after 30072 ms và overread 8 do thiếu container header. |
| **/control** | HTTP GET với query params (?serial=..., ?framesize=..., ?quality=...). | Điều chỉnh thông số cảm biến (framesize, quality, brightness, contrast, hmirror, vflip, special\_effect, flash, restart, resetwifi) và làm **cầu nối bắn dữ liệu ra cổng Grove UART**. |
| **/status**, **/get\_status** | HTTP GET $\rightarrow$ application/json. | Trả về trạng thái cấu hình camera dạng JSON: {"framesize":%u,"quality":%u,"brightness":%d,"contrast":%d,"hmirror":%u,"vflip":%u,"flash":0}. |
| **/scan**, **/save\_wifi** | HTTP GET/POST. | Quét danh sách mạng Wi-Fi xung quanh và lưu cấu hình SSID/Password vào phân vùng nvs. |

* **Giới hạn kỹ thuật của Web Server trên Firmware gốc:**  
  * **Single-Client Limit (Giới hạn 1 kết nối đồng thời):** Server nội bộ trên ESP32 chỉ phục vụ ổn định 1 luồng truy cập tại một thời điểm; nếu mở tab trình duyệt xem camera cùng lúc với script Python, camera sẽ trả về {"busy":1} hoặc làm rớt gói tin.  
  * **Chỉ nhận phương thức HTTP\_GET:** Các endpoint chỉ đăng ký handler cho lệnh GET, nên nếu kiểm tra bằng lệnh curl.exe \-I (gửi HEAD request) camera sẽ trả về lỗi HTTP/1.1 404 Not Found.

## **5\. Giao thức truyền thông UART & Cơ chế Deploy Mô hình AI**

### **Cấu hình & Cú pháp gói tin UART xuất sang YoloUNO**

* **Thông số đường truyền:** Giao tiếp UART phần cứng ở tốc độ **115200 baud**, cấu hình khung truyền SERIAL\_8N1.  
* **Cơ chế chuyển tiếp lệnh từ HTTP sang UART (/control?serial=...):**  
  * Khi nhận request theo cú pháp http\://\<IP\_Camera\>/control?serial=\<Nhãn\>;\<Độ\_tự\_tin\>;stop (ví dụ: \[http\://10.127.7.68/control?serial=PERSON\_1.8m;95;stop\](http\://10.127.7.68/control?serial=PERSON\_1.8m;95;stop)), firmware của camera tự động bóc tách 2 tham số trước dấu ;stop và xuất ra chân TX của cổng Grove theo định dạng hàm printf là **class:%s,%d\\n**.  
  * Ví dụ bản tin thực tế YoloUNO nhận được qua cổng D6-D5 (CAM\_RX\_PIN \= 9): class:PERSON\_1.8m,95 hoặc class:DIST\_2.2m,95.

### **3 Phương thức Deploy "Bộ não AI" tương thích với phần cứng Camera**

> 1. **Deploy qua Pipeline Hybrid Python \+ HTTP/UART Bridge (Phương án chủ lực đang chạy tại Lab):**  
   * Máy tính đọc khung hình trực tiếp từ \[http\://10.127.7.68/capture\](http\://10.127.7.68/capture), chạy mô hình **YOLO26n** (yolo26n.pt / yolo26n-cls.pt) để phát hiện đối tượng (người đi bộ, ô tô, trái cây), vẽ Bounding Box, tính kích thước thực tế $(W\times H)$ và khoảng cách $(D)$ theo mô hình Pinhole Camera.  
   * Sau khi tính toán, máy tính gọi API \[http\://10.127.7.68/control?serial=\](http\://10.127.7.68/control?serial=)... để chính chiếc Camera AI xuất chuỗi class:\<Nhãn\>,\<Độ\_tự\_tin\> qua cáp Grove xuống YoloUNO và hiển thị lên màn hình LCD 16x2 (địa chỉ I2C thực tế 0x21, chân SDA \= GPIO 11, SCL \= GPIO 12\) mà không cần xóa hay ghi đè firmware gốc của lab.  
> 2. **Deploy qua Giao diện Web tích hợp sẵn (Chuẩn TensorFlow.js / Teachable Machine):**  
   * Huấn luyện và xuất mô hình phân loại ảnh ra bộ 3 file model.json, metadata.json, weights.bin, sau đó nạp link trực tiếp vào giao diện camera\_webui\_3.html của firmware gốc (chỉ hỗ trợ bài toán phân loại nhãn, không hỗ trợ vẽ khung giới hạn và đo khoảng cách).  
> 3. **Deploy nhúng trực tiếp xuống Flash/PSRAM của ESP32-S3 (C++ / LiteRT):**  
   * Do giới hạn 8MB PSRAM và 16MB Flash, mô hình PyTorch (.pt) sau khi huấn luyện được xuất qua trung gian .onnx rồi nén sang định dạng **LiteRT (.tflite)** kèm **Lượng tử hóa 8-bit (INT8 Quantization)** để giảm 75% dung lượng (chuyển trọng số từ FP32 4-byte xuống INT8 1-byte) trước khi biên dịch nhúng vào firmware C++ (ESP-DL / ESP-NN) và nạp qua cổng USB Type-C. Khi kết thúc buổi thực hành, có thể nạp trả lại file backup\_original\_camera.bin qua cổng COM3 để khôi phục 100% trạng thái xuất xưởng cho phòng lab.