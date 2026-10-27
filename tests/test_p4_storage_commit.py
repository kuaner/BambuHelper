import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class P4StorageCommitTests(unittest.TestCase):
    def test_token_and_settings_commit_atomically_to_consumers(self):
        output = ROOT / ".cache" / "p4-storage-commit-test"
        subprocess.run([
            "c++", "-std=c++17", "-O2", "-I", str(ROOT / "include"),
            str(ROOT / "tests" / "p4_storage_commit_test.cpp"), "-o", str(output),
        ], check=True)
        subprocess.run([str(output)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
