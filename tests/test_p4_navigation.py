"""P4 gesture safety and display coordinates, compiled from production code."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class P4NavigationTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'host C++ compiler required')
    def test_gesture_capture_and_wake_suppression(self):
        source = r'''
#include "p4_dashboard_layout.h"
#include <cassert>
using namespace p4ui;
int main() {
  auto p=touchPoint(0,0,0); assert(p.x==0&&p.y==0);
  p=touchPoint(1023,599,2); assert(p.x==0&&p.y==0);
  p=touchPoint(0,0,2); assert(p.x==1023&&p.y==599);
  p=touchPoint(989,42,0); assert(p.x>=956&&p.y<68); // measured physical gear tap
  TouchTracker t;
  auto poll=[&](bool down,bool valid,int x,int y,unsigned ms,bool enabled=true) {
    return t.update(down,valid,{x,y},ms,enabled);
  };
  assert(poll(true,true,500,450,10)==Gesture::None);
  assert(poll(false,true,500,450,110)==Gesture::Tap);
  assert(t.origin().x==500);
  poll(true,true,500,450,200); poll(true,true,350,452,300);
  assert(poll(false,true,350,452,350)==Gesture::Left);
  poll(true,true,500,450,400); poll(true,true,650,450,500); poll(true,true,500,450,600);
  assert(poll(false,true,500,450,650)==Gesture::None); // swipe out and back is not tap
  poll(true,true,500,450,700,false); // wakes the panel
  poll(true,true,500,450,800,true);
  assert(poll(false,true,500,450,900)==Gesture::None);
  poll(true,true,500,450,1000); poll(true,false,500,450,1100); // bus failure or multitouch
  poll(true,true,500,450,1200);
  assert(poll(false,true,500,450,1300)==Gesture::None);
  poll(true,true,500,450,1400);
  assert(poll(false,true,500,450,3400)==Gesture::None); // long hold not a tap
  poll(true,true,500,450,3500); poll(true,true,500,450,3600,false); // modal takes input
  assert(poll(false,true,500,450,3700)==Gesture::None);
  poll(true,true,500,450,3800);
  assert(poll(false,false,500,450,4900)==Gesture::None); // synthetic stale release
  poll(true,true,500,450,0xfffffff0u);
  assert(poll(false,true,500,450,80)==Gesture::Tap); // millis wrap
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path/'probe.cpp').write_text(source)
            subprocess.run(['c++','-std=c++11','-I',str(ROOT/'include'),str(path/'probe.cpp'),'-o',str(path/'probe')],check=True)
            subprocess.run([str(path/'probe')],check=True)

if __name__ == '__main__': unittest.main()
