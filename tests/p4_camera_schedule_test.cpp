#include "p4_camera_schedule.h"
#include <cassert>
#include <cstdint>

int main() {
  assert(p4CameraRetryDelayMs(0) == 300000);
  assert(p4CameraRetryDelayMs(1) == 60000);
  assert(p4CameraRetryDelayMs(2) == 120000);
  assert(p4CameraRetryDelayMs(3) == 240000);
  assert(p4CameraRetryDelayMs(4) == 480000);
  assert(p4CameraRetryDelayMs(99) == 480000);
  // millis() wrap must not postpone a capture indefinitely.
  constexpr uint32_t beforeWrap = UINT32_MAX - 1000;
  assert(!p4CameraRetryDue(1000, beforeWrap, 0));
  assert(!p4CameraRetryDue(60000, beforeWrap, 0));
  assert(!p4CameraRetryDue(298998, beforeWrap, 0));
  assert(p4CameraRetryDue(298999, beforeWrap, 0));
  assert(!p4CameraRetryDue(100, beforeWrap, 0));
  // A missing image must not cause a 20 ms retry loop on its first failure.
  assert(!p4CameraRetryDue(20, 0, 0, false));
  assert(!p4CameraRetryDue(30000, 0, 1, true));
  assert(p4CameraRetryDue(60000, 0, 1, true));
  assert(!p4CameraRetryDue(24000, 0, 3, true));
  assert(p4CameraRetryDue(240000, 0, 3, true));
  // First visibility/MQTT readiness may fetch immediately, even shortly after
  // boot. A failed attempt uses its own one-minute-or-longer backoff.
  assert(p4CameraMissingImageDue(20, 0, 0));
  assert(p4CameraMissingImageDue(20000, 0, 0));
  assert(!p4CameraMissingImageDue(30000, 20000, 1));
  assert(p4CameraMissingImageDue(80000, 20000, 1));
}
