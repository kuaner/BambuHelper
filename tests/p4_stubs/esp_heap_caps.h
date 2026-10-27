#pragma once
#include <atomic>
#include <cstdlib>
#include <cassert>
constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
inline std::atomic<bool> testPsramReady{false};
inline std::atomic<unsigned> testHeapCalls{0};
inline std::atomic<unsigned> testHeapFrees{0};
inline void* heap_caps_malloc(size_t bytes, unsigned caps) {
  ++testHeapCalls;
  assert(caps & MALLOC_CAP_SPIRAM);
  return testPsramReady.load() ? std::malloc(bytes) : nullptr;
}
inline void* heap_caps_calloc(size_t count, size_t bytes, unsigned caps) {
  ++testHeapCalls;
  assert(caps & MALLOC_CAP_SPIRAM);
  return testPsramReady.load() ? std::calloc(count, bytes) : nullptr;
}
inline void heap_caps_free(void* value) { if (value) ++testHeapFrees; std::free(value); }
