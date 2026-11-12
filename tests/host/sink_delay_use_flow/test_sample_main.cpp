#include "A2dpSinkReceiver.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

// Compile the ACTUAL application main loop, replacing only facade/device I/O.
// All numbers/peers below are fictional host fixtures, never device commands.
#define main firmware_main_for_test
#include "examples/A2dpSinkDelayReportSample/main.cpp"
#undef main

namespace model {
struct Done {};
struct Step { const char *line; unsigned event = 0; };
const Step steps[] = {
    {"begin\n"}, {"status\n", 1}, {"connect\n"}, {"status\n", 2},
    {"report 1 7 2 101\n"}, {"report 1 7 2 101\n"}, // admission then duplicate
    {"report 2 7 2 200\n"}, {""}, // SDK busy, no retry on next poll
    {"report 3 7 2 "}, {"100\n", 3}, // same-generation callback reset mid-line
    {"report 4 7 2 100\n"}, {"disconnect\n"},
    {"report 5 7 3 100\n", 4}, // old tokens after disconnect
    {"connect\n"}, {"report 6 7 3 100\n", 5}, // reconnect must not retag
    {"report 7 8 5 0\n"}, // fresh explicit tokens, true zero
    {"report 8 8 5 6553500\n"}, // public NotNegotiated
    {"report 9 8 5 1\n"}, // public NotReady
    {"report 10 8 5 1\n"}, // public StaleSession
    {"report 11 8 5 1\n"}, // public InvalidDelay (modeled defensive result)
    {"status\n", 6}, // receiver failure permanently stops reports
    {"report 12 8 5 0\n"}, {"stop\n"}, {"connect\n"}, {"begin\n"}, {"status\n"}
};
size_t step = 0, at = 0;
bool in_poll = false;
unsigned begins = 0, connects = 0, disconnects = 0, reads_this_poll = 0;
A2dpSinkReceiver::State state;
IAudioSink *output = nullptr;
std::vector<std::pair<uint32_t, uint32_t>> calls;
}
struct A2dpSinkReceiver::Impl {};
A2dpSinkReceiver::A2dpSinkReceiver(IAudioSink &output, Settings settings) : impl_(new Impl) {
    assert(!settings.allow_pairing_confirmation && settings.peer[0] == 2);
    model::output = &output;
}
A2dpSinkReceiver::~A2dpSinkReceiver() = default;
A2dpSinkReceiver::BeginResult A2dpSinkReceiver::begin() {
    assert(!model::in_poll); ++model::begins; model::state.begun = true; return BeginResult::Ok;
}
void A2dpSinkReceiver::poll() {
    assert(!model::in_poll); model::in_poll = true;
    auto &s = model::state;
    switch (model::steps[model::step].event) {
    case 1: s.working = true; break;
    case 2:
    case 5:
        s.connecting = false; s.signaling = s.configured = s.streaming = true;
        s.generation = model::steps[model::step].event == 2 ? 7 : 8;
        model::output->reset();
        // The stub honestly rejects all PCM, with no report/API reentry.
        assert(model::output->write(nullptr, 0, 48000, 2) == 0);
        break;
    case 3: model::output->reset(); break;
    case 4: s.closing = s.signaling = s.configured = s.streaming = false; break;
    case 6: s.failed = true; s.streaming = false; break;
    }
    model::in_poll = false;
}
bool A2dpSinkReceiver::listen() { assert(!model::in_poll); return false; }
bool A2dpSinkReceiver::connect() {
    assert(!model::in_poll); ++model::connects; model::state.connecting = true; return true;
}
void A2dpSinkReceiver::disconnect() {
    assert(!model::in_poll); ++model::disconnects;
    model::state.closing = true; model::state.streaming = false; model::output->reset();
}
A2dpSinkReceiver::State A2dpSinkReceiver::snapshot() const { assert(!model::in_poll); return model::state; }
A2dpSinkReceiver::DelayReportResult A2dpSinkReceiver::reportDelay100us(uint32_t units, uint32_t generation) {
    assert(!model::in_poll);
    assert(generation == model::state.generation);
    model::calls.push_back({units, generation});
    const DelayReportStatus results[] = {DelayReportStatus::Accepted, DelayReportStatus::SdkRejected,
        DelayReportStatus::Accepted, DelayReportStatus::NotNegotiated, DelayReportStatus::NotReady,
        DelayReportStatus::StaleSession, DelayReportStatus::InvalidDelay};
    assert(model::calls.size() <= sizeof(results) / sizeof(results[0]));
    return {results[model::calls.size() - 1], static_cast<uint8_t>(model::calls.size() == 2 ? 0x0c : 0)};
}
void stdio_init_all() {}
int getchar_timeout_us(uint32_t timeout) {
    assert(!model::in_poll && timeout == 0);
    ++model::reads_this_poll;
    const auto *line = model::steps[model::step].line;
    return line[model::at] ? static_cast<unsigned char>(line[model::at++]) : -1;
}
void tight_loop_contents() {
    assert(model::reads_this_poll <= sink_delay_sample::input_bytes_per_poll);
    assert(model::steps[model::step].line[model::at] == '\0');
    model::at = model::reads_this_poll = 0;
    if (++model::step == sizeof(model::steps) / sizeof(model::steps[0])) throw model::Done{};
}
int main() {
    try { firmware_main_for_test(); assert(false); } catch (const model::Done &) {}
    assert(model::begins == 1 && model::connects == 2 && model::disconnects == 2);
    const std::vector<std::pair<uint32_t,uint32_t>> expected = {
        {2,7}, {2,7}, {0,8}, {65535,8}, {1,8}, {1,8}, {1,8}};
    assert(model::calls == expected);
    puts("PASS actual sample main loop: callback ordering, command dispatch, real adapter invocation, original generation, all statuses, busy/no retry, reset, reconnect, failure/stop");
}
