#pragma once
#if defined(BOARD_IS_WS_P4_7B)
#include "bambu_state.h"
#include "bambu_mqtt.h"
#include "spsc_latest.h"
#include <cstdint>

struct P4MqttCommand {
  enum Kind : uint8_t { Configure, Refresh, Reconnect };
  Kind kind = Configure;
  uint8_t slot = 0;
};

void p4RuntimeStart();
void p4RuntimeSync();
void p4WifiWorkerStart();
void p4WifiReconnect();
void p4WifiGotIp();
void p4MqttWorkerStart();
void p4MqttPost(const P4MqttCommand& command);
void p4MqttPostFallback(const P4MqttCommand& command);
bool p4WorkerConfigSnapshot(uint8_t slot, PrinterConfig& config);
bool p4CameraConfigSnapshot(uint8_t slot, PrinterConfig& config);
void p4WorkerPublishConfig(uint8_t slot, const PrinterConfig& config);
void p4PublishMqttSlot(uint8_t slot, const BambuState& state, const MqttDiag& diag);
bool p4MqttDiagSnapshot(uint8_t slot, MqttDiag& diag);
bool p4CameraSnapshot(uint8_t slot, BambuState& state);
void requestWiFiTransportRebuild();
#else
inline void p4RuntimeStart() {}
inline void p4RuntimeSync() {}
inline void initP4CameraService() {}
inline void startP4CameraService() {}
inline void tickP4CameraService() {}
#endif
