#pragma once
#include "A2dpSinkReceiver.h"
#include "AvrcpController.h"
#include "CommandParser.h"
#include "sample_config.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace avrcp_media_sample {
using picow::avrcp::Address;
using picow::avrcp::Binding;
using Controller = picow::avrcp::AvrcpController;
inline uint32_t nowMs() { return to_ms_since_boot(get_absolute_time()); }

// Standalone single-loop application, not another library adapter. All owners
// and callback storage remain alive until reset, including after terminal stop.
class Application {
public:
    static constexpr uint32_t pairing_ms = 300000;
    static constexpr uint32_t lifetime_ms = 900000;
    static constexpr uint32_t startup_ms = 15000;
    static constexpr uint32_t command_ms = 1500;
    Application(Address peer, bool pairing_allowed)
        : peer_(peer), pairing_allowed_(pairing_allowed), sink_(output_, {peer, false}) {}
    bool attempted() const { return attempted_; }
    Binding binding() const { return controller_.bindingForPeer(peer_); }

    void command(Command cmd, const Binding &input_binding) {
        if (cmd == Command::None) return;
        if (cmd == Command::Status) { report(); return; }
        if (cmd == Command::Start) { start(); return; }
        if (cmd == Command::Shutdown) { if (attempted_) stop(1); return; }
        if (cmd == Command::Invalid) { std::puts("DENIED malformed line"); return; }
        if (queue_lost_ || reason_) { stop(queue_lost_ ? 4 : reason_); return; }
        if (!running_ || !ready_) { std::puts("DENIED not ready"); return; }
        if (cmd == Command::Listen) {
            std::printf("LISTEN_LOCAL accepted=%u; no discoverability or pairing change\n", unsigned(sink_.listen()));
            return;
        }
        if (cmd == Command::Pair) {
            if (!pairing_allowed_) { std::puts("DENIED pairing disabled in build"); return; }
            if (pair_window_ || sink_.snapshot().signaling || binding().cid) {
                std::puts("DENIED pairing already active/connected"); return;
            }
            if (!sink_.listen()) { std::puts("DENIED Sink listen"); return; }
            pair_start_ = nowMs(); setPairing(true);
            std::puts("PAIR_WINDOW 300s, configured peer only; select this Pico on your source.");
            return;
        }
        if (cmd == Command::Connect) {
            // Establish audio first; connection admission is not a live binding.
            const auto link = BluetoothRuntime::instance().linkForPeer(peer_);
            if (!sink_.snapshot().signaling || !link.active) {
                std::puts("DENIED establish source A2DP connection first"); return;
            }
            std::printf("CONNECT_LOCAL rc=%d\n", controller_.connect(peer_)); return;
        }
        const auto b = binding();
        if (cmd == Command::Disconnect) {
            // Sink may already be connected even when AVRCP setup failed.
            // Do not let a partial old input line disconnect a newer CT binding.
            if (!(b == input_binding) || click_pending_ || query_pending_) {
                std::puts("DENIED stale binding or command busy"); return;
            }
            if (b.cid) std::printf("DISCONNECT_LOCAL rc=%d\n", controller_.disconnect(b));
            else std::puts("DISCONNECT_LOCAL no AVRCP binding; closing Sink");
            sink_.disconnect(); setPairing(false); return;
        }
        if (!b.cid || !(b == input_binding) || click_pending_ || query_pending_) {
            std::puts("DENIED stale/no binding or command busy"); return;
        }
        if (cmd == Command::Query) {
            const int rc = controller_.query(b, Controller::Query::PlayStatus);
            query_pending_ = rc == 0; command_start_ = nowMs(); query_binding_ = b;
            std::printf("QUERY_LOCAL rc=%d; await remote PlayStatus\n", rc); return;
        }
        if (cmd == Command::Pause || cmd == Command::Play) {
            const uint8_t op = cmd == Command::Pause ? 0x46 : 0x44;
            const int rc = controller_.press(b, op);
            std::printf("PRESS_LOCAL op=%u rc=%d; not remote success\n", unsigned(op), rc);
            if (!rc) {
                click_pending_ = true; click_binding_ = b; operation_ = op;
                press_seen_ = false; press_response_ = 0; command_start_ = nowMs();
            }
        }
    }

    void tick() {
        const auto ms = nowMs();
        if (attempted_) watchdog_update();
        // Enforce elapsed budgets before entering SDK poll, whose hold timer
        // can otherwise fire after a delayed main-loop iteration.
        if (running_) {
            if (queue_lost_) stop(4);
            else if (reason_) stop(reason_);
            else if (uint32_t(ms - start_) >= lifetime_ms) stop(2);
            else if (!ready_ && uint32_t(ms - start_) >= startup_ms) stop(3);
            else if ((click_pending_ || query_pending_) &&
                     uint32_t(ms - command_start_) >= command_ms) stop(10);
        }
        if (pair_window_ && uint32_t(ms - pair_start_) >= pairing_ms) {
            setPairing(false); std::puts("PAIR_WINDOW_CLOSED");
        }
        if (attempted_) { sink_.poll(); controller_.poll(nowMs()); drainEvents(); }
        if (running_ && (queue_lost_ || reason_)) stop(queue_lost_ ? 4 : reason_);
        if (running_ && ready_ && !ready_reported_) {
            ready_reported_ = true;
            std::puts("READY: verify local address; PAIR is explicit and build-gated.");
            Address local{}; gap_local_bd_addr(local.data());
            std::printf("LOCAL_ADDRESS %s\n", bd_addr_to_str(local.data()));
        }
    }

private:
    // Frames are deliberately discarded immediately. No audio output or capture.
    struct DiscardPcm final : IAudioSink {
        size_t write(const int16_t *, size_t frames, uint32_t, uint8_t) override { return frames; }
        void reset() override {}
    } output_;
    struct Record {
        Controller::Event event{};
        uint32_t ms = 0;
        std::array<uint8_t, 15> status{};
        bool play_status = false;
    };
    Address peer_;
    bool pairing_allowed_;
    A2dpSinkReceiver sink_;
    Controller controller_;
    std::array<Record, 32> records_{};
    unsigned head_ = 0, tail_ = 0, count_ = 0;
    uint32_t sequence_ = 0;
    bool attempted_ = false, running_ = false, ready_ = false, finished_ = false;
    bool ready_reported_ = false, pair_window_ = false, queue_lost_ = false;
    bool click_pending_ = false, query_pending_ = false, press_seen_ = false;
    uint8_t operation_ = 0, press_response_ = 0;
    unsigned reason_ = 0;
    uint32_t start_ = 0, pair_start_ = 0, command_start_ = 0;
    Binding click_binding_{}, query_binding_{};
    btstack_packet_callback_registration_t guard_registration_{};
    inline static Application *active_ = nullptr;

    void queue(const Record &r) {
        if (count_ == records_.size()) { queue_lost_ = true; return; }
        records_[tail_] = r; tail_ = (tail_ + 1) % records_.size(); ++count_;
    }
    static void event(const Controller::Event &e, void *context) {
        auto &a = *static_cast<Application *>(context);
        Record r; r.event = e; r.ms = nowMs(); a.queue(r);
    }
    static void sdkEvent(const Controller::SdkEvent &e, void *context) {
        auto &a = *static_cast<Application *>(context);
        if (!e.bytes || e.size < 3 || e.bytes[2] != AVRCP_SUBEVENT_PLAY_STATUS) return;
        if (e.size != 15 || e.bytes[0] != HCI_EVENT_AVRCP_META || unsigned(e.bytes[1]) + 2 != e.size) {
            a.queue_lost_ = true; return;
        }
        Record r; r.event.binding = e.binding; r.ms = nowMs(); r.play_status = true;
        std::memcpy(r.status.data(), e.bytes, r.status.size()); a.queue(r);
    }
    static void hci(uint8_t type, uint16_t, uint8_t *packet, uint16_t size) {
        if (!active_ || !packet || type != HCI_EVENT_PACKET || size < 2 || unsigned(packet[1]) + 2 != size) return;
        auto &a = *active_;
        if (packet[0] == BTSTACK_EVENT_STATE && size >= 3 && btstack_event_state_get_state(packet) == HCI_STATE_WORKING)
            a.ready_ = true;
        if (packet[0] == BTSTACK_EVENT_STATE && size >= 3 && btstack_event_state_get_state(packet) == HCI_STATE_OFF && a.ready_ && a.running_)
            a.reason_ = 12;
        if (packet[0] == HCI_EVENT_CONNECTION_COMPLETE && size >= 13 && !hci_event_connection_complete_get_status(packet)) {
            Address p{}; hci_event_connection_complete_get_bd_addr(packet, p.data());
            if (p != a.peer_) a.reason_ = 7;
        }
        if (packet[0] == HCI_EVENT_CONNECTION_REQUEST && size >= 12) {
            Address p{}; hci_event_connection_request_get_bd_addr(packet, p.data());
            if (p != a.peer_) a.reason_ = 7;
        }
        if (packet[0] == HCI_EVENT_USER_CONFIRMATION_REQUEST && size >= 12) {
            Address p{}; hci_event_user_confirmation_request_get_bd_addr(packet, p.data());
            if (!a.pair_window_ || p != a.peer_) a.reason_ = 8;
        }
        if (packet[0] == HCI_EVENT_PIN_CODE_REQUEST || packet[0] == HCI_EVENT_USER_PASSKEY_REQUEST) a.reason_ = 8;
    }
    void setPairing(bool allow) {
        auto &runtime = BluetoothRuntime::instance();
        runtime.setPairingPolicy(BluetoothRuntime::Profile::A2dpSink, peer_, allow);
        runtime.setPairingPolicy(BluetoothRuntime::Profile::Avrcp, peer_, allow);
        pair_window_ = allow; gap_discoverable_control(allow ? 1 : 0);
    }
    void start() {
        if (attempted_) { std::puts("DENIED restart: reset for a new process lifetime"); return; }
        attempted_ = true; running_ = true; start_ = nowMs(); active_ = this;
        watchdog_enable(5000, true);
        controller_.setCallback(event, this); controller_.setSdkCallback(sdkEvent, this);
        controller_.setCommandTimeout(700);
        Controller::Config config; config.peer_count = 1; config.peers[0] = peer_;
        if (sink_.begin() != A2dpSinkReceiver::BeginResult::Ok || !controller_.begin(config)) { stop(9); return; }
        guard_registration_.callback = hci; hci_add_event_handler(&guard_registration_);
        gap_set_local_name("AVRCP Media Control");
        std::puts("START accepted; await READY. No pairing or playback action yet.");
    }
    void stop(unsigned reason) {
        if (finished_) return;
        finished_ = true; reason_ = reason; running_ = false;
        click_pending_ = false; query_pending_ = false;
        if (BluetoothRuntime::instance().ownsRadio()) {
            setPairing(false); gap_connectable_control(0);
            controller_.stop(); sink_.disconnect(); BluetoothRuntime::instance().powerOff();
        }
        std::printf("SHUTDOWN_LOCAL reason=%u elapsed_ms=%lu queue_loss=%u; await HCI OFF\n",
            reason_, (unsigned long)(nowMs() - start_), unsigned(queue_lost_));
    }
    void drainEvents() {
        while (count_) {
            // Loss or a guard failure during poll invalidates the whole batch.
            // Log copied records, but do not release/interpret them as success.
            if (running_ && (queue_lost_ || reason_)) stop(queue_lost_ ? 4 : reason_);
            const Record r = records_[head_]; head_ = (head_ + 1) % records_.size(); --count_; ++sequence_;
            if (r.play_status) {
                const auto &p = r.status;
                const bool current = query_pending_ && r.event.binding == query_binding_ && r.event.binding == binding();
                std::printf("PLAY_STATUS seq=%lu ms=%lu cid=%u current=%u ctype=%u length_ms=%lu position_ms=%lu playback=%u interpreted=%u\n",
                    (unsigned long)sequence_, (unsigned long)r.ms, r.event.binding.cid, unsigned(current), unsigned(p[5]),
                    (unsigned long)little_endian_read_32(p.data(), 6), (unsigned long)little_endian_read_32(p.data(), 10), unsigned(p[14]),
                    unsigned(current && p[5] == AVRCP_CTYPE_RESPONSE_IMPLEMENTED_STABLE));
                if (current) query_pending_ = false;
                continue;
            }
            const auto &e = r.event;
            std::printf("CT_EVENT seq=%lu ms=%lu kind=%u cid=%u epoch=%lu generation=%lu op=%u response=%u peer_match=%u\n",
                (unsigned long)sequence_, (unsigned long)r.ms, unsigned(e.kind), e.binding.cid,
                (unsigned long)e.binding.epoch, (unsigned long)e.binding.generation, unsigned(e.operation), unsigned(e.response), unsigned(e.binding.peer == peer_));
            if (!running_) continue;
            if (e.kind == Controller::EventKind::Connected && e.binding.peer != peer_) { stop(7); continue; }
            if (e.kind == Controller::EventKind::Timeout || e.kind == Controller::EventKind::CustomError) { stop(10); continue; }
            if (e.kind == Controller::EventKind::Disconnected) { click_pending_ = false; query_pending_ = false; continue; }
            if (!click_pending_ || !(e.binding == click_binding_) || e.operation != operation_) continue;
            if (e.kind == Controller::EventKind::PressResponse) {
                press_seen_ = true; press_response_ = e.response;
                // Return from callback before mutation. Rejected presses may
                // already be releasing inside the public facade: do not repeat.
                if (controller_.phase(click_binding_) == Controller::Phase::Held) {
                    const int rc = controller_.release(click_binding_);
                    std::printf("RELEASE_LOCAL rc=%d\n", rc); if (rc) stop(11);
                }
            }
            if (e.kind == Controller::EventKind::ReleaseResponse) {
                const bool accepted = press_seen_ && press_response_ == 9 && e.response == 9;
                click_pending_ = false;
                std::printf("CLICK_COMPLETE remote_accepted=%u; query status and verify player UI, then wait before next command\n", unsigned(accepted));
            }
        }
    }
    void report() {
        const auto s = sink_.snapshot(); const auto d = sink_.diagnostics(); const auto b = binding(); const auto c = controller_.status();
        const bool initialized = BluetoothRuntime::instance().ownsRadio();
        // HCI getter requires initialized SDK storage. 255 means unavailable,
        // never a fabricated observation of the radio being OFF.
        const unsigned hci_state = initialized ? unsigned(hci_get_state()) : 255u;
        std::printf("STATUS initialized=%u hci=%u running=%u ready=%u pairing=%u sink_signaling=%u streaming=%u ct_cid=%u epoch=%lu generation=%lu phase=%u click_pending=%u query_pending=%u frames=%llu dropped=%llu decode_errors=%llu ct_errors=%lu malformed=%lu ignored=%lu\n",
            unsigned(initialized), hci_state, unsigned(running_), unsigned(ready_), unsigned(pair_window_), unsigned(s.signaling), unsigned(s.streaming), b.cid,
            (unsigned long)b.epoch, (unsigned long)b.generation, unsigned(controller_.phase(b)), unsigned(click_pending_), unsigned(query_pending_),
            (unsigned long long)d.delivered_frames, (unsigned long long)d.dropped_frames, (unsigned long long)d.decode_failures,
            (unsigned long)c.connection_errors, (unsigned long)c.malformed, (unsigned long)c.ignored);
    }
};
}
