import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class SpscLatestTests(unittest.TestCase):
    def test_full_buffer_revision_and_concurrent_payloads(self):
        output = ROOT / ".cache" / "spsc-latest-test"
        subprocess.run([
            "c++", "-std=c++17", "-O2", "-pthread", "-I", str(ROOT / "include"),
            str(ROOT / "tests" / "spsc_latest_test.cpp"), "-o", str(output),
        ], check=True)
        subprocess.run([str(output)], check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
