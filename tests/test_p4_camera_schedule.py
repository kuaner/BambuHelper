import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class P4CameraScheduleTests(unittest.TestCase):
    def test_success_backoff_cap_and_clock_wrap(self):
        output = ROOT / ".cache/p4-camera-schedule-test"
        subprocess.run(["c++", "-std=c++17", "-O2", "-I", str(ROOT / "include"),
                        str(ROOT / "tests/p4_camera_schedule_test.cpp"), "-o", str(output)], check=True)
        subprocess.run([str(output)], check=True, timeout=10)
