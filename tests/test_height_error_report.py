import csv
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import height_error_report as her  # noqa: E402

HEADER = ["ca", "nhom", "kich_ban", "nguoi", "khoang_cach_m", "H_thuoc_cm", "H_he_thong_cm", "delta_H_cm"]


def write_csv(folder, rows):
    path = pathlib.Path(folder) / "log.csv"
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(HEADER)
        w.writerows(rows)
    return path


class HeightErrorReportTests(unittest.TestCase):
    def test_stats_bias_mae_rmse(self):
        s = her.stats([2.0, -2.0, 4.0])
        self.assertAlmostEqual(s["bias"], 4 / 3)
        self.assertAlmostEqual(s["mae"], 8 / 3)
        self.assertAlmostEqual(s["rmse"], (24 / 3) ** 0.5)
        self.assertEqual(s["worst"], 4.0)
        self.assertAlmostEqual(s["within2"], 2 / 3)
        self.assertEqual(s["within5"], 1.0)

    def test_load_skips_empty_accepts_decimal_comma_and_flags_wrong_delta(self):
        with tempfile.TemporaryDirectory() as d:
            path = write_csv(d, [["F01", "chieu_cao", "", "", "3.0", "170,5", "172", ""],
                                 ["F02", "chieu_cao", "", "", "3.0", "", "", ""],
                                 ["F03", "khoang_cach", "", "", "2.6", "165", "163", "+5"],
                                 ["F04", "chieu_cao", "", "", "3.0", "abc", "160", ""]])
            measured, skipped, mismatched = her.load(path)
        self.assertEqual([m["ca"] for m in measured], ["F01", "F03"])
        self.assertAlmostEqual(measured[0]["delta"], 1.5)
        self.assertEqual(skipped, 2)
        self.assertEqual(mismatched, [("F03", 5.0, -2.0)])

    def test_report_groups_and_markdown_file(self):
        with tempfile.TemporaryDirectory() as d:
            path = write_csv(d, [["F01", "chieu_cao", "", "", "3.0", "170", "171", "1"],
                                 ["F07", "khoang_cach", "", "", "2.6", "170", "167", "-3"],
                                 ["F11", "lap_lai", "", "", "3.0", "170", "172", "2"]])
            md = pathlib.Path(d) / "out" / "h.md"
            with mock.patch("builtins.print"):
                self.assertEqual(her.main([str(path), "--md", str(md)]), 0)
            text = md.read_text(encoding="utf-8")
        self.assertIn("| Tất cả | 3 |", text)
        self.assertIn("| lap_lai | 1 |", text)
        self.assertIn("| 2.6 m | 1 | -3.0 |", text)
        self.assertIn("| F07 | khoang_cach | 2.6 | 170 | 167 | -3.0 |", text)

    def test_empty_template_exits_1(self):
        with mock.patch("builtins.print") as printed:
            self.assertEqual(her.main([str(ROOT / "docs" / "QA_FIELD_LOG.csv")]), 1)
        self.assertIn("Chua co ca nao", str(printed.call_args_list[0]))


if __name__ == "__main__":
    unittest.main()
