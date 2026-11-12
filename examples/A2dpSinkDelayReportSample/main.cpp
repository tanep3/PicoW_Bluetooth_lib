#include "A2dpSinkReceiver.h"
#include "DelayCommandFlow.h"
#include "pico/stdlib.h"
#include "sample_config.h"
#include <cstdio>

namespace {
namespace flow = sink_delay_sample;
// Integration stub, NOT a renderer or a latency measurement device. Replace
// with a bounded real output pipeline before reporting its measured latency.
// A zero accepted count honestly accounts for every PCM frame as dropped.
class UnconnectedOutput : public IAudioSink {
public:
    flow::OutputBoundary boundary;
    size_t write(const int16_t *, size_t, uint32_t, uint8_t) override { return 0; }
    void reset() override { boundary.reset(); } // No receiver API/logging here.
};

flow::ReportResult reportOnce(A2dpSinkReceiver &receiver, uint32_t units, uint32_t generation) {
    const auto result = receiver.reportDelay100us(units, generation);
    using Public = A2dpSinkReceiver::DelayReportStatus;
    using Local = flow::ReportStatus;
    switch (result.status) {
    case Public::Accepted: return {Local::Accepted, result.sdk_status};
    case Public::InvalidDelay: return {Local::InvalidDelay};
    case Public::StaleSession: return {Local::StaleSession};
    case Public::NotReady: return {Local::NotReady};
    case Public::NotNegotiated: return {Local::NotNegotiated};
    case Public::SdkRejected: return {Local::SdkRejected, result.sdk_status};
    }
    return {Local::NotReady};
}

flow::Context context(const A2dpSinkReceiver::State &s, const UnconnectedOutput &output) {
    // Deliberately stricter than the public API: only this sample's active
    // streaming output period may supply a measurement, never stopped PCM.
    return {s.generation, output.boundary.epoch(),
        s.begun && s.working && s.signaling && s.configured && s.streaming &&
        !s.connecting && !s.closing && !s.failed, output.boundary.exhausted()};
}

const char *outcomeName(flow::Outcome value) {
    using O = flow::Outcome;
    switch (value) {
    case O::None: return "none";
    case O::Control: return "control";
    case O::Malformed: return "malformed-line";
    case O::TooLong: return "line-too-long";
    case O::InvalidId: return "invalid-id";
    case O::DuplicateOrOldId: return "duplicate-or-old-id";
    case O::InvalidDelay: return "out-of-range-us";
    case O::BoundaryChanged: return "boundary-changed-during-input";
    case O::StaleMeasurement: return "stale-measurement-token";
    case O::NotReady: return "not-streaming-or-not-ready";
    case O::Stopped: return "sample-stopped";
    case O::Attempted: return "attempted-once";
    case O::Reentry: return "reentry-rejected";
    }
    return "unknown";
}
const char *resultName(flow::ReportStatus value) {
    using S = flow::ReportStatus;
    switch (value) {
    case S::Accepted: return "Accepted-local-SDK-admission-only";
    case S::InvalidDelay: return "InvalidDelay";
    case S::StaleSession: return "StaleSession";
    case S::NotReady: return "NotReady";
    case S::NotNegotiated: return "NotNegotiated";
    case S::SdkRejected: return "SdkRejected-no-retry";
    }
    return "unknown";
}
void logEvent(const flow::Event &event) {
    std::printf("report id=%lu outcome=%s", (unsigned long)event.command.id, outcomeName(event.outcome));
    if (event.outcome == flow::Outcome::Attempted) {
        std::printf(" generation=%lu output_epoch=%lu measured_us=%lu units100us=%lu result=%s",
            (unsigned long)event.command.generation, (unsigned long)event.command.output_epoch,
            (unsigned long)event.command.measured_us, (unsigned long)event.delay_100us,
            resultName(event.result.status));
        if (event.result.status == flow::ReportStatus::Accepted || event.result.status == flow::ReportStatus::SdkRejected)
            std::printf(" sdk_status=%u", unsigned(event.result.sdk_status));
    }
    std::puts("");
}
void printStatus(A2dpSinkReceiver &receiver, const UnconnectedOutput &output, const flow::Flow &commands) {
    const auto s = receiver.snapshot();
    const auto c = context(s, output);
    std::printf("generation=%lu output_epoch=%lu eligible=%u epoch_exhausted=%u last_id=%lu stopped=%u\n",
        (unsigned long)c.generation, (unsigned long)c.output_epoch, unsigned(c.ready), unsigned(c.epoch_exhausted),
        (unsigned long)commands.lastId(), unsigned(commands.stopped()));
    std::printf("begun=%u working=%u signaling=%u configured=%u streaming=%u connecting=%u closing=%u failed=%u\n",
        s.begun, s.working, s.signaling, s.configured, s.streaming, s.connecting, s.closing, s.failed);
}
int runExample() {
    static UnconnectedOutput output;
    static const A2dpSinkReceiver::Settings settings{{{SINK_DELAY_PEER_BYTES}}, false};
    static A2dpSinkReceiver receiver(output, settings);
    static flow::Flow commands;
    bool begin_attempted = false;
    bool failure_reported = false;
    stdio_init_all();
    std::puts("Radio not started. Whole lines: begin, listen, connect, disconnect, stop, status.");
    std::puts("report <id> <measurement_generation> <measurement_output_epoch> <actual_measured_us>");
    std::puts("No renderer/measurement hardware. Never invent latency. SSP confirmation disabled.");
    for (;;) {
        receiver.poll(); // All output callbacks complete BEFORE command handling.
        const auto state = receiver.snapshot();
        if (state.failed && !failure_reported) {
            failure_reported = true;
            commands.stop(); // Terminal error; no recovery/reconnect/report retry.
            std::puts("Receiver failed: command workflow stopped; keep polling, inspect status.");
        }
        const auto event = commands.poll(
            []() { return getchar_timeout_us(0); }, context(state, output),
            [&](uint32_t units, uint32_t original_generation) {
                return reportOnce(receiver, units, original_generation);
            });
        if (event.outcome == flow::Outcome::Control) {
            const auto kind = event.command.kind;
            if (kind == flow::Kind::Status) printStatus(receiver, output, commands);
            else if (kind == flow::Kind::Stop || kind == flow::Kind::Disconnect) {
                if (kind == flow::Kind::Stop) commands.stop();
                receiver.disconnect(); // Synchronous output reset invalidates old epochs.
                std::puts("Disconnect requested, not completed. stop is terminal; continue polling.");
            } else if (commands.stopped()) {
                std::puts("Sample stopped; no start/connect/listen retry. Status remains available.");
            } else if (kind == flow::Kind::Begin) {
                if (begin_attempted) std::puts("begin already attempted; no retry.");
                else {
                    begin_attempted = true;
                    const auto result = receiver.begin();
                    std::printf("begin=%u (local initialization result, not ready/connected)\n", unsigned(result));
                    if (result != A2dpSinkReceiver::BeginResult::Ok) commands.stop();
                }
            } else if (!state.working || state.failed || state.connecting || state.closing || state.signaling) {
                std::puts("Not idle/working; command rejected, no retry.");
            } else {
                const bool accepted = kind == flow::Kind::Listen ? receiver.listen() : receiver.connect();
                std::printf("%s accepted=%u (not connection/stream completion)\n",
                    kind == flow::Kind::Listen ? "listen" : "connect", unsigned(accepted));
            }
        } else if (event.outcome != flow::Outcome::None) logEvent(event);
        tight_loop_contents();
    }
}
} // namespace

int main() {
    // Default returns before stdio, receiver construction, begin or radio setup.
    // Ordinary if ensures the enabled path remains type-checked in both builds.
    if (!SINK_DELAY_ENABLE_RADIO) return 0;
    return runExample();
}
