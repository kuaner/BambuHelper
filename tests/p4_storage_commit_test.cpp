#include "p4_storage_commit.h"
#include "p4_factory_reset.h"
#include <cassert>
#include <cstring>
#include <string>

struct Config { char cloudUserId[16]{}; };
struct Slot { Config config; };
struct Settings { uint32_t revision = 0; Slot printers[2]{}; };

void name(Settings& value, const char* text) {
  std::strcpy(value.printers[0].config.cloudUserId, text);
}
std::string name(const Settings& value) { return value.printers[0].config.cloudUserId; }

int main() {
  P4StorageCommitter committer;
  uint32_t revision = 7;
  Settings value;
  strcpy(value.printers[0].config.cloudUserId, "old-user");
  strcpy(value.printers[1].config.cloudUserId, "old-user");
  int published = 0;
  auto failSettings = [](Settings&) { return false; };
  assert(!committer.settings(value, revision, failSettings,
                             [&](const Settings&) { ++published; }));
  assert(revision == 7 && published == 0);
  // Storage and every consumer must see the same generation. The write is
  // sequenced after allocation, irrespective of compiler argument order.
  uint32_t storedRevision = 0;
  assert(committer.settings(value, revision,
                            [&](Settings& stored) {
                              storedRevision = stored.revision;
                              return true;
                            },
                            [&](const Settings& publishedValue) {
                              assert(publishedValue.revision == storedRevision);
                              ++published;
                            }));
  assert(revision == 8 && storedRevision == 8 && published == 1);
  Settings failedValue;
  assert(!committer.settings(failedValue, revision,
                             [&](Settings& stored) {
                               assert(stored.revision == 9);
                               stored = Settings{};
                               return false;
                             },
                             [&](const Settings&) { assert(false); }));
  assert(revision == 8 && failedValue.revision == 0);

  // Fault injection after settings succeed but the new token fails. The old
  // token and settings are both rewritten and no candidate reaches MQTT/UI.
  std::string durableToken = "old-token";
  Settings durableSettings;
  std::strcpy(durableSettings.printers[0].config.cloudUserId, "old-account");
  Settings before = durableSettings, candidate = durableSettings;
  std::strcpy(candidate.printers[0].config.cloudUserId, "new-account");
  int settingsWrites = 0, tokenWrites = 0, accountPublished = 0;
  Settings publishedAccount;
  auto writeSettings = [&](Settings& value) {
    ++settingsWrites;
    durableSettings = value;
    return true;
  };
  auto failNewToken = [&](const char* token) {
    ++tokenWrites;
    if (std::strcmp(token, "new-token") == 0) return false;
    durableToken = token;
    return true;
  };
  assert(!p4CommitCloudAccount("new-token", durableToken.c_str(), before, candidate, revision,
                              writeSettings, failNewToken,
                              [&](const Settings& value) { ++accountPublished; publishedAccount = value; },
                              [&](Settings& actual) { actual = durableSettings; }));
  assert(settingsWrites == 2 && tokenWrites == 2 && accountPublished == 1);
  assert(durableToken == "old-token");
  assert(std::strcmp(durableSettings.printers[0].config.cloudUserId, "old-account") == 0);
  assert(std::strcmp(before.printers[0].config.cloudUserId, "old-account") == 0);
  assert(std::strcmp(candidate.printers[0].config.cloudUserId, "new-account") == 0);
  assert(name(publishedAccount) == name(before));
  assert(publishedAccount.revision == durableSettings.revision);

  int rollbackAttempts = 0;
  assert(!p4CommitCloudAccount("new-token", "old-token", before, candidate, revision,
    [](Settings&) { return true; },
    [&](const char*) { ++rollbackAttempts; return false; },
    [&](const Settings& value) { ++accountPublished; publishedAccount = value; },
    [&](Settings& actual) { actual = durableSettings; }));
  assert(rollbackAttempts == 2 && accountPublished == 2);
  assert(name(durableSettings) == name(before));

  // A candidate settings failure still attempts the token restoration. The
  // two stores are not one short-circuit condition.
  tokenWrites = 0;
  assert(!p4CommitCloudAccount("new-token", "old-token", before, candidate, revision,
    [](Settings&) { return false; }, failNewToken,
    [&](const Settings&) { ++accountPublished; },
    [&](Settings& actual) { actual = durableSettings; }));
  assert(tokenWrites == 1 && accountPublished == 3);

  Settings actual;
  int settingsRestores = 0, accountRestores = 0, cameraRestores = 0, publications = 0;
  auto reset = [&] { name(actual, "defaults"); return false; };
  auto restoreSettings = [&] { ++settingsRestores; return false; };
  auto restoreAccount = [&] { ++accountRestores; return true; };
  auto restoreCamera = [&] { ++cameraRestores; return false; };
  auto publishActual = [&] { ++publications; };
  P4FactoryResetResult result = p4FactoryResetStorage(reset, restoreSettings,
    restoreAccount, restoreCamera, publishActual);
  assert(!result.reset && !result.restored);
  assert(settingsRestores == 1 && accountRestores == 1 && cameraRestores == 1 && publications == 1);
  result = p4FactoryResetStorage([] { return true; }, restoreSettings,
    restoreAccount, restoreCamera, publishActual);
  assert(result.reset && !result.restored && publications == 2);
  assert(settingsRestores == 1 && accountRestores == 1 && cameraRestores == 1);
  result = p4FactoryResetStorage(reset, [] { return true; },
    [] { return true; }, [] { return true; }, publishActual);
  assert(!result.reset && result.restored && publications == 3);

  uint32_t wrapped = 0xffffffffU;
  assert(P4StorageCommitter::nextRevision(wrapped) == 1);
}
