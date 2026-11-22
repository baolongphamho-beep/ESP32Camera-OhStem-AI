"""Project 3: phát hiện người đi bộ trước đầu xe, vẽ bounding box, ước lượng
kích thước + khoảng cách, cảnh báo khi gần hơn SAFE_DISTANCE.

Kết quả gửi qua camera -> UART -> YOLO UNO (env yolo_uno_camera_lcd) để hiện lên LCD:
  PED_WARN_D1.80 (gần hơn SAFE_DISTANCE) / PED_SAFE_D3.20 / NO_PEOPLE
Chạy: .\\.venv-camera\\Scripts\\python.exe pedestrian_distance.py [IP camera]
"""
from pathlib import Path
import sys
import time

import cv2
import requests  # Thư viện gọi HTTP API tới camera
from ultralytics import YOLO

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "camera"))
from protocol import pedestrian_label, serial_control_url, ultralytics_model_path  # noqa: E402

# =====================================================================
# CẤU HÌNH KẾT NỐI CAMERA AI VISION OHSTEM QUA WI-FI & UART
# =====================================================================
# IP camera: truyền ở dòng lệnh, hoặc sửa giá trị mặc định này.
CAMERA_IP = sys.argv[1] if len(sys.argv) > 1 else "10.127.7.68"
BASE_URL = f"http://{CAMERA_IP}"
STREAM_URL = f"{BASE_URL}/stream"

FOCAL_LENGTH = 600.0  # Tiêu cự F (pixel)
REF_PERSON_HEIGHT = 1.65  # Chiều cao trung bình người đi bộ (m)
REF_SHOULDER_WIDTH = 0.45  # Bề ngang trung bình (m)
SAFE_DISTANCE = 2.5  # Khoảng cách cảnh báo nguy hiểm (m); LCD hiện "!! TOO CLOSE !!"
SEND_INTERVAL = 0.5  # Giây giữa hai lần gửi sang YOLO UNO (2 lần/giây)

# Tải mô hình YOLO (chạy trên máy tính để xử lý đồ họa bounding box)
model = YOLO(ultralytics_model_path(ROOT / "yolo26n.pt"))
session = requests.Session()


def send_to_uno(label, confidence):
  """Gửi nhãn qua /control; camera chuyển ra UART sang YOLO UNO."""
  try:
    session.get(serial_control_url(BASE_URL, label, confidence), timeout=0.3)
    return True
  except requests.RequestException:
    return False  # Bỏ qua nếu gói tin bị trễ mạng tạm thời

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

  # Chọn người GẦN NHẤT (box cao nhất), vì cảnh báo cần người gần xe nhất.
  current_person = None
  for r in results:
    for box in r.boxes:
      x1, y1, x2, y2 = map(int, box.xyxy[0])
      w_pixel = max(1, x2 - x1)
      h_pixel = max(1, y2 - y1)
      conf = float(box.conf[0])
      if current_person is None or h_pixel > current_person[5]:
        current_person = (x1, y1, x2, y2, w_pixel, h_pixel, conf)

  send_now = time.time() - last_send_time >= SEND_INTERVAL

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

    # 3. Gửi kết quả qua /control; camera bắn UART ra cổng Grove xuống YOLO UNO.
    #    Độ tin cậy gửi dạng 0..1 (UNO hiểu số sau ';' là xác suất).
    #    Giới hạn 2 lần/giây để tránh nghẽn Wi-Fi của camera.
    if send_now:
      label = pedestrian_label(distance_m, SAFE_DISTANCE)
      if send_to_uno(label, conf):
        print(f"[UART TX qua Camera] {label}")
      last_send_time = time.time()

  elif send_now:
    # Không thấy ai: báo cho UNO để LCD không giữ khoảng cách cũ.
    if send_to_uno("NO_PEOPLE", 1.0):
      print("[UART TX qua Camera] NO_PEOPLE")
    last_send_time = time.time()

  cv2.imshow("Project 3 - AI Vision Camera & Pedestrian Tracking", frame)
  if cv2.waitKey(1) & 0xFF == ord("q"):
    break

cap.release()
cv2.destroyAllWindows()