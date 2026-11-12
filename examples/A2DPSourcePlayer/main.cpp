// Developer steps: source -> settings -> begin -> poll. No BTstack event code.
#include "A2dpSourcePlayer.h"
#include "FixedTone.h"
#include "TraceQueue.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include <algorithm>
#include <cstdio>

namespace {
TraceQueue<32, 384> trace;
uint64_t nextControlSequence = 0, controlHistoryLost = 0;
std::array<a2dp::HistoryEntry, 64> historyDump;
size_t historyCount = 0, historyCursor = 0;
void diagnostics(A2dpSourcePlayer &player) {
  const auto s = player.snapshot();
  char line[384];
  const int n = std::snprintf(
      line, sizeof(line),
      "STATE us=%llu power=%u inquiry=%u signal=%u stream=%u credit=%u cid=%u "
      "seid=%u packets=%llu samples=%llu error=%u status=%d rejected=%llu "
      "history_lost=%llu control_lost=%llu events_lost=%llu trace_lost=%lu\n",
      s.now_us, unsigned(s.power), unsigned(s.inquiry), unsigned(s.signaling),
      unsigned(s.stream), unsigned(s.credit), s.cid, s.seid, s.packets,
      s.samples, unsigned(s.error), s.status, s.rejected,
      player.overwrittenHistory(), controlHistoryLost, player.droppedEvents(),
      static_cast<unsigned long>(trace.dropped()));
  if (n > 0 && size_t(n) < sizeof(line))
    trace.push(line, size_t(n));
}
// Inspect only newly retained entries. Reading history never advances
// Bluetooth. High-frequency media events are represented by STATE packet/sample
// counters.
void pollControlTrace(A2dpSourcePlayer &player) {
  const size_t count = player.historySize();
  if (!count)
    return;
  const uint64_t newest = player.history(count - 1).sequence;
  const uint64_t oldest = newest + 1 - count;
  if (nextControlSequence < oldest) {
    controlHistoryLost += oldest - nextControlSequence;
    nextControlSequence = oldest;
  }
  for (; nextControlSequence <= newest; ++nextControlSequence) {
    const auto h = player.history(size_t(nextControlSequence - oldest));
    using a2dp::Kind;
    bool important = h.event.kind != Kind::Observed &&
                     h.event.kind != Kind::SendCreditGranted &&
                     h.event.kind != Kind::MediaResult &&
                     h.event.kind != Kind::Tick;
    if (h.event.kind == Kind::Tick) {
      for (size_t i = 0; i < h.count; ++i)
        important |= h.actions[i].op == a2dp::Op::StartStream ||
                     h.actions[i].op == a2dp::Op::DisconnectA2dp;
    }
    // Never suppress the transition introducing an error, including send
    // failure.
    important |= h.after.error != h.before.error;
    if (!important)
      continue;
    char line[384];
    const int n = std::snprintf(
        line, sizeof(line),
        "CONTROL seq=%llu us=%llu event=%u raw=%u/%u cmd=%u cid=%u>%u "
        "power=%u>%u inquiry=%u>%u signal=%u>%u stream=%u>%u credit=%u>%u "
        "actions=%u[%u,%u,%u,%u] event_status=%d result_status=%d error=%u\n",
        h.sequence, h.event.now_us, unsigned(h.event.kind), h.event.raw_type,
        h.event.raw_subevent, h.event.command, h.before.cid, h.after.cid,
        unsigned(h.before.power), unsigned(h.after.power),
        unsigned(h.before.inquiry), unsigned(h.after.inquiry),
        unsigned(h.before.signaling), unsigned(h.after.signaling),
        unsigned(h.before.stream), unsigned(h.after.stream),
        unsigned(h.before.credit), unsigned(h.after.credit), unsigned(h.count),
        unsigned(h.actions[0].op), unsigned(h.actions[1].op),
        unsigned(h.actions[2].op), unsigned(h.actions[3].op), h.event.status,
        h.after.status, unsigned(h.after.error));
    if (n > 0)
      trace.push(line, size_t(n) < sizeof(line) ? size_t(n) : sizeof(line) + 1);
  }
}
void pollConsole(A2dpSourcePlayer &player) {
  tud_task();
  for (unsigned i = 0; i < 8 && tud_cdc_available(); ++i) {
    char c;
    if (tud_cdc_read(&c, 1) != 1)
      break;
    if (c == '?')
      diagnostics(player);
    if (c == 'c')
      player.connect();
    if (c == 'd')
      player.disconnect();
    if (c == 'h' && historyCursor == historyCount) {
      // Snapshot once, then stream one record per loop as USB space allows.
      historyCount = player.historySize();
      historyCursor = 0;
      for (size_t j = 0; j < historyCount; ++j)
        historyDump[j] = player.history(j);
    }
  }
  if (historyCursor < historyCount && trace.pending() < 32) {
    const auto &h = historyDump[historyCursor++];
    char line[256];
    const int n = std::snprintf(
        line, sizeof(line),
        "HISTORY seq=%llu us=%llu event=%u raw=%u/%u cmd=%u status=%d "
        "stream=%u>%u error=%u actions=%u[%u,%u,%u,%u]\n",
        h.sequence, h.event.now_us, unsigned(h.event.kind), h.event.raw_type,
        h.event.raw_subevent, h.event.command, h.event.status,
        unsigned(h.before.stream), unsigned(h.after.stream),
        unsigned(h.after.error), unsigned(h.count), unsigned(h.actions[0].op),
        unsigned(h.actions[1].op), unsigned(h.actions[2].op),
        unsigned(h.actions[3].op));
    if (n > 0 && size_t(n) < sizeof(line))
      trace.push(line, size_t(n));
  }
  if (!tud_cdc_connected() || !trace.pending())
    return;
  const auto n =
      std::min<size_t>({trace.remaining(), tud_cdc_write_available(), 256});
  if (n)
    trace.consume(tud_cdc_write(trace.front(), n));
  tud_cdc_write_flush();
}
} // namespace
int main() {
  // 1. Replace this source with your IAudioSource (signed int16 interleaved
  // stereo).
  static FixedTone source;
  source.configure(48000);
  // 2. Choose one speaker and PCM rate. The delay/duration are this trial's
  // policy.
  A2dpSourcePlayer::Settings settings;
  settings.target_address = PLAYER_TARGET_ADDRESS;
  settings.sample_rate = 48000;
  settings.auto_connect =
      false; // Start only after the capture is ready (USB c).
  settings.start_delay_us = 4000000;
  settings.play_duration_us = 3000000;
  static A2dpSourcePlayer player(source, settings);
  // 3. One owner initializes CYW43, BTstack, L2CAP, SDP and powers on.
  tusb_init();
  const auto result = player.begin();
  char boot[100];
  const int n = std::snprintf(
      boot, sizeof(boot),
      "BEGIN result=%u (0=OK); ?=state h=history c=connect d=disconnect\n",
      unsigned(result));
  trace.push(boot, size_t(n));
  uint64_t last = 0;
  while (true) {
    // 4. All Bluetooth and audio work stays on this one polling loop.
    player.poll();
    pollConsole(player);
    pollControlTrace(player);
    const auto now = time_us_64();
    if (now - last >= 1000000) {
      last = now;
      diagnostics(player);
    }
  }
}
