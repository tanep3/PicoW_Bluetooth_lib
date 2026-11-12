#include "A2dpSinkReceiver.h"
#include "sample_config.h"
#include "btstack.h"
#include "pico/stdlib.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

int sample_main();
namespace {
struct Done {};
enum class Change { None, Signaling, Failed, NotWorking, Closing, StopAdmission, Streaming, Connecting, NewGeneration };
struct Step { uint32_t now; int command = -1; Change change = Change::None; };
std::vector<Step> steps;
std::vector<std::string> events;
std::vector<std::pair<bool, uint32_t>> visibility;
A2dpSinkReceiver::State state;
size_t index = 0;
uint32_t now = 0;
bool accept_listen = true;
unsigned identity_reads = 0;
unsigned stdio_initializations = 0;
IAudioSink *output = nullptr;
void expect(std::initializer_list<const char *> expected) {
    std::vector<std::string> want;
    for (const auto *entry : expected) want.emplace_back(entry);
    if (events != want) {
        for (const auto &entry : events) std::fprintf(stderr, "%s ", entry.c_str());
        std::fputs("\n", stderr);
        assert(false && "unexpected SDK/control effect order");
    }
}
}
struct A2dpSinkReceiver::Impl {};
A2dpSinkReceiver::A2dpSinkReceiver(IAudioSink &sink, Settings settings)
    : impl_(new Impl) {
    assert((settings.peer == Address{{SINK_PCM_PEER_BYTES}}));
    assert(settings.allow_pairing_confirmation == (SINK_PCM_ALLOW_PAIRING != 0));
    output = &sink;
}
A2dpSinkReceiver::~A2dpSinkReceiver() = default;
A2dpSinkReceiver::BeginResult A2dpSinkReceiver::begin() {
    events.emplace_back("begin"); state.begun = state.working = true; return BeginResult::Ok;
}
void A2dpSinkReceiver::poll() {
    assert(index < steps.size());
    now = steps[index].now;
    switch (steps[index].change) {
    case Change::Signaling: state.signaling = true; break;
    case Change::Failed: state.failed = true; break;
    case Change::NotWorking: state.working = false; break;
    case Change::Closing: state.closing = true; break;
    case Change::StopAdmission: state.accepting = false; break;
    case Change::Streaming: state.streaming = true; break;
    case Change::Connecting: state.connecting = true; break;
    case Change::NewGeneration: ++state.generation; break;
    case Change::None: break;
    }
}
bool A2dpSinkReceiver::listen() {
    events.emplace_back("listen");
    if (accept_listen) state.accepting = true;
    return accept_listen;
}
bool A2dpSinkReceiver::connect() {
    events.emplace_back("connect"); state.connecting = true; return true;
}
void A2dpSinkReceiver::disconnect() {
    events.emplace_back("disconnect"); state.accepting = false;
    state.connecting = state.signaling = state.streaming = false;
    output->reset();
}
A2dpSinkReceiver::State A2dpSinkReceiver::snapshot() const { return state; }
A2dpSinkReceiver::Diagnostics A2dpSinkReceiver::diagnostics() const { return {}; }
void stdio_init_all() { ++stdio_initializations; }
absolute_time_t get_absolute_time() { return now; }
uint32_t to_ms_since_boot(absolute_time_t time) { return time; }
int getchar_timeout_us(uint32_t timeout) { assert(timeout == 0); return steps[index++].command; }
void tight_loop_contents() { if (index == steps.size()) throw Done{}; }
void gap_discoverable_control(uint8_t enabled) {
    assert(enabled <= 1);
    if (enabled) assert(state.working && state.accepting && !state.failed);
    events.emplace_back(enabled ? "show" : "hide");
    visibility.emplace_back(enabled != 0, now);
}
void gap_local_bd_addr(bd_addr_t address) {
    assert(state.working); ++identity_reads;
    const uint8_t synthetic[6]{2,0,0,0,0,2};
    std::copy(std::begin(synthetic), std::end(synthetic), address);
}
char *bd_addr_to_str(const bd_addr_t address) {
    assert(address[0] == 2 && address[5] == 2);
    static char synthetic[] = "02:00:00:00:00:02";
    return synthetic;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const std::string scenario = argv[1];
    if (scenario == "inert") {
        assert(!SINK_PCM_ENABLE_RADIO);
        assert(sample_main() == 0);
        assert(events.empty() && visibility.empty() && stdio_initializations == 0 && identity_reads == 0);
        std::puts("PASS inert: no receiver, stdio, identity or radio calls");
        return 0;
    }
    if (scenario == "default") steps = {{0,'l'},{60000,'?'},{60001,'d'}};
    else if (scenario == "startup_idle") steps = {{0,'?'},{60000,-1}};
    else if (scenario == "identity") steps = {{0,'i'},{60000,-1}};
    else if (scenario == "repeat_timeout") steps = {{0,'l'},{10,'l'},{59999,'?'},{60000,-1},{120000,-1}};
    else if (scenario == "rejected") { accept_listen = false; steps = {{0,'l'},{60000,-1}}; }
    else if (scenario == "cancel") steps = {{0,'l'},{10,'d'},{60000,-1}};
    else if (scenario == "outgoing") steps = {{0,'l'},{10,'c'},{20,-1}};
    else if (scenario == "reopen") steps = {{0,'l'},{60000,-1},{60001,'l'},{120000,-1},{120001,-1}};
    else if (scenario == "wrap") steps = {{0xfffffff0u,'l'},{uint32_t(0xfffffff0u+59999u),-1},{uint32_t(0xfffffff0u+60000u),-1}};
    else {
        Change change = Change::None;
        if (scenario == "signaling") change = Change::Signaling;
        if (scenario == "failed") change = Change::Failed;
        if (scenario == "not_working") change = Change::NotWorking;
        if (scenario == "closing") change = Change::Closing;
        if (scenario == "stop_admission") change = Change::StopAdmission;
        if (scenario == "streaming") change = Change::Streaming;
        if (scenario == "connecting") change = Change::Connecting;
        if (scenario == "short_session") change = Change::NewGeneration;
        assert(change != Change::None);
        steps = {{0,'l'},{30,-1,change},{120000,-1}};
    }
    try { (void)sample_main(); assert(false && "enabled sample unexpectedly returned"); }
    catch (const Done &) {}
    assert(stdio_initializations == 1);
    assert(identity_reads == (SINK_PCM_DISCOVERABLE_LISTEN ? (scenario == "identity" ? 2u : 1u) : 0u));
    if (scenario == "default") {
        assert(!SINK_PCM_DISCOVERABLE_LISTEN);
        expect({"begin","listen","disconnect"});
    } else {
        assert(SINK_PCM_DISCOVERABLE_LISTEN);
        if (scenario == "startup_idle" || scenario == "identity") expect({"begin"});
        else if (scenario == "rejected") expect({"begin","listen"});
        else if (scenario == "outgoing") expect({"begin","listen","show","hide","connect"});
        else if (scenario == "repeat_timeout" || scenario == "wrap" || scenario == "cancel")
            expect({"begin","listen","show","hide","disconnect"});
        else if (scenario == "reopen")
            expect({"begin","listen","show","hide","disconnect","listen","show","hide","disconnect"});
        else expect({"begin","listen","show","hide"});
    }
    std::vector<std::pair<bool, uint32_t>> expected_visibility;
    if (scenario == "repeat_timeout") expected_visibility = {{true,0},{false,60000}};
    else if (scenario == "cancel" || scenario == "outgoing") expected_visibility = {{true,0},{false,10}};
    else if (scenario == "reopen") expected_visibility = {{true,0},{false,60000},{true,60001},{false,120001}};
    else if (scenario == "wrap") expected_visibility = {{true,0xfffffff0u},{false,uint32_t(0xfffffff0u+60000u)}};
    else if (scenario != "default" && scenario != "startup_idle" && scenario != "identity" && scenario != "rejected")
        expected_visibility = {{true,0},{false,30}};
    assert(visibility == expected_visibility);
    std::printf("PASS %s: exact sample main-loop effects\n", scenario.c_str());
}
