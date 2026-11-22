import time
import cv2
import numpy as np
import requests

from ultralytics import YOLO


CAMERA_IP = "192.168.4.1"

STREAM_URL = f"http://{CAMERA_IP}/stream"
CAPTURE_URL = f"http://{CAMERA_IP}/capture"
CONTROL_URL = f"http://{CAMERA_IP}/control"

MODEL_PATH = "yolo26n.pt"

CONFIDENCE = 0.50

DEADBAND_PX = 8

STABLE_FRAMES = 6

COMMAND_INTERVAL = 0.18

EDGE_MARGIN_PX = 4


PHASE_FOOT = "FOOT"

PHASE_HEAD = "HEAD"

PHASE_DONE = "DONE"


model = YOLO(MODEL_PATH)

session = requests.Session()


def send_command(label, confidence=100):

    try:

        session.get(
            CONTROL_URL,
            params={
                "serial":
                f"{label};{confidence};stop"
            },
            timeout=0.35
        )

    except requests.RequestException:

        pass


def get_capture_frame():

    try:

        response = session.get(
            CAPTURE_URL,
            params={
                "_cb":
                int(time.time() * 1000)
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


def choose_person(results):

    best = None

    best_area = 0

    for result in results:

        for box in result.boxes:

            x1, y1, x2, y2 = map(
                int,
                box.xyxy[0]
            )

            confidence = float(
                box.conf[0]
            )

            area = (
                max(1, x2 - x1)
                *
                max(1, y2 - y1)
            )

            if area > best_area:

                best_area = area

                best = (
                    x1,
                    y1,
                    x2,
                    y2,
                    confidence
                )

    return best


cap = cv2.VideoCapture(
    STREAM_URL
)

use_stream = cap.isOpened()


phase = PHASE_FOOT

stable_count = 0

last_command_time = 0.0


print(
    "Automatic Height Tracking"
)

print(
    "R : reset"
)

print(
    "Q : quit"
)


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

        time.sleep(0.1)

        continue


    h, w = frame.shape[:2]

    center_x = w // 2

    center_y = h // 2


    cv2.line(
        frame,
        (
            center_x - 35,
            center_y
        ),
        (
            center_x + 35,
            center_y
        ),
        (0, 255, 255),
        2
    )

    cv2.line(
        frame,
        (
            center_x,
            center_y - 35
        ),
        (
            center_x,
            center_y + 35
        ),
        (0, 255, 255),
        2
    )

    cv2.circle(
        frame,
        (
            center_x,
            center_y
        ),
        4,
        (0, 0, 255),
        -1
    )


    results = model(
        frame,
        classes=[0],
        conf=CONFIDENCE,
        verbose=False
    )


    person = choose_person(
        results
    )


    now = time.time()

    command = None

    target = None


    if person is not None:

        (
            x1,
            y1,
            x2,
            y2,
            confidence
        ) = person


        cv2.rectangle(
            frame,
            (x1, y1),
            (x2, y2),
            (0, 255, 0),
            2
        )


        if phase == PHASE_FOOT:

            foot_x = (
                x1 + x2
            ) // 2

            foot_y = y2

            target = (
                foot_x,
                foot_y
            )


            error_y = (
                foot_y
                -
                center_y
            )


            if abs(error_y) <= DEADBAND_PX:

                stable_count += 1

                command = (
                    "FOOT_CENTER"
                )

            else:

                stable_count = 0

                if error_y < 0:

                    command = (
                        "FOOT_ABOVE"
                    )

                else:

                    command = (
                        "FOOT_BELOW"
                    )


            if (
                stable_count
                >=
                STABLE_FRAMES
            ):

                send_command(
                    "FOOT_CENTER",
                    int(
                        confidence
                        *
                        100
                    )
                )

                phase = PHASE_HEAD

                stable_count = 0

                last_command_time = now


        elif phase == PHASE_HEAD:

            if (
                y1
                <=
                EDGE_MARGIN_PX
            ):

                stable_count = 0

                command = (
                    "HEAD_SEARCH_UP"
                )

            else:

                head_x = (
                    x1 + x2
                ) // 2

                head_y = y1

                target = (
                    head_x,
                    head_y
                )


                error_y = (
                    head_y
                    -
                    center_y
                )


                if (
                    abs(error_y)
                    <=
                    DEADBAND_PX
                ):

                    stable_count += 1

                    command = (
                        "HEAD_CENTER"
                    )

                else:

                    stable_count = 0

                    if error_y < 0:

                        command = (
                            "HEAD_ABOVE"
                        )

                    else:

                        command = (
                            "HEAD_BELOW"
                        )


                if (
                    stable_count
                    >=
                    STABLE_FRAMES
                ):

                    send_command(
                        "HEAD_CENTER",
                        int(
                            confidence
                            *
                            100
                        )
                    )

                    phase = PHASE_DONE

                    stable_count = 0

                    last_command_time = now


        if target is not None:

            cv2.circle(
                frame,
                target,
                6,
                (255, 0, 255),
                -1
            )


    else:

        stable_count = 0

        if phase == PHASE_HEAD:

            command = (
                "HEAD_SEARCH_UP"
            )


    if (
        command is not None
        and
        phase != PHASE_DONE
        and
        now - last_command_time
        >=
        COMMAND_INTERVAL
    ):

        send_command(
            command
        )

        last_command_time = now


    cv2.putText(
        frame,
        f"Phase: {phase}",
        (10, 25),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.65,
        (255, 255, 255),
        2
    )


    if command is not None:

        cv2.putText(
            frame,
            f"State: {command}",
            (10, 52),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.55,
            (255, 255, 255),
            2
        )


    if phase == PHASE_DONE:

        cv2.putText(
            frame,
            "DONE - press R",
            (10, 80),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.55,
            (0, 255, 0),
            2
        )


    cv2.imshow(
        "Project 3 - Height Tracking",
        frame
    )


    key = cv2.waitKey(1) & 0xFF


    if key == ord("r"):

        phase = PHASE_FOOT

        stable_count = 0

        send_command(
            "RESET_HEIGHT"
        )


    elif key == ord("q"):

        break


if cap.isOpened():

    cap.release()


cv2.destroyAllWindows()