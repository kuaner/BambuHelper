// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Memory.hpp"
#include "TutkSession.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace bambu_camera {
enum class Error { None, Connect, Timeout, Protocol, TooLarge, Integrity, Credentials, Authentication, Clock };
const char* error_name(Error error);
// Safe diagnostic categories only: never include raw replies, tickets or keys.
enum class SnapshotFailure : uint8_t {
  None, Ticket, FrameLength, HeaderLength, HeaderJson, ResultMissing, RemoteResult,
  ReplyMissing, ImageLimit, TotalChanged, MetadataLength, MetadataJson,
  RangeMissing, RangeInvalid, DuplicateMismatch, Gap, EmptyChunk, FinalLength,
  Digest, Jpeg
};
const char* snapshot_failure_name(SnapshotFailure failure);

// The printer sends a small JSON metadata blob before the JPEG. It is not
// part of the image. Only a complete, contiguous, MD5-checked JPEG is exposed.
class SnapshotDownload {
public:
  static constexpr size_t kMaxImage=512*1024;
  explicit SnapshotDownload(uint32_t sequence=1):sequence_(sequence){}
  bool feed(const uint8_t* frame,size_t length);
  bool done() const {return done_;}
  Error error() const {return error_;}
  SnapshotFailure failure() const {return failure_;}
  const BambuVector<uint8_t>& bytes() const {return image_;}
  BambuVector<uint8_t> take(){return std::move(image_);}
private:
  uint32_t sequence_;
  size_t total_=0;
  bool done_=false;
  Error error_=Error::None;
  SnapshotFailure failure_=SnapshotFailure::None;
  BambuVector<uint8_t> image_;
  bool fail(Error error,SnapshotFailure failure){error_=error;failure_=failure;done_=true;image_.clear();return false;}
};

// The TUTK callback is the sole writer. Once it publishes completion, it no
// longer touches the download; the capture worker can acquire and move it.
class SnapshotReceiver {
  SnapshotDownload download_;
  std::atomic<bool> completed_{false};
public:
  void feed(const uint8_t* frame, size_t length) {
    if (completed_.load(std::memory_order_relaxed) || !frame || !length) return;
    download_.feed(frame, length);
    if (download_.done()) completed_.store(true, std::memory_order_release);
  }
  bool done() const { return completed_.load(std::memory_order_acquire); }
  Error error() const { return download_.error(); }             // only after done()
  SnapshotFailure failure() const { return download_.failure(); } // only after done()
  BambuVector<uint8_t> take() { return download_.take(); }      // only after done()
};

std::string snapshot_request(uint32_t sequence=1);

// Blocking development API. Call from a dedicated worker, never the UI/MQTT
// loop. The transport owns the session and closes it on success or failure.
Error capture(const obn::camera::tutk::TutkSessionParams& params,
              BambuVector<uint8_t>& jpeg,unsigned timeout_ms=45000,
              SnapshotFailure* failure=nullptr);
}
