"""Read scale/power telemetry over SSV1 without OpenCV or a camera."""
import argparse
import time

import serial

from usb_protocol import Parser


def open_serial_no_reset(name):
    port = serial.Serial(None, 115200, timeout=0.25, dsrdtr=False, rtscts=False)
    port.port = name
    port.dtr = False
    port.rts = False
    port.open()
    return port


def main():
    args_parser = argparse.ArgumentParser(description=__doc__)
    args_parser.add_argument("--port", required=True)
    args_parser.add_argument("--seconds", type=float, default=0,
                             help="stop after N seconds; 0 means until Ctrl+C")
    args = args_parser.parse_args()
    if args.seconds < 0:
        args_parser.error("--seconds must be nonnegative")

    parser = Parser()
    previous = None
    received = 0
    try:
        with open_serial_no_reset(args.port) as port:
            started = time.monotonic()
            print("Reading power telemetry; Ctrl+C stops this monitor only.", flush=True)
            while not args.seconds or time.monotonic() - started < args.seconds:
                for meta, _ in parser.feed(port.read(4096)):
                    if meta.get("kind") != "scale":
                        continue
                    received += 1
                    key = tuple(meta.get(k) for k in (
                        "phase", "cpu_mhz", "wifi_ps", "wifi_started",
                        "light_sleep_enabled", "usb_sleep_blocked", "no_ls_locks",
                        "ble_duty_percent", "weight_stable"))
                    if key == previous:
                        continue
                    previous = key
                    if not meta.get("light_sleep_enabled"):
                        sleep = "not configured"
                    elif meta.get("usb_sleep_blocked"):
                        sleep = "configured, blocked by USB"
                    elif meta.get("no_ls_locks", -1) > 0:
                        sleep = "configured, blocked by PM locks"
                    else:
                        sleep = "configured (actual sleep not measured)"
                    wifi = meta.get("wifi_ps", "?")
                    if not meta.get("wifi_started"):
                        wifi += " (Wi-Fi not started)"
                    print(
                        f"[{meta.get('phase', '?')}] CPU={meta.get('cpu_mhz', '?')}MHz "
                        f"BLE={meta.get('ble_duty_percent', '?')}% "
                        f"W={meta.get('weight_kg')}kg LOCK={meta.get('weight_stable')} | "
                        f"WiFi={wifi} | LightSleep={sleep} "
                        f"locks={meta.get('no_ls_locks', '?')}", flush=True)
    except KeyboardInterrupt:
        pass
    except serial.SerialException as exc:
        print(f"Serial error: {exc}")
        return 1
    if not received:
        print("No scale telemetry. Check port and flash the firmware.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
