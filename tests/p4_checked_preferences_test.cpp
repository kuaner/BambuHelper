#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using String = std::string;
enum PreferenceType { PT_I8, PT_U8, PT_I16, PT_U16, PT_I32, PT_U32,
                      PT_I64, PT_U64, PT_STR, PT_BLOB, PT_INVALID };

class Preferences {
  struct Value { PreferenceType type = PT_INVALID; std::vector<uint8_t> bytes; };
  std::map<std::string, Value> values_;
  bool started_ = false;

  template<typename T> size_t put(const char* key, T value, PreferenceType type) {
    if (failWrite || !started_) return 0;
    Value item{type, {}};
    item.bytes.resize(sizeof(value));
    memcpy(item.bytes.data(), &value, sizeof(value));
    values_[key] = item;
    if (corruptRead) values_[key].bytes[0] ^= 0xff;
    return sizeof(value);
  }
  template<typename T> T get(const char* key, T fallback) {
    const auto it = values_.find(key);
    if (!started_ || it == values_.end()) return fallback;
    T value{};
    memcpy(&value, it->second.bytes.data(), sizeof(value));
    return value;
  }
public:
  bool failBegin = false, failWrite = false, corruptRead = false, failClear = false;
  bool begin(const char*, bool = false) { started_ = !failBegin; return started_; }
  void end() { started_ = false; }
  bool isKey(const char* key) { return values_.count(key); }
  PreferenceType getType(const char* key) {
    const auto it = values_.find(key);
    return it == values_.end() ? PT_INVALID : it->second.type;
  }
  size_t getBytesLength(const char* key) { return values_[key].bytes.size(); }
  size_t getBytes(const char* key, void* out, size_t length) {
    const auto it = values_.find(key);
    if (!started_ || it == values_.end() || length < it->second.bytes.size()) return 0;
    memcpy(out, it->second.bytes.data(), it->second.bytes.size());
    return it->second.bytes.size();
  }
  uint8_t getUChar(const char* k, uint8_t d = 0) { return get(k, d); }
  uint16_t getUShort(const char* k, uint16_t d = 0) { return get(k, d); }
  uint32_t getUInt(const char* k, uint32_t d = 0) { return get(k, d); }
  uint32_t getULong(const char* k, uint32_t d = 0) { return get(k, d); }
  bool getBool(const char* k, bool d = false) { return getUChar(k, d) != 0; }
  int16_t getShort(const char* k, int16_t d = 0) { return get(k, d); }
  float getFloat(const char* k, float d = 0) { return get(k, d); }
  String getString(const char* key, const String& fallback = {}) {
    const auto it = values_.find(key);
    if (!started_ || it == values_.end() || it->second.type != PT_STR) return fallback;
    return String(it->second.bytes.begin(), it->second.bytes.end());
  }
  size_t putUChar(const char* k, uint8_t v) { return put(k, v, PT_U8); }
  size_t putUShort(const char* k, uint16_t v) { return put(k, v, PT_U16); }
  size_t putUInt(const char* k, uint32_t v) { return put(k, v, PT_U32); }
  size_t putULong(const char* k, uint32_t v) { return put(k, v, PT_U32); }
  size_t putBool(const char* k, bool v) { return putUChar(k, v); }
  size_t putFloat(const char* k, float v) { return put(k, v, PT_BLOB); }
  size_t putString(const char* key, const char* value) {
    if (failWrite || !started_) return 0;
    values_[key] = {PT_STR, std::vector<uint8_t>(value, value + strlen(value))};
    return strlen(value);
  }
  size_t putString(const char* key, const String& value) { return putString(key, value.c_str()); }
  size_t putBytes(const char* key, const void* value, size_t length) {
    if (failWrite || !started_) return 0;
    const auto* first = static_cast<const uint8_t*>(value);
    values_[key] = {PT_BLOB, std::vector<uint8_t>(first, first + length)};
    return length;
  }
  bool remove(const char* key) { if (failWrite) return false; return values_.erase(key) != 0; }
  bool clear() { if (failClear) return false; values_.clear(); return true; }
};

#include "p4_checked_preferences.h"
#include "p4_restart_gate.h"

static bool save(P4CheckedPreferences<Preferences>& store) {
  store.beginWriteVerification();
  store.begin("config", false);
  store.putString("empty", "");
  store.putString("name", "printer");
  store.putUChar("small", 0);
  store.putUShort("medium", 65535);
  store.putULong("large", 0xffffffffU);
  store.putBool("enabled", true);
  store.putFloat("ratio", 1.25f);
  const uint8_t slots[] = {1, 2, 3, 4};
  store.putBytes("slots", slots, sizeof(slots));
  store.end();
  return store.endWriteVerification();
}

int main() {
  P4CheckedPreferences<Preferences> store;
  assert(save(store));                         // empty strings and zero are valid
  store.backend().failWrite = true;
  assert(!save(store));                        // zero write length is not accepted blindly
  store.backend().failWrite = false;
  store.backend().corruptRead = true;
  assert(!save(store));                        // read-back mismatch is a failed commit
  store.backend().corruptRead = false;
  store.backend().failBegin = true;
  assert(!save(store));
  store.backend().failBegin = false;
  store.beginWriteVerification();
  store.backend().failClear = true;
  store.clear();
  assert(!store.endWriteVerification());

  P4RestartGate restart;
  const uint32_t first = restart.arm();
  assert(restart.complete(first, false));       // Failure result was consumed...
  assert(!restart.take(first));                 // ...but cannot trigger a restart
  const uint32_t second = restart.arm();
  assert(!restart.complete(first, true));       // stale acknowledgement cannot reboot
  assert(restart.complete(second, true));
  const uint32_t third = restart.arm();
  restart.cancel(third);
  assert(!restart.complete(third, true));       // queue timeout cancels its ticket

  const uint32_t wifiSave = restart.arm();
  assert(restart.complete(wifiSave, true));
  assert(restart.scheduled(wifiSave));      // a zero-delay save cannot cancel success
  assert(restart.take(wifiSave));           // the web task can restart once
  assert(!restart.take(wifiSave));
}
