// SPDX-License-Identifier: AGPL-3.0-only
#include <Snapshot.hpp>
#include <Sequence.hpp>
#include <ArduinoJson.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <openssl/evp.h>
using bambu_camera::SnapshotDownload;
using bambu_camera::Error;

static std::string md5(const std::string& s){
  unsigned char digest[16];unsigned size=0;assert(EVP_Digest(s.data(),s.size(),digest,&size,EVP_md5(),nullptr)==1);
  std::string out;for(auto b:digest){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;
}
static bool feed(SnapshotDownload& d,const std::string& f){return d.feed(reinterpret_cast<const uint8_t*>(f.data()),f.size());}
static std::string packet(int result,size_t offset,const std::string& bytes,size_t total,const std::string& hash=""){
  JsonDocument d;d["cmdtype"]=4;d["sequence"]=1;d["result"]=result;
  auto r=d["reply"].to<JsonObject>();r["offset"]=offset;r["size"]=bytes.size();r["total"]=total;if(!hash.empty())r["file_md5"]=hash;
  std::string out;serializeJson(d,out);return out+"\n\n"+bytes;
}
int main(){
  const std::string image="\xff\xd8"+std::string("image payload")+"\xff\xd9";
  SnapshotDownload d;
  assert(feed(d,"{\"cmdtype\":4,\"sequence\":1,\"result\":1,\"reply\":{\"total\":"+std::to_string(image.size())+",\"size\":0,\"mem_dl_param_size\":12}}\n\n{\"result\":0}"));
  assert(d.bytes().empty());assert(!d.done());
  assert(feed(d,packet(1,0,image.substr(0,5),image.size())));
  assert(feed(d,packet(1,0,image.substr(0,5),image.size()))); // identical retransmit
  assert(feed(d,packet(0,5,image.substr(5),image.size(),md5(image))));
  assert(d.done()&&d.error()==Error::None&&d.bytes().size()==image.size());
  SnapshotDownload corrupt;assert(!feed(corrupt,packet(0,0,image,image.size(),std::string(32,'0'))));assert(corrupt.error()==Error::Integrity);
  SnapshotDownload large;assert(!feed(large,packet(1,0,"x",SnapshotDownload::kMaxImage+1)));assert(large.error()==Error::TooLarge);
  SnapshotDownload hole;assert(!feed(hole,packet(1,2,"x",10)));assert(hole.error()==Error::Protocol);
  assert(hole.failure()==bambu_camera::SnapshotFailure::Gap);
  SnapshotDownload dup;assert(feed(dup,packet(1,0,"xx",5)));assert(!feed(dup,packet(1,0,"yy",5)));
  SnapshotDownload meta;assert(!feed(meta,"{\"cmdtype\":4,\"sequence\":1,\"result\":1,\"reply\":{\"mem_dl_param_size\":99}}\n\nx"));
  SnapshotDownload unfinished;assert(!feed(unfinished,packet(0,0,image,image.size()+1,md5(image))));
  SnapshotDownload invalid;assert(!feed(invalid,"bad json"));
  assert(invalid.failure()==bambu_camera::SnapshotFailure::HeaderJson);
  {
    // Completion publishes the full assembled buffer to another thread.
    // Later transport callbacks must not touch the moved-out image.
    bambu_camera::SnapshotReceiver receiver;
    const std::string first=packet(1,0,image.substr(0,5),image.size());
    const std::string last=packet(0,5,image.substr(5),image.size(),md5(image));
    std::thread writer([&] {
      receiver.feed(reinterpret_cast<const uint8_t*>(first.data()),first.size());
      receiver.feed(reinterpret_cast<const uint8_t*>(last.data()),last.size());
      for (int i=0;i<1000;++i) receiver.feed(reinterpret_cast<const uint8_t*>(last.data()),last.size());
    });
    while(!receiver.done()) std::this_thread::yield();
    assert(receiver.error()==Error::None && receiver.failure()==bambu_camera::SnapshotFailure::None);
    const auto received=receiver.take();
    assert(received.size()==image.size() && std::memcmp(received.data(),image.data(),image.size())==0);
    writer.join();
  }
  const auto request=bambu_camera::snapshot_request(4);const auto split=request.find("\n\n");JsonDocument r,p;
  assert(!deserializeJson(r,request.substr(0,split)));assert(!deserializeJson(p,request.substr(split+2)));
  assert(r["sequence"]==4&&p["sequence_id"]==4);assert(r["req"]["mem_dl_param_size"]==request.size()-split-2);
  assert(r["req"]["path"]=="mem:/26");
  std::cout<<"snapshot protocol: metadata, retransmits, bounds, offsets, integrity, request framing passed\n";
}
