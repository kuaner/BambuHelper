#pragma once

#include <cstdint>

namespace obn {
namespace camera {
namespace tutk {

// Packet and frame numbers wrap independently. Compare serial-number
// distance rather than relying on the ordering of the recovery map.
inline int32_t sequence_age(uint16_t newest, uint16_t candidate) {
  return int32_t(uint16_t(newest - candidate));
}

inline bool sequence_before(uint32_t left, uint32_t right) {
  return int32_t(left - right) < 0;
}

}
}
}
