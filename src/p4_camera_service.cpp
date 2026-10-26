#include "p4_camera_service.h"
#if defined(BOARD_IS_WS_P4_7B) && defined(P4_CLOUD_CAMERA_EXPERIMENTAL)
#include "p4_cloud_camera.h"
#include "settings.h"
#include "cloud_login.h"
#include "bambu_mqtt.h"
#include "wifi_manager.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include "p4_runtime.h"
#include "web_server.h"
#include "display_ui.h"
#include "display_p4.h"
#include "p4_dashboard_layout.h"

extern WebServer server;
namespace {
constexpr const char* kNamespace = "p4cam";
constexpr size_t kAuthLimit = 8000;
constexpr uint32_t kInterval = 60000;

struct ImageFrame {
  uint16_t pixels[p4ui::IMAGE_W * p4ui::IMAGE_CONTENT_H]{};
  char serial[20]{};
  uint8_t slot = 255;
};
struct NameFrame {
  char task[24]{}, serial[20]{}, name[128]{};
  uint16_t plate = 0;
};
struct CameraStatus {
  char phase[32] = "idle";
  bool busy = false;
  bool automatic = false;
  bool attempted = false;
  unsigned failures = 0;
  uint32_t durationMs = 0;
  uint32_t stackMin = 0;
  size_t minInternal = 0;
  size_t minPsram = 0;
  P4CameraResult result{};
};

// Camera is the sole producer; UI and HTTP each have their own status
// consumer. The decoded image and resolved name are read only by the UI.
SpscLatest<ImageFrame>* publishedImage = nullptr;
SpscLatest<NameFrame> publishedName;
SpscLatest<CameraStatus> statusForUi;
SpscLatest<CameraStatus> statusForHttp;
std::atomic<uint32_t> generation{0};
std::atomic<uint32_t> trigger{0};
std::atomic<bool> enabled{true};
std::atomic<bool> started{false};
std::atomic<bool> automatic{false};
std::atomic<uint8_t> selectedSlot{255};
CameraStatus status;
char cachedTask[24]{};
char cachedSerial[20]{};
uint16_t cachedPlate = 0;
char cachedName[128]{};

struct Job {
  String token, serial, firmware, certificate, key, platform, executable, deviceId, taskId;
  uint16_t plateIndex = 0;
  bool lookupName = false;
  CloudRegion region = REGION_CN;
  uint8_t slot = 255;
  ~Job() {
    if (token.length()) memset(token.begin(), 0, token.length());
    if (key.length()) memset(key.begin(), 0, key.length());
  }
};

void publishStatus() {
  statusForUi.update(status);
  statusForHttp.update(status);
}

void stage(const char* name) {
  strlcpy(status.phase, name, sizeof(status.phase));
  publishStatus();
  Serial.printf("[camera] stage=%s internal=%u psram=%u\n", name,
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                unsigned(ESP.getFreePsram()));
}

void sendJson(int code, JsonDocument& document) {
  String body;
  serializeJson(document, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}
void reply(int code, const char* state) {
  JsonDocument document;
  document["status"] = state;
  sendJson(code, document);
}

bool loadToken(char* out, size_t capacity) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) return false;
  const size_t length = prefs.isKey("cl_tok2") ? prefs.getBytesLength("cl_tok2") : 0;
  bool ok = false;
  if (length && length < capacity) {
    ok = prefs.getBytes("cl_tok2", out, capacity) == length;
    out[ok ? length : 0] = 0;
  } else if (!length) {
    const String legacy = prefs.getString("cl_token", "");
    if (legacy.length() && legacy.length() < capacity) {
      memcpy(out, legacy.c_str(), legacy.length() + 1);
      ok = true;
    }
  }
  prefs.end();
  return ok;
}

std::unique_ptr<Job> readJob(uint8_t slot) {
  if (slot >= MAX_ACTIVE_PRINTERS) return {};
  PrinterConfig config{};
  if (!p4CameraConfigSnapshot(slot, config) || config.mode == CONN_LOCAL || !config.serial[0]) return {};
  if (!enabled.load(std::memory_order_acquire)) return {};
  BambuState snapshot{};
  if (!p4CameraSnapshot(slot, snapshot) || !snapshot.connected) return {};
  auto job = std::unique_ptr<Job>(new(std::nothrow) Job);
  if (!job) return {};
  BambuVector<char> token(CLOUD_TOKEN_MAX);
  if (!loadToken(token.data(), token.size()) || !token[0]) return {};
  job->token = token.data();
  memset(token.data(), 0, token.size());
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return {};
  const size_t bytes = prefs.getBytesLength("auth");
  if (!bytes || bytes > kAuthLimit) { prefs.end(); return {}; }
  BambuVector<char> auth(bytes + 1);
  const size_t read = prefs.getBytes("auth", auth.data(), bytes);
  prefs.end();
  if (read != bytes) return {};
  auth[bytes] = 0;
  JsonDocument document;
  if (deserializeJson(document, auth.data(), bytes)) return {};
  job->certificate = document["certificate"] | "";
  job->key = document["private_key"] | "";
  job->platform = document["platform"] | "macos";
  job->firmware = document["firmware"] | "";
  job->deviceId = document["device_id"] | "";
  serializeJson(document["executable_info"], job->executable);
  memset(auth.data(), 0, auth.size());
  job->serial = config.serial;
  job->region = config.region;
  job->slot = slot;
  job->taskId = snapshot.taskId;
  job->plateIndex = snapshot.plateIdx;
  job->lookupName = !(strcmp(job->taskId.c_str(), cachedTask) == 0 &&
                      strcmp(job->serial.c_str(), cachedSerial) == 0 &&
                      job->plateIndex == cachedPlate && cachedName[0]);
  return job;
}

bool decodeImage(const BambuVector<uint8_t>& jpeg, const Job& job) {
  stage("decode");
  ImageFrame* frame = publishedImage->beginWrite();
  if (!frame) return false;
  lgfx::LGFX_Sprite decoded;
  decoded.setPsram(true);
  decoded.setColorDepth(16);
  if (!decoded.createSprite(p4ui::IMAGE_W, p4ui::IMAGE_CONTENT_H)) return false;
  // Preserve the whole source frame. Its 16:9 aspect ratio matches this box,
  // so no crop, border, zoom or source-dependent layout can occur.
  decoded.fillSprite(0xe739);  // PAPER, RGB565
  const bool ok = decoded.drawJpg(jpeg.data(), jpeg.size(), 0, 0,
                                  p4ui::IMAGE_W, p4ui::IMAGE_CONTENT_H,
                                  0, 0, 0, 0, lgfx::datum_t::middle_center);
  if (ok) {
    decoded.readRect(0, 0, p4ui::IMAGE_W, p4ui::IMAGE_CONTENT_H, frame->pixels);
    strlcpy(frame->serial, job.serial.c_str(), sizeof(frame->serial));
    frame->slot = job.slot;
    publishedImage->publish();
    generation.fetch_add(1, std::memory_order_release);
  }
  decoded.deleteSprite();
  return ok;
}

void capture(uint8_t slot) {
  auto job = readJob(slot);
  if (!job) {
    strlcpy(status.phase, "credentials_required", sizeof(status.phase));
    status.attempted = true;
    status.busy = false;
    publishStatus();
    return;
  }
  const uint32_t began = millis();
  status.busy = true;
  status.attempted = true;
  status.minInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  status.minPsram = ESP.getFreePsram();
  stage("starting");
  P4CameraAccount account;
  account.token = job->token.c_str();
  account.serial = job->serial.c_str();
  account.firmware = job->firmware.c_str();
  account.region = job->region;
  account.appCertificate = job->certificate.c_str();
  account.appPrivateKey = job->key.c_str();
  account.appPlatform = job->platform.c_str();
  account.executableInfo = job->executable.c_str();
  account.progress = stage;
  account.appDeviceId = job->deviceId.c_str();
  account.taskId = job->lookupName ? job->taskId.c_str() : nullptr;
  account.plateIndex = job->plateIndex;
  try {
    BambuVector<uint8_t> jpeg;
    P4CameraResult imageResult = p4CaptureCloudSnapshotFromAccount(account, jpeg);
    bool decoded = false;
    if (imageResult.error == bambu_camera::Error::None) {
      decoded = decodeImage(jpeg, *job);
      if (!decoded) imageResult.error = bambu_camera::Error::Protocol;
    }
    // The image and the design name are independent. A slow/failed metadata
    // query can neither erase a successful photo nor turn it into an error.
    if (job->lookupName) {
      P4CameraResult metadataResult;
      p4FetchCloudJobName(account, metadataResult);
      imageResult.metadataHttpStatus = metadataResult.metadataHttpStatus;
      if (metadataResult.jobName[0]) {
        strlcpy(cachedTask, job->taskId.c_str(), sizeof(cachedTask));
        strlcpy(cachedSerial, job->serial.c_str(), sizeof(cachedSerial));
        cachedPlate = job->plateIndex;
        strlcpy(cachedName, metadataResult.jobName, sizeof(cachedName));
        if (NameFrame* name = publishedName.beginWrite()) {
          strlcpy(name->name, cachedName, sizeof(name->name));
          strlcpy(name->task, cachedTask, sizeof(name->task));
          strlcpy(name->serial, cachedSerial, sizeof(name->serial));
          name->plate = cachedPlate;
          publishedName.publish();
          generation.fetch_add(1, std::memory_order_release);
        }
      }
    }
    status.result = imageResult;
    const bool ok = imageResult.error == bambu_camera::Error::None;
    status.failures = ok ? 0 : std::min(status.failures + 1, 4u);
    strlcpy(status.phase, ok ? "ready" : bambu_camera::error_name(imageResult.error), sizeof(status.phase));
  } catch (...) {
    status.result = P4CameraResult{};
    status.result.error = bambu_camera::Error::TooLarge;
    status.failures = std::min(status.failures + 1, 4u);
    strlcpy(status.phase, "allocation_failed", sizeof(status.phase));
  }
  status.stackMin = uxTaskGetStackHighWaterMark(nullptr);
  status.durationMs = millis() - began;
  status.busy = false;
  publishStatus();
  Serial.printf("[camera] result=%s duration=%u internal_min=%u stack_min=%u\n",
                status.phase, unsigned(status.durationMs), unsigned(status.minInternal), unsigned(status.stackMin));
}

void cameraTask(void*) {
  uint32_t handled = trigger.load();
  uint32_t nextAttempt = millis() + 10000;
  Preferences prefs;
  if (prefs.begin(kNamespace, true)) {
    enabled = !prefs.isKey("enabled") || prefs.getBool("enabled", true);
    automatic = prefs.isKey("auth") && prefs.getBool("auto", true);
    status.automatic = automatic.load();
    prefs.end();
  }
  publishStatus();
  for (;;) {
    if (WiFi.status() != WL_CONNECTED || isAPMode() || isOtaAutoInProgress()) nextAttempt = millis() + 5000;
    const uint32_t requested = trigger.load();
    const bool manual = requested != handled;
    if (manual) handled = requested;
    status.automatic = automatic.load();
    const bool scheduled = status.automatic && enabled.load(std::memory_order_acquire) && int32_t(millis() - nextAttempt) >= 0;
    const uint8_t slot = selectedSlot.load();
    const bool visible = slot < MAX_ACTIVE_PRINTERS && p4DashboardActive();
    if ((manual || scheduled) && WiFi.status() == WL_CONNECTED && !isAPMode() &&
        !isOtaAutoInProgress() && visible) {
      capture(slot);
      nextAttempt = millis() + (kInterval << status.failures);
    } else if (manual || scheduled) {
      nextAttempt = millis() + 5000;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void startWorker() {
  if (started.exchange(true)) return;
  constexpr size_t bytes = 32 * 1024;
  auto* stack = static_cast<StackType_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  static StaticTask_t control;
  if (!stack || !xTaskCreateStatic(cameraTask, "p4_camera", bytes / sizeof(StackType_t), nullptr, 1,
                                   stack, &control)) {
    if (stack) heap_caps_free(stack);
    started = false;
    strlcpy(status.phase, "task_allocation_failed", sizeof(status.phase));
    publishStatus();
  }
}

void credentials() {
  const String body = server.arg("plain");
  if (!body.length() || body.length() > kAuthLimit) { reply(400, "invalid_credentials_size"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, body)) { reply(400, "invalid_json"); return; }
  const String cert = doc["certificate"] | "", key = doc["private_key"] | "";
  const String platform = doc["platform"] | "macos", firmware = doc["firmware"] | "";
  if (!cert.startsWith("-----BEGIN CERTIFICATE-----") || cert.length() > 2500 ||
      !key.startsWith("-----BEGIN ") || key.length() > 2500 || firmware.length() > 32 ||
      (platform != "macos" && platform != "windows" && platform != "linux") ||
      !doc["executable_info"].is<JsonObject>()) {
    reply(400, "invalid_credentials");
    return;
  }
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) { reply(507, "nvs_unavailable"); return; }
  const bool ok = prefs.putBytes("auth", body.c_str(), body.length()) == body.length() && prefs.putBool("enabled", true) == 1;
  prefs.end();
  if (ok) { enabled = true; trigger.fetch_add(1); }
  reply(ok ? 200 : 507, ok ? "credentials_saved" : "nvs_write_failed");
}

void httpStatus() {
  CameraStatus current;
  if (const CameraStatus* value = statusForHttp.readLatest()) current = *value;
  JsonDocument doc;
  doc["phase"] = current.phase;
  doc["busy"] = current.busy;
  doc["automatic"] = current.automatic;
  doc["generation"] = generation.load();
  doc["interval_ms"] = kInterval;
  doc["failures"] = current.failures;
  doc["internal_free"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  doc["psram_free"] = ESP.getFreePsram();
  doc["internal_min_capture"] = current.minInternal;
  doc["psram_min_capture"] = current.minPsram;
  doc["worker_stack_min_bytes"] = current.stackMin;
  if (current.attempted && !current.busy) {
    doc["error"] = bambu_camera::error_name(current.result.error);
    doc["http_status"] = current.result.httpStatus;
    doc["service_code"] = current.result.serviceCode;
    doc["duration_ms"] = current.durationMs;
    doc["crypto_step"] = current.result.cryptoStep;
    doc["crypto_status"] = current.result.cryptoStatus;
    doc["metadata_http"] = current.result.metadataHttpStatus;
  }
  doc["rgb_ready"] = generation.load() > 0;
  doc["job_name"] = generation.load() ? "see_dashboard" : "pending";
  sendJson(200, doc);
}
}

void initP4CameraService() {
  if (!publishedImage) {
    void* memory = heap_caps_calloc(1, sizeof(SpscLatest<ImageFrame>), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory) publishedImage = new (memory) SpscLatest<ImageFrame>;
  }
  // Registration is cheap; the persistent worker starts with the runtime after
  // configuration snapshots are ready, not in the HTTP callback.
  server.on("/camera/credentials", HTTP_POST, credentials);
  server.on("/camera/credentials", HTTP_DELETE, [] {
    Preferences prefs;
    enabled = false;
    const bool ok = prefs.begin(kNamespace, false) && prefs.clear();
    prefs.end();
    trigger.fetch_add(1);
    reply(ok ? 200 : 507, ok ? "credentials_removed" : "nvs_unavailable");
  });
  server.on("/camera/capture", HTTP_POST, [] {
    trigger.fetch_add(1);
    reply(202, "capture_queued");
  });
  server.on("/camera/status", HTTP_GET, httpStatus);
  server.on("/camera/automatic", HTTP_POST, [] {
    const bool enabled = server.arg("enabled") == "1";
    Preferences prefs;
    const bool ok = prefs.begin(kNamespace, false) && prefs.putBool("auto", enabled) == 1;
    prefs.end();
    if (ok) {
      automatic = enabled;
      if (const CameraStatus* value = statusForHttp.readLatest()) {
        CameraStatus next = *value;
        next.automatic = enabled;
        statusForHttp.update(next);
      }
      trigger.fetch_add(1);
    }
    reply(ok ? 200 : 507, ok ? "preference_saved" : "nvs_write_failed");
  });
}

void startP4CameraService() {
  selectedSlot = rotState.displayIndex;
  startWorker();
}

void tickP4CameraService() {
  // UI only selects a printer and queues work. There is no join, network wait,
  // decode, NVS read, or short-lived worker creation on this path.
  if (!started.load()) return;
  const uint8_t slot = rotState.displayIndex;
  if (slot != selectedSlot) {
    selectedSlot = slot;
    trigger.fetch_add(1);
  }
}

uint32_t p4CameraGeneration() { return generation.load(); }

bool p4CameraBusy() {
  if (const CameraStatus* value = statusForUi.readLatest()) return value->busy;
  return false;
}

bool p4CameraJobName(const PrinterSlot& printer, char* out, size_t capacity) {
  if (!out || !capacity) return false;
  out[0] = 0;
  const auto* name = publishedName.readLatest();
  if (!name || !name->name[0] || strcmp(printer.config.serial, name->serial) ||
      strcmp(printer.state.taskId, name->task) || printer.state.plateIdx != name->plate) return false;
  strlcpy(out, name->name, capacity);
  return true;
}

bool drawP4Camera(lgfx::LovyanGFX& target, const PrinterSlot& printer, int x, int y) {
  if (!publishedImage) return false;
  const auto* frame = publishedImage->readLatest();
  if (!frame || frame->slot != rotState.displayIndex || strcmp(frame->serial, printer.config.serial)) return false;
  target.pushImage(x, y, p4ui::IMAGE_W, p4ui::IMAGE_CONTENT_H, frame->pixels);
  return true;
}
#endif
