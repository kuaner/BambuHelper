#include "spsc_latest.h"
#include <cassert>
#include <cstdint>
#include <thread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

struct Value { uint64_t sequence = 0; uint8_t payload[240]{}; };
#if !defined(SPSC_LATEST_TEST_HOOK)
static_assert(sizeof(SpscLatest<Value>) < sizeof(Value));
#endif

int main() {
  {
    SpscLatest<Value> mailbox;
    Value value{}, read;
    assert(!mailbox.copyLatest(read));
    assert(!mailbox.readNext(read));
    value.sequence = 7; mailbox.update(value);
    assert(mailbox.readNext(read) && read.sequence == 7);
    assert(!mailbox.readNext(read));
    assert(!mailbox.readNext(read));
    value.sequence = 8; mailbox.update(value);
    assert(mailbox.readNext(read) && read.sequence == 8);
    assert(!mailbox.readNext(read));
    value.sequence = 9; mailbox.update(value);
    assert(mailbox.readNext(read) && read.sequence == 9);
    assert(!mailbox.readNext(read));
  }
  {
    // A revision arriving after the copy belongs to the next read. An ordinary
    // snapshot read in this window must not accidentally acknowledge it.
    SpscLatest<Value> mailbox;
    std::mutex mutex;
    std::condition_variable cv;
    bool copied = false, finish = false;
    Value value{}, read;
    value.sequence = 20; mailbox.update(value);
    mailbox.testHooks(nullptr, nullptr, [&] {
      std::unique_lock<std::mutex> lock(mutex);
      copied = true; cv.notify_all();
      cv.wait(lock, [&] { return finish; });
    });
    std::thread consumer([&] { assert(mailbox.readNext(read) && read.sequence == 20); });
    {
      std::unique_lock<std::mutex> lock(mutex);
      cv.wait(lock, [&] { return copied; });
    }
    value.sequence = 21; mailbox.update(value);
    Value ordinary{};
    assert(mailbox.copyLatest(ordinary) && ordinary.sequence == 21);
    {
      std::lock_guard<std::mutex> lock(mutex);
      finish = true;
    }
    cv.notify_all(); consumer.join();
    mailbox.testHooks(nullptr, nullptr);
    assert(mailbox.readNext(read) && read.sequence == 21);
    assert(!mailbox.readNext(read));
  }
  {
    SpscLatest<Value> mailbox;
    Value value{}, read;
    value.sequence = 12;
    mailbox.update(value);
    assert(mailbox.copyLatest(read) && read.sequence == 12);
    // An ordinary snapshot read does not consume the one-shot revision.
    assert(mailbox.readNext(read) && read.sequence == 12);
    read.sequence = 12345;
    assert(!mailbox.readNext(read) && read.sequence == 12345);
    value.sequence = 13;
    mailbox.update(value);
    assert(mailbox.readNext(read) && read.sequence == 13);
  }
  {
    // A pinned reader must survive two publications. This deterministically
    // occupies Reading + old/new Ready and forces the producer to use Free.
    SpscLatest<Value> mailbox;
    std::mutex mutex;
    std::condition_variable cv;
    bool claimed = false, finish = false;
    mailbox.testHooks([&] {
      std::unique_lock<std::mutex> lock(mutex);
      claimed = true; cv.notify_all();
      cv.wait(lock, [&] { return finish; });
    }, nullptr);
    Value value{}, read;
    value.sequence = 1; mailbox.update(value);
    std::thread consumer([&] { assert(mailbox.copyLatest(read)); });
    {
      std::unique_lock<std::mutex> lock(mutex);
      cv.wait(lock, [&] { return claimed; });
    }
    value.sequence = 2; mailbox.update(value);
    value.sequence = 3; mailbox.update(value);
    {
      std::lock_guard<std::mutex> lock(mutex);
      finish = true;
    }
    cv.notify_all(); consumer.join();
    assert(read.sequence == 1);
    assert(mailbox.copyLatest(read) && read.sequence == 3);
    value.sequence = 4; mailbox.update(value);
    assert(mailbox.readNext(read) && read.sequence == 4);
    assert(!mailbox.readNext(read));
  }
  {
    SpscLatest<Value> mailbox;
    Value* pending = mailbox.beginWrite();
    assert(pending); pending->sequence = 42; mailbox.cancelWrite();
    Value value{}, read; value.sequence = 43; mailbox.update(value);
    assert(mailbox.copyLatest(read) && read.sequence == 43);
    mailbox.update(value);
    assert(mailbox.copyLatest(read) && read.sequence == 43);
  }
  {
    SpscLatest<Value, 8> mailbox;
    for (uint64_t i = 1; i <= 8; ++i) {
      Value value{}; value.sequence = i;
      for (auto& byte : value.payload) byte = static_cast<uint8_t>(i);
      mailbox.update(value);
    }
    Value read;
    for (int i = 0; i < 100; ++i) {
      assert(mailbox.copyLatest(read));
      assert(read.sequence == 8 && read.payload[0] == 8);
    }
  }
  {
    // Screenshot publications use two large slots and retry instead of
    // dropping the request while a consumer still holds the older frame.
    SpscLatest<Value, 2> mailbox;
    std::mutex mutex;
    std::condition_variable cv;
    bool claimed = false, finish = false;
    mailbox.testHooks([&] {
      std::unique_lock<std::mutex> lock(mutex);
      claimed = true; cv.notify_all();
      cv.wait(lock, [&] { return finish; });
    }, nullptr);
    Value value{}, read;
    value.sequence = 1; mailbox.update(value);
    std::thread consumer([&] { assert(mailbox.copyLatest(read)); });
    {
      std::unique_lock<std::mutex> lock(mutex);
      cv.wait(lock, [&] { return claimed; });
    }
    value.sequence = 2; mailbox.update(value);
    assert(!mailbox.beginWrite());
    {
      std::lock_guard<std::mutex> lock(mutex);
      finish = true;
    }
    cv.notify_all(); consumer.join();
    assert(read.sequence == 1);
    value.sequence = 3; mailbox.update(value);
    assert(mailbox.copyLatest(read) && read.sequence == 3);
  }
  {
    SpscLatest<Value> mailbox;
    std::thread producer([&] {
      for (uint64_t sequence = 1; sequence <= 150000; ++sequence) {
        Value value{}; value.sequence = sequence;
        for (auto& byte : value.payload) byte = static_cast<uint8_t>(sequence);
        mailbox.update(value);
      }
    });
    Value read;
    uint64_t previous = 0;
    while (previous < 150000) {
      if (!mailbox.copyLatest(read)) continue;
      assert(read.sequence >= previous);
      for (auto byte : read.payload) assert(byte == static_cast<uint8_t>(read.sequence));
      previous = read.sequence;
    }
    producer.join();
    assert(mailbox.copyLatest(read) && read.sequence == 150000);
  }
  {
    SpscLatest<Value> mailbox;
    std::atomic<bool> stop{false};
    std::thread producer([&] {
      for (uint64_t sequence = 1; sequence <= 50000; ++sequence) {
        Value value{}; value.sequence = sequence;
        for (auto& byte : value.payload) byte = static_cast<uint8_t>(sequence);
        if (sequence % 3 == 0) std::this_thread::yield();
        mailbox.update(value);
      }
      stop.store(true, std::memory_order_release);
    });
    Value read;
    uint64_t previous = 0;
    while (!stop.load(std::memory_order_acquire)) {
      if (mailbox.copyLatest(read)) {
        assert(read.sequence >= previous);
        previous = read.sequence;
        for (auto byte : read.payload) assert(byte == static_cast<uint8_t>(read.sequence));
      }
      if (previous % 2) std::this_thread::sleep_for(std::chrono::microseconds(20));
    }
    producer.join();
    assert(mailbox.readNext(read) && read.sequence == 50000);
    assert(!mailbox.readNext(read));
  }
}
