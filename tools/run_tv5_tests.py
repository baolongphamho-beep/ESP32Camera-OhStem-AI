"""Run TV5 host tests from a fresh contribution checkout, without firmware/USB."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cxx', help='C++17 compiler executable; defaults to g++ on PATH')
    args = ap.parse_args()
    compiler = args.cxx or shutil.which('g++')
    if not compiler:
        print('C++17 compiler missing. Install g++ or pass --cxx <path>. No PASS claimed.', file=sys.stderr)
        return 1
    result = unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.discover(str(ROOT / 'tests')))
    if not result.wasSuccessful() or result.skipped:
        print(f'Python suite has failures or {len(result.skipped)} skipped tests; install camera/requirements-usb.txt.')
        return 1
    build = ROOT / '.edge-deps' / 'qa-tv5'
    build.mkdir(parents=True, exist_ok=True)
    cases = [
        ('measurement_state', ['lib/Measurements']),
        ('scale_presence', ['lib/Measurements']),
        ('edge_sweep_logic', ['lib/EdgeSweep']),
        ('edge_sweep_fsm', ['lib/EdgeSweep', 'tests']),
        ('edge_geometry', []),
    ]
    # Upstream native integration uses a different capture gate. Test it as-is
    # when present, without replacing the local native firmware for this PR.
    if (ROOT / 'tests/camera_power_integration_test.cpp').is_file():
        cases.append(('camera_power_integration', ['lib/Measurements', 'firmware/edge_yolo/main']))
    try:
        for name, includes in cases:
            exe = build / (name + '.exe')
            command = [compiler, '-std=c++17']
            for folder in includes:
                command.extend(['-I', str(ROOT / folder)])
            command.extend([str(ROOT / 'tests' / (name + '_test.cpp')), '-o', str(exe)])
            subprocess.run(command, cwd=ROOT, check=True)
            subprocess.run([str(exe)], cwd=ROOT, check=True)
        sim = build / 'qa_log_sim.exe'
        subprocess.run([compiler, '-std=c++17', '-I', str(ROOT / 'lib/EdgeSweep'), '-I', str(ROOT / 'tests'),
                        str(ROOT / 'tests/qa_log_sim.cpp'), '-o', str(sim)], cwd=ROOT, check=True)
        simulated = subprocess.check_output([str(sim)], cwd=ROOT, text=True)
        sys.path.insert(0, str(ROOT / 'tools'))
        from qa_log_check import evaluate, parse_log
        checks = evaluate(parse_log(simulated.splitlines()), 1500, 10, 15, 15)
        if any(check.status != 'PASS' for check in checks):
            print('QA simulation did not pass every check')
            return 1
    except (subprocess.CalledProcessError, OSError) as error:
        print(f'TV5 test failed: {error}', file=sys.stderr)
        return 1
    print(f'TV5 host checks PASS: {result.testsRun} Python, {len(cases)} C++ programs, QA log simulation.')
    print('No physical scale/servo QA sign-off; no firmware flashed or serial port opened.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
