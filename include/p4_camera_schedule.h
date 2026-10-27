#pragma once
#include <cstdint>

// Successful captures repeat after five minutes. Failed captures back off to at
// most eight minutes, independent of whether an older image remains visible.
constexpr uint32_t p4CameraRetryDelayMs(unsigned failures) {
  return failures == 0 ? 300000u : (30000u << (failures > 4 ? 4 : failures));
}

constexpr bool p4CameraRetryDue(uint32_t now, uint32_t previous, unsigned failures) {
  return uint32_t(now - previous) >= p4CameraRetryDelayMs(failures);
}

constexpr bool p4CameraRetryDue(uint32_t now, uint32_t previous, unsigned failures,
                                bool attempted) {
  return attempted && p4CameraRetryDue(now, previous, failures);
}

// A visible printer without a current image needs its first capture as soon as
// MQTT and the network are ready. Only failures start the missing-image backoff.
constexpr bool p4CameraMissingImageDue(uint32_t now, uint32_t previousAttempt,
                                       unsigned failures) {
  return !previousAttempt || p4CameraRetryDue(now, previousAttempt, failures, true);
}
