#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <cstdio>

constexpr unsigned MALLOC_CAP_INTERNAL = 1;
constexpr unsigned MALLOC_CAP_SPIRAM = 2;
constexpr unsigned MALLOC_CAP_8BIT = 4;

namespace p4_json_test {
inline std::map<void*, unsigned> allocations;
inline size_t allocationsMade = 0, frees = 0, failedAllocations = 0;
inline size_t failAt = static_cast<size_t>(-1);
inline void* heapMalloc(size_t bytes, unsigned caps) {
  if (++allocationsMade == failAt) { ++failedAllocations; return nullptr; }
  void* pointer = std::malloc(bytes);
  if (pointer) allocations[pointer] = caps;
  return pointer;
}
}

inline void* heap_caps_malloc(size_t bytes, unsigned caps) {
  return p4_json_test::heapMalloc(bytes, caps);
}
inline void heap_caps_free(void* pointer) {
  if (!pointer) return;
  const auto found = p4_json_test::allocations.find(pointer);
  if (found != p4_json_test::allocations.end()) p4_json_test::allocations.erase(found);
  ++p4_json_test::frees;
  std::free(pointer);
}
