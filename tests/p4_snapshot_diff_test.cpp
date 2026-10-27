#include "p4_snapshot_diff.h"
#include <cassert>
#include <cstdint>

struct State { uint32_t progress = 0; uint8_t payload[240]{}; };
struct Diagnostics { uint32_t messages = 0; int error = 0; };

int main() {
  P4SnapshotDiff<State, Diagnostics> filter;
  State state;
  Diagnostics diagnostics;
  assert(filter.changed(state, diagnostics));
  for (int i = 0; i < 10000; ++i) assert(!filter.changed(state, diagnostics));
  state.progress = 1;
  assert(filter.changed(state, diagnostics));
  assert(!filter.changed(state, diagnostics));
  diagnostics.messages++;
  assert(filter.changed(state, diagnostics));
  diagnostics.error = -1;
  assert(filter.changed(state, diagnostics));
  assert(!filter.changed(state, diagnostics));
  filter.invalidate();
  assert(filter.changed(state, diagnostics));
  assert(!filter.changed(state, diagnostics));
}
