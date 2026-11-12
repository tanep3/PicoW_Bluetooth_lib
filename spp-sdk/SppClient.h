#pragma once
#include "../spp-core/SppCore.h"
#include "../spp/SdpRecords.h"
#include "btstack.h"
#include "SppErtm.h"
namespace spp {
// Process-lifetime, single-loop facade. bool means queued, not completed.
// Observe final operation state/result via snapshot/history. Payload pointers
// are not retained; explicitly configured ERTM storage must outlive the client.
class Client : private ActionExecutor {
public:
  enum class Transport : uint8_t { Basic = 0, Ertm = 3, Unknown = 0xff };
  struct Security {
    bool allow_confirmation = false;
    bool authentication = true;
  };

private:
  Engine<> engine_;
  Security security_;
  ErtmPool ertm_;
  static Client *active_;
  struct Binding {
    Handle handle{};
    Address peer{};
    uint32_t send = 0, acl = 0;
    uint16_t cid = 0;
    bool opened = false;
  };
  struct Offer {
    Address peer{};
    uint32_t acl = 0;
    uint16_t cid = 0;
  };
  std::array<Binding, RouteCount> bindings_{};
  std::array<Offer, 4> offers_{};
  std::array<std::array<uint8_t, 256>, EndpointCount> records_{};
  std::array<uint32_t, EndpointCount> record_handles_{};
  SdpRecords parser_;
  uint32_t init_ = 0, query_ = 0;
  bool cancelled_ = false;
  static void packet(uint8_t, uint16_t, uint8_t *, uint16_t);
  static void ertmRequest(rfcomm_ertm_request_t *);
  static void ertmReleased(uint16_t);
  static void sdpPacket(uint8_t, uint16_t, uint8_t *, uint16_t);
  void execute(const Action &, const State &, const uint8_t *, size_t) override;
  void linkOpened(Address peer, uint16_t handle, uint32_t epoch);

public:
  explicit Client(Config c) : Client(c, Security{}) {}
  Client(Config c, Security security)
      : engine_(c, *this), security_(security) {}
  Client(const Client &) = delete;
  Client &operator=(const Client &) = delete;
  static constexpr bool ertmCompiled() {
#if defined(ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE_FOR_RFCOMM) && defined(ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE) && defined(PICOW_SPP_SDK_OVERLAY)
    return true;
#else
    return false;
#endif
  }
  bool configureErtm(const ErtmPool::Config &config) {
    return ertmCompiled() && !engine_.pending() &&
        engine_.snapshot().runtime == Runtime::Stopped && ertm_.configure(config);
  }
  ErtmPool::Status ertmStatus() const { return ertm_.status(); }
  // Actual currently-open transport, not a requested/configured mode.
  // Invalid, stale, closed or unobservable bindings report Unknown.
  Transport transport(Handle) const;
  bool begin();
  void poll();
  bool connect(uint8_t slot, Address peer, uint8_t channel) {
    Event e;
    e.kind = Kind::ConnectChannel;
    e.slot = slot;
    e.peer = peer;
    e.channel = channel;
    return engine_.post(e);
  }
  bool connect(uint8_t slot, Address peer, Uuid uuid) {
    Event e;
    e.kind = Kind::ConnectUuid;
    e.slot = slot;
    e.peer = peer;
    e.uuid = uuid;
    return engine_.post(e);
  }
  bool discover(Address peer, Uuid uuid) {
    Event e;
    e.kind = Kind::Discover;
    e.peer = peer;
    e.uuid = uuid;
    return engine_.post(e);
  }
  bool cancelDiscovery(uint32_t token) {
    Event e;
    e.kind = Kind::CancelDiscovery;
    e.token = token;
    return engine_.post(e);
  }
  bool select(Handle h, uint8_t index) {
    auto s = engine_.snapshot();
    if (!matches(s, h))
      return false;
    Event e;
    e.kind = Kind::SelectCandidate;
    e.handle = h;
    e.bytes = index;
    e.token = s.routes[h.slot].query_token;
    return engine_.post(e);
  }
  bool disconnect(Handle h) {
    Event e;
    e.kind = Kind::Disconnect;
    e.handle = h;
    return engine_.post(e);
  }
  bool listen(uint8_t slot, uint8_t channel, Uuid uuid) {
    Event e;
    e.kind = Kind::Listen;
    e.slot = slot;
    e.channel = channel;
    e.uuid = uuid;
    return engine_.post(e);
  }
  // Withdraws SDP + incoming registration. Existing routes remain connected.
  // Does not switch shared GAP connectability or power off another profile.
  bool unlisten(uint8_t slot) {
    const auto s = engine_.snapshot();
    if (slot >= EndpointCount || s.endpoints[slot].phase != EndpointPhase::Listening)
      return false;
    Event e; e.kind = Kind::Unlisten; e.slot = slot;
    e.token = s.endpoints[slot].token;
    return engine_.post(e);
  }
  // true means queued. Inspect route.control_pending/control_token/status and
  // history for local SDK acceptance. No peer-ack promise is made by BTstack.
  bool control(Handle handle, ControlRequest request) {
    if (!validControl(request)) return false;
    const auto s = engine_.snapshot();
    if (!matches(s, handle) || s.routes[handle.slot].phase != Phase::Open)
      return false;
    Event e; e.kind = Kind::ControlRequested; e.handle = handle; e.control = request;
    return engine_.post(e);
  }
  bool sendLineStatus(Handle h, uint8_t value) {
    ControlRequest c; c.operation = Control::LineStatus; c.value = value;
    return control(h, c);
  }
  bool sendModemStatus(Handle h, uint8_t value) {
    ControlRequest c; c.operation = Control::ModemStatus; c.value = value;
    return control(h, c);
  }
  bool configurePort(Handle h, PortSettings port) {
    ControlRequest c; c.operation = Control::ConfigurePort; c.port = port;
    return control(h, c);
  }
  bool queryPort(Handle h) { return control(h, ControlRequest{}); }
  // Select another allowed peer only after all routes close, SDP drains and
  // pending callbacks are processed. Existing other-profile ACLs are untouched.
  bool selectPeer(Address peer) {
    if (peer == Address{} || engine_.pending() || ertm_.status().active) return false;
    for (const auto &b : bindings_) if (b.cid) return false;
    Event e; e.kind = Kind::SelectPeer; e.peer = peer;
    return engine_.post(e);
  }
  State snapshot() const { return engine_.snapshot(); }
  IoResult tryWrite(Handle h, const uint8_t *p, size_t n) {
    return engine_.tryWrite(h, p, n);
  }
  IoResult read(Handle h, uint8_t *p, size_t n) {
    return engine_.read(h, p, n);
  }
  size_t historySize() const { return engine_.historySize(); }
  HistoryEntry history(size_t i) const { return engine_.history(i); }
  uint64_t droppedEvents() const { return engine_.droppedEvents(); }
};
} // namespace spp
