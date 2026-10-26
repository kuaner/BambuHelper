#include "p4_runtime.h"
#if defined(BOARD_IS_WS_P4_7B)
#include "settings.h"
#include "wifi_manager.h"
#include "p4_camera_service.h"
#include <WiFi.h>
#include <atomic>
#include <cstring>
#include <new>
#include <esp_heap_caps.h>
#include <freertos/queue.h>

namespace {
// One producer and one consumer per mailbox. The UI, camera and diagnostics
// have independent cursors; they cannot consume each other's newest snapshot.
struct SharedSlot {
  SpscLatest<BambuState> stateForUi;
  SpscLatest<BambuState> stateForCamera;
  SpscLatest<MqttDiag> diagForUi;
  SpscLatest<MqttDiag> diagForHttp;
  SpscLatest<PrinterConfig> configForMqtt;
  SpscLatest<PrinterConfig> configForCamera;
};
SharedSlot* shared = nullptr;

enum WifiCommand : uint8_t { RebuildTransport = 1, Reconnect = 2, GotIp = 3 };
QueueHandle_t wifiCommands = nullptr;
std::atomic<bool> wifiStarted{false};
std::atomic<uint32_t> wifiCommandsDropped{0};

void publishConfig(uint8_t slot) {
  if (slot >= MAX_ACTIVE_PRINTERS) return;
  shared[slot].configForMqtt.update(printers[slot].config);
  shared[slot].configForCamera.update(printers[slot].config);
}

void postWifi(WifiCommand command) {
  // Failed rebuild hints do not accumulate: handleWiFi() has its own retry
  // policy, so one pending request is enough after a burst of socket errors.
  if (!wifiCommands || xQueueSend(wifiCommands, &command, 0) != pdPASS)
    wifiCommandsDropped.fetch_add(1, std::memory_order_relaxed);
}

void postWifiFromEvent(WifiCommand command) {
  if (!wifiCommands) return;
  BaseType_t wake = pdFALSE;
  if (xQueueSendFromISR(wifiCommands, &command, &wake) != pdPASS)
    wifiCommandsDropped.fetch_add(1, std::memory_order_relaxed);
  if (wake == pdTRUE) portYIELD_FROM_ISR();
}
}

void p4RuntimeStart() {
  if (!shared) {
    // The P4 has ample PSRAM; large immutable snapshots must not consume the
    // small internal DRAM region needed by TLS and the radio driver.
    shared = static_cast<SharedSlot*>(heap_caps_calloc(MAX_ACTIVE_PRINTERS, sizeof(SharedSlot),
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!shared) { Serial.println("P4 runtime: snapshot allocation failed"); return; }
    for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; ++i) new (&shared[i]) SharedSlot;
  }
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; ++i) publishConfig(i);
  p4RuntimeSync();
  p4WifiWorkerStart();
  p4MqttWorkerStart();
  startP4CameraService();
  tickP4CameraService();
  // Publishing configuration does not immediately force another capture; the
  // persistent camera owner keeps its normal interval.

}

void p4MqttPost(const P4MqttCommand& command) { p4MqttPostFallback(command); }

bool p4WorkerConfigSnapshot(uint8_t slot, PrinterConfig& config) {
  if (slot >= MAX_ACTIVE_PRINTERS) return false;
  const PrinterConfig* value = shared[slot].configForMqtt.readLatest();
  if (!value) return false;
  config = *value;
  return true;
}

bool p4CameraConfigSnapshot(uint8_t slot, PrinterConfig& config) {
  if (slot >= MAX_ACTIVE_PRINTERS) return false;
  const PrinterConfig* value = shared[slot].configForCamera.readLatest();
  if (!value) return false;
  config = *value;
  return true;
}

void p4WorkerPublishConfig(uint8_t slot, const PrinterConfig&) {
  // Config is committed by the setup/UI owner. Never publish from a worker or
  // retain a pointer to its private copy.
  publishConfig(slot);
}

void p4PublishMqttSlot(uint8_t slot, const BambuState& state, const MqttDiag& diag) {
  if (slot >= MAX_ACTIVE_PRINTERS) return;
  shared[slot].stateForUi.update(state);
  shared[slot].stateForCamera.update(state);
  shared[slot].diagForUi.update(diag);
  shared[slot].diagForHttp.update(diag);
}

bool p4MqttDiagSnapshot(uint8_t slot, MqttDiag& diag) {
  if (slot >= MAX_ACTIVE_PRINTERS) return false;
  const MqttDiag* value = shared[slot].diagForHttp.readLatest();
  if (!value) return false;
  diag = *value;
  return true;
}

bool p4CameraSnapshot(uint8_t slot, BambuState& state) {
  if (slot >= MAX_ACTIVE_PRINTERS) return false;
  const BambuState* value = shared[slot].stateForCamera.readLatest();
  if (!value) return false;
  state = *value;
  return true;
}

void requestWiFiTransportRebuild() { postWifi(RebuildTransport); }
void p4WifiReconnect() { postWifi(Reconnect); }
void p4WifiGotIp() { postWifiFromEvent(GotIp); }

void p4WifiWorkerStart() {
  if (wifiStarted.exchange(true)) return;
  wifiCommands = xQueueCreate(4, sizeof(uint8_t));
  if (!wifiCommands) { wifiStarted = false; return; }
  if (WiFi.status() == WL_CONNECTED) p4WifiReconnect();
  auto task = [](void*) {
    uint32_t lastHeartbeat = 0;
    for (;;) {
      uint8_t command;
      bool rebuild = false, reconnect = false, online = false;
      while (xQueueReceive(wifiCommands, &command, 0) == pdPASS) {
        rebuild |= command == RebuildTransport;
        reconnect |= command == Reconnect;
        online |= command == GotIp;
      }
      if (rebuild) recoverWiFiTransport();
      if (reconnect && !isAPMode()) refreshWiFiConnection();
      handleWiFi();
      if (online && WiFi.status() == WL_CONNECTED) p4WifiServiceOnline();
      const uint32_t now = millis();
      if (now - lastHeartbeat >= 10000) {
        lastHeartbeat = now;
        Serial.printf("[wifi-health] uptime=%lu status=%u heap=%u dropped=%lu\n",
                      now / 1000, unsigned(WiFi.status()), unsigned(ESP.getFreeHeap()),
                      (unsigned long)wifiCommandsDropped.exchange(0));
      }
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  };
  if (xTaskCreate(task, "p4_wifi", 8192, nullptr, 2, nullptr) != pdPASS) {
    vQueueDelete(wifiCommands);
    wifiCommands = nullptr;
    wifiStarted = false;
  }
}

void p4RuntimeSync() {
  if (!shared) return;
  for (uint8_t i = 0; i < MAX_ACTIVE_PRINTERS; ++i) {
    const BambuState* value = shared[i].stateForUi.readLatest();
    if (!value) continue;
    BambuState snapshot = *value;
    // Only presentation latches belong to the main/UI owner. A protocol
    // snapshot contains live printer state, not notification history.
    snapshot.finishBuzzerPlayed = printers[i].state.finishBuzzerPlayed;
    snapshot.finishEpoch = printers[i].state.finishEpoch;
    snapshot.finishTimeLatched = printers[i].state.finishTimeLatched;
    snapshot.doorAcknowledged = printers[i].state.doorAcknowledged;
    snapshot.lightOffDueMs = 0;
    snapshot.bedCooldownAlertArmed = false;
    snapshot.ctrlCmdSentMs = 0;
    printers[i].state = snapshot;
  }
}
#endif
