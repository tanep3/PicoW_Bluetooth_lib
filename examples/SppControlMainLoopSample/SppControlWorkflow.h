#pragma once
#include "../../spp-core/SppCore.h"

namespace spp_sample {
// An exclusive, single-loop owner of the supplied Client-like facade. No SDK
// effects live in this header; a deterministic fake can exercise this exact flow.
struct Options {
  spp::Address first_peer{}, next_peer{};
  uint8_t remote_channel = 1, local_channel = 2;
  uint64_t timeout_us = 10000000;
};
enum class Stage {
  Idle, WaitingReady, WaitingListen, WaitingOpen, WaitingControl,
  Withdrawing, Draining, SelectingPeer, Complete, Failed
};
enum class Failure {
  None, InvalidOptions, NotQueued, RuntimeFailed, HistoryLost,
  OperationRejected, RouteClosed, Timeout
};

template<class ClientLike> class Workflow {
public:
  using Transport = typename ClientLike::Transport;
  struct RouteObservation {
    uint32_t generation = 0;
    uint64_t line_events = 0, modem_events = 0, local_port_events = 0, remote_port_events = 0;
    uint8_t line_status = 0, modem_status = 0;
    spp::PortSettings local_port{}, remote_port{};
    Transport transport = Transport::Unknown;
  };
  struct Report {
    uint64_t history_sequence = 0;
    unsigned queued_requests = 0, controls_completed = 0, control_failures = 0;
    unsigned select_attempts = 0;
    uint32_t last_control_token = 0;
    int32_t last_control_status = 0;
    spp::Control last_control = spp::Control::QueryPort;
    spp::Error last_rejection = spp::Error::None;
    std::array<RouteObservation, spp::RouteCount> routes{};
  };
private:
  ClientLike &client_;
  Options options_;
  Stage stage_ = Stage::Idle;
  Failure failure_ = Failure::None;
  Report report_{};
  uint64_t stage_started_ = 0;
  bool stopping_ = false;
  spp::Handle handle_{};
  uint32_t generation_before_connect_ = 0, control_before_ = 0;
  unsigned control_index_ = 0;
  spp::Control pending_control_ = spp::Control::QueryPort;
  std::array<uint32_t, spp::RouteCount> disconnect_generation_{};
  std::array<uint32_t, spp::EndpointCount> unlisten_token_{};
  uint32_t cancelled_query_ = 0;

  void enter(Stage next, uint64_t now) { stage_ = next; stage_started_ = now; }
  void fail(Failure reason, uint64_t now) {
    if (stage_ == Stage::Failed) return;
    if (failure_ == Failure::None) failure_ = reason;
    stopping_ = true;
    // Cleanup is bounded too. Never fabricate Closed or release an ERTM lease.
    if (stage_ == Stage::Withdrawing || stage_ == Stage::Draining)
      enter(Stage::Failed, now);
    else enter(Stage::Withdrawing, now);
  }
  bool queued(bool accepted, uint64_t now) {
    if (accepted) { ++report_.queued_requests; return true; }
    fail(Failure::NotQueued, now); return false;
  }
  static bool same(spp::Handle a, spp::Handle b) {
    return a.slot == b.slot && a.generation == b.generation;
  }
  static bool localRequest(spp::Kind kind) {
    switch (kind) {
    case spp::Kind::Begin: case spp::Kind::Listen: case spp::Kind::ConnectChannel:
    case spp::Kind::ControlRequested: case spp::Kind::Unlisten:
    case spp::Kind::Disconnect: case spp::Kind::CancelDiscovery: case spp::Kind::SelectPeer:
      return true;
    default: return false;
    }
  }
  void observeHistory(uint64_t now) {
    for (size_t i = 0; i < client_.historySize(); ++i) {
      const auto h = client_.history(i); // Copied, bounded diagnostics.
      if (h.sequence <= report_.history_sequence) continue;
      if (h.sequence != report_.history_sequence + 1) {
        // Lost history is not proof of success. Stop initiating application work.
        report_.history_sequence = h.sequence;
        fail(Failure::HistoryLost, now);
      } else report_.history_sequence = h.sequence;
      if (localRequest(h.event.kind) && h.result != spp::Error::None) {
        report_.last_rejection = h.result;
        fail(Failure::OperationRejected, now);
      }
      if (h.event.kind == spp::Kind::ControlResult && same(h.event.handle, handle_) &&
          (h.result == spp::Error::None || h.result == spp::Error::Failed)) {
        report_.last_control_token = h.event.token;
        report_.last_control_status = h.event.status;
        report_.last_control = pending_control_;
        ++report_.controls_completed;
        if (h.event.status) ++report_.control_failures;
        // This is local SDK acceptance/rejection, NEVER a peer acknowledgement.
        // A control failure does not invalidate the independent byte stream.
      }
    }
  }
  void observeRoutes(const spp::State &s) {
    for (uint8_t i = 0; i < spp::RouteCount; ++i) {
      const auto &r = s.routes[i];
      auto &o = report_.routes[i];
      o.generation = r.generation;
      o.line_events = r.line_status_events; o.modem_events = r.modem_status_events;
      o.local_port_events = r.local_port_events; o.remote_port_events = r.remote_port_events;
      o.line_status = r.remote_line_status; o.modem_status = r.remote_modem_status;
      o.local_port = r.local_port; o.remote_port = r.remote_port;
      o.transport = client_.transport(spp::handleOf(s, i));
      // Values/counters describe this generation only, including setup-time
      // notifications. They are not correlated replies to our control requests.
    }
  }
  void sendNextControl(const spp::State &s, uint64_t now) {
    const auto &r = s.routes[handle_.slot];
    if (r.control_pending) return;
    control_before_ = r.control_token;
    bool accepted = false;
    switch (control_index_) {
    case 0:
      pending_control_ = spp::Control::QueryPort;
      accepted = client_.queryPort(handle_); break;
    case 1: {
      spp::PortSettings port; // 115200, 8 data bits, 1 stop bit, no parity.
      port.baud = spp::Baud::B115200;
      pending_control_ = spp::Control::ConfigurePort;
      accepted = client_.configurePort(handle_, port); break;
    }
    case 2:
      pending_control_ = spp::Control::LineStatus;
      accepted = client_.sendLineStatus(handle_, 0); break; // No line error.
    default:
      pending_control_ = spp::Control::ModemStatus;
      accepted = client_.sendModemStatus(handle_, 0x0c); break; // RTC | RTR, not EA.
    }
    if (queued(accepted, now)) enter(Stage::WaitingControl, now);
  }
  void withdraw(const spp::State &s, uint64_t now) {
    bool all_disabled = true;
    for (uint8_t i = 0; i < spp::EndpointCount; ++i) {
      const auto &e = s.endpoints[i];
      if (e.phase == spp::EndpointPhase::Failed) {
        fail(Failure::OperationRejected, now); return;
      }
      if (e.phase != spp::EndpointPhase::Disabled) all_disabled = false;
      if (e.phase == spp::EndpointPhase::Listening && unlisten_token_[i] != e.token) {
        if (queued(client_.unlisten(i), now)) unlisten_token_[i] = e.token;
        return; // One application mutation per step.
      }
    }
    // Unlisten does not close existing routes. Observe Disabled before teardown.
    if (all_disabled) enter(Stage::Draining, now);
  }
  void drain(const spp::State &s, uint64_t now) {
    for (const auto &endpoint : s.endpoints) {
      if (endpoint.phase != spp::EndpointPhase::Disabled) {
        enter(Stage::Withdrawing, now); return;
      }
    }
    if (s.query.busy && s.query.phase == spp::QueryPhase::Querying &&
        cancelled_query_ != s.query.token) {
      if (queued(client_.cancelDiscovery(s.query.token), now)) cancelled_query_ = s.query.token;
      return;
    }
    bool all_closed = true;
    for (uint8_t i = 0; i < spp::RouteCount; ++i) {
      const auto &r = s.routes[i];
      if (r.phase != spp::Phase::Closed) all_closed = false;
      if (r.phase != spp::Phase::Closed && r.phase != spp::Phase::Closing &&
          disconnect_generation_[i] != r.generation) {
        if (queued(client_.disconnect(spp::handleOf(s, i)), now))
          disconnect_generation_[i] = r.generation;
        return;
      }
    }
    // Cancelled != drained. Closed DLCs != released multiplexer storage.
    if (!all_closed || s.query.busy || client_.ertmStatus().active) return;
    if (stopping_) {
      enter(failure_ == Failure::None ? Stage::Complete : Stage::Failed, now); return;
    }
    ++report_.select_attempts;
    // Only Client can check its private pending queue / SDK bindings. false
    // means NOT queued, so bounded retry after another poll is safe here.
    if (client_.selectPeer(options_.next_peer)) {
      ++report_.queued_requests; enter(Stage::SelectingPeer, now);
    }
  }
public:
  Workflow(ClientLike &client, Options options) : client_(client), options_(options) {}
  bool start(uint64_t now) {
    if (stage_ != Stage::Idle) return false;
    if (options_.first_peer == spp::Address{} || options_.next_peer == spp::Address{} ||
        options_.first_peer == options_.next_peer || !options_.timeout_us ||
        !spp::validChannel(options_.remote_channel) || !spp::validChannel(options_.local_channel) ||
        client_.snapshot().config.peer != options_.first_peer) {
      failure_ = Failure::InvalidOptions; enter(Stage::Failed, now); return false;
    }
    enter(Stage::WaitingReady, now);
    return queued(client_.begin(), now); // Runtime is not Ready until polled.
  }
  void requestStop(uint64_t now) {
    if (stage_ == Stage::Idle || done()) return;
    stopping_ = true;
    // Do not overtake a queued begin/listen/connect/selection: an unchanged
    // snapshot is not proof that its side effects have already drained.
    if (stage_ != Stage::WaitingReady && stage_ != Stage::WaitingListen &&
        stage_ != Stage::WaitingOpen && stage_ != Stage::SelectingPeer &&
        stage_ != Stage::Withdrawing && stage_ != Stage::Draining)
      enter(Stage::Withdrawing, now);
  }
  void step(uint64_t now) {
    if (stage_ == Stage::Idle || done()) return;
    client_.poll(); // All facade calls, including reads, are on this same loop.
    observeHistory(now);
    const auto s = client_.snapshot();
    observeRoutes(s);
    if (s.runtime == spp::Runtime::Failed) {
      if (failure_ == Failure::None) failure_ = Failure::RuntimeFailed;
      enter(Stage::Failed, now); return;
    }
    if (done()) return;
    if (now < stage_started_ || now - stage_started_ >= options_.timeout_us) {
      fail(Failure::Timeout, now); return;
    }
    // Do not claim cleanup completion while a queued begin is still pending.
    if (s.runtime != spp::Runtime::Ready) return;
    switch (stage_) {
    case Stage::WaitingReady:
      if (stopping_) { enter(Stage::Withdrawing, now); break; }
      if (queued(client_.listen(0, options_.local_channel, spp::Uuid::from16(0x1101)), now))
        enter(Stage::WaitingListen, now);
      break;
    case Stage::WaitingListen:
      if (s.endpoints[0].phase == spp::EndpointPhase::Failed) {
        fail(Failure::OperationRejected, now); break;
      }
      if (s.endpoints[0].phase == spp::EndpointPhase::Listening) {
        if (stopping_) { enter(Stage::Withdrawing, now); break; }
        generation_before_connect_ = s.routes[0].generation;
        if (queued(client_.connect(0, options_.first_peer, options_.remote_channel), now))
          enter(Stage::WaitingOpen, now);
      }
      break;
    case Stage::WaitingOpen:
      if (s.routes[0].generation == generation_before_connect_) break;
      if (stopping_) { enter(Stage::Withdrawing, now); break; }
      if (s.routes[0].phase == spp::Phase::Closed) {
        fail(Failure::RouteClosed, now); break;
      }
      if (s.routes[0].phase == spp::Phase::Open) {
        handle_ = spp::handleOf(s, 0); sendNextControl(s, now);
      }
      break;
    case Stage::WaitingControl: {
      if (!spp::matches(s, handle_) || s.routes[handle_.slot].phase != spp::Phase::Open) {
        fail(Failure::RouteClosed, now); break;
      }
      const auto &r = s.routes[handle_.slot];
      if (r.control_token == control_before_ || r.control_pending ||
          report_.last_control_token != r.control_token) break;
      if (++control_index_ == 4) enter(Stage::Withdrawing, now);
      else sendNextControl(s, now);
      break;
    }
    case Stage::Withdrawing: withdraw(s, now); break;
    case Stage::Draining: drain(s, now); break;
    case Stage::SelectingPeer:
      if (s.config.peer == options_.next_peer)
        enter(stopping_ ? Stage::Withdrawing : Stage::Complete, now);
      break;
    default: break;
    }
  }
  bool done() const { return stage_ == Stage::Complete || stage_ == Stage::Failed; }
  Stage stage() const { return stage_; }
  Failure failure() const { return failure_; }
  const Report &report() const { return report_; }
};
} // namespace spp_sample
