#include "spsc_latest.h"
#include <cassert>
#include <cstdint>
#include <thread>

struct Value { uint64_t sequence = 0; uint8_t payload[240]{}; };

int main() {
  {
    SpscLatest<Value, 8> mailbox;
    // Fill every slot before the consumer runs. Selection is by revision, not
    // slot order, so it must be the newest complete object.
    for (uint64_t i = 1; i <= 8; ++i) {
      Value value{};
      value.sequence = i;
      value.payload[0] = static_cast<uint8_t>(i);
      mailbox.update(value);
    }
    const Value* value = mailbox.readLatest();
    assert(value && value->sequence == 8 && value->payload[0] == 8);
    for (int i = 0; i < 100; ++i) assert(mailbox.readLatest()->sequence == 8);
  }
  {
    SpscLatest<Value, 3> mailbox;
    std::thread producer([&] {
      for (uint64_t sequence = 1; sequence <= 150000; ++sequence) {
        Value value{};
        value.sequence = sequence;
        for (auto& byte : value.payload) byte = static_cast<uint8_t>(sequence);
        mailbox.update(value);
      }
    });
    uint64_t previous = 0;
    while (previous < 150000) {
      const Value* value = mailbox.readLatest();
      if (!value) continue;
      assert(value->sequence >= previous);  // never goes back in time
      for (auto byte : value->payload) assert(byte == static_cast<uint8_t>(value->sequence));
      previous = value->sequence;
    }
    producer.join();
  }
  {
    // A pending newest value is retained while the consumer holds every other
    // slot, and supersedes an already-written older Ready slot when possible.
    SpscLatest<Value, 3> mailbox;
    Value value{};
    value.sequence = 1;
    mailbox.update(value);
    value.sequence = 2;
    mailbox.update(value);
    const Value* first = mailbox.readLatest();
    assert(first && first->sequence == 2);
    value.sequence = 3;
    mailbox.update(value);
    value.sequence = 4;
    mailbox.update(value);
    assert(mailbox.readLatest()->sequence >= 3);
  }
}
