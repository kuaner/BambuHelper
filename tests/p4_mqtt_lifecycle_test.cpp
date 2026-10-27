#include "p4_mqtt_lifecycle.h"
#include "p4_mqtt_commands.h"
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <thread>

int main() {
  P4MqttLifecycle<2> lifecycle;
  assert(!lifecycle.stopped(0));
  lifecycle.resume();
  lifecycle.stop(0);
  assert(lifecycle.stopped(0));
  assert(!lifecycle.stopped(1));
  lifecycle.resume();
  assert(!lifecycle.stopped(0));
  lifecycle.stop(1);
  assert(lifecycle.stopped(1));
  assert(!lifecycle.stopped(0));
  P4MqttCommands<2> commands;
  commands.configure(0);
  commands.refresh(0);
  assert(commands.takeConfigure(0));
  assert(!commands.takeConfigure(0));
  assert(commands.takeRefresh(0));
  commands.reconnect(0);
  commands.configure(0);
  assert(commands.takeReconnect(0));
  assert(commands.takeConfigure(0));
  commands.refresh(1);
  commands.configure(1);
  commands.discard(1);
  assert(!commands.takeRefresh(1));
  assert(!commands.takeConfigure(1));
  commands.configure(0);
  commands.reconnect(0);
  commands.refresh(0);
  // Wi-Fi/OTA resume drops transient work but must apply a configuration
  // committed while that connection was stopped.
  commands.discardTransient(0);
  assert(commands.takeConfigure(0));
  assert(!commands.takeReconnect(0));
  assert(!commands.takeRefresh(0));
  {
    // The older stop reserves its sequence first, but publishes after a newer
    // stop and an intervening resume. It must not overwrite the newer stop.
    P4MqttLifecycle<1> concurrent;
    std::mutex mutex;
    std::condition_variable condition;
    bool reserved = false, release = false;
    concurrent.testHook([&](uint32_t sequence) {
      if (sequence != 2) return;
      std::unique_lock<std::mutex> lock(mutex);
      reserved = true; condition.notify_all();
      condition.wait(lock, [&] { return release; });
    });
    std::thread earlier([&] { concurrent.stop(0); });
    {
      std::unique_lock<std::mutex> lock(mutex);
      condition.wait(lock, [&] { return reserved; });
    }
    concurrent.resume();
    concurrent.stop(0);
    {
      std::lock_guard<std::mutex> lock(mutex);
      release = true;
    }
    condition.notify_all(); earlier.join();
    assert(concurrent.stopped(0));
    concurrent.resume();
    assert(!concurrent.stopped(0));
  }
  return 0;
}
