import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class P4SnapshotDiffTests(unittest.TestCase):
    def test_repeated_state_does_not_publish(self):
        output = ROOT / ".cache" / "p4-snapshot-diff-test"
        subprocess.run([
            "c++", "-std=c++17", "-O2", "-I", str(ROOT / "include"),
            str(ROOT / "tests" / "p4_snapshot_diff_test.cpp"), "-o", str(output),
        ], check=True)
        subprocess.run([str(output)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
