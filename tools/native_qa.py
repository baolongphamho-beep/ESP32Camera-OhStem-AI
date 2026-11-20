"""TV5 offline QA for native SSV1 binary captures or metadata JSONL.

This reports measured packet evidence, not physical scale/servo QA sign-off.
Host receive times include serial/network scheduling and are not UNO millis().
"""
import argparse
import csv
import json
from pathlib import Path
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "camera"))
from usb_protocol import Parser, SessionFilter
from telemetry import number

TIMINGS = ("processing_ms", "capture_ms", "decode_ms", "preprocess_ms",
           "inference_ms", "postprocess_ms")
CSV_FIELDS = ("packet", "host_ms", "kind", "phase", "session_id", "accepted",
              "preview", "jpeg_bytes", "people_count", *TIMINGS,
              "height_cm", "weight_kg", "weight_stable", "bmi", "free_psram")


def load_records(path, fmt="auto"):
    data = Path(path).read_bytes()
    if fmt == "auto":
        fmt = "jsonl" if Path(path).suffix.lower() in {".jsonl", ".json"} else "ssv1"
    if fmt == "ssv1":
        parser, rows = Parser(), []
        for offset in range(0, len(data), 65536):
            rows.extend({"meta": meta, "jpeg_bytes": len(jpeg)}
                        for meta, jpeg in parser.feed(data[offset:offset + 65536]))
        if not rows:
            raise ValueError("No complete CRC-valid SSV1 packets. For metadata logs use --format jsonl.")
        return rows, "ssv1 (CRC-valid packets only; corrupt/truncated byte count unknown)"
    text = data.decode("utf-16" if data.startswith((b'\xff\xfe', b'\xfe\xff')) else "utf-8-sig")
    rows = []
    for index, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except (ValueError, TypeError) as error:
            raise ValueError(f"Invalid JSONL at line {index}") from error
        if not isinstance(row, dict):
            raise ValueError(f"Expected an object at line {index}")
        if "packets" in row and "last_frame" in row:
            raise ValueError("This is a probe summary, not a packet log. Record with --log first.")
        if row.get("kind") != "recording_start" and "meta" not in row:
            row = {"meta": row}  # Legacy bare JSON, without receive timestamps.
        if row.get("kind") != "recording_start" and not isinstance(row.get("meta"), dict):
            raise ValueError(f"Missing metadata at line {index}")
        rows.append(row)
    if not rows or not any("meta" in row for row in rows):
        raise ValueError("No packet metadata in the log")
    return rows, "jsonl (host receive times when recorded; no JPEG images)"


def analyze(rows, processing_max_ms=399, capture_max_ms=None):
    sessions = SessionFilter()
    counts, phases, frame_rows, export_rows = {}, {}, [], []
    stale, frames_with_id, missing_id, boots, malformed = 0, 0, 0, 0, 0
    last_time, previous_phase, phase_since = None, None, None
    observed_holds = []
    for row in rows:
        if row.get("kind") == "recording_start":
            sessions, last_time, previous_phase, phase_since = SessionFilter(), None, None, None
            continue
        meta = row["meta"]
        at = number(row.get("host_ms"))
        if "host_ms" in row and at is None:
            malformed += 1
        if at is not None:
            if at < 0 or last_time is not None and at < last_time:
                malformed += 1
            last_time = at
        kind = meta.get("kind")
        if not isinstance(kind, str):
            malformed += 1
            continue
        counts[kind] = counts.get(kind, 0) + 1
        accepted = sessions.accept(meta)
        out = {key: meta.get(key) for key in CSV_FIELDS if key in meta}
        out.update(packet=len(export_rows) + 1, host_ms=at, accepted=accepted,
                   jpeg_bytes=row.get("jpeg_bytes"))
        export_rows.append(out)
        if kind == "boot":
            boots += 1
            previous_phase, phase_since = None, None
        if not accepted:
            if kind == "frame":
                stale += 1
            continue
        phase = meta.get("phase")
        if isinstance(phase, str):
            phases[phase] = phases.get(phase, 0) + 1
            # Sampled phase duration is useful evidence, but is NOT a hold timer
            # measured from physical unload, which current metadata lacks.
            if phase != previous_phase:
                if previous_phase == "RESULT_HOLD" and at is not None and phase_since is not None and at >= phase_since:
                    observed_holds.append(at - phase_since)
                previous_phase, phase_since = phase, at
        if kind == "frame":
            sid = meta.get("session_id")
            if isinstance(sid, int) and not isinstance(sid, bool):
                frames_with_id += 1
            else:
                missing_id += 1
            for key in TIMINGS:
                if meta.get(key) is not None and (number(meta[key]) is None or meta[key] < 0):
                    malformed += 1
            frame_rows.append(meta)

    def metric(key):
        values = [p[key] for p in frame_rows if number(p.get(key)) is not None and p[key] >= 0]
        return {"samples": len(values), "min": min(values), "median": statistics.median(values),
                "max": max(values)} if values else {"samples": 0, "min": None, "median": None, "max": None}

    metrics = {key: metric(key) for key in TIMINGS}
    checks = []

    def check(name, status, detail):
        checks.append({"name": name, "status": status, "detail": detail})

    check("packet_data", "FAIL" if malformed else "PASS" if export_rows else "N/A",
          f"{len(export_rows)} packets; {malformed} invalid timing/metadata values")
    proc = metrics["processing_ms"]
    check("observed_processing", "N/A" if not proc["samples"] else
          "PASS" if proc["max"] <= processing_max_ms else "FAIL",
          f"{proc['samples']} frame samples; limit {processing_max_ms:g} ms; excludes camera fetch/USB/display")
    capture = metrics["capture_ms"]
    check("observed_capture", "N/A" if capture_max_ms is None or not capture["samples"] else
          "PASS" if capture["max"] <= capture_max_ms else "FAIL",
          "No threshold requested" if capture_max_ms is None else f"Limit {capture_max_ms:g} ms")
    check("frame_session_isolation", "FAIL" if stale else
          "PASS" if frames_with_id and not missing_id else "N/A",
          f"{stale} late/blocked frames; {missing_id} accepted frames without session_id. "
          "PASS covers observed packet IDs only, not correctness of BLE-to-person association.")
    check("boot_observation", "INFO", f"{boots} boot packets (absence is not proof of no reset)")
    # No timing of physical contact, reference heights, multi-person ground
    # truth or full brownout log exists in the current SSV1 packet contract.
    for name in ("QA1 physical wake", "QA2 height accuracy", "QA3 near/multiple ground truth",
                 "QA4 physical step-off", "QA5 hold after physical unload", "QA6 brownout/stress"):
        check(name, "N/A", "Requires field evidence; packet timing alone cannot sign off this case")
    return {"synthetic": any(row.get("synthetic") is True for row in rows),
            "counts": counts, "phases": phases, "frames": len(frame_rows), "boot_packets": boots,
            "metrics_ms": metrics, "observed_hold_phase_ms": observed_holds, "checks": checks,
            "limitations": ["host_ms is receive time on PC, not UNO millis or camera exposure time",
                            "Sampled phase duration is not physical unload-to-LCD hold duration",
                            "JSONL logs contain metadata only; source images are not retained"]}, export_rows


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--format", choices=("auto", "jsonl", "ssv1"), default="auto")
    parser.add_argument("--json", type=Path, help="Save summary report")
    parser.add_argument("--csv", type=Path, help="Export packet evidence, not inferred physical cycles")
    parser.add_argument("--processing-max-ms", type=float, default=399)
    parser.add_argument("--capture-max-ms", type=float)
    args = parser.parse_args(argv)
    for value in (args.processing_max_ms, args.capture_max_ms):
        if value is not None and (number(value) is None or value <= 0):
            parser.error("Timing thresholds must be finite and positive")
    try:
        rows, source = load_records(args.log, args.format)
        report, csv_rows = analyze(rows, args.processing_max_ms, args.capture_max_ms)
        report["source_format"] = source
        if args.json and args.csv and args.json.resolve() == args.csv.resolve():
            raise ValueError("JSON report and CSV output must be different files")
        for path in (args.json, args.csv):
            if path:
                if path.resolve() == args.log.resolve():
                    raise ValueError("Output must not overwrite the input log")
                path.parent.mkdir(parents=True, exist_ok=True)
        if args.json:
            args.json.write_text(json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False), encoding="utf-8")
        if args.csv:
            with args.csv.open("w", newline="", encoding="utf-8-sig") as f:
                writer = csv.DictWriter(f, fieldnames=CSV_FIELDS, extrasaction="ignore")
                writer.writeheader()
                writer.writerows(csv_rows)
    except (ValueError, OSError, UnicodeError) as error:
        print(f"Cannot analyze log: {error}", file=sys.stderr)
        return 2
    if report["synthetic"]:
        print("SYNTHETIC SAMPLE: not hardware evidence")
    print(f"Packets: {sum(report['counts'].values())}; accepted frames: {report['frames']}; source: {source}")
    for key, stats in report["metrics_ms"].items():
        if stats["samples"]:
            print(f"{key}: n={stats['samples']} min/median/max={stats['min']}/{stats['median']}/{stats['max']} ms")
    for item in report["checks"]:
        print(f"[{item['status']}] {item['name']}: {item['detail']}")
    return 1 if any(item["status"] == "FAIL" for item in report["checks"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())
