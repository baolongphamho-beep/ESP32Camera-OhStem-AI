import time
import cv2
import numpy as np
import requests

CAMERA_IP = "192.168.4.1"

STREAM_URL = f"http://{CAMERA_IP}/stream"
CAPTURE_URL = f"http://{CAMERA_IP}/capture"
CONTROL_URL = f"http://{CAMERA_IP}/control"

session = requests.Session()


def send_command(label, confidence=100):
    try:
        session.get(
            CONTROL_URL,
            params={
                "serial": f"{label};{confidence};stop"
            },
            timeout=0.4
        )
    except requests.RequestException:
        pass


def get_capture_frame():
    try:
        response = session.get(
            CAPTURE_URL,
            params={
                "_cb": int(time.time() * 1000)
            },
            timeout=1.0
        )

        response.raise_for_status()

        data = np.frombuffer(
            response.content,
            dtype=np.uint8
        )

        frame = cv2.imdecode(
            data,
            cv2.IMREAD_COLOR
        )

        return frame

    except Exception:
        return None


cap = cv2.VideoCapture(STREAM_URL)

use_stream = cap.isOpened()

print("MANUAL HEIGHT TEST")
print("W : quay camera len")
print("S : quay camera xuong")
print("F : luu goc chan")
print("H : luu goc dinh dau va tinh chieu cao")
print("R : reset phep do")
print("Q : thoat")


while True:

    frame = None

    if use_stream:
        ok, frame = cap.read()

        if not ok:
            use_stream = False
            cap.release()

    if frame is None:
        frame = get_capture_frame()

    if frame is None:

        frame = np.zeros(
            (240, 320, 3),
            dtype=np.uint8
        )

        cv2.putText(
            frame,
            "Khong doc duoc camera",
            (20, 120),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.55,
            (255, 255, 255),
            1
        )

    h, w = frame.shape[:2]

    center_x = w // 2
    center_y = h // 2

    cv2.line(
        frame,
        (center_x - 30, center_y),
        (center_x + 30, center_y),
        (0, 255, 255),
        2
    )

    cv2.line(
        frame,
        (center_x, center_y - 30),
        (center_x, center_y + 30),
        (0, 255, 255),
        2
    )

    cv2.circle(
        frame,
        (center_x, center_y),
        4,
        (0, 0, 255),
        -1
    )

    cv2.putText(
        frame,
        "W/S Servo | F Foot | H Head | R Reset | Q Quit",
        (10, 25),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.45,
        (0, 255, 0),
        1
    )

    cv2.imshow(
        "Manual Height Measurement",
        frame
    )

    key = cv2.waitKey(20) & 0xFF

    if key == ord("w"):

        send_command("MANUAL_UP")

    elif key == ord("s"):

        send_command("MANUAL_DOWN")

    elif key == ord("f"):

        send_command("MARK_FOOT")

        print(
            "Da gui lenh luu goc chan"
        )

    elif key == ord("h"):

        send_command("MARK_HEAD")

        print(
            "Da gui lenh luu goc dau"
        )

    elif key == ord("r"):

        send_command("RESET_HEIGHT")

        print(
            "Da reset phep do"
        )

    elif key == ord("q"):

        break


if cap.isOpened():
    cap.release()

cv2.destroyAllWindows()