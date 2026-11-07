"""Read structured MCU diagnostics without GUI/inference or configuration changes."""
import argparse
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "camera"))
from usb_protocol import Parser


def main():
    import serial
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM9")
    parser.add_argument("--seconds", type=float, default=15)
    parser.add_argument("--output", type=Path, default=ROOT / "firmware/edge_yolo/build/device_probe.json")
    args = parser.parse_args()
    decoder = Parser()
    records = []
    until = time.monotonic() + args.seconds
    with serial.Serial(args.port, 115200, timeout=.1) as port:
        while time.monotonic() < until:
            for metadata, jpeg in decoder.feed(port.read(min(max(port.in_waiting, 1), 65536))):
                records.append(metadata)
                if len(records) == 1 or metadata != records[-2]:
                    print(json.dumps(metadata), flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(records, indent=2), encoding="utf-8")
    if not records:
        print("No valid USB packets; verify firmware boot and port ownership.")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
