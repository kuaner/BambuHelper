import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class P4SettingsMergeTests(unittest.TestCase):
    def test_cloud_and_web_edits_preserve_their_own_fields(self):
        output = ROOT / ".cache" / "p4-settings-merge-test"
        subprocess.run([
            "c++", "-std=c++17", "-O2", "-I", str(ROOT / "include"),
            str(ROOT / "tests" / "p4_settings_merge_test.cpp"), "-o", str(output),
        ], check=True)
        subprocess.run([str(output)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
