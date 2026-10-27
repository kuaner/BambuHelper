#include "p4_json_allocator.h"
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

using namespace p4_json_test;

unsigned region(const void* pointer) {
  const auto* address = static_cast<const unsigned char*>(pointer);
  for (const auto& entry : allocations) {
    const auto* raw = static_cast<const unsigned char*>(entry.first);
    if (address > raw && address - raw <= 32) return entry.second;
  }
  assert(false);
}

int main() {
  P4JsonAllocator allocator;
  void* small = allocator.allocate(16);
  assert(region(small) == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  allocator.deallocate(small);
  {
    JsonDocument document(&allocator);
    const std::string longValue(4000, 'x');
    assert(!deserializeJson(document, "{\"key\":\"" + longValue + "\"}"));
    assert(document["key"].as<std::string>() == longValue);
    document["other"] = longValue;
    assert(document.overflowed() == false);
    bool sawExternal = false;
    for (const auto& entry : allocations) {
      assert(entry.second == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) ||
             entry.second == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      sawExternal |= entry.second == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    assert(sawExternal);
  }
  assert(allocations.empty());

  {
    JsonDocument document(&allocator);
    document["small"] = 1;
    assert(document["small"] == 1);
  }
  assert(allocations.empty() && frees > 0);

  void* original = allocator.allocate(32);
  assert(original);
  std::memset(original, 0x5a, 32);
  const size_t beforeFailure = allocationsMade;
  failAt = beforeFailure + 1;
  assert(!allocator.reallocate(original, 4096));
  assert(failedAllocations == 1);
  assert(allocations.count(static_cast<unsigned char*>(original) - sizeof(uint64_t) * 4));
  for (size_t i = 0; i < 32; ++i) assert(static_cast<unsigned char*>(original)[i] == 0x5a);
  void* larger = allocator.reallocate(original, 2048);
  assert(larger && region(larger) == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  original = larger;
  allocator.deallocate(original);
  assert(allocations.empty());

  {
    const std::string json = "{\"key\":\"" + std::string(4000, 'x') +
      "\",\"other\":\"" + std::string(2000, 'y') + "\",\"array\":[1,2,3,4,5,6,7,8]}";
    size_t allocationCount = 0;
    {
      JsonDocument document(&allocator);
      const size_t before = allocationsMade;
      assert(!deserializeJson(document, json));
      allocationCount = allocationsMade - before;
    }
    assert(allocations.empty() && allocationCount > 2);
    // Fail each real allocation reached by this document. ArduinoJson must
    // release all prior pools and strings, including a failed resize.
    for (size_t failure = 1; failure <= allocationCount; ++failure) {
      failAt = allocationsMade + failure;
      JsonDocument document(&allocator);
      const auto error = deserializeJson(document, json);
      if (error) {
        assert(error == DeserializationError::NoMemory);
        assert(document.overflowed());
      }
      // A failed shrink retains its wider original buffer; parsing may
      // succeed. Every other failure must leave a destructible document.
      document.clear();
      assert(allocations.empty());
    }
    failAt = static_cast<size_t>(-1);
  }

  {
    JsonDocument document(&allocator);
    document["final"] = true;
  }
  assert(allocations.empty());
  assert(!allocator.allocate(0));
  allocator.deallocate(nullptr);
}
