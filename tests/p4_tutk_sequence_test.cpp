#include "Sequence.hpp"
#include <cassert>
#include <cstdint>

using obn::camera::tutk::sequence_age;
using obn::camera::tutk::sequence_before;

int main() {
  assert(sequence_age(0, 65535) == 1);
  assert(sequence_age(65535, 0) == 65535);
  assert(sequence_age(3, 65534) == 5);
  assert(sequence_age(65534, 3) == 65531);
  assert(sequence_age(42, 42) == 0);
  assert(sequence_age(0, 32768) == 32768);
  // Frame ordering uses the full 32-bit serial-number space, not a group ID.
  assert(sequence_before(0xfffffffeU, 0xffffffffU));
  assert(!sequence_before(0xffffffffU, 0xfffffffeU));
  assert(sequence_before(0xfffffffeU, 1));
  assert(!sequence_before(1, 0xfffffffeU));
  assert(!sequence_before(17, 17));
}
