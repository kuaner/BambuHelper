#define TASMOTA_PLUG_COUNT 2
#include "p4_settings_merge.h"
#include <cassert>
#include <cstring>

struct Printer { char name[24]{}; char serial[20]{}; char cloudUserId[16]{}; uint8_t region = 0; uint8_t brightness = 0; };
struct Settings {
  uint32_t revision = 0;
  struct Slot { Printer config; uint64_t telemetry = 0; } printers[2]{};
  int rotState = 0, netSettings = 0, dispSettings = 0, gaugeLabels = 0, dpSettings = 0;
  int buttonType = 0, buttonPin = 0, buzzerSettings = 0, ledSettings = 0;
  int tasmotaSettings[TASMOTA_PLUG_COUNT]{};
  char tasmotaCurrency[8]{};
  float tasmotaTariffPerKwh = 0;
  char wifiSSID[33]{}, wifiPass[65]{};
  uint8_t brightness = 0, activePrinterIndex = 0;
  bool dualPrinterUnsafe = false, quadPrinterBeta = false, wifiTxCapped = false;
};

int main() {
  Settings base{};
  std::strcpy(base.printers[0].config.name, "old name");
  std::strcpy(base.wifiSSID, "studio");
  base.brightness = 100;
  Settings cloud = base;
  std::strcpy(cloud.printers[0].config.cloudUserId, "fresh-id");
  cloud.revision = 9;
  Settings edit = base;
  std::strcpy(edit.printers[0].config.name, "new name");
  const Settings merged = p4MergeSettings(cloud, base, edit, 2);
  assert(std::strcmp(merged.printers[0].config.cloudUserId, "fresh-id") == 0);
  assert(std::strcmp(merged.printers[0].config.name, "new name") == 0);
  assert(std::strcmp(merged.wifiSSID, "studio") == 0);
  assert(merged.brightness == 100);
  assert(merged.revision == 9); // Only the storage owner advances this value.

  Settings displayEdit = edit;
  displayEdit.dispSettings = 42;
  displayEdit.tasmotaSettings[0] = 71;
  displayEdit.tasmotaTariffPerKwh = 0.23f;
  std::strcpy(displayEdit.tasmotaCurrency, "CNY");
  Settings unrelatedCloud = cloud;
  std::strcpy(unrelatedCloud.printers[0].config.name, "new name");
  const Settings unrelated = p4MergeSettings(unrelatedCloud, edit, displayEdit, 2);
  assert(std::strcmp(unrelated.printers[0].config.name, "new name") == 0);
  assert(std::strcmp(unrelated.printers[0].config.cloudUserId, "fresh-id") == 0);
  assert(unrelated.dispSettings == 42);
  assert(unrelated.tasmotaSettings[0] == 71);
  assert(unrelated.tasmotaTariffPerKwh == 0.23f);
  assert(std::strcmp(unrelated.tasmotaCurrency, "CNY") == 0);

  Settings serialEdit = cloud;
  std::strcpy(serialEdit.printers[0].config.serial, "new-serial");
  const Settings serialChanged = p4MergeSettings(cloud, cloud, serialEdit, 2);
  assert(serialChanged.printers[0].config.cloudUserId[0] == 0);

  Settings live = base;
  live.printers[0].telemetry = 1234;
  edit.printers[0].telemetry = 7;
  const Settings withLiveState = p4MergeSettings(live, base, edit, 2);
  assert(withLiveState.printers[0].telemetry == 1234);

  // A token commit changes the account-derived id in the background. The
  // browser must first adopt that committed base, then apply its form delta.
  Settings browser = base;
  Settings tokenCommitted = base;
  tokenCommitted.revision = 10;
  std::strcpy(tokenCommitted.printers[0].config.cloudUserId, "new-account");
  browser = tokenCommitted;
  Settings form = browser;
  std::strcpy(form.printers[0].config.name, "renamed printer");
  const Settings saved = p4MergeSettings(tokenCommitted, browser, form, 2);
  assert(std::strcmp(saved.printers[0].config.cloudUserId, "new-account") == 0);
  assert(std::strcmp(saved.printers[0].config.name, "renamed printer") == 0);
}
