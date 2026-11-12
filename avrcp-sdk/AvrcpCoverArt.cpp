#include "AvrcpCoverArt.h"
#include "AvrcpShared.h"
#include "classic/goep_client.h"
#include "classic/sdp_client.h"
#include <algorithm>
#include <cstring>
#if !defined(ENABLE_AVRCP_COVER_ART) || !defined(ENABLE_GOEP_L2CAP) ||         \
    !defined(ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE)
#error Cover Art requires ENABLE_AVRCP_COVER_ART, ENABLE_GOEP_L2CAP and ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
#endif
extern "C" uint8_t picow_avrcp_cover_art_client_cancel(uint16_t cid);
namespace picow::avrcp {
namespace {
using Cover = AvrcpCoverArt;
using B = Cover::Binding;
struct Slot {
  B binding{};
  avrcp_cover_art_client_t client{};
  alignas(16) std::array<uint8_t, 10000> ertm{};
  uint32_t usable = 0;
  std::array<char, 8> handle{};
  std::array<char, 257> descriptor{};
  bool leased = false, connected = false, closing = false, busy = false,
       cancel_queued = false;
  Cover::Operation operation = Cover::Operation::Image;
  uint32_t request = 0, bytes = 0;
  int local_error = 0;
  uint32_t timeout_ms = 0, started_ms = 0;
  bool timer_armed = false;
};
std::array<Slot, 2> slots;
Cover *owner = nullptr;
bool claimed = false;
uint32_t generation = 0, request = 0;
l2cap_ertm_config_t ertm_config = {1, 2, 2000, 12000, 512, 4, 4, 1};
Slot *cid(uint16_t id) {
  for (auto &s : slots)
    if (id && s.leased && s.binding.cid == id)
      return &s;
  return nullptr;
}
Slot *peer(const Cover::Address &a) {
  for (auto &s : slots)
    if (s.leased && s.binding.peer == a)
      return &s;
  return nullptr;
}
bool live(const B &b) {
  auto l = BluetoothRuntime::instance().linkForPeer(b.peer);
  if (!l.active || l.epoch != b.epoch)
    return false;
  if (b.avrcp_cid) {
    auto *c = avrcp_get_connection_for_avrcp_cid_for_role(AVRCP_CONTROLLER,
                                                          b.avrcp_cid);
    if (!c || memcmp(c->remote_addr, b.peer.data(), b.peer.size()))
      return false;
  }
  return true;
}
Slot *bound(const B &b) {
  auto *s = cid(b.cid);
  return s && s->binding.peer == b.peer && s->binding.epoch == b.epoch &&
                 s->binding.generation == b.generation &&
                 s->binding.avrcp_cid == b.avrcp_cid && live(b)
             ? s
             : nullptr;
}
void clear(Slot &s) {
  s.binding = {};
  s.leased = s.connected = s.closing = s.busy = s.cancel_queued = false;
  s.request = s.bytes = 0;
  s.local_error = 0;
  s.timeout_ms = s.started_ms = 0;
  s.timer_armed = false;
  // Do not zero SDK/client/ERTM here: release callback may be inside SDK
  // cleanup.
}
uint32_t safeSize(Slot &s) {
  uint32_t fixed = 16 - (reinterpret_cast<uintptr_t>(s.ertm.data()) & 15) +
                   4 * sizeof(l2cap_ertm_rx_packet_state_t) +
                   4 * sizeof(l2cap_ertm_tx_packet_state_t) + 512;
  if (HCI_ACL_PAYLOAD_SIZE < 56 || s.ertm.size() < fixed + 8 * 48)
    return 0;
  uint32_t mps =
      std::min<uint32_t>((s.ertm.size() - fixed) / 8, HCI_ACL_PAYLOAD_SIZE - 8);
  return fixed + 8 * mps;
}
bool validHandle(const char *p) {
  if (!p)
    return false;
  for (unsigned i = 0; i < 7; ++i)
    if (p[i] < '0' || p[i] > '9')
      return false;
  return p[7] == 0;
}
size_t descriptorLength(const char *p) {
  if (!p)
    return 0;
  size_t n = 0;
  while (n < 257 && p[n])
    ++n;
  return n;
}
} // namespace
bool AvrcpCoverArt::allowed(const Address &a) const {
  for (unsigned i = 0; i < config_.peer_count; ++i)
    if (config_.peers[i] == a)
      return true;
  return false;
}
bool AvrcpCoverArt::begin(const Config &c, uint32_t max_bytes) {
  if (claimed || !max_bytes || c.peer_count < 1 || c.peer_count > 2 ||
      !AvrcpShared::instance().initialized() ||
      BluetoothRuntime::instance().failed())
    return false;
  for (unsigned i = 0; i < c.peer_count; ++i)
    if (c.peers[i] == Address{} || (i && c.peers[0] == c.peers[i]))
      return false;
  for (auto &s : slots) {
    s.usable = safeSize(s);
    if (!s.usable)
      return false;
  }
  claimed = true;
  owner = this;
  config_ = c;
  limit_ = max_bytes;
  goep_client_init();
  avrcp_cover_art_client_init();
  status_.started = true;
  return true;
}
void AvrcpCoverArt::setCallback(void (*cb)(const Event &, void *), void *c) {
  if (!dispatching_) {
    callback_ = cb;
    context_ = c;
  }
}
void AvrcpCoverArt::setDataHandler(bool (*cb)(const Data &, void *), void *c) {
  if (!dispatching_) {
    sink_ = cb;
    sink_context_ = c;
  }
}
void AvrcpCoverArt::emit(const Event &e) {
  if (callback_) {
    dispatching_ = true;
    callback_(e, context_);
    dispatching_ = false;
  }
}
int AvrcpCoverArt::connect(const Address &a) {
  if (!status_.started || status_.stopped || dispatching_ || !allowed(a) ||
      peer(a))
    return ERROR_CODE_COMMAND_DISALLOWED;
  auto link = BluetoothRuntime::instance().linkForPeer(a);
  if (!link.active)
    return ERROR_CODE_COMMAND_DISALLOWED;
  if (BluetoothRuntime::instance().sdpOwned() || !sdp_client_ready())
    return SDP_QUERY_BUSY;
  Slot *s = nullptr;
  for (auto &candidate : slots)
    if (!candidate.leased) {
      s = &candidate;
      break;
    }
  if (!s)
    return BTSTACK_MEMORY_ALLOC_FAILED;
  s->timeout_ms = connection_timeout_ms_;
  s->timer_armed = false;
  s->leased = true;
  s->binding = {a, link.epoch, ++generation, 0, 0};
  auto addr = a;
  int rc = avrcp_cover_art_client_connect(&s->client, packet, addr.data(),
                                          s->ertm.data(), s->usable,
                                          &ertm_config, &s->binding.cid);
  if (s->leased && s->client.avrcp_cid)
    s->binding.avrcp_cid = s->client.avrcp_cid;
  if (rc && s->leased) {
    if (!s->client.cover_art_cid)
      clear(*s);
    else {
      s->closing = true;
      s->cancel_queued = true;
    }
  }
  return status_.last_error = rc;
}
AvrcpCoverArt::Binding AvrcpCoverArt::bindingForPeer(const Address &a) const {
  auto *s = peer(a);
  return s && s->connected && !s->closing && !s->cancel_queued &&
                 live(s->binding)
             ? s->binding
             : B{};
}
int AvrcpCoverArt::get(const B &b, Operation op, const char *handle,
                       const char *descriptor) {
  auto *s = bound(b);
  if (!status_.started || status_.stopped || dispatching_ || !s ||
      !s->connected || s->closing || s->cancel_queued || s->busy || !sink_)
    return ERROR_CODE_COMMAND_DISALLOWED;
  size_t len = descriptorLength(descriptor);
  if (!validHandle(handle) || len > 256)
    return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
  memcpy(s->handle.data(), handle, 8);
  if (len)
    memcpy(s->descriptor.data(), descriptor, len);
  s->descriptor[len] = 0;
  s->timeout_ms = object_timeout_ms_;
  s->timer_armed = false;
  s->busy = true;
  s->operation = op;
  s->request = ++request;
  s->bytes = 0;
  s->local_error = 0;
  int rc =
      op == Operation::Thumbnail
          ? avrcp_cover_art_client_get_linked_thumbnail(b.cid, s->handle.data())
      : op == Operation::Properties
          ? avrcp_cover_art_client_get_image_properties(b.cid, s->handle.data())
          : avrcp_cover_art_client_get_image(b.cid, s->handle.data(),
                                             descriptor ? s->descriptor.data()
                                                        : nullptr);
  if (rc)
    s->busy = false;
  return status_.last_error = rc;
}
int AvrcpCoverArt::thumbnail(const B &b, const char *h) {
  return get(b, Operation::Thumbnail, h, nullptr);
}
int AvrcpCoverArt::image(const B &b, const char *h, const char *d) {
  return get(b, Operation::Image, h, d);
}
int AvrcpCoverArt::properties(const B &b, const char *h) {
  return get(b, Operation::Properties, h, nullptr);
}
int AvrcpCoverArt::disconnect(const B &b) {
  auto *s = bound(b);
  if (!status_.started || dispatching_ || !s || !s->connected || s->closing ||
      s->cancel_queued || s->busy)
    return ERROR_CODE_COMMAND_DISALLOWED;
  s->closing = true;
  int rc = avrcp_cover_art_client_disconnect(b.cid);
  if (rc && s->leased)
    s->closing = false;
  return status_.last_error = rc;
}
int AvrcpCoverArt::cancel(const B &b) {
  if (!bound(b))
    return ERROR_CODE_COMMAND_DISALLOWED;
  return cancelPeer(b.peer);
}
int AvrcpCoverArt::cancelPeer(const Address &a) {
  auto *s = peer(a);
  if (!status_.started || status_.stopped || dispatching_ || !s || s->closing)
    return ERROR_CODE_COMMAND_DISALLOWED;
  const int previous_error = s->local_error;
  const bool previous_cancel_queued = s->cancel_queued;
  if (!s->local_error)
    s->local_error = Cancelled;
  s->closing = true;
  s->cancel_queued = false;
  int rc = picow_avrcp_cover_art_client_cancel(s->binding.cid);
  if (rc && s->leased) {
    s->closing = false;
    s->local_error = previous_error;
    s->cancel_queued = previous_cancel_queued;
  }
  return status_.last_error = rc;
}
bool AvrcpCoverArt::setTimeouts(uint32_t connection_ms, uint32_t object_ms) {
  if (dispatching_ || status_.stopped || connection_ms > 0x7fffffffu ||
      object_ms > 0x7fffffffu)
    return false;
  connection_timeout_ms_ = connection_ms;
  object_timeout_ms_ = object_ms;
  return true;
}
void AvrcpCoverArt::poll() { pollImpl(false, 0); }
void AvrcpCoverArt::poll(uint32_t now_ms) { pollImpl(true, now_ms); }
void AvrcpCoverArt::pollImpl(bool timed, uint32_t now_ms) {
  if (!status_.started || status_.stopped || dispatching_)
    return;
  BluetoothRuntime::instance().poll();
  for (auto &s : slots) {
    if (timed && s.leased && !s.closing && !s.cancel_queued &&
        (!s.connected || s.busy) && s.timeout_ms && live(s.binding)) {
      if (!s.timer_armed) {
        s.started_ms = now_ms;
        s.timer_armed = true;
      } else if (uint32_t(now_ms - s.started_ms) >= s.timeout_ms) {
        s.local_error = Timeout;
        s.cancel_queued = true;
        status_.last_error = Timeout;
        // Connections have no Complete event. Notify once before closing;
        // GET completion still comes from SDK completion/release callbacks.
        if (!s.connected)
          emit({Kind::Error, s.binding, Timeout});
      }
    }
    if (s.leased && (s.cancel_queued || (!live(s.binding) && !s.closing))) {
      s.cancel_queued = false;
      s.closing = true;
      int rc = picow_avrcp_cover_art_client_cancel(s.binding.cid);
      if (rc && s.leased) {
        s.closing = false;
        s.cancel_queued = true;
        status_.last_error = rc;
      }
    }
  }
}
void AvrcpCoverArt::stop() {
  if (dispatching_ || owner != this)
    return;
  status_.started = false;
  status_.stopped = true;
  owner = nullptr;
  for (auto &s : slots)
    if (s.leased) {
      s.closing = true;
      s.cancel_queued = false;
      int rc = picow_avrcp_cover_art_client_cancel(s.binding.cid);
      if (rc)
        status_.last_error = rc;
    }
}
void AvrcpCoverArt::packet(uint8_t t, uint16_t channel, uint8_t *p,
                           uint16_t n) {
  if (t == BIP_DATA_PACKET) {
    auto *s = cid(channel);
    if (!s || !owner || !live(s->binding) || s->closing || !s->busy ||
        s->cancel_queued || (!p && n))
      return;
    if (n > owner->limit_ - s->bytes) {
      s->local_error = SizeLimit;
      s->cancel_queued = true;
      return;
    }
    Data data{s->binding, s->request, s->bytes, s->operation, p, n};
    owner->dispatching_ = true;
    bool accepted = owner->sink_ && owner->sink_(data, owner->sink_context_);
    owner->dispatching_ = false;
    if (!accepted) {
      s->local_error = SinkRejected;
      s->cancel_queued = true;
      return;
    }
    s->bytes += n;
    return;
  }
  if (t != HCI_EVENT_PACKET || !p || n < 3 || unsigned(p[1]) + 2 != n ||
      p[0] != HCI_EVENT_AVRCP_META) {
    if (owner)
      ++owner->status_.malformed;
    return;
  }
  uint8_t kind = p[2];
  if (kind == AVRCP_SUBEVENT_COVER_ART_CONNECTION_ESTABLISHED) {
    if (n != 14) {
      if (owner)
        ++owner->status_.malformed;
      return;
    }
    auto *s = cid(little_endian_read_16(p, 12));
    if (!s) {
      if (owner)
        ++owner->status_.ignored;
      return;
    }
    Address addr{};
    reverse_bd_addr(p + 4, addr.data());
    if (addr != s->binding.peer) {
      s->closing = true;
      s->cancel_queued = true;
      return;
    }
    const uint16_t control = little_endian_read_16(p, 10);
    if (!p[3] &&
        (!control || (s->client.avrcp_cid && s->client.avrcp_cid != control))) {
      s->closing = true;
      s->cancel_queued = true;
      return;
    }
    s->binding.avrcp_cid = control;
    auto b = s->binding;
    if (p[3]) {
      const bool timed_out = s->local_error == Timeout;
      bool retained = s->client.cover_art_cid != 0;
      if (!retained)
        clear(*s);
      else {
        s->closing = true;
        s->cancel_queued = true;
      }
      if (owner && !timed_out)
        owner->emit({Kind::Error, b, p[3]});
      return;
    }
    s->connected = true;
    s->timer_armed = false;
    if (!owner || !live(b) || s->closing || s->cancel_queued) {
      s->closing = true;
      s->cancel_queued = true;
      if (!owner)
        picow_avrcp_cover_art_client_cancel(b.cid);
      return;
    }
    owner->emit({Kind::Connected, b});
    return;
  }
  if (kind == AVRCP_SUBEVENT_COVER_ART_OPERATION_COMPLETE) {
    if (n != 6) {
      if (owner)
        ++owner->status_.malformed;
      return;
    }
    auto *s = cid(little_endian_read_16(p, 3));
    if (!s || !s->busy)
      return;
    s->busy = false;
    s->timer_armed = false;
    if (s->client.state == AVRCP_COVER_ART_W4_ABORT_COMPLETE) {
      s->closing = true;
      s->cancel_queued = true;
    }
    int status = s->local_error ? s->local_error : int(p[5]);
    if (owner)
      owner->emit({Kind::Complete, s->binding, status, s->operation, s->request,
                   s->bytes});
    return;
  }
  if (kind == AVRCP_SUBEVENT_COVER_ART_CONNECTION_RELEASED) {
    if (n != 5) {
      if (owner)
        ++owner->status_.malformed;
      return;
    }
    auto *s = cid(little_endian_read_16(p, 3));
    if (!s)
      return;
    auto b = s->binding;
    auto op = s->operation;
    auto req = s->request, bytes = s->bytes;
    bool busy = s->busy;
    int error = s->local_error ? s->local_error : OBEX_DISCONNECTED;
    clear(*s);
    if (owner) {
      if (busy)
        owner->emit({Kind::Complete, b, error, op, req, bytes});
      owner->emit({Kind::Disconnected, b});
    }
    return;
  }
  if (owner)
    ++owner->status_.ignored;
}
} // namespace picow::avrcp
