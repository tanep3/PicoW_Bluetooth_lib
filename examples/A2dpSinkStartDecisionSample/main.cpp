#include "A2dpSinkReceiver.h"
#include "pico/stdlib.h"
#include "sample_config.h"
#include <cstdio>

// No audio hardware. Count all PCM as dropped; never pretend it was rendered.
struct UnconnectedOutput : IAudioSink {
    void reset() override {}
    size_t write(const int16_t *, size_t, uint32_t, uint8_t) override { return 0; }
};
int main() {
    stdio_init_all();
    static UnconnectedOutput output;
    A2dpSinkReceiver::Settings settings;
    settings.peer = {{SINK_START_PEER_BYTES}};
    settings.require_start_decision = true;
    static A2dpSinkReceiver receiver(output, settings);
    bool begun = false, permit = false;
    std::puts("START decision sample: radio disabled by default, PCM is dropped.");
    std::puts("b begin; l listen; c connect; a allow future START; r refuse future START; x disconnect; s status");
    for (;;) {
        if (begun) receiver.poll();
        // Policy is chosen BEFORE the remote request. Never block for human
        // input while the peer's START response timer is running.
        const auto request = receiver.pendingStart();
        if (request.request_id) {
            const auto result = receiver.decideStart(request, permit);
            std::printf("START generation=%lu id=%llu allow=%u result=%u sdk=%u (local admission only)\n",
                (unsigned long)request.generation, (unsigned long long)request.request_id,
                unsigned(permit), unsigned(result.status), unsigned(result.sdk_status));
            if (result.status != A2dpSinkReceiver::StartDecisionStatus::Accepted) receiver.disconnect();
        }
        const int command = getchar_timeout_us(0); // At most one command/loop.
        switch (command) {
        case 'a': permit = true; std::puts("Future START: allow"); break;
        case 'r': permit = false; std::puts("Future START: refuse"); break;
        case 'b':
#if SINK_START_ENABLE_RADIO
            if (!begun) {
                const auto result = receiver.begin();
                begun = result == A2dpSinkReceiver::BeginResult::Ok;
                std::printf("begin=%u (FeatureUnavailable if optional support absent)\n", unsigned(result));
            }
#else
            std::puts("Radio disabled at build time; begin not called.");
#endif
            break;
        case 'l': if (begun) std::printf("listen=%u\n", unsigned(receiver.listen())); break;
        case 'c': if (begun) std::printf("connect-admitted=%u\n", unsigned(receiver.connect())); break;
        case 'x': if (begun) receiver.disconnect(); break;
        case 's': {
            const auto state = receiver.snapshot();
            std::printf("feature=%u begun=%u signaling=%u streaming=%u failed=%u\n",
                unsigned(receiver.startDecisionsAvailable()), unsigned(begun), unsigned(state.signaling),
                unsigned(state.streaming), unsigned(state.failed));
            break;
        }
        default: break;
        }
        sleep_ms(1);
    }
}
