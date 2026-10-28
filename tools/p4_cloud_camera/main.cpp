// SPDX-License-Identifier: AGPL-3.0-only
#include <Snapshot.hpp>
#include <ArduinoJson.h>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <obn/log.hpp>

int main(int argc,char** argv){
  if(argc!=3){std::cerr<<"Usage: camera_probe PRIVATE_TICKET_JSON OUTPUT_JPEG\n";return 2;}
  umask(0077);std::ifstream input(argv[1]);JsonDocument ticket;
  if(!input||deserializeJson(ticket,input)){std::cerr<<"Invalid ticket file\n";return 2;}
  JsonVariantConst root=ticket.as<JsonVariantConst>();
  if(root["data"].is<JsonObjectConst>())root=root["data"];
  obn::camera::tutk::TutkSessionParams params;
  params.uid=root["ttcode"]|"";params.passwd=root["passwd"]|"";
  params.authkey=root["authkey"]|"";params.region=root["region"]|"cn";params.relay_id=params.uid;
#ifdef BAMBU_CAMERA_DEBUG
  bambu_camera_debug::secrets={params.uid,params.passwd,params.authkey};
#endif
  BambuVector<uint8_t> jpeg;
  bambu_camera::SnapshotDiagnostics diagnostics;
  const auto result=bambu_camera::capture(params,jpeg,45000,nullptr,nullptr,&diagnostics);
  std::cout<<"result="<<bambu_camera::error_name(result)<<" bytes="<<jpeg.size()<<"\n";
  std::cout<<"transport path="<<unsigned(diagnostics.connectionPath)
           <<" datagrams="<<diagnostics.transportDatagrams
           <<" framed="<<diagnostics.transportFramedPackets
           <<" frames="<<diagnostics.transportFramesEmitted
           <<" fec_recovered="<<diagnostics.fecRecovered
           <<" fec_lost="<<diagnostics.fecLost
           <<" frame_drops="<<diagnostics.transportFrameDrops
           <<" seq_gaps="<<diagnostics.avSequenceGaps
           <<" seq_reordered="<<diagnostics.avSequenceReordered
           <<" invalid_slices="<<diagnostics.invalidSliceHeaders
           <<" pending_slices="<<diagnostics.pendingFrameSlices
           <<"/"<<diagnostics.pendingFrameExpectedSlices
           <<" send_failures="<<diagnostics.appSendFailures
           <<" rcvbuf="<<diagnostics.socketReceiveBufferBytes<<"\n";
  if(result!=bambu_camera::Error::None)return 1;
  std::ofstream output(argv[2],std::ios::binary|std::ios::trunc);
  output.write(reinterpret_cast<const char*>(jpeg.data()),jpeg.size());
  return output?0:1;
}
