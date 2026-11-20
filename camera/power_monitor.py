"""Read scale/power telemetry over SSV1 without OpenCV or a camera."""
import argparse
import math
import time

import serial

from usb_protocol import Parser
from telemetry import PacketRecorder, power_lines, display


def main():
    args_parser = argparse.ArgumentParser(description=__doc__)
    args_parser.add_argument("--port", required=True)
    args_parser.add_argument("--seconds", type=float, default=0,
                             help="stop after N seconds; 0 means until Ctrl+C")
    args_parser.add_argument("--log", help="Append image-free metadata QA JSONL")
    args = args_parser.parse_args()
    if not math.isfinite(args.seconds) or args.seconds < 0:
        args_parser.error("--seconds must be nonnegative")

    parser = Parser()
    previous = None
    received = 0
    try:
        with PacketRecorder(args.log) as recorder, serial.Serial(args.port, 115200, timeout=0.25) as port:
            started = time.monotonic()
            print("Reading power telemetry; Ctrl+C stops this monitor only.", flush=True)
            while not args.seconds or time.monotonic() - started < args.seconds:
                for meta, _ in parser.feed(port.read(4096)):
                    recorder.write(meta)
                    if meta.get("kind") != "scale":
                        continue
                    received += 1
                    key = tuple(meta.get(k) for k in (
                        "phase", "cpu_mhz", "wifi_ps", "wifi_started",
                        "light_sleep_enabled", "usb_sleep_blocked", "no_ls_locks",
                        "ble_duty_percent", "weight_stable", "weight_kg", "height_cm", "bmi"))
                    if key == previous:
                        continue
                    previous = key
                    print(
                        f"[{meta.get('phase', '?')}] CPU={meta.get('cpu_mhz', '?')}MHz "
                        f"BLE={meta.get('ble_duty_percent', '?')}% "
                        f"W={display(meta.get('weight_kg'), ' kg')} LOCK={meta.get('weight_stable')} | "
                        + " | ".join(power_lines(meta)), flush=True)
    except KeyboardInterrupt:
        pass
    except serial.SerialException as exc:
        print(f"Serial error: {exc}")
        return 1
    if not received:
        print("No kind=scale telemetry. Check port and firmware compatibility.")
        print("Local Plan 2 firmware currently sends kind=status; this monitor needs TV2 telemetry.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
