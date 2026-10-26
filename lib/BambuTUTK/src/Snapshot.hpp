// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Memory.hpp"
#include "TutkSession.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

namespace bambu_camera {
enum class Error { None, Connect, Timeout, Protocol, TooLarge, Integrity, Credentials, Authentication, Clock };
const char* error_name(Error error);

// The printer sends a small JSON metadata blob before the JPEG. It is not
// part of the image. Only a complete, contiguous, MD5-checked JPEG is exposed.
class SnapshotDownload {
public:
  static constexpr size_t kMaxImage=512*1024;
  explicit SnapshotDownload(uint32_t sequence=1):sequence_(sequence){}
  bool feed(const uint8_t* frame,size_t length);
  bool done() const {return done_;}
  Error error() const {return error_;}
  const BambuVector<uint8_t>& bytes() const {return image_;}
  BambuVector<uint8_t> take(){return std::move(image_);}
private:
  uint32_t sequence_;
  size_t total_=0;
  bool done_=false;
  Error error_=Error::None;
  BambuVector<uint8_t> image_;
  bool fail(Error error){error_=error;done_=true;image_.clear();return false;}
};

std::string snapshot_request(uint32_t sequence=1);

// Blocking development API. Call from a dedicated worker, never the UI/MQTT
// loop. The transport owns the session and closes it on success or failure.
Error capture(const obn::camera::tutk::TutkSessionParams& params,
              BambuVector<uint8_t>& jpeg,unsigned timeout_ms=45000);
}
