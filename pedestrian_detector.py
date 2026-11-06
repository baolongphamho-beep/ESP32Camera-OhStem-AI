import time
import cv2
import requests  # Thư viện gọi HTTP API tới camera
from ultralytics import YOLO

# =====================================================================
# CẤU HÌNH KẾT NỐI CAMERA AI VISION OHSTEM QUA WI-FI & UART
# =====================================================================
# Thay đổi IP này thành IP hiển thị trên màn hình OLED 2.0 inch của camera tại lab
CAMERA_IP = "10.127.7.68"
STREAM_URL = f"http://{CAMERA_IP}/stream"
CONTROL_URL = f"http://{CAMERA_IP}/control"

FOCAL_LENGTH = 600.0  # Tiêu cự F (pixel)
REF_PERSON_HEIGHT = 1.65  # Chiều cao trung bình người đi bộ (m)
REF_SHOULDER_WIDTH = 0.45  # Bề ngang trung bình (m)
SAFE_DISTANCE = 2.5  # Khoảng cách cảnh báo nguy hiểm (m)

# Tải mô hình YOLO (chạy trên máy tính để xử lý đồ họa bounding box)
model = YOLO("yolo26n.pt")

print(f"Đang kết nối luồng video từ Camera AI tại: {STREAM_URL}...")
cap = cv2.VideoCapture(STREAM_URL)

if not cap.isOpened():
  print(
      f"[LỖI] Không thể mở luồng video từ {STREAM_URL}. Hãy kiểm tra lại kết nối"
      " Wi-Fi và IP của camera!"
  )
  exit()

print(
    "==> ĐÃ KẾT NỐI THÀNH CÔNG VỚI CAMERA AI VISION! Bắt đầu chạy giám sát..."
)

last_send_time = 0

while cap.isOpened():
  ret, frame = cap.read()
  if not ret:
    print("Mất kết nối khung hình từ camera, đang thử kết nối lại...")
    time.sleep(1)
    cap = cv2.VideoCapture(STREAM_URL)
    continue

  # Nhận diện người đi bộ (class 0: person)
  results = model(frame, classes=[0], conf=0.5, verbose=False)

  current_person = None
  for r in results:
    for box in r.boxes:
      x1, y1, x2, y2 = map(int, box.xyxy[0])
      w_pixel = max(1, x2 - x1)
      h_pixel = max(1, y2 - y1)
      conf = float(box.conf[0])
      current_person = (x1, y1, x2, y2, w_pixel, h_pixel, conf)
      break

  if current_person is not None:
    x1, y1, x2, y2, w_pixel, h_pixel, conf = current_person

    # 1. Tính toán khoảng cách và kích thước thực tế
    dist_h = (REF_PERSON_HEIGHT * FOCAL_LENGTH) / h_pixel
    dist_w = (REF_SHOULDER_WIDTH * FOCAL_LENGTH) / w_pixel
    distance_m = 0.7 * dist_h + 0.3 * dist_w

    est_width_m = (w_pixel * distance_m) / FOCAL_LENGTH
    est_height_m = (h_pixel * distance_m) / FOCAL_LENGTH

    # 2. Vẽ Bounding Box cảnh báo trên màn hình máy tính
    color = (0, 0, 255) if distance_m < SAFE_DISTANCE else (0, 255, 0)
    cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)

    text_info = f"Person | Dist: {distance_m:.2f}m | Sz: {est_width_m:.2f}x{est_height_m:.2f}m"
    cv2.putText(
        frame,
        text_info,
        (x1, max(25, y1 - 10)),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.55,
        color,
        2,
    )

    # 3. Gửi lệnh qua API của camera để camera tự động bắn UART ra cổng Grove xuống YoloUNO
    # (Giới hạn tần suất gửi 2 lần/giây để tránh nghẽn mạng Wi-Fi của camera)
    if time.time() - last_send_time > 0.5:
      try:
        # Chuỗi định dạng kết quả theo đúng cấu trúc firmware gốc: class:<Tên>,<Độ_tự_tin>
        label_str = f"DIST_{distance_m:.1f}m"
        conf_pct = int(conf * 100)
        requests.get(
            f"{CONTROL_URL}?serial={label_str};{conf_pct};stop", timeout=0.2
        )
        print(
            f"[UART TX qua Camera] Đã đẩy dữ liệu cách {distance_m:.2f}m sang"
            " YoloUNO"
        )
        last_send_time = time.time()
      except Exception as e:
        pass  # Bỏ qua nếu gói tin bị trễ mạng tạm thời

  cv2.imshow("Project 3 - AI Vision Camera & Pedestrian Tracking", frame)
  if cv2.waitKey(1) & 0xFF == ord("q"):
    break

cap.release()
cv2.destroyAllWindows()