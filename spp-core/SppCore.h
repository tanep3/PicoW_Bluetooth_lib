#pragma once
// Deterministic rules and a bounded, single-loop engine.
// No BTstack/Pico dependency. See docs/spp_state_transitions_jp.md.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace spp {
constexpr size_t RouteCount = 2, EndpointCount = 2;
constexpr size_t TxCapacity = 1024, RxCapacity = 1024, MaxFrame = 1024;
constexpr size_t CandidateCapacity = 8, MaxActions = 6;
constexpr size_t RxCreditWindow = 4, ControlQueueReserve = 8;
constexpr uint8_t NoRoute = 0xff;
using Address = std::array<uint8_t, 6>;
struct Uuid {
  std::array<uint8_t, 16> bytes{};
  static Uuid from16(uint16_t value) {
    return {{{0, 0, static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value),
              0, 0, 0x10, 0, 0x80, 0, 0, 0x80, 0x5f, 0x9b, 0x34, 0xfb}}};
  }
};
struct Handle { uint32_t generation = 0; uint8_t slot = NoRoute; };
struct Candidate { uint32_t record = 0; uint8_t channel = 0; };
enum class Runtime { Stopped, Starting, Ready, Failed };
enum class Phase { Closed, Resolving, Selecting, Connecting, Open, Closing };
enum class EndpointPhase { Disabled, Registering, Listening, Failed, Unregistering };
enum class QueryPhase { Idle, Querying, Complete, Failed, TimedOut, Cancelled };
enum class Error {
  None, InvalidInput, InvalidHandle, NotReady, NotConnected, Busy, WouldBlock,
  Failed, Timeout, NotFound, CandidateOverflow, StaleEvent, QueueOverflow,
  ClockRegression, RxOverflow, TokenExhausted, DuplicateChannel, Cancelled
};
enum class Kind {
  Begin, InitResult, RuntimeFault, QueueOverflow, Tick,
  ConnectChannel, ConnectUuid, Discover, SdpCandidate, SdpComplete, SdpQuiesced,
  SelectCandidate, CancelDiscovery, Listen, ListenResult, Incoming, Opened, AclOpened,
  WriteRequested, CanSend, CanSendFailed, SendResult, RxBytes, ReadRequested,
  CreditResult, Disconnect, DisconnectFailed, Closed, AclClosed,
  SendDeferred, // Append only: diagnostic histories retain the earlier Kind IDs.
  Unlisten, UnlistenResult, ControlRequested, ControlResult,
  RemoteLineStatus, RemoteModemStatus, PortConfiguration, SelectPeer
};
enum class Op {
  Initialize, QuiesceRuntime, StartSdp, CancelSdp, RegisterService,
  Connect, Accept, Reject, RequestCanSend, Send, GrantCredits, Disconnect,
  CancelConnect, UnregisterService, Control, SelectPeer
};
struct Config {
  Address peer{}; // One permitted peer at a time; selectPeer requires quiescent routes.
  uint64_t init_timeout_us = 10000000, sdp_timeout_us = 10000000;
};
// Wire values are explicit: this SDK incorrectly aliases all RPN_DATA_BITS_*
// constants to zero. The adapter passes these verified wire encodings instead.
enum class Baud : uint8_t { B2400, B4800, B7200, B9600, B19200, B38400, B57600, B115200, B230400 };
enum class DataBits : uint8_t { Five, Six, Seven, Eight };
enum class StopBits : uint8_t { One, OneAndHalf };
enum class Parity : uint8_t { None = 0, Odd = 1, Even = 3, Mark = 5, Space = 7 };
enum class Control : uint8_t { LineStatus, ModemStatus, ConfigurePort, QueryPort };
struct PortSettings {
  Baud baud = Baud::B9600;
  DataBits data_bits = DataBits::Eight;
  StopBits stop_bits = StopBits::One;
  Parity parity = Parity::None;
  uint8_t flow_control = 0, xon = 0, xoff = 0;
};
struct ControlRequest {
  PortSettings port{};
  Control operation = Control::QueryPort;
  uint8_t value = 0;
};
inline bool validPort(const PortSettings &p) {
  const auto parity = static_cast<uint8_t>(p.parity);
  return static_cast<uint8_t>(p.baud) <= 8 &&
      static_cast<uint8_t>(p.data_bits) <= 3 &&
      static_cast<uint8_t>(p.stop_bits) <= 1 &&
      (parity == 0 || parity == 1 || parity == 3 || parity == 5 || parity == 7) &&
      (p.flow_control & ~0x3f) == 0;
}
inline bool validControl(const ControlRequest &c) {
  switch (c.operation) {
  case Control::LineStatus:
    return c.value == 0 || ((c.value & 1) && (c.value & 0x0e) && !(c.value & ~0x0f));
  case Control::ModemStatus: return (c.value & ~0xce) == 0;
  case Control::ConfigurePort:
    // BTstack's configuration API cannot set XON/XOFF characters.
    return validPort(c.port) && !c.port.xon && !c.port.xoff;
  case Control::QueryPort: return true;
  }
  return false;
}
struct Route {
  Address peer{};
  Uuid uuid{};
  std::array<Candidate, CandidateCapacity> candidates{};
  uint64_t tx_accepted = 0, tx_sent = 0, tx_discarded = 0;
  uint64_t rx_received = 0, rx_read = 0, rx_discarded = 0;
  uint64_t writable_epoch = 0;
  uint32_t generation = 0, query_token = 0, send_token = 0, credit_token = 0;
  int32_t status = 0;
  Phase phase = Phase::Closed;
  Error error = Error::None;
  uint16_t cid = 0, mtu = 0, tx_size = 0, rx_size = 0;
  uint16_t tx_head = 0, rx_head = 0, sending = 0, credits = 0;
  uint8_t channel = 0, candidate_count = 0;
  bool can_send_pending = false, credit_pending = false;
  ControlRequest control{};
  uint32_t control_token = 0;
  int32_t control_status = 0;
  bool control_pending = false;
  // Latest peer observations plus monotonically increasing per-generation
  // counters; history retains individual copied notifications while bounded.
  PortSettings local_port{}, remote_port{};
  uint64_t line_status_events = 0, modem_status_events = 0;
  uint64_t local_port_events = 0, remote_port_events = 0;
  uint8_t remote_line_status = 0, remote_modem_status = 0;
  uint16_t txFree() const { return phase == Phase::Open ? TxCapacity - tx_size : 0; }
};
struct Endpoint {
  Uuid uuid{};
  uint32_t token = 0;
  int32_t status = 0;
  EndpointPhase phase = EndpointPhase::Disabled;
  uint8_t channel = 0;
};
struct Query {
  Address peer{};
  Uuid uuid{};
  std::array<Candidate, CandidateCapacity> candidates{};
  Handle route{};
  uint64_t deadline = 0;
  uint32_t token = 0;
  int32_t status = 0;
  QueryPhase phase = QueryPhase::Idle;
  Error error = Error::None;
  uint8_t count = 0;
  bool busy = false;
};
struct State {
  Config config{};
  std::array<Route, RouteCount> routes{};
  std::array<Endpoint, EndpointCount> endpoints{};
  Query query{};
  uint64_t now_us = 0, init_deadline = 0, rejected = 0;
  uint32_t serial = 0, init_token = 0, acl_token = 0;
  uint16_t acl_cid = 0;
  bool acl_active = false;
  Runtime runtime = Runtime::Stopped;
  Error error = Error::None;
  int32_t status = 0;
};
// bytes is a byte count except SelectCandidate, where it is a candidate index.
// Connect*/Listen use slot; callbacks use handle. Query callbacks use token.
struct Event {
  uint64_t now_us = 0;
  Address peer{};
  Uuid uuid{};
  Candidate candidate{};
  ControlRequest control{};
  bool remote = false;
  Handle handle{};
  uint32_t token = 0;
  int32_t status = 0;
  uint16_t cid = 0, mtu = 0, bytes = 0;
  Kind kind = Kind::Tick;
  Error error = Error::None;
  uint8_t slot = NoRoute, channel = 0;
};
struct Action {
  Handle handle{};
  uint32_t token = 0;
  Op op = Op::Initialize;
  uint16_t cid = 0, bytes = 0;
};
struct Transition {
  State next{};
  std::array<Action, MaxActions> actions{};
  uint16_t accepted = 0, consumed = 0;
  uint8_t count = 0, writable_mask = 0;
  Error result = Error::None;
  void add(Op op, Handle h = {}, uint32_t token = 0, uint16_t cid = 0,
           uint16_t bytes = 0) {
    actions[count++] = {h, token, op, cid, bytes};
  }
};
inline bool validChannel(uint8_t channel) { return channel >= 1 && channel <= 30; }
inline bool matches(const State &s, Handle h) {
  return h.slot < RouteCount && h.generation != 0 &&
         s.routes[h.slot].generation == h.generation;
}
inline Handle handleOf(const State &s, uint8_t slot) {
  return slot < RouteCount ? Handle{s.routes[slot].generation, slot} : Handle{};
}

struct Rules {
private:
  static uint64_t deadline(uint64_t now, uint64_t delta) {
    return delta > UINT64_MAX - now ? UINT64_MAX : now + delta;
  }
  static void discard(Route &r) {
    r.tx_discarded += r.tx_size;
    r.rx_discarded += r.rx_size;
    r.tx_size = r.rx_size = r.tx_head = r.rx_head = r.sending = r.credits = 0;
    r.can_send_pending = r.credit_pending = r.control_pending = false;
  }
  static void runtimeFail(Transition &t, Error error, int32_t status) {
    auto &n = t.next;
    n.runtime = Runtime::Failed; n.error = error; n.status = status;
    for (auto &r : n.routes) {
      discard(r); r.phase = Phase::Closed; r.cid = 0; r.error = error;
    }
    for (auto &p : n.endpoints) p.phase = EndpointPhase::Failed;
    if (n.query.busy) {
      n.query.phase = QueryPhase::Failed; n.query.error = error;
      // QuiesceRuntime owns teardown; no new begin/query is permitted.
    }
    t.count = 0; t.result = error; t.add(Op::QuiesceRuntime);
  }
  static uint32_t token(Transition &t) {
    if (t.next.serial == UINT32_MAX) {
      runtimeFail(t, Error::TokenExhausted, 0); return 0;
    }
    return ++t.next.serial;
  }
  static void reject(Transition &t, Error why) {
    ++t.next.rejected; t.result = why;
  }
  static bool duplicate(const State &s, uint8_t except, const Address &peer,
                        uint8_t channel) {
    for (size_t i = 0; i < RouteCount; ++i) {
      const auto &r = s.routes[i];
      if (i != except && r.phase != Phase::Closed && r.channel == channel &&
          r.peer == peer) return true;
    }
    return false;
  }
  static void routeFail(Transition &t, uint8_t slot, Error error, int32_t status) {
    auto &r = t.next.routes[slot];
    discard(r); r.error = error; r.status = status;
    if (r.cid) {
      r.phase = Phase::Closing;
      t.add(Op::Disconnect, handleOf(t.next, slot), 0, r.cid);
    } else r.phase = Phase::Closed;
  }
  static void endQueryRoute(Transition &t, Error error) {
    const auto h = t.next.query.route;
    if (matches(t.next, h) && t.next.routes[h.slot].phase == Phase::Resolving) {
      routeFail(t, h.slot, error, t.next.query.status);
    }
  }
  static void cancelQuery(Transition &t, QueryPhase phase, Error error) {
    auto &q = t.next.query;
    q.phase = phase; q.error = error;
    endQueryRoute(t, error);
    t.add(Op::CancelSdp, q.route, q.token);
  }
  static bool freshRoute(Transition &t, uint8_t slot, const Address &peer) {
    auto &r = t.next.routes[slot];
    if (r.generation == UINT32_MAX) {
      runtimeFail(t, Error::TokenExhausted, 0); return false;
    }
    const auto generation = r.generation + 1;
    const auto epoch = r.writable_epoch;
    r = Route{}; r.generation = generation; r.peer = peer; r.writable_epoch = epoch;
    return true;
  }
  static void startConnect(Transition &t, uint8_t slot, uint8_t channel) {
    auto &r = t.next.routes[slot];
    if (duplicate(t.next, slot, r.peer, channel)) {
      routeFail(t, slot, Error::DuplicateChannel, 0);
      t.result = Error::DuplicateChannel; return;
    }
    r.channel = channel; r.phase = Phase::Connecting;
    t.add(Op::Connect, handleOf(t.next, slot));
  }
  static void requestSend(Transition &t, uint8_t slot) {
    auto &r = t.next.routes[slot];
    if (!r.tx_size || r.can_send_pending || r.sending) return;
    const auto id = token(t); if (!id) return;
    r.send_token = id; r.can_send_pending = true;
    t.add(Op::RequestCanSend, handleOf(t.next, slot), id, r.cid);
  }
  static void grant(Transition &t, uint8_t slot) {
    auto &r = t.next.routes[slot];
    if (r.credit_pending || !r.mtu) return;
    const auto frames = std::min<size_t>(RxCreditWindow, (RxCapacity - r.rx_size) / r.mtu);
    if (frames <= r.credits) return;
    const auto id = token(t); if (!id) return;
    const auto more = static_cast<uint16_t>(frames - r.credits);
    r.credits += more; r.credit_pending = true; r.credit_token = id;
    t.add(Op::GrantCredits, handleOf(t.next, slot), id, r.cid, more);
  }
  static Transition finish(const State &s, Transition t) {
    for (size_t i = 0; i < RouteCount; ++i) {
      if (!s.routes[i].txFree() && t.next.routes[i].txFree()) {
        ++t.next.routes[i].writable_epoch;
        t.writable_mask |= static_cast<uint8_t>(1u << i);
      }
    }
    return t;
  }
public:
  static Transition apply(const State &s, const Event &e) {
    Transition t; t.next = s;
    auto &n = t.next;
    // Time is supplied only by Tick; other callbacks cannot regress the clock.
    if (e.kind == Kind::QueueOverflow || e.kind == Kind::RuntimeFault) {
      runtimeFail(t, e.kind == Kind::QueueOverflow ? Error::QueueOverflow : Error::Failed,
                  e.status); return t;
    }
    if (e.kind == Kind::Tick) {
      if (e.now_us < s.now_us) runtimeFail(t, Error::ClockRegression, 0);
      else {
        n.now_us = e.now_us;
        if (s.runtime == Runtime::Starting && e.now_us >= s.init_deadline)
          runtimeFail(t, Error::Timeout, 0);
        else if (s.query.phase == QueryPhase::Querying &&
                 e.now_us >= s.query.deadline)
          cancelQuery(t, QueryPhase::TimedOut, Error::Timeout);
      }
      return finish(s, t);
    }
    if (e.kind == Kind::Begin) {
      if (s.runtime != Runtime::Stopped) reject(t, Error::NotReady);
      else if (s.config.peer == Address{} || !s.config.init_timeout_us ||
               !s.config.sdp_timeout_us) reject(t, Error::InvalidInput);
      else {
        n.init_token = token(t);
        if (n.init_token) {
          n.runtime = Runtime::Starting;
          n.init_deadline = deadline(n.now_us, n.config.init_timeout_us);
          t.add(Op::Initialize, {}, n.init_token);
        }
      }
      return t;
    }
    if (e.kind == Kind::InitResult) {
      if (s.runtime != Runtime::Starting || e.token != s.init_token)
        reject(t, Error::StaleEvent);
      else if (e.status) runtimeFail(t, Error::Failed, e.status);
      else n.runtime = Runtime::Ready;
      return t;
    }
    if (s.runtime != Runtime::Ready) { reject(t, Error::NotReady); return t; }
    switch (e.kind) {
    case Kind::SelectPeer: {
      if (e.peer == Address{}) { reject(t, Error::InvalidInput); break; }
      if (s.query.busy) { reject(t, Error::Busy); break; }
      bool busy = false;
      for (const auto &r : s.routes) busy |= r.phase != Phase::Closed;
      for (const auto &p : s.endpoints)
        busy |= p.phase == EndpointPhase::Registering || p.phase == EndpointPhase::Unregistering;
      if (busy) { reject(t, Error::Busy); break; }
      if (s.config.peer == e.peer) break;
      // Existing ACLs may still belong to A2DP/AVRCP. Do not disconnect them.
      // Retire every route handle and query result before selecting a new peer.
      bool exhausted = false;
      for (const auto &r : s.routes) exhausted |= r.generation == UINT32_MAX;
      if (exhausted) { runtimeFail(t, Error::TokenExhausted, 0); break; }
      for (uint8_t slot = 0; slot < RouteCount; ++slot) freshRoute(t, slot, e.peer);
      n.config.peer = e.peer; n.query = Query{};
      n.acl_active = false; n.acl_cid = 0; n.acl_token = 0;
      t.add(Op::SelectPeer);
      break;
    }
    case Kind::ConnectChannel:
    case Kind::ConnectUuid:
    case Kind::Discover: {
      const bool for_route = e.kind != Kind::Discover;
      const bool with_sdp = e.kind != Kind::ConnectChannel;
      if (e.peer != s.config.peer ||
          (for_route && e.slot >= RouteCount) ||
          (!with_sdp && !validChannel(e.channel))) {
        reject(t, Error::InvalidInput); break;
      }
      if ((for_route && s.routes[e.slot].phase != Phase::Closed) ||
          (with_sdp && s.query.busy)) { reject(t, Error::Busy); break; }
      if (!with_sdp && duplicate(s, e.slot, e.peer, e.channel)) {
        reject(t, Error::DuplicateChannel); break;
      }
      if (for_route && !freshRoute(t, e.slot, e.peer)) break;
      if (!with_sdp) { startConnect(t, e.slot, e.channel); break; }
      const auto id = token(t); if (!id) break;
      n.query = Query{};
      auto &q = n.query;
      q.peer = e.peer; q.uuid = e.uuid; q.token = id;
      q.busy = true; q.phase = QueryPhase::Querying;
      q.deadline = deadline(n.now_us, n.config.sdp_timeout_us);
      if (for_route) {
        q.route = handleOf(n, e.slot);
        auto &r = n.routes[e.slot];
        r.phase = Phase::Resolving; r.uuid = e.uuid; r.query_token = id;
      }
      t.add(Op::StartSdp, q.route, id);
      break;
    }
    case Kind::SdpCandidate: {
      auto &q = n.query;
      if (q.phase != QueryPhase::Querying || e.token != q.token) {
        reject(t, Error::StaleEvent); break;
      }
      if (!validChannel(e.candidate.channel)) {
        cancelQuery(t, QueryPhase::Failed, Error::InvalidInput); break;
      }
      bool found = false;
      for (size_t i = 0; i < q.count; ++i)
        found |= q.candidates[i].record == e.candidate.record &&
                 q.candidates[i].channel == e.candidate.channel;
      if (found) break;
      if (q.count == CandidateCapacity) {
        cancelQuery(t, QueryPhase::Failed, Error::CandidateOverflow); break;
      }
      q.candidates[q.count++] = e.candidate;
      break;
    }
    case Kind::SdpComplete: {
      auto &q = n.query;
      if (q.phase != QueryPhase::Querying || e.token != q.token) {
        reject(t, Error::StaleEvent); break;
      }
      q.busy = false; q.status = e.status;
      q.phase = (e.status || e.error != Error::None) ? QueryPhase::Failed : QueryPhase::Complete;
      q.error = e.error != Error::None ? e.error :
          (e.status ? Error::Failed : (q.count ? Error::None : Error::NotFound));
      if (q.error != Error::None) { endQueryRoute(t, q.error); break; }
      if (matches(n, q.route) && n.routes[q.route.slot].phase == Phase::Resolving) {
        auto &r = n.routes[q.route.slot];
        r.candidates = q.candidates; r.candidate_count = q.count;
        if (q.count == 1) startConnect(t, q.route.slot, q.candidates[0].channel);
        else r.phase = Phase::Selecting;
      }
      break;
    }
    case Kind::CancelDiscovery:
      if (s.query.phase != QueryPhase::Querying || e.token != s.query.token)
        reject(t, Error::StaleEvent);
      else cancelQuery(t, QueryPhase::Cancelled, Error::Cancelled);
      break;
    case Kind::SdpQuiesced:
      if (!s.query.busy || s.query.phase == QueryPhase::Querying ||
          e.token != s.query.token) reject(t, Error::StaleEvent);
      else n.query.busy = false;
      break;
    case Kind::SelectCandidate:
      if (!matches(s, e.handle)) reject(t, Error::InvalidHandle);
      else {
        const auto &r = s.routes[e.handle.slot];
        if (r.phase != Phase::Selecting || e.token != r.query_token)
          reject(t, Error::StaleEvent);
        else if (e.bytes >= r.candidate_count) reject(t, Error::InvalidInput);
        else startConnect(t, e.handle.slot, r.candidates[e.bytes].channel);
      }
      break;
    case Kind::Listen: {
      if (e.slot >= EndpointCount || !validChannel(e.channel)) {
        reject(t, Error::InvalidInput); break;
      }
      if (s.endpoints[e.slot].phase != EndpointPhase::Disabled &&
          s.endpoints[e.slot].phase != EndpointPhase::Failed) {
        reject(t, Error::Busy); break;
      }
      bool used = false;
      for (size_t i = 0; i < EndpointCount; ++i)
        used |= i != e.slot && s.endpoints[i].channel == e.channel &&
                (s.endpoints[i].phase == EndpointPhase::Listening ||
                 s.endpoints[i].phase == EndpointPhase::Registering ||
                 s.endpoints[i].phase == EndpointPhase::Unregistering);
      if (used) { reject(t, Error::DuplicateChannel); break; }
      const auto id = token(t); if (!id) break;
      auto &p = n.endpoints[e.slot];
      p.uuid = e.uuid; p.channel = e.channel; p.token = id;
      p.status = 0; p.phase = EndpointPhase::Registering;
      t.add(Op::RegisterService, {0, e.slot}, id);
      break;
    }
    case Kind::ListenResult:
      if (e.slot >= EndpointCount ||
          s.endpoints[e.slot].phase != EndpointPhase::Registering ||
          e.token != s.endpoints[e.slot].token) reject(t, Error::StaleEvent);
      else {
        auto &p = n.endpoints[e.slot]; p.status = e.status;
        p.phase = e.status ? EndpointPhase::Failed : EndpointPhase::Listening;
      }
      break;
    case Kind::Unlisten:
      if (e.slot >= EndpointCount) reject(t, Error::InvalidInput);
      else if (e.token != s.endpoints[e.slot].token) reject(t, Error::StaleEvent);
      else if (s.endpoints[e.slot].phase != EndpointPhase::Listening)
        reject(t, Error::NotReady);
      else {
        const auto id = token(t); if (!id) break;
        auto &p = n.endpoints[e.slot];
        p.token = id; p.phase = EndpointPhase::Unregistering;
        t.add(Op::UnregisterService, {0, e.slot}, id);
      }
      break;
    case Kind::UnlistenResult:
      if (e.slot >= EndpointCount ||
          s.endpoints[e.slot].phase != EndpointPhase::Unregistering ||
          e.token != s.endpoints[e.slot].token) reject(t, Error::StaleEvent);
      else {
        auto &p = n.endpoints[e.slot]; p.status = e.status;
        p.phase = e.status ? EndpointPhase::Failed : EndpointPhase::Disabled;
      }
      break;
    case Kind::Incoming: {
      bool listening = false, cid_used = false;
      uint8_t slot = NoRoute;
      for (const auto &p : s.endpoints)
        listening |= p.phase == EndpointPhase::Listening && p.channel == e.channel;
      for (uint8_t i = 0; i < RouteCount; ++i) {
        if (s.routes[i].phase == Phase::Closed && slot == NoRoute) slot = i;
        cid_used |= s.routes[i].cid == e.cid && s.routes[i].phase != Phase::Closed;
      }
      // A repeated notification for an owned cid must never decline that
      // already-accepted DLC. Invalid/stale identities have no SDK action.
      if (!e.cid || cid_used || !e.token) {
        reject(t, Error::StaleEvent); break;
      }
      if (e.peer != s.config.peer) {
        // Foreign ACLs are not tracked as this core's permitted peer. The
        // executor must validate this incoming ACL token/cid before decline.
        reject(t, Error::InvalidInput); t.add(Op::Reject, {}, e.token, e.cid); break;
      }
      if (!s.acl_active || e.token != s.acl_token) {
        reject(t, Error::StaleEvent); break;
      }
      if (!listening || slot == NoRoute || duplicate(s, NoRoute, e.peer, e.channel)) {
        reject(t, Error::InvalidInput); t.add(Op::Reject, {}, e.token, e.cid); break;
      }
      if (!freshRoute(t, slot, e.peer)) break;
      auto &r = n.routes[slot];
      r.phase = Phase::Connecting; r.channel = e.channel; r.cid = e.cid;
      t.add(Op::Accept, handleOf(n, slot), 0, e.cid);
      break;
    }
    case Kind::AclOpened:
      // Shared runtime supplies a strictly increasing link epoch, retaining
      // it on every callback from that ACL, including its final disconnect.
      if (e.peer != s.config.peer || !e.token || e.token <= s.acl_token ||
          s.acl_active) { reject(t, Error::StaleEvent); break; }
      n.acl_token = e.token; n.acl_cid = e.cid; n.acl_active = true;
      break;
    case Kind::AclClosed:
      if (e.peer != s.config.peer || !s.acl_active || e.token != s.acl_token ||
          e.cid != s.acl_cid) { reject(t, Error::StaleEvent); break; }
      n.acl_active = false;
      for (auto &r : n.routes) {
        if (r.phase == Phase::Closed || r.peer != e.peer) continue;
        discard(r); r.phase = Phase::Closed; r.cid = 0;
        r.error = Error::Failed; r.status = e.status;
      }
      if (n.query.phase == QueryPhase::Querying && n.query.peer == e.peer)
        cancelQuery(t, QueryPhase::Failed, Error::Failed);
      break;
    default: {
      if (!matches(s, e.handle)) { reject(t, Error::InvalidHandle); break; }
      const uint8_t slot = e.handle.slot;
      auto &r = n.routes[slot];
      const auto &old = s.routes[slot];
      const bool cid_match = e.cid == r.cid && r.cid != 0;
      switch (e.kind) {
      case Kind::Opened: {
        if ((old.phase != Phase::Connecting && old.phase != Phase::Closing) ||
            e.peer != r.peer || e.channel != r.channel ||
            (!e.status && (!s.acl_active || e.token != s.acl_token)) ||
            (r.cid && e.cid != r.cid)) { reject(t, Error::StaleEvent); break; }
        if (e.status) {
          discard(r); r.phase = Phase::Closed; r.cid = 0;
          r.error = Error::Failed; r.status = e.status; break;
        }
        bool collision = false;
        for (uint8_t i = 0; i < RouteCount; ++i)
          collision |= i != slot && n.routes[i].phase != Phase::Closed &&
                       n.routes[i].cid == e.cid;
        if (!e.cid || collision) {
          runtimeFail(t, Error::InvalidInput, 0); break;
        }
        r.cid = e.cid;
        if (old.phase == Phase::Closing) {
          t.add(Op::Disconnect, e.handle, 0, e.cid); break;
        }
        if (!e.mtu || e.mtu > MaxFrame) {
          routeFail(t, slot, Error::InvalidInput, 0); break;
        }
        r.mtu = e.mtu; r.phase = Phase::Open; grant(t, slot); break;
      }
      case Kind::Disconnect:
        if (r.phase == Phase::Closed || r.phase == Phase::Closing) {
          reject(t, Error::NotConnected); break;
        }
        if (r.phase == Phase::Resolving) {
          cancelQuery(t, QueryPhase::Cancelled, Error::Cancelled); break;
        }
        if (r.phase == Phase::Selecting) {
          r.phase = Phase::Closed; r.error = Error::Cancelled; break;
        }
        discard(r); r.phase = Phase::Closing; r.error = Error::Cancelled;
        t.add(r.cid ? Op::Disconnect : Op::CancelConnect, e.handle, 0, r.cid);
        break;
      case Kind::Closed:
        if (old.phase != Phase::Connecting && old.phase != Phase::Open &&
            old.phase != Phase::Closing) { reject(t, Error::StaleEvent); break; }
        if (e.cid != old.cid) { reject(t, Error::StaleEvent); break; }
        discard(r); r.phase = Phase::Closed; r.cid = 0;
        // A normal close confirmation must not erase the SDK failure that
        // initiated teardown. A reported close failure still takes precedence.
        if (e.status || !r.status) r.status = e.status;
        break;
      case Kind::DisconnectFailed:
        if (r.phase != Phase::Closing || e.cid != r.cid) {
          reject(t, Error::StaleEvent); break;
        }
        // Do not recycle a slot when an SDK disconnect/cancel failed.
        r.error = Error::Failed; r.status = e.status; break;
      case Kind::ControlRequested:
        if (r.phase != Phase::Open) { reject(t, Error::NotConnected); break; }
        if (!validControl(e.control)) { reject(t, Error::InvalidInput); break; }
        if (r.control_pending) { reject(t, Error::Busy); break; }
        r.control_token = token(t); if (!r.control_token) break;
        r.control = e.control; r.control_status = 0; r.control_pending = true;
        t.add(Op::Control, e.handle, r.control_token, r.cid);
        break;
      case Kind::ControlResult:
        if (r.phase != Phase::Open || !cid_match || !r.control_pending ||
            e.token != r.control_token) { reject(t, Error::StaleEvent); break; }
        // This is SDK acceptance, not peer acknowledgement. A rejected control
        // operation must not destroy the independent full-duplex byte stream.
        r.control_pending = false; r.control_status = e.status;
        if (e.status) t.result = Error::Failed;
        break;
      case Kind::RemoteLineStatus:
      case Kind::RemoteModemStatus:
      case Kind::PortConfiguration:
        if ((r.phase != Phase::Open && r.phase != Phase::Connecting) ||
            !s.acl_active || e.token != s.acl_token || e.peer != r.peer ||
            !e.cid || (r.cid && !cid_match)) {
          reject(t, Error::StaleEvent); break;
        }
        // Initial MSC/RPN can arrive before CHANNEL_OPENED. The adapter binds
        // this identity to the current ACL; do not lose the initial settings.
        if (!r.cid) r.cid = e.cid;
        if (e.kind == Kind::RemoteLineStatus) {
          r.remote_line_status = e.control.value; ++r.line_status_events;
        } else if (e.kind == Kind::RemoteModemStatus) {
          r.remote_modem_status = e.control.value; ++r.modem_status_events;
        } else if (!validPort(e.control.port)) {
          reject(t, Error::InvalidInput);
        } else if (e.remote) {
          r.remote_port = e.control.port; ++r.remote_port_events;
        } else {
          r.local_port = e.control.port; ++r.local_port_events;
        }
        break;
      case Kind::WriteRequested:
        if (r.phase != Phase::Open) { reject(t, Error::NotConnected); break; }
        t.accepted = std::min<uint16_t>(e.bytes, r.txFree());
        r.tx_size += t.accepted; r.tx_accepted += t.accepted;
        if (!t.accepted && e.bytes) t.result = Error::WouldBlock;
        requestSend(t, slot); break;
      case Kind::ReadRequested:
        if (r.phase != Phase::Open) { reject(t, Error::NotConnected); break; }
        t.consumed = std::min<uint16_t>(e.bytes, r.rx_size);
        r.rx_size -= t.consumed; r.rx_read += t.consumed;
        r.rx_head = (r.rx_head + t.consumed) % RxCapacity;
        if (!t.consumed && e.bytes) t.result = Error::WouldBlock;
        grant(t, slot); break;
      case Kind::CanSend:
      case Kind::CanSendFailed:
        if (r.phase != Phase::Open || !cid_match || !r.can_send_pending ||
            e.token != r.send_token) { reject(t, Error::StaleEvent); break; }
        r.can_send_pending = false;
        if (e.kind == Kind::CanSendFailed) routeFail(t, slot, Error::Failed, e.status);
        else {
          r.sending = std::min(r.tx_size, r.mtu);
          t.add(Op::Send, e.handle, r.send_token, r.cid, r.sending);
        }
        break;
      case Kind::SendResult:
      case Kind::SendDeferred:
        if (r.phase != Phase::Open || !cid_match || !r.sending ||
            e.token != r.send_token) { reject(t, Error::StaleEvent); break; }
        if (e.kind == Kind::SendDeferred) {
          // CanSend is a readiness hint, not a reserved transport buffer.
          // Nothing was accepted by the adapter: retain the owned FIFO bytes
          // and invalidate this attempt before requesting another opportunity.
          r.sending = 0; t.result = Error::WouldBlock;
          requestSend(t, slot); break;
        }
        if (e.status) { routeFail(t, slot, Error::Failed, e.status); break; }
        r.tx_size -= r.sending; r.tx_sent += r.sending;
        r.tx_head = (r.tx_head + r.sending) % TxCapacity; r.sending = 0;
        requestSend(t, slot); break;
      case Kind::CreditResult:
        if (r.phase != Phase::Open || !cid_match || !r.credit_pending ||
            e.token != r.credit_token) { reject(t, Error::StaleEvent); break; }
        r.credit_pending = false;
        if (e.status) routeFail(t, slot, Error::Failed, e.status);
        else grant(t, slot);
        break;
      case Kind::RxBytes:
        if (r.phase != Phase::Open || !cid_match) {
          reject(t, Error::StaleEvent); break;
        }
        if (!r.credits || e.bytes > r.mtu || e.bytes > RxCapacity - r.rx_size) {
          routeFail(t, slot, Error::RxOverflow, 0); break;
        }
        --r.credits; r.rx_size += e.bytes; r.rx_received += e.bytes;
        // Tiny frames may release whole-frame reservation space.
        grant(t, slot); break;
      default: reject(t, Error::InvalidInput); break;
      }
      break;
    }
    }
    return finish(s, t);
  }
};

struct PayloadStorage {
  std::array<uint8_t, TxCapacity> tx{};
  std::array<uint8_t, RxCapacity> rx{};
};
struct RouteView {
  uint32_t generation = 0;
  Phase phase = Phase::Closed;
  Error error = Error::None;
  uint16_t cid = 0, tx = 0, rx = 0, writable = 0;
};
struct View {
  std::array<RouteView, RouteCount> routes{};
  Runtime runtime = Runtime::Stopped;
  Error error = Error::None;
  QueryPhase query = QueryPhase::Idle;
};
inline View view(const State &s) {
  View v; v.runtime = s.runtime; v.error = s.error; v.query = s.query.phase;
  for (size_t i = 0; i < RouteCount; ++i) {
    const auto &r = s.routes[i];
    v.routes[i] = {r.generation, r.phase, r.error, r.cid, r.tx_size, r.rx_size, r.txFree()};
  }
  return v;
}
struct HistoryEntry {
  uint64_t sequence = 0;
  Event event{};
  View before{}, after{};
  std::array<Action, MaxActions> actions{};
  Error result = Error::None;
  uint8_t count = 0, writable_mask = 0;
};
struct IoResult { size_t bytes = 0; Error error = Error::None; };
class ActionExecutor {
public:
  virtual ~ActionExecutor() = default;
  // Send payload is borrowed only for this call. Copy it before returning if
  // an adapter needs asynchronous ownership. Other actions have nullptr/0.
  virtual void execute(const Action &, const State &, const uint8_t *, size_t) = 0;
};
// Queue payload copies are shared-runtime cost, not per-route cost. RX credits
// reserve capacity before callbacks arrive, including frames still in this queue.
struct QueuedEvent { Event event{}; std::array<uint8_t, MaxFrame> payload{}; };
template <size_t QueueSize = 16, size_t HistorySize = 16> class Engine {
  static_assert(QueueSize >= RouteCount * RxCreditWindow + ControlQueueReserve,
                "reserve slots for every credited RX frame and control events");
  static_assert(HistorySize > 0, "positive history bound required");
  State state_{};
  std::array<PayloadStorage, RouteCount> payload_{};
  std::array<QueuedEvent, QueueSize> queue_{};
  std::array<HistoryEntry, HistorySize> history_{};
  std::array<uint8_t, MaxFrame> send_buffer_{};
  ActionExecutor &executor_;
  size_t head_ = 0, size_ = 0, history_head_ = 0, history_size_ = 0;
  uint64_t dropped_ = 0, overwritten_ = 0, sequence_ = 0;
  bool draining_ = false, overflow_ = false;

  void commit(const Event &e, Transition &t, const State &before) {
    state_ = t.next; // Synchronous callback observes committed state.
    // Idle clock ticks must not evict the diagnostic transitions.
    if (e.kind != Kind::Tick || t.count || t.result != Error::None ||
        before.runtime != state_.runtime || before.query.phase != state_.query.phase) {
      auto &h = history_[history_head_];
      h = {++sequence_, e, view(before), view(state_), t.actions, t.result,
           t.count, t.writable_mask};
      history_head_ = (history_head_ + 1) % HistorySize;
      if (history_size_ < HistorySize) ++history_size_; else ++overwritten_;
    }
    for (size_t i = 0; i < t.count; ++i) {
      const auto &a = t.actions[i];
      size_t len = 0;
      if (a.op == Op::Send) {
        const auto &r = state_.routes[a.handle.slot];
        len = a.bytes;
        for (size_t j = 0; j < len; ++j)
          send_buffer_[j] = payload_[a.handle.slot].tx[(r.tx_head + j) % TxCapacity];
      }
      executor_.execute(a, state_, len ? send_buffer_.data() : nullptr, len);
      if (overflow_) break;
    }
  }
  void failOverflow() {
    size_ = 0; overflow_ = false;
    const State before = state_;
    Event e; e.kind = Kind::QueueOverflow;
    auto t = Rules::apply(state_, e); commit(e, t, before);
    // Teardown callback noise cannot resume a failed engine.
    size_ = 0; overflow_ = false;
  }
  bool push(const Event &e, const uint8_t *bytes) {
    if (state_.runtime == Runtime::Failed) return false;
    if (size_ == QueueSize) { ++dropped_; overflow_ = true; return false; }
    auto &q = queue_[(head_ + size_) % QueueSize];
    q.event = e;
    if (e.kind == Kind::RxBytes && e.bytes && bytes) std::memcpy(q.payload.data(), bytes, e.bytes);
    ++size_; return true;
  }
public:
  explicit Engine(Config config, ActionExecutor &executor) : executor_(executor) {
    state_.config = config;
  }
  State snapshot() const { return state_; }
  size_t pending() const { return size_; }
  uint64_t droppedEvents() const { return dropped_; }
  uint64_t overwrittenHistory() const { return overwritten_; }
  size_t historySize() const { return history_size_; }
  HistoryEntry history(size_t oldest_index) const {
    return oldest_index < history_size_
      ? history_[(history_head_ + HistorySize - history_size_ + oldest_index) % HistorySize]
      : HistoryEntry{};
  }
  bool post(Event e) {
    // Ownership-sensitive byte operations must go through their copy API.
    if (e.kind == Kind::RxBytes || e.kind == Kind::WriteRequested ||
        e.kind == Kind::ReadRequested) return false;
    return push(e, nullptr);
  }
  bool receive(Event e, const uint8_t *bytes, size_t length) {
    if (length > MaxFrame || (length && !bytes)) {
      ++dropped_; overflow_ = true; return false;
    }
    e.kind = Kind::RxBytes; e.bytes = static_cast<uint16_t>(length);
    return push(e, bytes);
  }
  size_t poll(size_t budget = QueueSize) {
    if (draining_) return 0;
    draining_ = true;
    size_t done = 0;
    if (overflow_) failOverflow();
    while (size_ && done < budget) {
      const auto q = queue_[head_];
      head_ = (head_ + 1) % QueueSize; --size_;
      const State before = state_;
      auto t = Rules::apply(state_, q.event);
      if (q.event.kind == Kind::RxBytes && t.result == Error::None &&
          matches(before, q.event.handle) &&
          t.next.routes[q.event.handle.slot].phase == Phase::Open) {
        const auto slot = q.event.handle.slot;
        const auto &r = before.routes[slot];
        for (size_t j = 0; j < q.event.bytes; ++j)
          payload_[slot].rx[(r.rx_head + r.rx_size + j) % RxCapacity] = q.payload[j];
      }
      commit(q.event, t, before); ++done;
      if (overflow_) failOverflow();
      if (state_.runtime == Runtime::Failed) { size_ = 0; break; }
    }
    draining_ = false; return done;
  }
  // This is a copying partial-accept API. No caller pointer is retained.
  IoResult tryWrite(Handle h, const uint8_t *bytes, size_t length) {
    if (draining_ || size_ || overflow_) return {0, Error::Busy};
    if (length && !bytes) return {0, Error::InvalidInput};
    Event e; e.kind = Kind::WriteRequested; e.handle = h;
    e.bytes = static_cast<uint16_t>(std::min<size_t>(length, UINT16_MAX));
    const State before = state_;
    auto t = Rules::apply(state_, e);
    if (t.accepted && h.slot < RouteCount) {
      const auto &r = before.routes[h.slot];
      for (size_t j = 0; j < t.accepted; ++j)
        payload_[h.slot].tx[(r.tx_head + r.tx_size + j) % TxCapacity] = bytes[j];
    }
    draining_ = true; commit(e, t, before);
    if (overflow_) failOverflow();
    draining_ = false;
    return {t.accepted, t.result};
  }
  IoResult read(Handle h, uint8_t *bytes, size_t capacity) {
    if (draining_ || size_ || overflow_) return {0, Error::Busy};
    if (capacity && !bytes) return {0, Error::InvalidInput};
    Event e; e.kind = Kind::ReadRequested; e.handle = h;
    e.bytes = static_cast<uint16_t>(std::min<size_t>(capacity, UINT16_MAX));
    const State before = state_;
    auto t = Rules::apply(state_, e);
    if (t.consumed && h.slot < RouteCount) {
      const auto &r = before.routes[h.slot];
      for (size_t j = 0; j < t.consumed; ++j)
        bytes[j] = payload_[h.slot].rx[(r.rx_head + j) % RxCapacity];
    }
    draining_ = true; commit(e, t, before);
    if (overflow_) failOverflow();
    draining_ = false;
    return {t.consumed, t.result};
  }
};
} // namespace spp
