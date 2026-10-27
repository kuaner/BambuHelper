"""Run the P4 allocator against the actual ArduinoJson implementation."""
import shutil
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JSON = ROOT / '.pio/libdeps/ws_p4_lcd_7b_rev1/ArduinoJson/src'


class P4JsonAllocatorTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++') and JSON.exists(), 'Build P4 first')
    def test_regions_growth_and_allocation_failures(self):
        output = ROOT / '.cache' / 'p4-json-allocator-test'
        subprocess.run([
            'c++', '-std=c++17', '-O1', '-g', '-DBOARD_IS_WS_P4_7B=1',
            '-DARDUINOJSON_ENABLE_ARDUINO_STRING=0',
            '-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0',
            '-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0',
            '-DARDUINOJSON_ENABLE_PROGMEM=0',
            '-I', str(ROOT / 'tests/p4_json_stubs'), '-I', str(JSON),
            '-I', str(ROOT / 'include'), '-I', str(ROOT / 'tools/p4_preview/stubs'),
            str(ROOT / 'tests/p4_json_allocator_test.cpp'), '-o', str(output),
        ], check=True)
        subprocess.run([str(output)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
