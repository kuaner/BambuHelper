// Synthetic data only. Uses the actual firmware renderer, PNG decoder and fonts.
#include "display_p4.h"
#include "settings.h"
#include "fonts.h"
#include <cassert>
#include <fstream>
#include <vector>
#include <filesystem>
DisplaySettings dispSettings = {};
int main(int argc,char** argv) {
  const std::string output=argc>1?argv[1]:".cache/p4-preview";
  std::filesystem::create_directories(output);
  PrinterSlot p={};
  snprintf(p.config.name,sizeof(p.config.name),"演示打印机");
  snprintf(p.config.serial,sizeof(p.config.serial),"20P-DEMO");
  snprintf(p.config.ip,sizeof(p.config.ip),"192.0.2.20");
  auto& s=p.state;
  s.connected=s.printing=s.dualNozzle=true; s.activeNozzle=1;
  s.gcodeStateId=GCODE_RUNNING; s.progress=52; s.remainingMinutes=536;
  s.layerNum=488; s.totalLayers=1272; s.p4Known=0xffffffff;
  s.nozzleTempN[0]=54; s.nozzleTempN[1]=250; s.nozzleTargetN[1]=250;
  s.bedTemp=s.bedTarget=60; s.chamberTemp=45;
  s.wifiSignal=-42; s.speedLevel=2; s.coolingFanPct=30; s.chamberFanPct=80;
  s.nozzleDiameter=.4f; snprintf(s.nozzleType,sizeof(s.nozzleType),"hardened_steel");
  snprintf(s.subtaskName,sizeof(s.subtaskName),"像素风可堆叠大容量收纳箱");
  s.ams.present=true; s.ams.unitCount=s.ams.trayUnitCount=2; s.ams.activeTray=0;
  const uint16_t colors[]={0xfd00,0x04bb,0xcdd6,0x0248};
  const int remains[]={60,62,91,19};
  for(int i=0;i<2;++i) {
    s.ams.units[i].present=true; s.ams.units[i].id=i; s.ams.units[i].trayCount=4;
    s.ams.units[i].temp=31; s.ams.units[i].humidityRawKnown=true;
    s.ams.units[i].humidityRaw=i?0:5;
    for(int j=0;j<4;++j) {
      auto& t=s.ams.trays[i*4+j]; t.present=true; t.colorRgb565=colors[j]; t.remain=remains[j];
      snprintf(t.type,sizeof(t.type),"%s",j==1?"PLA Basic":"PETG Basic");
    }
  }
  s.ams.vtPresent=true; snprintf(s.ams.vtType,sizeof(s.ams.vtType),"PLA");
  P4DashboardView v;
  lgfx::LGFX_Sprite frame; frame.setColorDepth(16); frame.createSprite(1024,600);
  auto render=[&](const char* name) {
    drawP4Dashboard(frame,p,v,1792937760,"192.0.2.30",true);
    std::ofstream out(output+"/"+name+".ppm",std::ios::binary);
    out<<"P6\n1024 600\n255\n";
    std::vector<lgfx::bgr888_t> row(1024);
    for(int y=0;y<600;++y) { frame.readRectRGB(0,y,1024,1,row.data()); out.write(reinterpret_cast<char*>(row.data()),row.size()*3); }
  };
  render("dashboard-demo");
  const auto gradientA = p4ui::p4ProgressPixel(0, 340, 300, 0x65d9);
  const auto gradientB = p4ui::p4ProgressPixel(339, 340, 300, 0x65d9);
  assert(gradientA != gradientB);
  assert(p4ui::p4ProgressPixel(0, 340, 300, 0x65d9) ==
         p4ui::p4ProgressPixel(0, 340, 1500, 0x65d9));
  const int untouched = frame.readPixel(346, 96);
  drawP4Progress(frame,p4ui::CONTENT,p4ui::PROGRESS_Y,p4ui::RIGHT-p4ui::CONTENT,52,300,accentP4(p.state));
  assert(frame.readPixel(400,p4ui::PROGRESS_Y+3) != frame.readPixel(600,p4ui::PROGRESS_Y+3));
  assert(frame.readPixel(346,96) == untouched);
  frame.setFont(cachedFont(FONT_P4_PROGRESS));
  assert(frame.textWidth("100")<235); frame.setFont(nullptr);
  s.progress=100; render("progress-100"); s.progress=52;
  // Navigation tests execute the same hit targets used by the firmware.
  using namespace p4ui;
  p4DashboardGesture(v,s,Gesture::Left,{600,450}); assert(v.amsPage==1); render("ams-zero-humidity");
  p4DashboardGesture(v,s,Gesture::Tap,{500,180}); assert(v.page==0); render("detail-job");
  p4DashboardGesture(v,s,Gesture::Tap,{370,110}); assert(v.page==1); render("detail-filament");
  p4DashboardGesture(v,s,Gesture::Left,{600,450}); assert(v.amsPage==2); render("external-spool");
  p4DashboardGesture(v,s,Gesture::Tap,{600,110}); assert(v.page==2); render("detail-device");
  p4DashboardGesture(v,s,Gesture::Tap,{840,110}); assert(v.page==3); render("detail-alerts-empty");
  s.printError=0x03008007; s.hmsCount=3;
  s.hms[0]={0x03001a00,0x00020002}; s.hms[1]=s.hms[0]; s.hms[2]=s.hms[0];
  render("detail-alerts");
  p4DashboardGesture(v,s,Gesture::Up,{600,450}); assert(v.alertOffset==2);
  p4DashboardGesture(v,s,Gesture::Tap,{980,35}); assert(v.page==4); render("settings");
  p4DashboardGesture(v,s,Gesture::Tap,{925,35}); assert(v.page==-1);
  assert(p4DashboardGesture(v,s,Gesture::Tap,{925,35})==P4Action::Refresh);
  s.connected=false; render("offline");
  s={}; snprintf(s.subtaskName,sizeof(s.subtaskName),"超长中文任务名称用于验证边界不会覆盖右侧信息或超出屏幕显示范围"); render("unknown-data");
  s.connected=true; s.gcodeStateId=GCODE_IDLE; s.ams.unitCount=6; s.ams.trayUnitCount=4;
  for(int i=0;i<6;++i) { s.ams.units[i].present=true; s.ams.units[i].id=i<4?i:128+i-4; }
  v.amsPage=5; render("overflow-ams-ht");
  snprintf(p.config.serial,sizeof(p.config.serial),"039-DEMO"); render("a1-idle");
  frame.setFont(nullptr); frame.deleteSprite(); releaseCachedFonts();
}
