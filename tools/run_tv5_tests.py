"""Run every host test of this branch (Python + C++), no firmware or serial port needed.

  python tools/run_tv5_tests.py                       # g++ from PATH
  python tools/run_tv5_tests.py --cxx C:/msys64/ucrt64/bin/g++.exe

Exit 0 only when all Python tests pass (none skipped) and every C++ test program
compiles and returns 0.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAIN = ["-I", "firmware/edge_yolo/main", "-I", "lib/Measurements"]
DISPLAY = ["-DLCD_ENABLED=1", "-DCAMERA_ENABLED=1", "-DSCALE_ENABLE_QR=0", "-DSCALE_ENABLE_FORCE=0",
           "-DCAMERA_RX_PIN=9", "-DCAMERA_TX_PIN=8", "-I", "tests/lcd_stubs", "-I", "variants/yolo_uno",
           "-I", "lib/Config", "-I", "lib/Measurements", "-I", "lib/Display"]
# test name -> (compiler arguments, extra sources)
SPECIAL = {
    "servo_driver_test": (["-I", "tests/servo_stubs"] + MAIN, ["firmware/edge_yolo/main/servo_ctrl.cpp"]),
    "display_probe_test": (DISPLAY, []),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cxx", help="C++17 compiler; defaults to g++ on PATH")
    args = ap.parse_args()
    compiler = args.cxx or shutil.which("g++")
    if not compiler:
        print("No C++17 compiler: install g++ or pass --cxx. No PASS claimed.", file=sys.stderr)
        return 1
    result = unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.discover(str(ROOT / "tests")))
    if not result.wasSuccessful() or result.skipped:
        print(f"Python tests failed or skipped ({len(result.skipped)} skipped).")
        return 1
    failed = []
    tests = sorted((ROOT / "tests").glob("*_test.cpp"))
    with tempfile.TemporaryDirectory() as build:
        for source in tests:
            name = source.stem
            flags, extra = SPECIAL.get(name, (MAIN, []))
            exe = Path(build) / (name + ".exe")
            cmd = [compiler, "-std=c++17", *flags, str(source), *[str(ROOT / e) for e in extra], "-o", str(exe)]
            built = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
            ran = built.returncode == 0 and subprocess.run([str(exe)], cwd=ROOT, capture_output=True).returncode == 0
            print(f"{'PASS' if ran else 'FAIL'}  {name}")
            if not ran:
                failed.append(name)
                if built.returncode:
                    print(built.stderr[:600])
    print(f"\nPython: {result.testsRun} tests OK | C++: {len(tests) - len(failed)}/{len(tests)} programs PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
