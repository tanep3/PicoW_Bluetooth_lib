#include "examples/A2dpSinkDelayReportSample/DelayCommandFlow.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <utility>
using namespace sink_delay_sample;

// ALL measurements/generations/peers in host fixtures are fictional and are
// never supplied to a device. The reporter models local API results, not RF.
struct Reporter {
    unsigned calls = 0;
    uint32_t units = 0, generation = 0;
    ReportResult next{ReportStatus::Accepted};
    ReportResult operator()(uint32_t u, uint32_t g) { ++calls; units = u; generation = g; return next; }
};
struct Input {
    std::string bytes;
    size_t at = 0, reads = 0;
    int operator()() { ++reads; return at == bytes.size() ? -1 : static_cast<unsigned char>(bytes[at++]); }
};
constexpr Context active{7, 11, true, false};
std::string report(uint32_t id, uint32_t us, uint32_t gen = 7, uint32_t epoch = 11) {
    return "report " + std::to_string(id) + " " + std::to_string(gen) + " " + std::to_string(epoch) + " " + std::to_string(us) + "\n";
}
Event next(Flow &flow, Input &input, Reporter &reporter, Context state = active) {
    for (unsigned i = 0; i < 100; ++i) {
        const auto before = input.reads;
        const auto event = flow.poll(input, state, reporter);
        assert(input.reads - before <= input_bytes_per_poll);
        if (event.outcome != Outcome::None || input.at == input.bytes.size()) return event;
    }
    assert(false); return {};
}
Event submit(Flow &flow, Reporter &reporter, const std::string &line, Context state = active) {
    Input input{line}; return next(flow, input, reporter, state);
}
void parser() {
    for (const auto kind : {Kind::Begin, Kind::Listen, Kind::Connect, Kind::Disconnect, Kind::Stop, Kind::Status}) {
        const char *names[] = {"", "begin", "listen", "connect", "disconnect", "stop", "status"};
        Flow f; Reporter r;
        auto e = submit(f, r, std::string(names[unsigned(kind)]) + "\r\n");
        assert(e.outcome == Outcome::Control && e.command.kind == kind && r.calls == 0);
    }
    const std::vector<std::string> malformed = {
        "", " ", "REPORT 1 7 11 0", "report 1 7 11", "report 1 7 11 ", "report 1 7 11 0 extra",
        "report +1 7 11 0", "report 1 -7 11 0", "report 1 7 11 -1", "report 1 7 11 0.5",
        "report 1 7 11 0x10", " report 1 7 11 0", "report  1 7 11 0", "report 1 7 11 0 ",
        "report 4294967296 7 11 0", "report 1 4294967296 11 0", "report 1 7 4294967296 0",
        "report 1 7 11 4294967296", "report\t1 7 11 0", "report 1 7 11 0\rX", "status\r\r",
        std::string("report 1 7 11 0\0", 16), std::string(1, char(255)), std::string(1, char(8))};
    for (const auto &bad : malformed) {
        Flow f; Reporter r; Input input{bad + "\n" + report(1, 0)};
        assert(next(f, input, r).outcome == Outcome::Malformed && r.calls == 0 && f.lastId() == 0);
        assert(next(f, input, r).outcome == Outcome::Attempted && r.calls == 1);
    }
    // Exact capacity, then one byte over; overlong tail is never a command.
    const std::string prefix = "report 1 7 11 ";
    Flow f; Reporter r;
    assert(submit(f, r, prefix + std::string(max_line_bytes - prefix.size(), '0') + "\n").outcome == Outcome::Attempted);
    Input input{std::string(max_line_bytes + 1, 'x') + "report 2 7 11 0\n" + report(2, 0)};
    assert(next(f, input, r).outcome == Outcome::TooLong && r.calls == 1);
    assert(next(f, input, r).outcome == Outcome::Attempted && r.calls == 2);
    Flow partial; Reporter p;
    assert(submit(partial, p, "report 1 7 11 123").outcome == Outcome::None && p.calls == 0);
    assert(submit(partial, p, "\n").outcome == Outcome::Attempted && p.units == 2);
}
void conversionsAndIds() {
    Flow f; Reporter r;
    const uint32_t values[] = {0, 1, 99, 100, 101, 6553400, 6553401, 6553499, 6553500};
    uint32_t id = 0;
    for (auto us : values) {
        const auto e = submit(f, r, report(++id, us));
        assert(e.outcome == Outcome::Attempted && r.units == (us + 99) / 100 && r.generation == 7);
        assert(e.command.measured_us == us && e.command.generation == 7 && f.lastId() == id);
    }
    const auto calls = r.calls;
    for (auto us : {6553501u, 4294967295u}) {
        assert(submit(f, r, report(++id, us)).outcome == Outcome::InvalidDelay);
        assert(f.lastId() == id && r.calls == calls);
        assert(submit(f, r, report(id, 0)).outcome == Outcome::DuplicateOrOldId);
    }
    assert(submit(f, r, report(0, 0)).outcome == Outcome::InvalidId);
    assert(submit(f, r, report(1, 0)).outcome == Outcome::DuplicateOrOldId);
    assert(submit(f, r, report(UINT32_MAX, 0)).outcome == Outcome::Attempted);
    assert(submit(f, r, report(UINT32_MAX, 0)).outcome == Outcome::DuplicateOrOldId);
    assert(submit(f, r, report(1, 0)).outcome == Outcome::DuplicateOrOldId);
}
void lifecycle() {
    Flow f; Reporter r;
    for (const auto &pair : {std::pair<uint32_t,uint32_t>{0,11}, {7,0}, {6,11}, {7,10}, {8,11}, {7,12}}) {
        const auto id = f.lastId() + 1;
        assert(submit(f, r, report(id, 100, pair.first, pair.second)).outcome == Outcome::StaleMeasurement);
        assert(f.lastId() == id && r.calls == 0);
    }
    assert(submit(f, r, report(20,100), {7,11,false,false}).outcome == Outcome::NotReady);
    assert(submit(f, r, report(21,100), {7,11,true,true}).outcome == Outcome::StaleMeasurement);
    // Each state change while a line is incomplete rejects the entire line.
    for (const auto changed : {Context{8,11,true,false}, Context{7,12,true,false},
                               Context{7,11,false,false}, Context{7,11,true,true}}) {
        Flow partial; Reporter p;
        assert(submit(partial, p, "report 1 7 11 ").outcome == Outcome::None);
        assert(submit(partial, p, "100\n", changed).outcome == Outcome::BoundaryChanged);
        assert(p.calls == 0 && partial.lastId() == 1);
    }
    // A temporary readiness gap with no bytes must stay latched even when
    // readiness returns before the final newline, without relying on reset.
    {
        Flow partial; Reporter p;
        assert(submit(partial, p, "report 1 7 11 ").outcome == Outcome::None);
        assert(submit(partial, p, "", {7,11,false,false}).outcome == Outcome::None);
        assert(submit(partial, p, "100\n").outcome == Outcome::BoundaryChanged && p.calls == 0);
    }
    // Stream stops/resets/restarts in one poll: same generation, new epoch.
    OutputBoundary boundary(11); boundary.reset();
    assert(submit(f, r, report(22,100), {7,boundary.epoch(),true,false}).outcome == Outcome::StaleMeasurement);
    // Several complete lines in transport input: only one is handled per poll.
    Flow queued; Reporter q; Input lines{report(1,100) + report(2,100) + report(3,100)};
    assert(next(queued, lines, q).outcome == Outcome::Attempted && q.calls == 1);
    assert(next(queued, lines, q, {7,12,true,false}).outcome == Outcome::StaleMeasurement && q.calls == 1);
    assert(next(queued, lines, q, {8,13,true,false}).outcome == Outcome::StaleMeasurement && q.calls == 1);
    assert(submit(queued, q, report(4,100,8,13), {8,13,true,false}).outcome == Outcome::Attempted);
    assert(q.generation == 8); // New explicit fixture, not implicit retagging.
    OutputBoundary end(UINT32_MAX); end.reset(); end.reset();
    assert(end.exhausted() && end.epoch() == UINT32_MAX);
    assert(OutputBoundary(0).exhausted());
    f.stop();
    assert(submit(f, r, report(23,0)).outcome == Outcome::Stopped && f.lastId() == 23);
    assert(submit(f, r, report(23,0)).outcome == Outcome::DuplicateOrOldId);
    assert(submit(f, r, "status\n").outcome == Outcome::Control);
}
void apiResultsAndNoRetry() {
    for (const auto status : {ReportStatus::Accepted, ReportStatus::InvalidDelay, ReportStatus::StaleSession,
                             ReportStatus::NotReady, ReportStatus::NotNegotiated, ReportStatus::SdkRejected}) {
        Flow f; Reporter r; r.next = {status, static_cast<uint8_t>(status == ReportStatus::SdkRejected ? 0x0c : 0)};
        const auto event = submit(f, r, report(1,12345));
        assert(event.outcome == Outcome::Attempted && event.result.status == status && event.result.sdk_status == r.next.sdk_status);
        assert(r.calls == 1 && r.units == 124 && r.generation == 7 && f.lastId() == 1);
        for (int i = 0; i < 8; ++i) assert(submit(f, r, "").outcome == Outcome::None);
        assert(submit(f, r, report(1,12345)).outcome == Outcome::DuplicateOrOldId && r.calls == 1);
    }
    // SDK-equivalent reentry cannot consume new input or recursively attempt.
    Flow f; Input first{report(1,0)}, nested{report(2,0)}; Reporter fallback;
    auto callback = [&](uint32_t, uint32_t) {
        assert(f.lastId() == 1);
        assert(f.poll(nested, active, fallback).outcome == Outcome::Reentry);
        assert(nested.reads == 0 && fallback.calls == 0);
        return ReportResult{ReportStatus::SdkRejected, 0x0c};
    };
    assert(f.poll(first, active, callback).outcome == Outcome::Attempted);
    assert(f.lastId() == 1 && fallback.calls == 0);
    assert(next(f, nested, fallback).outcome == Outcome::Attempted && fallback.calls == 1);
}
void deterministicMalformedStress() {
    uint32_t rng = 0x1397a5u;
    for (unsigned i = 0; i < 4000; ++i) {
        Flow f; Reporter r; std::string bad = "!"; // Cannot be a valid command.
        for (unsigned j = 0; j < i % 97; ++j) {
            rng = rng * 1664525u + 1013904223u;
            const auto byte = static_cast<unsigned char>(rng >> 24);
            bad += char(byte == '\n' ? 'x' : byte);
        }
        const auto event = submit(f, r, bad + "\n");
        assert(event.outcome == Outcome::Malformed || event.outcome == Outcome::TooLong);
        assert(r.calls == 0 && f.lastId() == 0);
        assert(submit(f, r, report(1,0)).outcome == Outcome::Attempted && r.calls == 1);
    }
}
int main() {
    parser(); conversionsAndIds(); lifecycle(); apiResultsAndNoRetry(); deterministicMalformedStress();
    puts("PASS Sink delay command flow: parser, bounds, rounding, IDs, epochs, lifecycle, reentry, API results, no retry; 4000 malformed fixtures");
}
