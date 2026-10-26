"""Cloud cover selection must never use another job's or plate's image."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
JSON=ROOT/'.pio/libdeps/ws_p4_lcd_7b_rev1/ArduinoJson/src'
class CloudThumbnailTests(unittest.TestCase):
 @unittest.skipUnless(shutil.which('c++') and JSON.exists(),'Build P4 first')
 def test_exact_task_plate_and_https(self):
  source=r'''
#include "cloud_thumbnail.h"
#include <cassert>
int main() {
 JsonDocument d; char out[128];
 auto pick=[&](const char* json,const char* task,int plate) {
  assert(!deserializeJson(d,json));
  return selectCloudThumbnail(d.as<JsonObjectConst>(),task,plate,out,sizeof(out));
 };
 assert(pick(R"({"context":{"plates":[{"index":1,"thumbnail":{"url":"https://test/one.png"}},{"index":2,"thumbnail":{"url":"https://test/two.png"}}]}})","42",2));
 assert(strcmp(out,"https://test/two.png")==0);
 assert(!pick(R"({"context":{"plates":[{"index":1,"thumbnail":{"url":"https://test/one.png"}}]}})","42",2));
 assert(pick(R"({"hits":[{"id":99,"plateIndex":1,"cover":"https://test/wrong.png"},{"id":42,"plateIndex":2,"cover":"https://test/right.jpg"}]})","42",2));
 assert(strcmp(out,"https://test/right.jpg")==0);
 assert(!pick(R"({"hits":[{"id":99,"cover":"https://test/wrong.png"}]})","42",1));
 assert(!pick(R"({"hits":[{"id":42,"plateIndex":2,"cover":"https://test/wrong-plate.png"}]})","42",1));
 assert(!pick(R"({"id":42,"plateIndex":1,"cover":"http://test/unsafe.jpg"})","42",1));
 assert(pick(R"({"id":"42","plateIndex":1,"cover":"https://test/right.jpg"})","42",1));
 assert(!pick("{}","42",1)); assert(out[0]==0);
}
'''
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'test.cpp').write_text(source)
   subprocess.run(['c++','-std=c++17','-DARDUINOJSON_ENABLE_ARDUINO_STRING=0','-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0','-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0','-DARDUINOJSON_ENABLE_PROGMEM=0','-I',str(JSON),'-I',str(ROOT/'src'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
