import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class SpscLatestTests(unittest.TestCase):
    def test_p4_boot_allocates_only_after_psram_is_available(self):
        output = ROOT / ".cache" / "spsc-latest-boot-test"
        subprocess.run([
            "c++", "-std=c++17", "-O2", "-pthread", "-DBOARD_IS_WS_P4_7B",
            "-I", str(ROOT / "tests/p4_stubs"), "-I", str(ROOT / "include"),
            str(ROOT / "tests/spsc_latest_boot_test.cpp"), "-o", str(output),
        ], check=True)
        subprocess.run([str(output)], check=True, timeout=30)

    def test_full_buffer_revision_and_concurrent_payloads(self):
        output = ROOT / ".cache" / "spsc-latest-test"
        subprocess.run([
            "c++", "-std=c++17", "-O2", "-pthread", "-DSPSC_LATEST_TEST_HOOK",
            "-I", str(ROOT / "include"),
            str(ROOT / "tests" / "spsc_latest_test.cpp"), "-o", str(output),
        ], check=True)
        subprocess.run([str(output)], check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
