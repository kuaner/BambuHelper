import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class P4CheckedPreferencesTests(unittest.TestCase):
    def test_readback_failures_and_restart_acks(self):
        output = ROOT / ".cache" / "p4-checked-preferences-test"
        subprocess.run([
            "c++", "-std=c++17", "-O2", "-I", str(ROOT / "include"),
            str(ROOT / "tests" / "p4_checked_preferences_test.cpp"), "-o", str(output),
        ], check=True)
        subprocess.run([str(output)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
