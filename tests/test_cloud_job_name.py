"""Design names must belong to the exact MQTT task, device and plate."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
JSON = ROOT / '.pio/libdeps/ws_p4_lcd_7b_rev1/ArduinoJson/src'


class CloudJobNameTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++') and JSON.exists(), 'Build P4 first')
    def test_names_and_identity(self):
        source = r'''
#include "cloud_job_name.h"
#include <cassert>
int main() {
  JsonDocument d; char out[128];
  auto pick=[&](const char* text,const char* task="42",const char* device="printer-a",int plate=1) {
    assert(!deserializeJson(d,text));
    return selectCloudJobName(d.as<JsonObjectConst>(),task,device,plate,out,sizeof(out));
  };
  assert(pick(R"({"hits":[{"id":99,"deviceId":"printer-a","plateIndex":1,"designTitle":"Wrong job"},{"id":42,"deviceId":"printer-a","plateIndex":1,"title":"0.2mm preset","designTitle":"Desk organizer"}]})"));
  assert(strcmp(out,"Desk organizer")==0);
  assert(!pick(R"({"hits":[{"id":42,"deviceId":"printer-b","plateIndex":1,"designTitle":"Wrong printer"}]})"));
  assert(!pick(R"({"hits":[{"id":42,"deviceId":"printer-a","plateIndex":2,"designTitle":"Wrong plate"}]})"));
  assert(!pick(R"({"hits":[{"id":42,"plateIndex":1,"designTitle":"Missing owner"}]})"));
  assert(pick(R"({"hits":[{"id":"42","deviceId":"printer-a","plateIndex":1,"plateName":" Custom plate ","designTitle":"Project"}]})"));
  assert(strcmp(out,"Custom plate")==0);
  assert(pick(R"({"hits":[{"id":42,"deviceId":"printer-a","plateIndex":1,"designTitle":"Original","designTitleTranslated":"Localized"}]})"));
  assert(strcmp(out,"Localized")==0);
  assert(!pick(R"({"hits":[{"id":42,"deviceId":"printer-a","plateIndex":1,"title":"Only a preset"}]})"));
  assert(out[0]==0);
  assert(!pick("{}","0"));
  assert(!deserializeJson(d,R"({"hits":[{"id":42,"deviceId":"printer-a","plateIndex":1,"designTitle":"桌面整理器"}]})"));
  char clipped[5];
  assert(selectCloudJobName(d.as<JsonObjectConst>(),"42","printer-a",1,clipped,sizeof(clipped)));
  assert(strcmp(clipped,"桌")==0);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / 'test.cpp').write_text(source)
            subprocess.run(['c++', '-std=c++17',
                            '-DARDUINOJSON_ENABLE_ARDUINO_STRING=0',
                            '-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0',
                            '-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0',
                            '-DARDUINOJSON_ENABLE_PROGMEM=0',
                            '-I', str(JSON), '-I', str(ROOT / 'src'),
                            str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
            subprocess.run([str(p / 'test')], check=True)
