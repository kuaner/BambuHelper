// Synthetic data only. Uses the actual firmware renderer, PNG decoder and fonts.
#include "display_p4.h"
#include "p4_camera_service.h"
#include "settings.h"
#include "fonts.h"
#include <cassert>
#include <fstream>
#include <vector>
#include <filesystem>
#include <cstdio>
DisplaySettings dispSettings = {};
P4UiSettings p4UiSettings;
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
  assert(!v.showCamera);
  {
    bool live=false,chosen=false; uint32_t seen=0;
    followP4CameraDefault(live,chosen,seen,1,false);
    assert(!live&&seen==1);
    followP4CameraDefault(live,chosen,seen,2,true);
    assert(live);
    chosen=true; live=false;
    followP4CameraDefault(live,chosen,seen,3,true);
    assert(!live);
  }
  // The remaining preview cases exercise both manually selected images.
  v.showCamera = true;
  lgfx::LGFX_Sprite frame; frame.setColorDepth(16); frame.createSprite(1024,600);
  auto render=[&](const char* name) {
    drawP4Dashboard(frame,p,v,1792937760,"192.0.2.30",true);
    std::ofstream out(output+"/"+name+".ppm",std::ios::binary);
    out<<"P6\n1024 600\n255\n";
    std::vector<lgfx::bgr888_t> row(1024);
    for(int y=0;y<600;++y) { frame.readRectRGB(0,y,1024,1,row.data()); out.write(reinterpret_cast<char*>(row.data()),row.size()*3); }
  };
  using namespace p4ui;
  render("dashboard-camera");
  std::vector<uint16_t> cameraPage(1024 * 600);
  frame.readRect(0, 0, 1024, 600, cameraPage.data());
  p4DashboardGesture(v, s, p4ui::Gesture::Tap, {180, 200});
  assert(v.page == -1 && !v.showCamera);
  render("dashboard-printer");
  std::vector<uint16_t> printerPage(1024 * 600);
  frame.readRect(0, 0, 1024, 600, printerPage.data());
  size_t outsideDifferences = 0;
  for (int y = 0; y < 600; ++y) for (int x = 0; x < 1024; ++x) {
    const bool picture = x >= IMAGE_X && x < IMAGE_X + IMAGE_W &&
                         y >= IMAGE_Y && y <= IMAGE_DOTS_Y + 5;
    const bool dots = y >= IMAGE_DOTS_Y - 5 && y <= IMAGE_DOTS_Y + 5;
    const bool timing = x >= IMAGE_X && x < IMAGE_X + IMAGE_W &&
                        y >= IMAGE_META_Y - 12 && y < IMAGE_META_Y + 12;
    if (!picture && !dots && !timing && cameraPage[y * 1024 + x] != printerPage[y * 1024 + x]) {
      if (++outsideDifferences < 5)
        std::fprintf(stderr, "Unexpected image-switch change at %d,%d\n", x, y);
    }
  }
  assert(outsideDifferences == 0);
  p4DashboardGesture(v, s, p4ui::Gesture::Tap, {180, 200});
  assert(v.showCamera);
  // Gestures are scoped to the rectangle where they began. A swipe-like tap
  // on the carousel must never switch AMS or latch its manual-page override.
  const auto originalAmsPage = v.amsPage;
  p4DashboardGesture(v,s,Gesture::Left,{180,200});
  assert(!v.showCamera && v.amsPage==originalAmsPage && !v.amsManual);
  p4DashboardGesture(v,s,Gesture::Right,{180,200});
  assert(v.showCamera && v.amsPage==originalAmsPage && !v.amsManual);
  p4DashboardGesture(v,s,Gesture::Left,{180,PANEL_NETWORK_Y});
  assert(v.showCamera && v.amsPage==originalAmsPage && !v.amsManual);
  p4DashboardGesture(v,s,Gesture::Left,{600,180});
  assert(v.showCamera && v.amsPage==originalAmsPage && !v.amsManual);
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
  s.ams.activeTray=5;followP4ActiveAms(v,s.ams);assert(v.amsPage==1&&!v.amsManual);
  s.ams.activeTray=0;followP4ActiveAms(v,s.ams);assert(v.amsPage==0);
  p4DashboardGesture(v,s,Gesture::Left,{600,450}); assert(v.amsPage==1); render("ams-zero-humidity");
  followP4ActiveAms(v,s.ams);assert(v.amsPage==1&&v.amsManual);
  assert(v.showCamera);
  p4DashboardGesture(v,s,Gesture::Tap,{500,180}); assert(v.page==-1);
  p4DashboardGesture(v,s,Gesture::Tap,{500,500}); assert(v.page==-1);
  p4DashboardGesture(v,s,Gesture::Tap,{180,200}); assert(v.page==-1&&!v.showCamera);
  p4DashboardGesture(v,s,Gesture::Tap,{180,200}); assert(v.page==-1&&v.showCamera);
  p4DashboardGesture(v,s,Gesture::Tap,{924,38}); assert(v.page==0); render("detail-job");
  p4DashboardGesture(v,s,Gesture::Tap,{370,110}); assert(v.page==1); render("detail-filament");
  p4DashboardGesture(v,s,Gesture::Left,{600,450}); assert(v.amsPage==2); render("external-spool");
  p4DashboardGesture(v,s,Gesture::Tap,{600,110}); assert(v.page==2); render("detail-device");
  p4DashboardGesture(v,s,Gesture::Tap,{840,110}); assert(v.page==3); render("detail-alerts-empty");
  s.printError=0x03008007; s.hmsCount=3;
  s.hms[0]={0x03001a00,0x00020002}; s.hms[1]=s.hms[0]; s.hms[2]=s.hms[0];
  render("detail-alerts");
  p4DashboardGesture(v,s,Gesture::Up,{600,450}); assert(v.alertOffset==2);
  p4DashboardGesture(v,s,Gesture::Tap,{980,35}); assert(v.page==4); render("settings");
  p4DashboardGesture(v,s,Gesture::Tap,{1000,38}); assert(v.page==4);
  p4DashboardGesture(v,s,Gesture::Tap,{1023,38}); assert(v.page==4);
  p4DashboardGesture(v,s,Gesture::Tap,{955,38}); assert(v.page==4);
  v.page=-1;
  p4DashboardGesture(v,s,Gesture::Tap,{949,38}); assert(v.page==0);
  p4DashboardGesture(v,s,Gesture::Tap,{1000,38}); assert(v.page==4);
  p4DashboardGesture(v,s,Gesture::Tap,{949,38}); assert(v.page==-1);
  v.page=4;
  p4DashboardGesture(v,s,Gesture::Tap,{924,35}); assert(v.page==-1);
  p4DashboardGesture(v,s,Gesture::Tap,{924,35}); assert(v.page==0);
  p4DashboardGesture(v,s,Gesture::Tap,{924,35}); assert(v.page==-1);
  s.connected=false; render("offline");
  s={}; snprintf(s.subtaskName,sizeof(s.subtaskName),"超长中文任务名称用于验证边界不会覆盖右侧信息或超出屏幕显示范围"); render("unknown-data");
  s.connected=true; s.gcodeStateId=GCODE_IDLE; s.ams.unitCount=6; s.ams.trayUnitCount=4;
  for(int i=0;i<6;++i) { s.ams.units[i].present=true; s.ams.units[i].id=i<4?i:128+i-4; }
  v.amsPage=5; render("overflow-ams-ht");
  snprintf(p.config.serial,sizeof(p.config.serial),"039-DEMO"); render("a1-idle");
  frame.setFont(nullptr); frame.deleteSprite(); releaseCachedFonts();
}
