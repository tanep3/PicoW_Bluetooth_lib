#include "A2dpSinkReceiver.h"
#include "BoundedPcmSink.h"
#include "ReportFraming.h"
#include "DiscoverableListenWindow.h"
#include "btstack.h"
#include "pico/stdlib.h"
#include "sample_config.h"
#if SINK_PCM_USB_CAPTURE
#include "UsbPcmCapture.h"
#endif
#include <array>
#include <cstdio>

namespace {
using Consumer = sink_pcm_sample::BoundedPcmSink<8192>;
constexpr size_t drain_frames = 512;

void reportTestIdentity() {
    bd_addr_t local_address;
    gap_local_bd_addr(local_address);
    std::printf("Test Sink address=%s; verify this exact address before selecting it on the source.\n",
        bd_addr_to_str(local_address));
}

// A real bounded PCM operation: sample magnitude measurement, not a player.
// Optional USB capture is armed only by an explicit r command while streaming.
struct Meter {
    uint32_t epoch = 0;
    uint64_t frames = 0;
    uint32_t peak = 0;
    void consume(const int16_t *pcm, const Consumer::PullResult &block) {
        if (epoch != block.epoch) { epoch = block.epoch; frames = 0; peak = 0; }
        for (size_t i = 0; i < block.frames * block.channels; ++i) {
            const int32_t sample = pcm[i]; // Widen before negating -32768.
            const uint32_t magnitude = sample < 0 ? uint32_t(-sample) : uint32_t(sample);
            if (magnitude > peak) peak = magnitude;
        }
        frames += block.frames;
    }
};

void report(const A2dpSinkReceiver &receiver, const Consumer &output, const Meter &meter) {
    static uint32_t last_report_id = 0;
    uint32_t report_id = 0;
    if (!sink_pcm_sample::nextReportId(last_report_id, report_id)) {
        std::puts("Sink status report ID exhausted; no framed status available.");
        return;
    }
    const auto state = receiver.snapshot();
    const auto radio = receiver.diagnostics();
    const auto fifo = output.statistics();
    std::printf(SINK_PCM_REPORT_PREFIX "working=%u accepting=%u connecting=%u closing=%u signaling=%u streaming=%u failed=%u\n",
        (unsigned long)report_id, 1u,
        state.working, state.accepting, state.connecting, state.closing,
        state.signaling, state.streaming, state.failed);
    std::printf(SINK_PCM_REPORT_PREFIX "PCM rate=%lu channels=%u queued=%u epoch=%lu measured_frames=%llu peak=%lu\n",
        (unsigned long)report_id, 2u,
        (unsigned long)output.sampleRate(), unsigned(output.channels()), unsigned(output.queuedFrames()),
        (unsigned long)output.epoch(), (unsigned long long)meter.frames, (unsigned long)meter.peak);
    std::printf(SINK_PCM_REPORT_PREFIX "receiver delivered=%llu dropped=%llu; FIFO accepted=%llu rejected=%llu drained=%llu flushed=%llu resets=%llu format_changes=%llu\n",
        (unsigned long)report_id, 3u,
        (unsigned long long)radio.delivered_frames, (unsigned long long)radio.dropped_frames,
        (unsigned long long)fifo.accepted_frames, (unsigned long long)fifo.rejected_frames,
        (unsigned long long)fifo.drained_frames, (unsigned long long)fifo.flushed_frames,
        (unsigned long long)fifo.reset_calls, (unsigned long long)fifo.format_changes);
}

int runRadioExample() {
    // Constructed only in the explicitly enabled path, never destroyed/replaced
    // for reconnect. The FIFO, receiver, meter and pull destination all persist.
    static Consumer output;
    static const A2dpSinkReceiver::Settings settings{
        {{SINK_PCM_PEER_BYTES}}, SINK_PCM_ALLOW_PAIRING != 0};
    static A2dpSinkReceiver receiver(output, settings);
    static std::array<int16_t, drain_frames * Consumer::max_channels> block{};
    static Meter meter;
    sink_pcm_sample::DiscoverableListenWindow discovery;
#if SINK_PCM_USB_CAPTURE
    tusb_init();
    static UsbPcmCapture capture;
#endif
    stdio_init_all();

    const auto begun = receiver.begin();
    if (begun != A2dpSinkReceiver::BeginResult::Ok) {
        std::printf("begin failed=%u; no begin retry\n", unsigned(begun));
        // Do not recreate the receiver, whose registration is process-lifetime.
        for (;;) {
#if SINK_PCM_USB_CAPTURE
            tud_task();
#endif
            receiver.poll(); tight_loop_contents();
        }
    }
    // begin() accepted initialization, not readiness or connection completion.
    // Continue polling; only later user input can request listen/connect.
    bool ready_reported = false;
    bool failed_reported = false;
    for (;;) {
#if SINK_PCM_USB_CAPTURE
        const bool capture_was_busy = capture.busy();
        tud_task();
#endif
        receiver.poll(); // write/reset finish before the application pulls PCM.
        if (SINK_PCM_DISCOVERABLE_LISTEN) {
            const auto action = discovery.poll(to_ms_since_boot(get_absolute_time()), receiver.snapshot());
            if (action != sink_pcm_sample::DiscoverableListenWindow::Action::None) {
                gap_discoverable_control(0);
                if (action == sink_pcm_sample::DiscoverableListenWindow::Action::Expired) {
                    receiver.disconnect();
                    std::puts("Discoverable listen expired; incoming admission stopped. Use l explicitly to retry.");
                }
            }
        }
        // Receiver callbacks are finished: drain this finite FIFO completely,
        // in <=512-frame chunks, before decoding another receiver-poll burst.
        do {
            const auto pulled = output.pull(block.data(), drain_frames * output.channels());
            meter.consume(block.data(), pulled); // Also synchronize an empty/reset epoch.
#if SINK_PCM_USB_CAPTURE
            capture.consume(block.data(), pulled.frames, pulled.sample_rate, pulled.channels, pulled.epoch);
            tud_task();
            const auto diag = receiver.diagnostics();
            capture.poll(to_ms_since_boot(get_absolute_time()), receiver.snapshot().streaming,
                         diag.dropped_frames, diag.decode_failures);
#endif
        } while (output.queuedFrames());
        // Do not retain a copied block across another poll/reset/session boundary.
        const auto state = receiver.snapshot();
        if (state.failed && !failed_reported) {
            failed_reported = true;
            std::puts("Sink failed; inspect status, no automatic restart/reconnect.");
        }
        if (state.working && !state.failed && !ready_reported) {
            ready_reported = true;
            std::puts("Ready: l=listen, c=one outgoing attempt, d=disconnect, ?=status");
#if SINK_PCM_USB_CAPTURE
            std::puts("Optional capture: r=record next five seconds over binary USB; use capture_usb.py");
#endif
            if (SINK_PCM_DISCOVERABLE_LISTEN) {
                std::puts("Test controls: i=repeat local Bluetooth identity (does not open discovery)");
                reportTestIdentity();
            }
        }
#if SINK_PCM_USB_CAPTURE
        // Discard input during capture rather than defer a second recording command.
        if (capture_was_busy || capture.busy()) {
            // stdio driver is disabled during binary output; read its CDC RX directly.
            tud_cdc_read_flush();
            continue;
        }
#endif
        const int command = getchar_timeout_us(0);
        switch (command) {
        case 'l':
        case 'c': {
            if (!state.working || state.failed || state.closing || state.connecting || state.signaling || state.streaming) {
                std::puts("Not idle/working; inspect ?, or disconnect and wait for closure.");
                break;
            }
            if (SINK_PCM_DISCOVERABLE_LISTEN && command == 'l' && discovery.active()) {
                std::puts("Discoverable listen already pending; original deadline unchanged.");
                break;
            }
            if (SINK_PCM_DISCOVERABLE_LISTEN && command == 'c' && discovery.close()) {
                gap_discoverable_control(0);
            }
            const bool accepted = command == 'l' ? receiver.listen() : receiver.connect();
            if (SINK_PCM_DISCOVERABLE_LISTEN && command == 'l' && accepted &&
                discovery.open(to_ms_since_boot(get_absolute_time()), receiver.snapshot().generation)) {
                gap_discoverable_control(1);
                std::puts("Discoverable listen requested; 60-second timeout checked by main loop, fixed source only.");
            }
            std::printf("%s accepted=%u (not connection/stream completion)\n",
                command == 'l' ? "listen" : "connect", unsigned(accepted));
            break;
        }
#if SINK_PCM_USB_CAPTURE
        case 'r': {
            const auto diag = receiver.diagnostics();
            if (!state.streaming || !capture.start(output.sampleRate(), output.channels(), output.epoch(),
                    to_ms_since_boot(get_absolute_time()), diag.dropped_frames, diag.decode_failures))
                std::puts("CAPTURE_REJECTED not streaming or invalid format");
            break;
        }
#endif
        case 'd':
            if (SINK_PCM_DISCOVERABLE_LISTEN && discovery.close()) gap_discoverable_control(0);
            receiver.disconnect(); // reset() flushes queued PCM synchronously.
            // Meter has no PCM queue. Synchronize its epoch before status output.
            meter.consume(block.data(), output.pull(block.data(), 0));
            std::puts("Disconnect requested; wait for closing/signaling/connecting=0.");
            break;
        case '?':
            report(receiver, output, meter);
            break;
        case 'i':
            if (SINK_PCM_DISCOVERABLE_LISTEN && state.working && !state.failed) reportTestIdentity();
            break;
        default:
            break;
        }
        tight_loop_contents();
    }
}
} // namespace

int main() {
    // Ordinary if keeps the complete example type-checked in the default build.
    // Return before stdio, receiver construction, begin() or radio initialization.
    if (!SINK_PCM_ENABLE_RADIO) return 0;
    return runRadioExample();
}
