"""Height error table for the report, from docs/QA_FIELD_LOG.csv (filled in at the lab).

Reads every row with both H_thuoc_cm (tape measure) and H_he_thong_cm (system result),
computes delta = system - tape, and prints bias (mean error), MAE, RMSE, worst |error|,
share within +-2 / +-5 cm, overall and per group (nhom) and per distance. Repeated
measurements of one person ("lap_lai") also give the spread (repeatability).
Decimal commas ("170,5") are accepted. Rows without both heights are skipped and counted.

  py -3 tools/height_error_report.py docs/QA_FIELD_LOG.csv
  py -3 tools/height_error_report.py docs/QA_FIELD_LOG.csv --md .edge-deps/qa/height-errors.md

Exit code 1 if no row has both heights yet (nothing measured), 0 otherwise. The numbers
come only from the CSV; nothing is simulated.
"""
import argparse
import csv
import math
import statistics
import sys
from pathlib import Path


def number(text):
    if text is None:
        return None
    text = str(text).strip().replace(",", ".")
    if not text:
        return None
    try:
        value = float(text)
    except ValueError:
        return None
    return value if math.isfinite(value) else None


def load(path):
    with open(path, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    measured, skipped, mismatched = [], 0, []
    for row in rows:
        tape, system = number(row.get("H_thuoc_cm")), number(row.get("H_he_thong_cm"))
        if tape is None or system is None:
            skipped += 1
            continue
        delta = system - tape
        written = number(row.get("delta_H_cm"))
        if written is not None and abs(written - delta) > 0.05:
            mismatched.append((row.get("ca", "?"), written, round(delta, 2)))
        measured.append({"ca": row.get("ca", ""), "nhom": row.get("nhom", ""),
                         "distance": number(row.get("khoang_cach_m")), "tape": tape,
                         "system": system, "delta": delta})
    return measured, skipped, mismatched


def stats(deltas):
    n = len(deltas)
    abs_d = [abs(d) for d in deltas]
    return {"n": n,
            "bias": statistics.fmean(deltas),
            "mae": statistics.fmean(abs_d),
            "rmse": math.sqrt(statistics.fmean(d * d for d in deltas)),
            "worst": max(abs_d),
            "sd": statistics.stdev(deltas) if n > 1 else 0.0,
            "within2": sum(a <= 2 for a in abs_d) / n,
            "within5": sum(a <= 5 for a in abs_d) / n}


def table(title, groups):
    lines = [f"### {title}", "",
             "| Nhóm | n | Lệch TB (cm) | MAE (cm) | RMSE (cm) | Lệch lớn nhất (cm) | Độ lệch chuẩn (cm) | ≤ ±2 cm | ≤ ±5 cm |",
             "|---|---|---|---|---|---|---|---|---|"]
    for name, deltas in groups:
        s = stats(deltas)
        lines.append(f"| {name} | {s['n']} | {s['bias']:+.1f} | {s['mae']:.1f} | {s['rmse']:.1f} | {s['worst']:.1f} | "
                     f"{s['sd']:.1f} | {s['within2']:.0%} | {s['within5']:.0%} |")
    return lines + [""]


def report(measured, skipped, mismatched, source):
    lines = [f"## Sai số chiều cao (nguồn: `{source}`)", "",
             f"{len(measured)} ca có cả chiều cao thước và chiều cao hệ thống; {skipped} dòng chưa đủ số liệu, bỏ qua.",
             "Lệch = hệ thống − thước dây (dương: hệ thống đo cao hơn).", ""]
    lines += table("Tổng hợp", [("Tất cả", [m["delta"] for m in measured])])
    by_group = {}
    for m in measured:
        by_group.setdefault(m["nhom"] or "(không ghi)", []).append(m["delta"])
    lines += table("Theo nhóm ca", sorted(by_group.items()))
    by_distance = {}
    for m in measured:
        key = f"{m['distance']:g} m" if m["distance"] is not None else "(không ghi)"
        by_distance.setdefault(key, []).append(m["delta"])
    lines += table("Theo khoảng cách", sorted(by_distance.items()))
    lines += ["### Từng ca", "", "| Ca | Nhóm | Khoảng cách (m) | Thước (cm) | Hệ thống (cm) | Lệch (cm) |",
              "|---|---|---|---|---|---|"]
    for m in measured:
        dist = f"{m['distance']:g}" if m["distance"] is not None else ""
        lines.append(f"| {m['ca']} | {m['nhom']} | {dist} | {m['tape']:g} | {m['system']:g} | {m['delta']:+.1f} |")
    if mismatched:
        lines += ["", "**Cột delta_H_cm ghi tay khác giá trị tính lại:** " +
                  ", ".join(f"{ca} (ghi {w:g}, tính {c:g})" for ca, w, c in mismatched)]
    return lines


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="?", default="docs/QA_FIELD_LOG.csv")
    ap.add_argument("--md", help="also write the Markdown table to this file")
    args = ap.parse_args(argv)
    if not Path(args.csv).is_file():
        ap.error(f"CSV not found: {args.csv}")
    measured, skipped, mismatched = load(args.csv)
    if not measured:
        print(f"Chua co ca nao du H_thuoc_cm va H_he_thong_cm trong {args.csv} ({skipped} dong trong).")
        return 1
    text = "\n".join(report(measured, skipped, mismatched, args.csv))
    print(text)
    if args.md:
        Path(args.md).parent.mkdir(parents=True, exist_ok=True)
        Path(args.md).write_text(text + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
