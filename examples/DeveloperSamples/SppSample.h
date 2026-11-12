#pragma once
#include "SampleConsole.h"
#include "PeerPairing.h"
#include "SppClient.h"
#include <array>
#include <cstring>

#include "PendingMessage.h"

inline int runSpp(unsigned routes, bool reconnect) {
  sample::Address peer;
  if (!sample::start(peer)) sample::idle();
  static spp::Client client(spp::Config{peer},
    spp::Client::Security{bool(SAMPLE_ALLOW_PAIRING), true}); // Fixed peer only.
  if (!client.begin()) { puts("begin queue rejected"); sample::idle(); }
  std::array<PendingMessage, 2> pending{};
  sample::RetryBudget budget;
  bool retry_enabled = false, attempt_pending = false, saw_activity = false;
  uint64_t deadline = 0;
  puts("c=connect configured channel(s); u=UUID lookup route 0; s=select candidate 0");
  puts("l=listen on configured local channel(s); reset to remove listeners");
  puts("w=send greeting once; d=disconnect/cancel retries; ?=status; r=bounded retry demo");
  PeerPairing::configure(peer, bool(SAMPLE_ALLOW_PAIRING));
  while (true) {
    PeerPairing::poll();
    client.poll();
    const auto state = client.snapshot();
    const auto now = time_us_64();
    const int command = getchar_timeout_us(0);
    if (command == '?' ) {
      printf("runtime=%u error=%u status=%ld dropped=%llu attempts=%u/3\n",
        unsigned(state.runtime), unsigned(state.error), (long)state.status,
        (unsigned long long)client.droppedEvents(), budget.used);
      for (unsigned i = 0; i < routes; ++i) {
        const auto& endpoint = state.endpoints[i];
        printf("endpoint=%u phase=%u status=%ld channel=%u\n", i,
          unsigned(endpoint.phase), (long)endpoint.status, endpoint.channel);
        const auto& r = state.routes[i];
        printf("route=%u channel=%u gen=%lu phase=%u error=%u status=%ld accepted=%llu sent=%llu discarded=%llu rx=%llu candidates=%u\n",
          i, r.channel, (unsigned long)r.generation, unsigned(r.phase), unsigned(r.error), (long)r.status,
          (unsigned long long)r.tx_accepted, (unsigned long long)r.tx_sent,
          (unsigned long long)r.tx_discarded, (unsigned long long)r.rx_read, r.candidate_count);
        for (unsigned j = 0; j < r.candidate_count; ++j)
          printf("  candidate=%u channel=%u\n", j, r.candidates[j].channel);
      }
    }
    if (command == 'd') {
      retry_enabled = false;
      for (unsigned i = 0; i < routes; ++i) {
        if (state.routes[i].phase != spp::Phase::Closed)
          printf("disconnect queued=%u\n", client.disconnect(spp::handleOf(state, i)));
        pending[i].active = false;
      }
    }
    if (state.runtime != spp::Runtime::Ready) continue;
    if (command == 'l') {
      for (unsigned i = 0; i < routes; ++i)
        printf("listen endpoint=%u queued=%u\n", i, client.listen(i,
          uint8_t(i ? SAMPLE_CHANNEL_1 : SAMPLE_CHANNEL_0), spp::Uuid::from16(0x1101)));
    }
    if (command == 'r' && reconnect && !retry_enabled && budget.used < 3) {
      retry_enabled = true; // Budget never resets, even if r is pressed again.
      puts("At most 3 attempts per boot; each session limited to 10 seconds.");
    }
    if (attempt_pending) {
      saw_activity |= state.routes[0].phase != spp::Phase::Closed;
      if ((saw_activity && state.routes[0].phase == spp::Phase::Closed) || now >= deadline) {
        if (state.routes[0].phase != spp::Phase::Closed)
          printf("timeout disconnect queued=%u\n", client.disconnect(spp::handleOf(state, 0)));
        attempt_pending = false;
        budget.closed(now); // Wait 5 seconds after closure/timeout, not after issue.
      }
    }
    if (retry_enabled && !attempt_pending && state.routes[0].phase == spp::Phase::Closed && budget.take(now)) {
      attempt_pending = client.connect(0, peer, uint8_t(SAMPLE_CHANNEL_0));
      saw_activity = false;
      deadline = now + 10000000;
      printf("attempt=%u queued=%u\n", budget.used, attempt_pending);
    }
    // Manual and automatic modes are separate; d cancels the automatic mode.
    if (!retry_enabled && command == 'c') {
      for (unsigned i = 0; i < routes; ++i)
        if (state.routes[i].phase == spp::Phase::Closed)
          printf("connect route=%u queued=%u\n", i,
            client.connect(i, peer, uint8_t(i ? SAMPLE_CHANNEL_1 : SAMPLE_CHANNEL_0)));
    }
    if (!retry_enabled && command == 'u' && state.routes[0].phase == spp::Phase::Closed)
      printf("UUID connect queued=%u\n", client.connect(0, peer, spp::Uuid::from16(0x1101)));
    if (command == 's' && state.routes[0].phase == spp::Phase::Selecting)
      printf("selection queued=%u\n", client.select(spp::handleOf(state, 0), 0));
    for (unsigned i = 0; i < routes; ++i) {
      if (command == 'w' && state.routes[i].phase == spp::Phase::Open && !pending[i].active)
        pending[i] = {spp::handleOf(state, i), 0, true};
      pending[i].poll(client, state, i);
      if (state.routes[i].phase == spp::Phase::Open) {
        uint8_t received[64];
        const auto result = client.read(spp::handleOf(state, i), received, sizeof(received));
        // Consume at most 64 bytes per route per loop. Replace with your parser.
        // Do not assume a read corresponds to one write/message. No echo/priority policy.
        (void)result;
      }
    }
  }
}
