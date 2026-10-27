#include "spsc_latest.h"
#include <cassert>
#include <thread>
struct Value { uint32_t sequence = 0; char payload[240]{}; };
SpscLatest<Value> bootMailbox;

int main() {
  assert(testHeapCalls.load() == 0);  // No allocation before PSRAM joins heap.
  Value read; read.sequence = 99;
  assert(!bootMailbox.copyLatest(read) && read.sequence == 99);
  Value value; value.sequence = 1;
  bootMailbox.update(value);  // A failed PSRAM allocation cannot consume RAM.
  assert(testHeapCalls.load() == 1 && !bootMailbox.copyLatest(read));
  testPsramReady = true;
  std::thread producer([] {
    for (unsigned i = 1; i <= 100000; ++i) {
      Value next; next.sequence = i;
      bootMailbox.update(next);
    }
  });
  do {} while (!bootMailbox.copyLatest(read) || read.sequence != 100000);
  producer.join();
  assert(testHeapCalls.load() == 2);  // One successful allocation, then reuse.
  auto* external = SpscLatest<Value, 2>::createExternal();
  assert(external);
  external->update(value);
  assert(external->copyLatest(read) && read.sequence == 1);
  SpscLatest<Value, 2>::destroyExternal(external);
  assert(testHeapFrees.load() == 1);
}
