#include "AvrcpBrowsing.h"
#include "AvrcpCoverArt.h"
#include "pico/stdlib.h"
#include "sample_config.h"
#include "OwnedRecordQueue.h"
#include "CoverArtFlow.h"
#include <cstdio>
#include <cstring>

namespace {
using namespace picow::avrcp;
constexpr Address peer{{AVRCP_SAMPLE_PEER_BYTES}};
// Every callback runs synchronously on this same polling thread. This queue is
// NOT an IRQ/core/thread-safe queue. Callbacks only copy, count, or return a
// bounded decision; they never call a profile mutator, print, or do file I/O.
struct Record {
  enum class Kind { Control, Sdk, Browse, Request, Art, Data } kind{};
  AvrcpController::Event control{};
  AvrcpBrowsing::Event browse{};
  AvrcpBrowsing::RequestToken token{};
  AvrcpCoverArt::Event art{};
  AvrcpCoverArt::Data data{};
  Binding sdk_binding{};
  uint16_t size = 0;
  std::array<uint8_t, 512> bytes{};
};
struct Application {
  AvrcpTarget target;
  AvrcpController control;
  AvrcpBrowsing browsing;
  AvrcpCoverArt cover;
  avrcp_sample::OwnedRecordQueue<Record, 8> queue;
  static constexpr size_t art_capacity = 16 * 1024;
  avrcp_sample::CoverArtFlow<art_capacity> art_flow;
  avrcp_sample::ArtCommandLine art_input;
  bool art_prompted = false;
  avrcp_sample::FlowState reported_art_state = avrcp_sample::FlowState::WaitingSignal;
  static avrcp_sample::ArtBinding artBinding(const AvrcpCoverArt::Binding &b) {
    return {b.peer, b.epoch, b.generation, b.cid, b.avrcp_cid};
  }
  static avrcp_sample::ArtOperation artOperation(AvrcpCoverArt::Operation op) {
    switch (op) {
    case AvrcpCoverArt::Operation::Thumbnail: return avrcp_sample::ArtOperation::Thumbnail;
    case AvrcpCoverArt::Operation::Image: return avrcp_sample::ArtOperation::Image;
    case AvrcpCoverArt::Operation::Properties: return avrcp_sample::ArtOperation::Properties;
    }
    return static_cast<avrcp_sample::ArtOperation>(255);
  }
  template<class B> static avrcp_sample::SessionBinding sessionBinding(const B &b) {
    return {b.peer, b.epoch, b.generation, b.cid};
  }
  avrcp_sample::ArtLive artLive() const {
    return {sessionBinding(control.bindingForPeer(peer)),
            sessionBinding(browsing.bindingForPeer(peer)),
            artBinding(cover.bindingForPeer(peer)), queue.size() == 0, queue.failed()};
  }
  // Sole Cover Art command owner. These adapters are invoked only by the
  // main-loop flow; callbacks only enqueue records. No independent GETs exist.
  int connect(const Address &address) {
    const int result = cover.connect(address); local("Cover Art connect", result); return result;
  }
  int get(const avrcp_sample::ArtBinding &binding, avrcp_sample::ArtOperation op,
          const char *real_handle) {
    const AvrcpCoverArt::Binding b{binding.peer, binding.epoch, binding.generation,
                                 binding.cid, binding.avrcp_cid};
    int result = -1;
    switch (op) {
    case avrcp_sample::ArtOperation::Thumbnail: result = cover.thumbnail(b, real_handle); break;
    case avrcp_sample::ArtOperation::Image: result = cover.image(b, real_handle); break;
    case avrcp_sample::ArtOperation::Properties: result = cover.properties(b, real_handle); break;
    }
    local("Cover Art GET", result); return result;
  }
  int cancel(const Address &address) {
    const int result = cover.cancelPeer(address); local("Cover Art cancel", result); return result;
  }
  void serviceArt(uint32_t now) {
    art_flow.tick(now, artLive(), *this);
    // A preceding query may synchronously enqueue events. Drain those first,
    // then validate the captured full signalling session before the SDP call.
    if (art_flow.takeBrowseAttempt(artLive())) {
      const int result = browsing.connect(peer);
      local("Browsing connect", result); art_flow.browseAdmission(result);
    }
    if (reported_art_state != art_flow.state()) {
      reported_art_state = art_flow.state();
      std::printf("Cover Art flow=%u failure=%u local=%d cancel=%d\n",
                  unsigned(art_flow.state()), unsigned(art_flow.failure()),
                  art_flow.localStatus(), art_flow.cancelStatus());
    }
    if (AVRCP_SAMPLE_ENABLE_ART_INPUT && !art_prompted &&
        art_flow.state() == avrcp_sample::FlowState::Ready) {
      art_prompted = true;
      std::printf("Cover Art ready. Enter art thumbnail|image|properties REAL_HANDLE, or cancel.\n"
                  "Use a seven-digit handle learned from this live peer's metadata.\n");
    }
  }
  void readArtInput() {
    if (!AVRCP_SAMPLE_ENABLE_ART_INPUT) return;
    // At most 16 nonblocking characters per loop. Not a terminal/readline UI.
    for (unsigned i = 0; i < 16; ++i) {
      const int c = getchar_timeout_us(0);
      if (c == PICO_ERROR_TIMEOUT) break;
      avrcp_sample::ArtCommand command;
      const auto result = art_input.feed(static_cast<char>(c), command,
          art_flow.state() == avrcp_sample::FlowState::Ready);
      if (result == avrcp_sample::ArtCommandLine::Result::None) continue;
      if (result == avrcp_sample::ArtCommandLine::Result::Cancel) art_flow.cancel();
      else if (result == avrcp_sample::ArtCommandLine::Result::Request) {
        const auto admitted = art_flow.submit(command, artLive());
        std::printf("Cover Art input result=%u (0=queued, not transfer success)\n", unsigned(admitted));
      } else std::printf("Cover Art input rejected: malformed or not ready at line start\n");
    }
  }
  bool overflow = false;
  bool stopping = false;
  bool connect_attempted = false;
  static constexpr bool use_deadlines = true;

  bool push(Record r, const uint8_t *bytes = nullptr, uint16_t n = 0) {
    const bool accepted = queue.push(r, bytes, n);
    overflow = queue.failed();
    return accepted;
  }

  static Application &self(void *p) { return *static_cast<Application *>(p); }
  static void onControl(const AvrcpController::Event &e, void *p) {
    Record r; r.kind = Record::Kind::Control; r.control = e; self(p).push(r);
  }
  static void onSdk(const AvrcpController::SdkEvent &e, void *p) {
    Record r; r.kind = Record::Kind::Sdk; r.sdk_binding = e.binding;
    self(p).push(r, e.bytes, e.size); // Never save e.bytes.
  }
  static void onBrowse(const AvrcpBrowsing::Event &e, void *p) {
    Record r; r.kind = Record::Kind::Browse; r.browse = e;
    r.browse.bytes = nullptr; // No borrowed pointer may survive the callback.
    self(p).push(r, e.bytes, e.size);
  }
  static void onRequest(const AvrcpBrowsing::Request &e, void *p) {
    Record r; r.kind = Record::Kind::Request; r.token = e.token;
    // No catalogue implemented: only the owned token is needed to reject.
    // If implementing a catalogue, copy e.bytes within the same bounded budget.
    self(p).push(r);
  }
  static void onArt(const AvrcpCoverArt::Event &e, void *p) {
    Record r; r.kind = Record::Kind::Art; r.art = e; self(p).push(r);
  }
  static bool onData(const AvrcpCoverArt::Data &d, void *p) {
    Record r; r.kind = Record::Kind::Data; r.data = d; r.data.bytes = nullptr;
    // false asks the adapter for deferred cancellation, not pause/backpressure.
    return self(p).push(r, d.bytes, d.size);
  }
  static void local(const char *operation, int result) {
    std::printf("%s local_status=%d (0 means admission only)\n", operation, result);
  }
  bool begin() {
    target.setPassThroughHandler([](const AvrcpTarget::PassThroughRequest &, void *) {
      return AvrcpTarget::PassThroughDecision::Rejected; // No fake player action.
    }, nullptr);
    target.setRequestHandler([](const AvrcpTarget::Request &, void *) {
      return AvrcpTarget::Response{0, 0}; // AVRCP INVALID_COMMAND, not success.
    }, nullptr);
    target.setPlayback(UINT32_MAX, UINT32_MAX, 0xff); // Unknown, no player.
    control.setCallback(onControl, this);
    control.setSdkCallback(onSdk, this);
    browsing.setCallback(onBrowse, this);
    browsing.setRequestCallback(onRequest, this);
    cover.setCallback(onArt, this);
    cover.setDataHandler(onData, this);
    AvrcpTarget::Config config;
    config.peer_count = 1; config.peers[0] = peer;
    // These owners are one-shot and share one signalling runtime/allowlist.
    // begin may power the radio; main never reaches here in the default build.
    if (!target.begin(config, true) || !control.begin(config, true) ||
        !browsing.begin(config, true) || !cover.begin(config, art_capacity)) return false;
    // No automatic pairing confirmation, discovery or link-key import here.
    if (use_deadlines && (!control.setCommandTimeout(15000) ||
        !browsing.setTimeouts(15000, 15000, 15000, 15000) ||
        !cover.setTimeouts(15000, 30000))) return false;
    return true;
  }
  void consumeOne() {
    if (stopping) return;
    Record r;
    if (!queue.pop(r)) return;
    switch (r.kind) {
    case Record::Kind::Control:
      std::printf("control event=%u response=%u\n", unsigned(r.control.kind), r.control.response);
      if (r.control.kind == AvrcpController::EventKind::Connected) {
        // Only the first copied event matching the current full live binding
        // starts Browsing. No rebind/retry after a disconnect or generation change.
        if (art_flow.signalConnected(sessionBinding(r.control.binding), artLive())) {
          local("PlayStatus", control.query(r.control.binding, AvrcpController::Query::PlayStatus));
        }
      } else if (r.control.kind == AvrcpController::EventKind::Disconnected ||
                 r.control.kind == AvrcpController::EventKind::Reset) art_flow.disconnected();
      break;
    case Record::Kind::Sdk:
      // A copied SDK packet is NOT itself a successful remote reply. Decode the
      // matching subevent, response type and fields with size checks in a product.
      std::printf("SDK event copied: %u bytes, cid=%u; not decoded\n", r.size, r.sdk_binding.cid);
      break;
    case Record::Kind::Request:
      // A disconnected/expired token may fail locally. Do not retry a stale token.
      local("Browsing reject INVALID_COMMAND", browsing.reject(r.token, 0x00));
      break;
    case Record::Kind::Browse:
      if (r.browse.kind == AvrcpBrowsing::Kind::Disconnected ||
          r.browse.kind == AvrcpBrowsing::Kind::Error) art_flow.disconnected();
      if (r.browse.kind == AvrcpBrowsing::Kind::Connected) {
        if (art_flow.browseConnected(sessionBinding(r.browse.binding), artLive(), r.browse.status))
          local("Player list", browsing.folderItems(r.browse.binding,
                AVRCP_BROWSING_MEDIA_PLAYER_LIST, 0, 0));
      } else if (r.browse.kind == AvrcpBrowsing::Kind::Response && r.size) {
        // Event.status is transport status; payload byte zero is AVRCP status.
        std::printf("Browsing remote_status=%u success=%u\n", r.bytes[0], r.bytes[0] == 0x04);
      } else {
        std::printf("Browsing event=%u local_status=%d\n", unsigned(r.browse.kind), r.browse.status);
      }
      break;
    case Record::Kind::Art:
      if (r.art.kind == AvrcpCoverArt::Kind::Connected) {
        art_flow.connected(artBinding(r.art.binding), r.art.status, artLive());
      } else if (r.art.kind == AvrcpCoverArt::Kind::Complete) {
        const bool accepted = art_flow.complete(artBinding(r.art.binding),
            artOperation(r.art.operation), r.art.request, r.art.status, r.art.bytes);
        std::printf("Cover Art object complete=%u bytes=%u failure=%u (not decoded)\n",
                    unsigned(accepted), unsigned(art_flow.object().size()), unsigned(art_flow.object().failure()));
      } else if (r.art.kind == AvrcpCoverArt::Kind::Error) {
        art_flow.error();
      } else if (r.art.kind == AvrcpCoverArt::Kind::Disconnected) {
        art_flow.disconnected();
      }
      std::printf("Cover Art event=%u status=%d request=%lu\n", unsigned(r.art.kind),
                  r.art.status, static_cast<unsigned long>(r.art.request));
      break;
    case Record::Kind::Data:
      if (r.size != r.data.size) art_flow.error();
      else if (!art_flow.data(artBinding(r.data.binding), artOperation(r.data.operation),
                             r.data.request, r.data.offset, r.bytes.data(), r.size))
        std::printf("Cover Art discarded failure=%u\n", unsigned(art_flow.object().failure()));
      break;
    }
  }
  void poll(uint32_t now) {
    target.poll(); // Drives the shared polling runtime.
    if (use_deadlines) { control.poll(now); browsing.poll(now); cover.poll(now); }
    else { control.poll(); browsing.poll(); cover.poll(); }
  }
  void stop() {
    stopping = true;
    if (overflow) art_flow.queueLoss();
    art_flow.stop();
    // At most one explicit flow cancellation, including pending connect. A
    // failed local cancel is recorded, never retried by the application. The
    // facade's terminal stop below still owns its documented deferred cleanup.
    art_flow.tick(to_ms_since_boot(get_absolute_time()), artLive(), *this);
    local("Pending signalling cancel", control.cancelConnection(peer));
    cover.stop(); browsing.stop(); control.stop(); target.stop();
    // stop() is terminal but deliberately does not power off shared A2DP/SPP.
    // This standalone sample owns no other profiles, so it can power off here.
    local("Standalone radio power off", BluetoothRuntime::instance().powerOff());
  }
};
int runRadioExample() {
  static Application app; // Never recreate owners after stop, even on failure.
  stdio_init_all();
  if (!app.begin()) { app.stop(); return 1; }
  const uint32_t start = to_ms_since_boot(get_absolute_time());
  for (;;) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    app.poll(now);
    if (app.overflow || uint32_t(now - start) >= 60000) break;
    if (!app.connect_attempted && hci_get_state() == HCI_STATE_WORKING) {
      app.connect_attempted = true; // No unbounded busy/failed connect retries.
      Application::local("Signalling connect", app.control.connect(peer));
    }
    app.serviceArt(now); // Invalidate lost live sessions before consuming old records.
    app.consumeOne();
    app.readArtInput();
    app.serviceArt(now); // GET only after the copied Connected and an empty queue.
    sleep_ms(1);
  }
  app.stop();
  return app.overflow ? 2 : 0;
}
} // namespace
int main() {
  // Ordinary if keeps the use-pattern type-checked in inert builds as well.
  if (!AVRCP_SAMPLE_ENABLE_RADIO) return 0;
  return runRadioExample();
}
