#include "p4_transport_gate.h"
#include <cassert>
#include <thread>

int main() {
  P4AuxTransportGate gate;
  assert(gate.tryAcquire(P4AuxTransportOwner::Cloud));
  assert(!gate.tryAcquire(P4AuxTransportOwner::Cloud));
  assert(!gate.tryAcquire(P4AuxTransportOwner::Camera));
  gate.release(P4AuxTransportOwner::Camera);
  assert(gate.owner() == P4AuxTransportOwner::Cloud);
  gate.release(P4AuxTransportOwner::Cloud);
  assert(gate.tryAcquire(P4AuxTransportOwner::Camera));

  // Even a forced ownership race leaves exactly one owner. Losers return
  // immediately; neither path waits for a network operation to finish.
  gate.release(P4AuxTransportOwner::Camera);
  std::thread cloud([&] { gate.tryAcquire(P4AuxTransportOwner::Cloud); });
  std::thread camera([&] { gate.tryAcquire(P4AuxTransportOwner::Camera); });
  cloud.join(); camera.join();
  const auto owner = gate.owner();
  assert(owner == P4AuxTransportOwner::Cloud || owner == P4AuxTransportOwner::Camera);
  gate.release(owner);
  assert(gate.owner() == P4AuxTransportOwner::None);
  return 0;
}
