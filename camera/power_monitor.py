"""Read scale/power telemetry over SSV1 without OpenCV or a camera.

Merged on branch feat/ui-viewer-qa (TV5, 22/11): the smartscale-edge-ai version
(telemetry_change_key, format_telemetry_line, open_serial_no_reset) plus the TV5
additions (--port auto, --log metadata-only JSONL, finite --seconds check).
"""
import argparse
import math
import time

from serial_port import add_port_argument, port_or_exit
from telemetry import PacketRecorder
from usb_protocol import Parser


def format_metric(value, unit="", fmt=".1f"):
    if value is None or isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        return "--"
    formatted = format(value, fmt)
    return f"{formatted} {unit}" if unit else formatted


def open_serial_no_reset(name):
    import serial
    port = serial.Serial(None, 115200, timeout=0.25, dsrdtr=False, rtscts=False)
    port.port = name
    port.dtr = False
    port.rts = False
    port.open()
    return port


def telemetry_change_key(meta):
    base = tuple(meta.get(k) for k in (
        "phase", "cpu_mhz", "wifi_ps", "wifi_started",
        "light_sleep_enabled", "usb_sleep_blocked", "no_ls_locks",
        "ble_duty_percent", "weight_stable", "lcd_backlight", "hold_sec"))
    if meta.get("phase") == "RESULT_HOLD":
        return base + (meta.get("height_cm"), meta.get("bmi"))
    return base


def format_telemetry_line(meta):
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
    lcd = ("ON" if meta.get("lcd_backlight") is True
           else ("OFF" if meta.get("lcd_backlight") is False else "?"))
    extra = ""
    if meta.get("phase") == "RESULT_HOLD" or (
            meta.get("height_cm") is not None and meta.get("bmi") is not None):
        h_text = format_metric(meta.get("height_cm"), "cm", ".1f")
        bmi_text = format_metric(meta.get("bmi"), "", ".2f")
        hold_sec = meta.get("hold_sec")
        hold_text = f" HOLD={hold_sec}s" if isinstance(hold_sec, int) and not isinstance(hold_sec, bool) else ""
        extra = f" H={h_text} BMI={bmi_text}{hold_text}"
    return (
        f"[{meta.get('phase', '?')}] CPU={meta.get('cpu_mhz', '?')}MHz "
        f"BLE={meta.get('ble_duty_percent', '?')}% "
        f"W={meta.get('weight_kg')}kg LOCK={meta.get('weight_stable')} "
        f"LCD={lcd}{extra} | "
        f"WiFi={wifi} | LightSleep={sleep} "
        f"locks={meta.get('no_ls_locks', '?')}"
    )


def main():
    import serial

    args_parser = argparse.ArgumentParser(description=__doc__)
    add_port_argument(args_parser)
    args_parser.add_argument("--seconds", type=float, default=0,
                             help="stop after N seconds; 0 means until Ctrl+C")
    args_parser.add_argument("--log", help="Append image-free metadata QA JSONL")
    args = args_parser.parse_args()
    if not math.isfinite(args.seconds) or args.seconds < 0:
        args_parser.error("--seconds must be nonnegative")
    args.port = port_or_exit(args.port)

    parser = Parser()
    previous = None
    received = 0
    try:
        with PacketRecorder(args.log) as recorder, open_serial_no_reset(args.port) as port:
            started = time.monotonic()
            print("Reading power telemetry; Ctrl+C stops this monitor only.", flush=True)
            while not args.seconds or time.monotonic() - started < args.seconds:
                for meta, _ in parser.feed(port.read(4096)):
                    recorder.write(meta)
                    if meta.get("kind") != "scale":
                        continue
                    received += 1
                    key = telemetry_change_key(meta)
                    if key == previous:
                        continue
                    previous = key
                    print(format_telemetry_line(meta), flush=True)
    except KeyboardInterrupt:
        pass
    except serial.SerialException as exc:
        print(f"Serial error: {exc}")
        return 1
    if not received:
        print("No kind=scale telemetry. Check port and firmware compatibility.")
        print("The Plan 2 servo firmware sends kind=status; this monitor needs TV2 scale telemetry.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
