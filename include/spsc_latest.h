#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

// One producer, one consumer, fixed storage. Neither side waits for the other.
// The producer owns a dirty value until a free slot is available. The consumer
// only borrows the newest completed value, so it never sees a partial update.
template<typename T, size_t N = 3>
class SpscLatest {
  static_assert(N >= 3, "three slots cover writing, reading and the newest value");
  enum : uint8_t { Free, Writing, Ready, Reading };
  struct Slot {
    T value{};
    uint32_t revision = 0;
    std::atomic<uint8_t> state{Free};
  };
  Slot slots_[N];
  int writing_ = -1;
  int reading_ = -1;
  const T* current_ = nullptr;
  uint32_t nextRevision_ = 1;
  uint32_t lastRevision_ = 0;
  T dirty_{};

  int acquireFree() {
    if (writing_ >= 0) return writing_;
    for (size_t i = 0; i < N; ++i) {
      uint8_t expected = Free;
      if (slots_[i].state.compare_exchange_strong(expected, Writing,
          std::memory_order_acquire, std::memory_order_relaxed)) {
        writing_ = static_cast<int>(i);
        return writing_;
      }
    }
    // The consumer has not claimed an older completed value. Replace it now
    // instead of retaining a dirty value indefinitely until another update.
    // Only the producer performs Ready -> Writing; the consumer may still win
    // this CAS, in which case that slot remains owned by the reader.
    for (size_t i = 0; i < N; ++i) {
      uint8_t expected = Ready;
      if (slots_[i].state.compare_exchange_strong(expected, Writing,
          std::memory_order_acquire, std::memory_order_relaxed)) {
        writing_ = static_cast<int>(i);
        return writing_;
      }
    }
    return -1;
  }
public:
  SpscLatest() = default;
  SpscLatest(const SpscLatest&) = delete;
  SpscLatest& operator=(const SpscLatest&) = delete;

  // Returns writable storage, or null when the UI has all other slots. In the
  // latter case update() retains the newest value until storage frees up.
  T* beginWrite() {
    const int slot = acquireFree();
    return slot >= 0 ? &slots_[slot].value : nullptr;
  }

  void publish() {
    const int slot = writing_;
    if (slot < 0) return;
    slots_[slot].revision = nextRevision_++;
    if (nextRevision_ == 0) nextRevision_ = 1;
    slots_[slot].state.store(Ready, std::memory_order_release);
    writing_ = -1;
    // Superseded completed values are garbage, not alternative newer choices.
    for (size_t i = 0; i < N; ++i) {
      if (static_cast<int>(i) == slot) continue;
      uint8_t expected = Ready;
      slots_[i].state.compare_exchange_strong(expected, Free,
                                               std::memory_order_acq_rel,
                                               std::memory_order_relaxed);
    }
  }

  void update(const T& value) {
    dirty_ = value;
    if (T* slot = beginWrite()) { *slot = dirty_; publish(); }
  }

  // Returned pointer is valid until the next readLatest() on the consumer.
  const T* readLatest() {
    for (size_t i = 0; i < N; ++i) {
      const uint32_t revision = slots_[i].revision;
      if (slots_[i].state.load(std::memory_order_relaxed) != Ready ||
          static_cast<int32_t>(revision - lastRevision_) <= 0) continue;
      uint8_t expected = Ready;
      if (slots_[i].state.compare_exchange_strong(expected, Reading,
          std::memory_order_acquire, std::memory_order_relaxed)) {
        if (reading_ >= 0) slots_[reading_].state.store(Free, std::memory_order_release);
        reading_ = static_cast<int>(i);
        current_ = &slots_[i].value;
        lastRevision_ = revision;
      }
    }
    return current_;
  }

};
