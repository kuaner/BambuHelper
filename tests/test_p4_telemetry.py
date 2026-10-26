"""Exercise nozzle metadata merging with the real ArduinoJson implementation."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
JSON_INCLUDE = ROOT / '.pio/libdeps/ws_p4_lcd_7b_rev1/ArduinoJson/src'

class P4TelemetryTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++') and JSON_INCLUDE.exists(), 'Build P4 first')
    def test_nozzle_metadata_is_keyed_by_extruder_and_retained_on_delta(self):
        source = r'''
#include "p4_telemetry.h"
#include <cassert>
int main() {
 BambuState s{}; JsonDocument doc;
 auto merge=[&](const char* json) { assert(!deserializeJson(doc,json)); mergeP4NozzleMetadata(doc.as<JsonObjectConst>(),s); };
 merge(R"({"device":{"nozzle":{"info":[{"id":1,"diameter":"0.4","type":"AH01"},{"id":0,"diameter":0.6,"type":"hardened_steel"}]}}})");
 assert(s.nozzleDiameterN[0]>.59f && s.nozzleDiameterN[1]>.39f);
 assert(strcmp(s.nozzleTypeN[1],"AH01")==0);
 merge(R"({"device":{"nozzle":{"info":[{"id":1,"type":"stainless_steel"}]}}})");
 assert(s.nozzleDiameterN[1]>.39f && strcmp(s.nozzleTypeN[0],"hardened_steel")==0);
 merge(R"({"device":{"nozzle":{"info":[{"id":1,"diameter":"bad"},{"id":0,"diameter":null},{"id":2,"diameter":0.8},{"diameter":0.2}]}}})");
 assert(s.nozzleDiameterN[0]>.59f && s.nozzleDiameterN[1]>.39f);
 merge(R"({"device":{"nozzle":{"info":[{"id":1,"diameter":"nan"},{"id":0,"diameter":-1}]}}})");
 assert(s.nozzleDiameterN[0]>.59f && s.nozzleDiameterN[1]>.39f);
 merge("{}"); assert(s.nozzleDiameterN[0]>.59f);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            (p/'probe.cpp').write_text(source)
            subprocess.run(['c++','-std=c++17','-DBOARD_IS_WS_P4_7B=1',
                '-DARDUINOJSON_ENABLE_ARDUINO_STRING=0','-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0',
                '-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0','-DARDUINOJSON_ENABLE_PROGMEM=0',
                '-I',str(ROOT/'tools/p4_preview/stubs'),'-I',str(JSON_INCLUDE),
                '-I',str(ROOT/'include'),'-I',str(ROOT/'src'),str(p/'probe.cpp'),'-o',str(p/'probe')],check=True)
            subprocess.run([str(p/'probe')],check=True)

if __name__=='__main__': unittest.main()
