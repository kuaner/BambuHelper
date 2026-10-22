"""Exercise the real ArduinoJson AMS merge against full and delta reports."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
JSON_INCLUDE = ROOT / ".pio/libdeps/ws_p4_lcd_7b_rev1/ArduinoJson/src"


class AmsTelemetryTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("c++") and JSON_INCLUDE.exists(), "Build the P4 env first")
    def test_numeric_string_zero_and_delta_reports(self):
        arduino = r'''
        #pragma once
        #include <cstdint>
        #include <cstddef>
        #include <cstring>
        #include <cstdio>
        inline size_t strlcpy(char* dst, const char* src, size_t cap) {
          size_t n = strlen(src);
          if (cap) { size_t k = n < cap - 1 ? n : cap - 1; memcpy(dst, src, k); dst[k] = 0; }
          return n;
        }
        '''
        source = r'''
        #include <cassert>
        #include "ams_telemetry.h"
        int main() {
          AmsState state{};
          JsonDocument doc;
          auto apply = [&](const char* json, bool snapshot) {
            assert(!deserializeJson(doc, json));
            mergeAmsUnits(state, doc.as<JsonArrayConst>(), snapshot);
          };
          // Two AMS2 units at 0% and 1%, one legacy AMS without a raw sensor.
          apply(R"([{"id":"0","humidity":"5","humidity_raw":"0","temp":"29.5"},
                     {"id":1,"humidity":5,"humidity_raw":1,"temp":30},
                     {"id":"2","humidity":"5","temp":"27"}])", true);
          assert(state.unitCount == 3);
          assert(state.units[0].humidityRawKnown && state.units[0].humidityRaw == 0);
          assert(state.units[1].humidityRawKnown && state.units[1].humidityRaw == 1);
          assert(!state.units[2].humidityRawKnown);
          char label[24];
          formatAmsHumidity(label, sizeof(label), state.units[0]);
          assert(strcmp(label, "0%") == 0);
          formatAmsHumidity(label, sizeof(label), state.units[2]);
          assert(strcmp(label, "L5") == 0);
          // One-unit delta must retain every other unit and previous humidity.
          apply(R"([{"id":"1","temp":"32","left_dry_time":15}])", false);
          assert(state.unitCount == 3 && state.units[0].humidityRawKnown);
          assert(state.units[1].humidityRaw == 1 && state.units[1].temp == 32);
          assert(state.anyDrying && state.units[1].dryRemainMin == 15);
          // Reordering a full unit list must retain values by raw id.
          apply(R"([{"id":2,"humidity":5},{"id":"1"},{"id":0}])", true);
          assert(!state.units[0].humidityRawKnown && state.units[0].id == 2);
          assert(state.units[1].humidityRawKnown && state.units[1].humidityRaw == 1);
          assert(state.units[2].humidityRawKnown && state.units[2].humidityRaw == 0);
          // Malformed values do not erase a valid cached reading.
          apply(R"([{"id":"1","humidity_raw":null},{"id":0,"humidity_raw":"bad"}])", false);
          assert(state.units[1].humidityRaw == 1 && state.units[2].humidityRawKnown);
          // A real zero and a stopped dryer replace previous nonzero values.
          apply(R"([{"id":1,"humidity_raw":0,"dry_time":"0"}])", false);
          assert(state.units[1].humidityRaw == 0 && state.units[1].humidityRawKnown);
          assert(!state.anyDrying);
        }
        '''
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / "Arduino.h").write_text(arduino)
            (folder / "probe.cpp").write_text(source)
            exe = folder / "probe"
            subprocess.run(["c++", "-std=c++11", "-DARDUINOJSON_ENABLE_ARDUINO_STRING=0",
                            "-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0", "-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0",
                            "-DARDUINOJSON_ENABLE_PROGMEM=0", "-I", tmp, "-I", str(JSON_INCLUDE),
                            "-I", str(ROOT / "include"), "-I", str(ROOT / "src"),
                            str(folder / "probe.cpp"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
