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
  const auto result=bambu_camera::capture(params,jpeg);
  std::cout<<"result="<<bambu_camera::error_name(result)<<" bytes="<<jpeg.size()<<"\n";
  if(result!=bambu_camera::Error::None)return 1;
  std::ofstream output(argv[2],std::ios::binary|std::ios::trunc);
  output.write(reinterpret_cast<const char*>(jpeg.data()),jpeg.size());
  return output?0:1;
}
