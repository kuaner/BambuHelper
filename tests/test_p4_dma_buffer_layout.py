import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class P4DmaBufferLayoutTests(unittest.TestCase):
    def test_cache_line_alignment_and_overflow(self):
        output = ROOT / ".cache/p4-dma-buffer-layout-test"
        subprocess.run(["c++", "-std=c++17", "-O2",
                        "-I", str(ROOT / "include"),
                        str(ROOT / "tests/p4_dma_buffer_layout_test.cpp"),
                        "-o", str(output)], check=True)
        subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    unittest.main()
