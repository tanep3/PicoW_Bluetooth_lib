#pragma once
#include <cstdint>

namespace sink_pcm_sample {
// Standalone sample policy only. No SDK calls, peer changes or automatic reopen.
// Call from the single main loop at intervals shorter than uint32_t's wrap.
class DiscoverableListenWindow {
public:
    static constexpr uint32_t duration_ms = 60000;
    enum class Action { None, Hide, Expired };

    bool open(uint32_t now_ms, uint32_t generation) {
        if (active_) return false; // A repeated command never extends the deadline.
        opened_ms_ = now_ms;
        generation_ = generation;
        active_ = true;
        return true;
    }
    bool close() {
        const bool was_active = active_;
        active_ = false;
        return was_active;
    }
    bool active() const { return active_; }

    template<class State>
    Action poll(uint32_t now_ms, const State &state) {
        if (!active_) return Action::None;
        // poll() may consume an establishment and release together. The final
        // idle flags alone must not preserve visibility across that session.
        if (state.generation != generation_ || !state.working || state.failed || state.closing || state.connecting ||
            state.signaling || state.streaming || !state.accepting) {
            active_ = false;
            return Action::Hide;
        }
        if (uint32_t(now_ms - opened_ms_) >= duration_ms) {
            active_ = false;
            return Action::Expired;
        }
        return Action::None;
    }
private:
    uint32_t opened_ms_ = 0;
    uint32_t generation_ = 0;
    bool active_ = false;
};
} // namespace sink_pcm_sample
