"""Guard the P4 rendering boundary with targeted source-structure checks."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class P4RuntimeArchitectureTests(unittest.TestCase):
    def test_flash_writers_have_internal_execution_stacks(self):
        # NVS and OTA turn off caches. A PSRAM stack deterministically panics
        # even when reads, networking and all ordinary UI checks pass.
        cloud = (ROOT / "src/p4_cloud_worker.cpp").read_text()
        cloud_start = cloud[cloud.index("void p4CloudWorkerStart"):]
        web = (ROOT / "src/web_server.cpp").read_text()
        web_start = web[web.index("void p4WebServiceStart"):web.index("bool p4WebRequestRestart")]
        for start in (cloud_start, web_start):
            allocation = start[start.index("auto* stack ="):start.index("static StaticTask_t") if "static StaticTask_t" in start else start.index("auto* control =")]
            self.assertIn("MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT", allocation)
            self.assertNotIn("MALLOC_CAP_SPIRAM", allocation)

    def test_all_task_control_blocks_use_internal_memory(self):
        mqtt = (ROOT / "src/p4_mqtt_worker.cpp").read_text()
        allocation = mqtt[mqtt.index("StaticTask_t* control ="):mqtt.index("if (!control)")]
        self.assertIn("MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT", allocation)
        self.assertNotIn("MALLOC_CAP_SPIRAM", allocation)

    def test_diagnostic_capture_is_on_demand_and_after_render(self):
        service = (ROOT / "src/p4_ui_service.cpp").read_text()
        capture = service[service.index("void p4UiPublishStatus"):]
        self.assertIn("capturedSerial.load", capture)
        self.assertIn("captureSerial.load", capture)
        self.assertNotIn("lastFrame", capture)
        self.assertNotIn("< 250", capture)
        main = (ROOT / "src/main.cpp").read_text()
        loop = main[main.index("void loop()"):]
        p4 = loop[loop.index("#if defined(BOARD_IS_WS_P4_7B)"):loop.index("#else", loop.index("#if defined(BOARD_IS_WS_P4_7B)"))]
        self.assertLess(p4.index("updateDisplay()"), p4.rindex("p4UiPublishStatus()"))
        self.assertIn("updateDisplayedPrinterScreenState();", p4)
        self.assertNotIn("if (isWiFiConnected() && !isAPMode()) updateDisplayedPrinterScreenState()", p4)

    def test_screenshot_timeout_cannot_return_an_old_frame(self):
        web = (ROOT / "src/web_server.cpp").read_text()
        capture = web[web.index("static void handleScreenBmp()"):]
        self.assertIn("p4UiFrameReady(request)", capture)
        self.assertIn('server.send(503, "text/plain", "Screen snapshot is not ready; retry without using an older frame")', capture)

    def test_render_path_does_not_own_large_image_or_settings_temporaries(self):
        camera = (ROOT / "src/p4_camera_service.cpp").read_text()
        presence = camera[camera.index("bool p4CameraHasImage"):camera.index("void followP4CameraDefault")]
        self.assertNotIn("ImageFrame frame{}", presence)
        self.assertIn("ImageFrame& frame=*imageForUi", presence)
        runtime = (ROOT / "src/p4_runtime.cpp").read_text()
        sync = runtime[runtime.index("void p4RuntimeSync"):runtime.index("bool p4CloudCommitSettings")]
        self.assertNotIn("P4UiSettings settings{}", sync)
        self.assertIn("readNext(*settingsForUi)", sync)
        main = (ROOT / "src/main.cpp").read_text()
        self.assertNotIn("P4UiSettings initialSettings", main)
        self.assertNotIn("P4UiSettings bootSettings", main)

    def test_screenshot_storage_is_lazy_and_acknowledges_the_copied_request(self):
        service = (ROOT / "src/p4_ui_service.cpp").read_text()
        startup = service[service.index("void p4UiServiceStart"):service.index("void p4UiPublishStatus")]
        capture = service[service.index("void p4UiPublishStatus"):service.index("void p4UiRepaint")]
        request = service[service.index("uint32_t p4UiRequestFrame"):service.index("bool p4UiFrameReady")]
        self.assertNotIn("heap_caps_calloc", startup)
        self.assertNotIn("heap_caps_calloc", capture)
        self.assertIn("heap_caps_calloc", request)
        self.assertIn("capturedSerial.store(request, std::memory_order_release)", capture)

    def test_main_p4_branch_has_no_network_service(self):
        main = (ROOT / "src/main.cpp").read_text()
        start = main.index("#if defined(BOARD_IS_WS_P4_7B)", main.index("void loop()"))
        end = main.index("#else", start)
        branch = re.sub(r"//.*", "", main[start:end])
        for call in ("handleWiFi", "handleWebServer", "handleBambuMqtt", "ssdpTick",
                     "thumbService", "HTTPClient", "cloudFetch", "WiFiClient"):
            self.assertNotIn(call, branch)

    def test_runtime_sync_does_not_run_external_io_or_legacy_automation(self):
        runtime = (ROOT / "src/p4_runtime.cpp").read_text()
        sync = runtime[runtime.index("void p4RuntimeSync()"):runtime.index("bool p4CloudCommitSettings")]
        for call in ("ledTick", "initLed", "previewLed", "ledTriggerTestEffect",
                     "handleWiFi", "handleBambuMqtt", "server.handleClient",
                     "saveSettings", "loadSettings", "p4Store", "p4Reset",
                     "HTTPClient", "cloudFetch", "initBuzzer", "touchInit"):
            self.assertNotIn(call, sync)

    def test_storage_owner_publishes_camera_auth(self):
        camera = (ROOT / "src/p4_camera_service.cpp").read_text()
        self.assertNotIn("publishedAuth", camera)
        self.assertNotIn("Preferences", camera)
        cloud = (ROOT / "src/p4_cloud_worker.cpp").read_text()
        self.assertIn("cameraPublication->publish", cloud)
        self.assertIn("CameraPublication* value = cameraPublication->beginWrite()", cloud)

    def test_wifi_failure_has_independent_http_and_ui_consumers(self):
        runtime = (ROOT / "src/p4_runtime.cpp").read_text()
        self.assertIn("SpscLatest<WifiFailInfo> wifiFailureForUi, wifiFailureForHttp;", runtime)
        self.assertIn("wifiFailureForUi.update(failure); wifiFailureForHttp.update(failure);", runtime)
        wifi = (ROOT / "src/wifi_manager.cpp").read_text()
        self.assertIn("p4UiWifiFailureSnapshot(snapshot);", wifi)

    def test_visibility_does_not_postpone_camera_retry(self):
        camera = (ROOT / "src/p4_camera_service.cpp").read_text()
        worker = camera[camera.index("void cameraTask(void*)"):camera.index("void startWorker()")]
        self.assertIn("lastMissingAttempt", worker)
        self.assertIn("lastPeriodic", worker)
        self.assertIn("!currentJobImage(slot)", worker)
        self.assertNotIn("nextAttempt", worker)
        visibility = camera[camera.index("void p4CameraVisibility"):camera.index("void tickP4CameraService")]
        self.assertNotIn("trigger.fetch_add", visibility)
        mqtt = (ROOT / "src/p4_mqtt_worker.cpp").read_text()
        resume = mqtt[mqtt.index("if (wasStopped[slot])"):mqtt.index("const bool configure")]
        self.assertIn("commands.discardTransient(slot);", resume)
        self.assertNotIn("commands.discard(slot);", resume)

    def test_wifi_owner_publishes_real_transport_state(self):
        runtime = (ROOT / "src/p4_runtime.cpp").read_text()
        worker = runtime[runtime.index("void p4WifiWorkerStart"):]
        task = worker[worker.index("auto task = [](void*) {"):worker.index("constexpr size_t bytes")]
        self.assertEqual(worker.count("wifiTask = xTaskGetCurrentTaskHandle();"), 1)
        self.assertIn("wifiTask = xTaskGetCurrentTaskHandle();", task)
        wifi = (ROOT / "src/wifi_manager.cpp").read_text()
        owner = wifi[wifi.index("bool isWiFiOwnedByWorker"):wifi.index("void getWiFiIpText")]
        self.assertIn("void* owner = p4WifiTaskHandle();", owner)
        self.assertIn("owner && xTaskGetCurrentTaskHandle() == owner", owner)

    def test_wifi_settings_update_has_no_fallible_scratch_allocation(self):
        wifi = (ROOT / "src/wifi_manager.cpp").read_text()
        update = wifi[wifi.index("bool p4WifiUpdateSettings"):wifi.index("bool p4WifiHasCredentials")]
        self.assertNotIn("heap_caps_calloc", update)
        self.assertIn("memcmp(&wifiSettings.netSettings", update)
        runtime = (ROOT / "src/p4_runtime.cpp").read_text()
        task = runtime[runtime.index("auto task = [](void*) {"):runtime.index("constexpr size_t bytes")]
        self.assertIn("candidate.revision != appliedRevision", task)

    def test_socket_workers_start_only_after_lwip_is_ready(self):
        runtime = (ROOT / "src/p4_runtime.cpp").read_text()
        wifi = runtime[runtime.index("auto task = [](void*) {"):runtime.index("constexpr size_t bytes")]
        self.assertLess(wifi.index("Network.begin()"), wifi.index("initWiFi()"))
        self.assertLess(wifi.index("Network.begin()"), wifi.index("networkReady.store(true"))
        web = (ROOT / "src/web_server.cpp").read_text()
        start = web[web.index("void p4WebServiceStart()"):]
        task = web[web.index("static void p4WebTask"):web.index("void p4WebServiceStart()")]
        self.assertLess(start.index("network.wifiOwnerReady"), start.index("xTaskCreateStatic"))
        self.assertIn("initWebServer()", task)
        self.assertIn("p4WebServiceStart();", wifi)

    def test_wifi_applies_first_saved_generation_after_startup(self):
        runtime = (ROOT / "src/p4_runtime.cpp").read_text()
        task = runtime[runtime.index("auto task = [](void*) {"):runtime.index("constexpr size_t bytes")]
        update = task.index("if (p4WifiSettingsSnapshot(candidate))")
        applied = task.index("appliedRevision = candidate.revision;", update)
        started = task.index("haveSettings = true;", update)
        self.assertLess(update, applied)
        self.assertLess(applied, started)
        self.assertNotIn("if (haveSettings) appliedRevision", task)

    def test_cloud_requests_are_safe_after_partial_initialization(self):
        cloud = (ROOT / "src/p4_cloud_worker.cpp").read_text()
        build = cloud[cloud.index("__attribute__((noinline)) bool postBuiltStorage"):]
        self.assertIn("!requestBuildScratch", build)
        network = cloud[cloud.index("__attribute__((noinline)) bool postBuiltNetwork"):]
        self.assertLess(network.index("if (!started"), network.index("memset(request, 0, sizeof(*request));"))
        self.assertIn("!httpRequestScratch", network)
        status = cloud[cloud.index("void p4CloudStatusJson"):]
        self.assertIn("!httpStatusScratch", status)
        self.assertLess(build.index("if (!started"), build.index("memset(request, 0, sizeof(*request));"))
        self.assertIn("publishedSettings = static_cast<P4UiSettings*>", cloud)

    def test_background_service_start_has_one_retrying_owner(self):
        web = (ROOT / "src/web_server.cpp").read_text()
        task = web[web.index("static void p4WebTask"):web.index("void p4WebServiceStart")]
        for call in ("p4CloudWorkerStart", "startP4CameraService", "p4SettingsPublish"):
            self.assertNotIn(call, task)
        runtime = (ROOT / "src/p4_runtime.cpp").read_text()
        workers = runtime[runtime.index("void p4RuntimeStartWorkers"):runtime.index("bool postUiByte")]
        self.assertIn("if (!coreReady || pendingUiSettings) return;", workers)
        self.assertIn("p4CloudWorkerStart();", workers)
        self.assertIn("startP4CameraService();", workers)

    def test_background_storage_cannot_arm_or_cancel_a_web_save(self):
        cloud = (ROOT / "src/p4_cloud_worker.cpp").read_text()
        execute = cloud[cloud.index("__attribute__((noinline)) void executeStorage"):cloud.index("void task(void*)")]
        self.assertNotIn("p4WebArmRestart", execute)
        acknowledgement = execute[execute.index("if (request.ticket)"):]
        self.assertNotIn("p4WebArmRestart", acknowledgement)
        self.assertNotIn("p4WebCancelRestart(0", acknowledgement)
        self.assertIn("if (request.ticket)", execute)
        self.assertIn("const StorageResult result{request.ticket, storageSequence, stored}", execute)

    def test_token_and_restart_failures_do_not_leak_to_next_save(self):
        web = (ROOT / "src/web_server.cpp").read_text()
        commit = web[web.index("bool p4CommitWebSettings"):web.index("void p4WebInitialize")]
        save_wifi = web[web.index("static void handleSaveWifi"):web.index("// Live brightness preview")]
        self.assertIn("restartAfterSaveMs.store(1000", save_wifi)
        self.assertLess(commit.index("restartAfterSaveMs.exchange(0"),
                        commit.index("p4WebArmRestart()"))
        self.assertNotIn("restartAfterSaveMs.store(0", commit)
        self.assertIn("p4WebSyncCommittedBase();", web)
        cloud = (ROOT / "src/p4_cloud_worker.cpp").read_text()
        self.assertIn("if (!p4WebFinishRestart(request.ticket, stored)) return;", cloud)


if __name__ == "__main__":
    unittest.main()
