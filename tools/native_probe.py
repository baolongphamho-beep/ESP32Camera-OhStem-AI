"""Read native SSV1 health/status without changing configuration or running PC AI."""
import argparse
import json
import math
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "camera"))
from serial_port import add_port_argument, port_or_exit
from usb_protocol import Parser
from telemetry import PacketRecorder


def main():
    import serial

    args_parser = argparse.ArgumentParser(description=__doc__)
    add_port_argument(args_parser)
    args_parser.add_argument("--seconds", type=float, default=20)
    args_parser.add_argument("--output", type=Path)
    args_parser.add_argument("--log", type=Path, help="Append metadata-only QA JSONL with host receive times")
    args_parser.add_argument("--preview", action="store_true", help="Lease camera-only YOLO test mode")
    args_parser.add_argument("--image", type=Path, help="Save the first received JPEG locally")
    args_parser.add_argument("--person-image", type=Path, help="Save the first JPEG with a person locally")
    args = args_parser.parse_args()
    if not math.isfinite(args.seconds) or args.seconds <= 0:
        args_parser.error("--seconds must be finite and positive")
    args.port = port_or_exit(args.port)
    parser = Parser()
    packets = []
    received = 0
    frame_count = 0
    person_saved = False
    with PacketRecorder(args.log) as recorder, serial.Serial(args.port, 115200, timeout=.25) as device:
        deadline = time.monotonic() + args.seconds
        renew_at = 0
        try:
            while time.monotonic() < deadline:
                if args.preview and time.monotonic() >= renew_at:
                    device.write(b"PREVIEW ON\n")
                    renew_at = time.monotonic() + 3
                data = device.read(min(max(device.in_waiting, 1), 65536))
                received += len(data)
                for metadata, jpeg in parser.feed(data):
                    recorder.write(metadata, len(jpeg))
                    packets.append(metadata)
                    if metadata.get("kind") == "frame" and jpeg:
                        frame_count += 1
                        if args.image and frame_count == 1:
                            args.image.parent.mkdir(parents=True, exist_ok=True)
                            args.image.write_bytes(jpeg)
                        if args.person_image and metadata.get("boxes") and not person_saved:
                            args.person_image.parent.mkdir(parents=True, exist_ok=True)
                            args.person_image.write_bytes(jpeg)
                            person_saved = True
                    if len(packets) == 1 or metadata.get("kind") == "boot" or frame_count == 1 and jpeg:
                        print(json.dumps(metadata, ensure_ascii=True), flush=True)
        finally:
            if args.preview:
                device.write(b"PREVIEW OFF\n")
                device.flush()
    phases = sorted({p["phase"] for p in packets if isinstance(p.get("phase"), str)})
    frame_timings = [{key: p.get(key) for key in
                     ("processing_ms", "capture_ms", "decode_ms", "preprocess_ms", "inference_ms", "postprocess_ms")}
                    for p in packets if p.get("kind") == "frame"]
    result = {"port": args.port, "seconds": args.seconds, "bytes": received,
              "packets": len(packets), "frames": frame_count, "phases": phases,
              "frame_timings": frame_timings,
              "person_frames": sum(p.get("kind") == "frame" and bool(p.get("boxes")) for p in packets),
              "last_person_frame": next((p for p in reversed(packets) if p.get("kind") == "frame" and p.get("boxes")), None),
              "last": packets[-1] if packets else None,
              "last_frame": next((p for p in reversed(packets) if p.get("kind") == "frame"), None),
              "messages": sorted({p["message"] for p in packets if isinstance(p.get("message"), str)}),
              "boot": [p for p in packets if p.get("kind") == "boot"]}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=True))
    return 0 if (frame_count if args.preview else phases) else 1


if __name__ == "__main__":
    raise SystemExit(main())
