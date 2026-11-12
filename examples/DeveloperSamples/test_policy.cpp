#include "SamplePolicy.h"
#include "../../spp-core/SppCore.h"
#include <cassert>
#include <cstdio>
int main() {
  sample::Address address;
  assert(sample::parsePeer("01:23:45:67:89:aB", address));
  assert(address[0] == 1 && address[5] == 0xab);
  for (const char* bad : {"", "00:00:00:00:00:00", "FF:FF:FF:FF:FF:FF",
      "01:23:45:67:89:GG", "01-23:45:67:89:AB", "1:23:45:67:89:AB", "01:23:45:67:89:ABx"}) {
    assert(!sample::parsePeer(bad, address));
    assert(address == sample::Address{});
  }
  assert(sample::liveAclUsable(true, 1, 0));
  assert(sample::liveAclUsable(true, 1, 0x0eff));
  assert(!sample::liveAclUsable(true, 0, 0));
  assert(!sample::liveAclUsable(false, 1, 0));
  assert(!sample::liveAclUsable(true, 1, 0x0f00));
  sample::PairingWindow window;
  assert(!window.start(0, false, true, false));
  assert(!window.start(0, true, false, false));
  assert(!window.start(0, true, true, true));
  assert(window.start(0, true, true, false));
  assert(!window.start(1, true, true, false)); // Repeated p cannot extend window.
  assert(!window.shouldClose(59999999, false));
  assert(window.shouldClose(60000000, false));
  assert(window.shouldClose(1, true));
  window.close();
  assert(!window.active && !window.shouldClose(60000000, false));
  sample::RetryBudget budget;
  assert(budget.take(0));
  assert(!budget.take(4999999));
  budget.closed(10000000);
  assert(!budget.take(14999999));
  assert(budget.take(15000000));
  assert(budget.take(20000000));
  assert(!budget.take(UINT64_C(1000000000)));
  spp::State state;
  state.routes[0].generation = 1;
  const auto old = spp::handleOf(state, 0);
  assert(spp::matches(state, old));
  ++state.routes[0].generation;
  assert(!spp::matches(state, old));
  assert(spp::matches(state, spp::handleOf(state, 0)));
  puts("PASS: strict peer parsing, bounded cooldown, stale generation guard");
}
