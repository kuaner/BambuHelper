// SPDX-License-Identifier: AGPL-3.0-only
#include "Snapshot.hpp"
#include <ArduinoJson.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#if defined(ESP_PLATFORM) || defined(BAMBU_CAMERA_MBEDTLS)
#include <mbedtls/md.h>
#else
#include <openssl/evp.h>
#endif

namespace bambu_camera {
const char* error_name(Error e){
  switch(e){case Error::None:return "ok";case Error::Connect:return "connect_failed";
    case Error::Timeout:return "timeout";case Error::Protocol:return "invalid_reply";
    case Error::TooLarge:return "image_too_large";case Error::Integrity:return "integrity_failed";
    case Error::Credentials:return "invalid_application_credentials";
    case Error::Authentication:return "cloud_authentication_failed";
    case Error::Clock:return "clock_not_synced";}
  return "unknown";
}

static bool matches_md5(const BambuVector<uint8_t>& bytes,const char* expected){
  if(!expected||std::strlen(expected)!=32)return false;
  uint8_t digest[16];
#if defined(ESP_PLATFORM) || defined(BAMBU_CAMERA_MBEDTLS)
  if(mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_MD5),bytes.data(),bytes.size(),digest)!=0)return false;
#else
  unsigned n=0;
  if(EVP_Digest(bytes.data(),bytes.size(),digest,&n,EVP_md5(),nullptr)!=1||n!=16)return false;
#endif
  unsigned diff=0;
  for(size_t i=0;i<32;++i){
    const char c="0123456789abcdef"[(i&1)?digest[i/2]&15:digest[i/2]>>4];
    diff|=static_cast<unsigned char>(c)^static_cast<unsigned char>(expected[i]);
  }
  return diff==0;
}

bool SnapshotDownload::feed(const uint8_t* frame,size_t length){
  if(done_)return error_==Error::None;
  if(!frame||length==0||length>64*1024)return fail(Error::Protocol);
  size_t split=0;
  while(split+1<length&&(frame[split]!='\n'||frame[split+1]!='\n'))++split;
  const bool binary=split+1<length;
  const size_t json_length=binary?split:length;
  if(json_length>4096)return fail(Error::Protocol);
  JsonDocument document;
  if(deserializeJson(document,frame,json_length))return fail(Error::Protocol);
  if(!document["cmdtype"].is<uint32_t>()||!document["sequence"].is<uint32_t>())return true;
  if(document["cmdtype"].as<uint32_t>()!=4||document["sequence"].as<uint32_t>()!=sequence_)return true;
  if(!document["result"].is<int>())return fail(Error::Protocol);
  const int result=document["result"].as<int>();
  if(result!=0&&result!=1)return fail(Error::Protocol);
  JsonObjectConst reply=document["reply"].as<JsonObjectConst>();
  if(reply.isNull())return fail(Error::Protocol);
  if(reply["total"].is<uint64_t>()){
    const auto total=reply["total"].as<uint64_t>();
    if(!total||total>kMaxImage)return fail(Error::TooLarge);
    if(total_&&total_!=total)return fail(Error::Protocol);
    total_=static_cast<size_t>(total);
  }
  const uint8_t* payload=binary?frame+split+2:nullptr;
  size_t available=binary?length-split-2:0;
  if(!reply["mem_dl_param_size"].isNull()){
    if(!reply["mem_dl_param_size"].is<unsigned>())return fail(Error::Protocol);
    const auto metadata=reply["mem_dl_param_size"].as<unsigned>();
    if(!metadata||metadata>1024||metadata>available)return fail(Error::Protocol);
    JsonDocument meta;
    if(deserializeJson(meta,payload,metadata)||!meta["result"].is<int>()||meta["result"].as<int>()!=0)return fail(Error::Protocol);
    payload+=metadata;available-=metadata;
  }
  if(available){
    if(!total_||!reply["offset"].is<uint64_t>()||!reply["size"].is<uint64_t>())return fail(Error::Protocol);
    const auto offset=reply["offset"].as<uint64_t>();const auto size=reply["size"].as<uint64_t>();
    if(size!=available||offset>total_||size>total_-offset)return fail(Error::Protocol);
    if(offset<image_.size()){
      if(size>image_.size()-offset||memcmp(image_.data()+offset,payload,size)!=0)return fail(Error::Protocol);
    }else if(offset==image_.size()){
      image_.insert(image_.end(),payload,payload+available);
    }else return fail(Error::Protocol);
  }else if(reply["size"].is<uint64_t>()&&reply["size"].as<uint64_t>()!=0)return fail(Error::Protocol);
  if(result==0){
    if(!total_||image_.size()!=total_)return fail(Error::Protocol);
    if(!matches_md5(image_,reply["file_md5"]|static_cast<const char*>(nullptr)))return fail(Error::Integrity);
    if(image_.size()<4||image_[0]!=0xff||image_[1]!=0xd8||image_[image_.size()-2]!=0xff||image_.back()!=0xd9)return fail(Error::Integrity);
    done_=true;
  }
  return true;
}

std::string snapshot_request(uint32_t sequence){
  const auto n=std::to_string(sequence);
  const std::string parameters="{\"command\":\"get_project_file\",\"file_rel_path\":\"\",\"peer_host\":\"studio\",\"sequence_id\":"+n+",\"version\":1}";
  return "{\"mtype\":12289,\"cmdtype\":4,\"sequence\":"+n+",\"req\":{\"path\":\"mem:/26\",\"offset\":0,\"mem_dl_param_size\":"+std::to_string(parameters.size())+"}}\n\n"+parameters;
}

Error capture(const obn::camera::tutk::TutkSessionParams& params,BambuVector<uint8_t>& jpeg,unsigned timeout_ms){
  using Clock=std::chrono::steady_clock;
  if(params.uid.size()!=20||params.authkey.size()>32||params.passwd.empty()||params.passwd.size()>256)return Error::Protocol;
  jpeg.clear();SnapshotDownload transfer;std::mutex mutex;std::condition_variable changed;
  const auto deadline=Clock::now()+std::chrono::milliseconds(timeout_ms);
  obn::camera::tutk::TutkSession session;
  session.join(params,[&](const uint8_t* data,int size,int64_t,bool){
    std::lock_guard<std::mutex> lock(mutex);
    if(size>0)transfer.feed(data,static_cast<size_t>(size));
    changed.notify_one();
  },obn::camera::tutk::TutkSession::Mode::Ctrl);
  while(session.is_joined()&&!session.is_ready()&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(20));
  Error result=Error::Connect;
  if(session.is_ready()&&session.send_ctrl(snapshot_request())){
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait_until(lock,deadline,[&]{return transfer.done();});
    result=transfer.done()?transfer.error():Error::Timeout;
    if(result==Error::None)jpeg=transfer.take();
  }else if(Clock::now()>=deadline)result=Error::Timeout;
  session.leave();return result;
}
}
