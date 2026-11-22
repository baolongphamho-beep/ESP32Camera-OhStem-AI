"""Read scale/power telemetry over SSV1 without OpenCV or a camera."""
import argparse
import time

from usb_protocol import Parser
from usb_viewer import format_metric


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
        print("No scale telemetry. Check port and flash the firmware.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
