#include "sample_config.h"
#include "SppClient.h"
#include "SppControlWorkflow.h"
#include "pico/stdlib.h"
#include <cstdio>

namespace {
constexpr spp::Address first_peer{{SPP_SAMPLE_FIRST_PEER_BYTES}};
constexpr spp::Address next_peer{{SPP_SAMPLE_NEXT_PEER_BYTES}};
using Flow = spp_sample::Workflow<spp::Client>;
const char *transportName(spp::Client::Transport t) {
  switch (t) {
  case spp::Client::Transport::Basic: return "Basic";
  case spp::Client::Transport::Ertm: return "ERTM";
  default: return "Unknown";
  }
}
int runRadioExample() {
  // Security defaults: authenticated, no automatic pairing confirmation.
  // Static Client and buffers survive timeouts/failure and every SDK callback.
  static spp::Client client(spp::Config{first_peer});
#if SPP_SAMPLE_ENABLE_ERTM
  alignas(16) static std::array<uint8_t, 12288> ertm_a{}, ertm_b{};
  spp::ErtmPool::Config ertm;
  ertm.link.ertm_mandatory = 0; // Optional: Basic fallback is allowed.
  ertm.count = 2;
  ertm.buffers = {{{ertm_a.data(), static_cast<uint32_t>(ertm_a.size())},
                   {ertm_b.data(), static_cast<uint32_t>(ertm_b.size())}}};
  if (!client.configureErtm(ertm)) return 2; // Before begin, once only.
#endif
  spp_sample::Options options;
  options.first_peer = first_peer; options.next_peer = next_peer;
  options.remote_channel = SPP_SAMPLE_REMOTE_CHANNEL;
  options.local_channel = SPP_SAMPLE_LOCAL_CHANNEL;
  static Flow flow(client, options);
  stdio_init_all();
  flow.start(time_us_64());
  Flow::Report previous{};
  auto previous_stage = spp_sample::Stage::Idle;
  while (!flow.done()) {
    flow.step(time_us_64());
    const auto &r = flow.report();
    if (flow.stage() != previous_stage) {
      std::printf("stage=%u queued=%u (queued is not completion)\n",
                  unsigned(flow.stage()), r.queued_requests);
      previous_stage = flow.stage();
    }
    if (r.controls_completed != previous.controls_completed) {
      std::printf("control=%u token=%lu local SDK status=%ld; not peer acknowledgement\n",
                  unsigned(r.last_control), static_cast<unsigned long>(r.last_control_token),
                  static_cast<long>(r.last_control_status));
    }
    for (size_t i = 0; i < r.routes.size(); ++i) {
      const auto &o = r.routes[i]; const auto &old = previous.routes[i];
      if (o.generation != old.generation || o.transport != old.transport)
        std::printf("route=%u generation=%lu actual transport=%s\n", unsigned(i),
                    static_cast<unsigned long>(o.generation), transportName(o.transport));
      if (o.generation != old.generation || o.line_events != old.line_events ||
          o.modem_events != old.modem_events || o.local_port_events != old.local_port_events ||
          o.remote_port_events != old.remote_port_events) {
        std::printf("route=%u line=%u (%llu events), modem wire byte=%u (%llu events), "
                    "local port events=%llu, remote port events=%llu\n", unsigned(i),
                    o.line_status, static_cast<unsigned long long>(o.line_events),
                    o.modem_status, static_cast<unsigned long long>(o.modem_events),
                    static_cast<unsigned long long>(o.local_port_events),
                    static_cast<unsigned long long>(o.remote_port_events));
        // o.local_port and o.remote_port own the latest typed configuration.
      }
    }
    previous = r;
    sleep_ms(1);
  }
  const auto leases = client.ertmStatus();
  std::printf("finished stage=%u failure=%u control SDK failures=%u leases=%u\n",
              unsigned(flow.stage()), unsigned(flow.failure()),
              flow.report().control_failures, leases.active);
  std::printf("last lifecycle rejection=%u\n", unsigned(flow.report().last_rejection));
  // There is no reset/recreate/end contract for Client. Do not free leases or
  // power off a shared radio as pretend cleanup. Retain owners, keep polling.
  for (;;) { client.poll(); sleep_ms(1); }
}
} // namespace
int main() {
  // Ordinary if type-checks the real path even in inert builds. Default returns
  // before stdio, Client construction, configureErtm, begin, poll or radio use.
  if (!SPP_SAMPLE_ENABLE_RADIO) return 0;
  return runRadioExample();
}
