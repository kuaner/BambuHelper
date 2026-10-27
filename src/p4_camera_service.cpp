#define P4_SETTINGS_NO_ALIAS
#include "p4_camera_service.h"
#if defined(BOARD_IS_WS_P4_7B) && defined(P4_CLOUD_CAMERA_EXPERIMENTAL)
#include "p4_cloud_camera.h"
#include "settings.h"
#include "cloud_login.h"
#include "bambu_mqtt.h"
#include "wifi_manager.h"
#include <ArduinoJson.h>
#include <WebServer.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include "p4_runtime.h"
#include "p4_cloud_worker.h"
#include "web_server.h"
#include "display_ui.h"
#include "display_p4.h"
#include "p4_dashboard_layout.h"
#include "p4_camera_schedule.h"
#include "p4_json_allocator.h"

extern WebServer server;
namespace {
constexpr size_t kAuthLimit = 8000;
constexpr uint32_t kInterval = p4CameraRetryDelayMs(0);

struct ImageFrame {
  uint16_t pixels[p4ui::IMAGE_W * p4ui::IMAGE_CONTENT_H]{};
  char serial[20]{};
  char task[24]{};
  uint16_t plate = 0;
  uint8_t slot = 255;
  time_t capturedAt = 0;
};
struct NameFrame {
  char task[24]{}, serial[20]{}, name[128]{};
  uint16_t plate = 0;
};
struct CameraStatus {
  char phase[32] = "idle";
  char blockedBy[24]{};
  bool clockReady = false;
  bool busy = false;
  bool automatic = false;
  bool credentialsReady = false;
  bool attempted = false;
  unsigned failures = 0;
  uint32_t durationMs = 0;
  uint32_t stackMin = 0;
  size_t minInternal = 0;
  size_t minPsram = 0;
  time_t capturedAt = 0;
  time_t nextUpdateAt = 0;
  P4CameraResult result{};
};

// Cloud storage owns the auth mailbox; the camera worker owns the others.
// UI and HTTP each have independent status consumers.
SpscLatest<ImageFrame>* publishedImage = nullptr;
ImageFrame* imageForUi = nullptr;
uint32_t imageGenerationForUi = 0;
SpscLatest<NameFrame> nameForUi, nameForHttp;
SpscLatest<CameraStatus> statusForUi;
SpscLatest<CameraStatus> statusForHttp;
char* authScratch = nullptr;
std::atomic<uint32_t> generation{0};
std::atomic<uint32_t> revision{0};
std::atomic<uint32_t> trigger{0};
std::atomic<bool> enabled{true};
std::atomic<bool> started{false};
std::atomic<uint8_t> selectedSlot{255};
std::atomic<bool> dashboardVisible{false};
CameraStatus status;
CameraStatus statusForUiValue;  // Retain the last complete value on a bounded read miss.
char cachedTask[24]{};
char cachedSerial[20]{};
uint16_t cachedPlate = 0;
char cachedName[128]{};
NameFrame capturedJob;
uint8_t capturedSlot = 255;
enum class JobReadResult : uint8_t { Ready, Offline, Credentials, Resources };

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
  revision.fetch_add(1, std::memory_order_release);
}

void stage(const char* name) {
  const size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t psram = ESP.getFreePsram();
  status.minInternal = std::min(status.minInternal, internal);
  status.minPsram = std::min(status.minPsram, psram);
  strlcpy(status.phase, name, sizeof(status.phase));
  publishStatus();
  Serial.printf("[camera] stage=%s internal=%u psram=%u\n", name,
                unsigned(internal), unsigned(psram));
}

void sendJson(int code, JsonDocument& document) {
  String body;
  serializeJson(document, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}
void reply(int code, const char* state) {
  JsonDocument document(p4JsonAllocator());
  document["status"] = state;
  sendJson(code, document);
}

std::unique_ptr<Job> readJob(uint8_t slot, JobReadResult& outcome) {
  outcome = JobReadResult::Offline;
  if (slot >= MAX_ACTIVE_PRINTERS) return {};
  PrinterConfig config{};
  if (!p4CameraConfigSnapshot(slot, config) || config.mode == CONN_LOCAL || !config.serial[0]) return {};
  if (!enabled.load(std::memory_order_acquire)) { outcome = JobReadResult::Credentials; return {}; }
  BambuState snapshot{};
  if (!p4CameraSnapshot(slot, snapshot) || !snapshot.connected) return {};
  auto job = std::unique_ptr<Job>(new(std::nothrow) Job);
  if (!job) { outcome = JobReadResult::Resources; return {}; }
  BambuVector<char> token(CLOUD_TOKEN_MAX);
  if (!p4CameraTokenSnapshot(token.data(), token.size()) || !token[0]) {
    outcome = JobReadResult::Credentials; return {};
  }
  job->token = token.data();
  memset(token.data(), 0, token.size());
  if (!authScratch || !authScratch[0]) {
    outcome = JobReadResult::Credentials; return {};
  }
  const size_t bytes = strnlen(authScratch, kAuthLimit + 1);
  if (bytes > kAuthLimit) { outcome = JobReadResult::Credentials; return {}; }
  BambuVector<char> authBuffer(bytes + 1);
  memcpy(authBuffer.data(), authScratch, bytes);
  authBuffer[bytes] = 0;
  JsonDocument document(p4JsonAllocator());
  if (deserializeJson(document, authBuffer.data(), bytes)) {
    outcome = JobReadResult::Credentials; return {};
  }
  job->certificate = document["certificate"] | "";
  job->key = document["private_key"] | "";
  job->platform = document["platform"] | "macos";
  job->firmware = document["firmware"] | "";
  job->deviceId = document["device_id"] | "";
  serializeJson(document["executable_info"], job->executable);
  memset(authBuffer.data(), 0, authBuffer.size());
  job->serial = config.serial;
  job->region = config.region;
  job->slot = slot;
  job->taskId = snapshot.taskId;
  job->plateIndex = snapshot.plateIdx;
  job->lookupName = !(strcmp(job->taskId.c_str(), cachedTask) == 0 &&
                      strcmp(job->serial.c_str(), cachedSerial) == 0 &&
                      job->plateIndex == cachedPlate && cachedName[0]);
  outcome = JobReadResult::Ready;
  return job;
}

bool currentJobImage(uint8_t slot) {
  if (capturedSlot != slot || slot >= MAX_ACTIVE_PRINTERS) return false;
  PrinterConfig config{};
  BambuState state{};
  if (!p4CameraConfigSnapshot(slot, config) || !p4CameraSnapshot(slot, state)) return false;
  return !strcmp(capturedJob.serial,config.serial) &&
         !strcmp(capturedJob.task,state.taskId) && capturedJob.plate==state.plateIdx;
}

bool decodeImage(const BambuVector<uint8_t>& jpeg, const Job& job) {
  stage("decode");
  // The producer releases every uncompleted claim. Otherwise a decode or
  // allocation failure can leave a whole image-sized slot in Writing forever.
  struct Claim {
    bool pending = true;
    ~Claim() { if (pending) publishedImage->cancelWrite(); }
  } claim;
  ImageFrame* frame = publishedImage->beginWrite();
  if (!frame) return false;
  lgfx::LGFX_Sprite decoded;
  decoded.setPsram(true);
  decoded.setColorDepth(16);
  if (!decoded.createSprite(p4ui::IMAGE_W, p4ui::IMAGE_CONTENT_H)) {
    return false;
  }
  // Reserve a fixed 16:9 viewport and caption strip. Contain other source
  // ratios too, so a caption can never cover pixels from a taller image.
  decoded.fillSprite(p4ui::PANEL_BACKGROUND);
  const bool ok = decoded.drawJpg(jpeg.data(), jpeg.size(), 0, p4ui::CAMERA_OFFSET_Y,
                                  p4ui::IMAGE_W, p4ui::CAMERA_H,
                                  0, 0, 0, 0, lgfx::datum_t::middle_center);
  if (ok) {
    decoded.readRect(0, 0, p4ui::IMAGE_W, p4ui::IMAGE_CONTENT_H, frame->pixels);
    strlcpy(frame->serial, job.serial.c_str(), sizeof(frame->serial));
    strlcpy(frame->task, job.taskId.c_str(), sizeof(frame->task));
    frame->plate = job.plateIndex;
    frame->slot = job.slot;
    frame->capturedAt = time(nullptr);
    status.capturedAt = frame->capturedAt;
    publishedImage->publish();
    capturedSlot = job.slot;
    strlcpy(capturedJob.serial, job.serial.c_str(), sizeof(capturedJob.serial));
    strlcpy(capturedJob.task, job.taskId.c_str(), sizeof(capturedJob.task));
    capturedJob.plate = job.plateIndex;
    generation.fetch_add(1, std::memory_order_release);
    revision.fetch_add(1, std::memory_order_release);
    claim.pending = false;
  }
  decoded.deleteSprite();
  return ok;
}

bool capture(uint8_t slot) {
  // Only one auxiliary TLS/DTLS session may run. If cloud HTTPS owns the
  // slot, leave this attempt for the next scheduler pass instead of waiting.
  if (!p4AuxTransportTryAcquire(P4AuxTransportOwner::Camera)) return false;
  struct Release {
    ~Release() { p4AuxTransportRelease(P4AuxTransportOwner::Camera); }
  } release;
  JobReadResult outcome=JobReadResult::Offline;
  auto job = readJob(slot,outcome);
  if (!job) {
    strlcpy(status.phase, outcome==JobReadResult::Credentials ? "credentials_required" :
            outcome==JobReadResult::Resources ? "resources_unavailable" : "waiting_for_mqtt",
            sizeof(status.phase));
    status.attempted = true;
    status.busy = false;
    publishStatus();
    return true;
  }
  const uint32_t began = millis();
  status.busy = true;
  status.nextUpdateAt = 0;
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
        NameFrame name;
        strlcpy(name.name, cachedName, sizeof(name.name));
        strlcpy(name.task, cachedTask, sizeof(name.task));
        strlcpy(name.serial, cachedSerial, sizeof(name.serial));
        name.plate = cachedPlate;
        nameForUi.update(name); nameForHttp.update(name);
        revision.fetch_add(1, std::memory_order_release);
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
  Serial.printf("[camera] result=%s detail=%s duration=%u internal_sample_min=%u stack_min=%u\n",
                status.phase, bambu_camera::snapshot_failure_name(status.result.snapshotFailure),
                unsigned(status.durationMs), unsigned(status.minInternal), unsigned(status.stackMin));
  return true;
}

void cameraTask(void*) {
  uint32_t handled = trigger.load();
  uint32_t lastPeriodic = millis(), lastMissingAttempt = 0;
  bool nextEnabled = false, nextAutomatic = false;
  P4NetworkStatus network{};
  P4UiStatus ui{};
  BambuState printer{};
  uint8_t printerSlot = 255;
  bool haveNetwork = false, haveUi = false, havePrinter = false;
  publishStatus();
  for (;;) {
    if (p4CloudCameraSnapshot(authScratch, kAuthLimit + 1, nextEnabled, nextAutomatic)) {
      enabled.store(nextEnabled, std::memory_order_release);
      status.credentialsReady = authScratch[0] != 0;
      status.automatic = nextAutomatic;
      if (!nextAutomatic || !nextEnabled) status.nextUpdateAt = 0;
      publishStatus();
    }
    haveNetwork = p4CameraNetworkSnapshot(network) || haveNetwork;
    const bool online = haveNetwork && network.wifiOwnerReady &&
                        network.online && !network.ap && !network.ota;
    const uint32_t requested = trigger.load();
    const bool manual = requested != handled;
    status.automatic = nextAutomatic;
    const uint8_t slot = selectedSlot.load();
    if (slot != printerSlot) {
      printerSlot = slot;
      havePrinter = false;
      printer = {};
    }
    havePrinter = p4CameraSnapshot(slot, printer) || havePrinter;
    const bool printerReady = havePrinter && printer.connected &&
                              printer.gcodeStateId != GCODE_UNKNOWN;
    haveUi = p4CameraUiSnapshot(ui) || haveUi;
    const bool visible = dashboardVisible.load(std::memory_order_acquire) &&
      haveUi && ui.dashboard && ui.page == 255 && !ui.nightBlackout && ui.slot == slot;
    // Snapshot signing requires Unix time. NTP is owned by the Wi-Fi worker;
    // clock failure must not become a rapid series of camera attempts.
    const bool clockReady = time(nullptr) >= 1735689600;
    const bool automaticCapture = nextAutomatic && nextEnabled;
    const bool missing=automaticCapture && online && visible && !currentJobImage(slot);
    const uint32_t now=millis();
    const bool resourceReady = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= 64 * 1024;
    const bool periodic=automaticCapture && online && visible &&
                        p4CameraRetryDue(now,lastPeriodic,status.failures);
    const bool retry=missing && p4CameraMissingImageDue(now,lastMissingAttempt,status.failures);
    const char* blocked = !status.credentialsReady || !nextEnabled ? "credentials" :
      !online ? "network" : !printerReady ? "mqtt" : !visible ? "not_visible" :
      !clockReady ? "clock" : !resourceReady ? "internal_memory" : "";
    if (strcmp(status.blockedBy, blocked) != 0 || status.clockReady != clockReady) {
      strlcpy(status.blockedBy, blocked, sizeof(status.blockedBy));
      status.clockReady = clockReady;
      publishStatus();
    }
    if (online && printerReady && visible && clockReady && resourceReady &&
        (manual || (status.credentialsReady && nextEnabled && (retry || periodic)))) {
      if (capture(slot)) {
        handled = requested;
        const uint32_t completed = millis();
        lastPeriodic=completed;
        // A failed first capture also starts the missing-image backoff.
        if (automaticCapture || status.failures) lastMissingAttempt=completed;
        // Display the exact same backoff the scheduler uses, even while a
        // previous successful image remains visible.
        const uint32_t nextDelay = p4CameraRetryDelayMs(status.failures);
        status.nextUpdateAt = automaticCapture ? time(nullptr) + nextDelay / 1000 : 0;
        publishStatus();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void startWorker() {
  if (started.exchange(true)) return;
  constexpr size_t bytes = 32 * 1024;
  authScratch = static_cast<char*>(heap_caps_calloc(kAuthLimit + 1, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  imageForUi = static_cast<ImageFrame*>(heap_caps_calloc(1,sizeof(ImageFrame),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  auto* stack = static_cast<StackType_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  static StaticTask_t control;
  if (!authScratch || !imageForUi || !stack || !xTaskCreateStatic(cameraTask, "p4_camera", bytes / sizeof(StackType_t), nullptr, 1,
                                   stack, &control)) {
    if (stack) heap_caps_free(stack);
    if (authScratch) { heap_caps_free(authScratch); authScratch = nullptr; }
    if (imageForUi) { heap_caps_free(imageForUi); imageForUi = nullptr; }
    started = false;
    strlcpy(status.phase, "task_allocation_failed", sizeof(status.phase));
    publishStatus();
  }
}

void credentials() {
  const String body = server.arg("plain");
  if (!body.length() || body.length() > kAuthLimit) { reply(400, "invalid_credentials_size"); return; }
  JsonDocument doc(p4JsonAllocator());
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
  String error;
  const bool ok = p4CloudSubmitCredentials(body.c_str(), body.length(), error);
  if (ok) trigger.fetch_add(1);
  reply(ok ? 202 : 507, ok ? "credentials_queued" : error.c_str());
}

void httpStatus() {
  CameraStatus current;
  statusForHttp.copyLatest(current);
  JsonDocument doc(p4JsonAllocator());
  doc["phase"] = current.phase;
  doc["blocked_by"] = current.blockedBy;
  doc["clock_ready"] = current.clockReady;
  doc["busy"] = current.busy;
  doc["automatic"] = current.automatic;
  doc["credentials_ready"] = current.credentialsReady;
  doc["generation"] = generation.load();
  doc["interval_ms"] = kInterval;
  doc["captured_at"] = current.capturedAt;
  doc["next_update_at"] = current.nextUpdateAt;
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
    doc["snapshot_failure"] = bambu_camera::snapshot_failure_name(current.result.snapshotFailure);
  }
  doc["rgb_ready"] = generation.load() > 0;
  NameFrame name{};
  const bool hasName = nameForHttp.copyLatest(name) && name.name[0];
  doc["job_name"] = hasName ? name.name : "pending";
  sendJson(200, doc);
}
}

void initP4CameraService() {
  if (!publishedImage) {
    publishedImage = SpscLatest<ImageFrame>::createExternal();
  }
  registerP4CameraRoutes();
}

void registerP4CameraRoutes() {
  // Registration is cheap; the persistent worker starts with the runtime after
  // configuration snapshots are ready, not in the HTTP callback.
  server.on("/camera/credentials", HTTP_POST, credentials);
  server.on("/camera/credentials", HTTP_DELETE, [] {
    const bool ok = p4CloudDeleteCredentials();
    trigger.fetch_add(1);
    reply(ok ? 202 : 503, ok ? "removal_queued" : "storage_unavailable");
  });
  server.on("/camera/capture", HTTP_POST, [] {
    trigger.fetch_add(1);
    reply(202, "capture_queued");
  });
  server.on("/camera/status", HTTP_GET, httpStatus);
  server.on("/camera/automatic", HTTP_POST, [] {
    const bool enabled = server.arg("enabled") == "1";
    const bool ok = p4CloudSetAutomatic(enabled);
    if (ok) trigger.fetch_add(1);
    reply(ok ? 202 : 503, ok ? "preference_queued" : "storage_unavailable");
  });
}

void startP4CameraService() {
  if (!publishedImage) {
    publishedImage = SpscLatest<ImageFrame>::createExternal();
  }
  if (!publishedImage) return;
  startWorker();
}

void p4CameraVisibility(uint8_t slot, bool visible) {
  // These values affect the next scheduling decision, not the retry deadline.
  // Automatic mode handles initial/periodic capture; only an explicit capture
  // request needs the separate trigger mailbox.
  selectedSlot.store(slot, std::memory_order_release);
  dashboardVisible.store(visible, std::memory_order_release);
}

void tickP4CameraService() {
  // Selection is delivered by p4CameraVisibility from the UI owner.
}

uint32_t p4CameraGeneration() { return revision.load(std::memory_order_acquire); }

bool p4CameraBusy() {
  statusForUi.copyLatest(statusForUiValue);
  return statusForUiValue.busy;
}

void p4CameraTiming(const PrinterSlot& printer, P4CameraTiming& timing) {
  timing = {};
  if (p4CameraHasImage(printer)) timing.capturedAt = imageForUi->capturedAt;
  statusForUi.copyLatest(statusForUiValue);
  timing.nextUpdateAt = statusForUiValue.nextUpdateAt;
  timing.busy = statusForUiValue.busy;
  timing.automatic = statusForUiValue.automatic;
  strlcpy(timing.blockedBy, statusForUiValue.blockedBy, sizeof(timing.blockedBy));
}

bool p4CameraHasImage(const PrinterSlot& printer) {
  if (!publishedImage || !imageForUi) return false;
  // UI owns this PSRAM scratch value. A boolean presence query must never
  // allocate a whole RGB565 frame on loopTask's small internal stack.
  ImageFrame& frame=*imageForUi;
  const uint32_t next = generation.load(std::memory_order_acquire);
  if (next != imageGenerationForUi && publishedImage->copyLatest(frame)) imageGenerationForUi = next;
  const bool found=imageGenerationForUi && frame.slot < MAX_ACTIVE_PRINTERS &&
    !strcmp(frame.serial,printer.config.serial) && !strcmp(frame.task,printer.state.taskId) &&
    frame.plate==printer.state.plateIdx;
  return found;
}

void followP4CameraDefault(bool& showCamera,bool& imageChosen,uint32_t& lastGeneration,
                           uint32_t currentGeneration,bool hasCurrentImage) {
  if (lastGeneration==currentGeneration) return;
  lastGeneration=currentGeneration;
  if (!imageChosen && hasCurrentImage) showCamera=true;
}

bool p4CameraJobName(const PrinterSlot& printer, char* out, size_t capacity) {
  if (!out || !capacity) return false;
  out[0] = 0;
  NameFrame name{};
  if (!nameForUi.copyLatest(name) || !name.name[0] || strcmp(printer.config.serial, name.serial) ||
      strcmp(printer.state.taskId, name.task) || printer.state.plateIdx != name.plate) return false;
  strlcpy(out, name.name, capacity);
  return true;
}

bool drawP4Camera(lgfx::LovyanGFX& target, const PrinterSlot& printer, int x, int y) {
  if (!p4CameraHasImage(printer)) return false;
  const ImageFrame& frame=*imageForUi;
  if (strcmp(frame.serial,printer.config.serial) || strcmp(frame.task,printer.state.taskId) ||
      frame.plate!=printer.state.plateIdx) return false;
  target.pushImage(x,y,p4ui::IMAGE_W,p4ui::IMAGE_CONTENT_H,frame.pixels);
  return true;
}
#endif
