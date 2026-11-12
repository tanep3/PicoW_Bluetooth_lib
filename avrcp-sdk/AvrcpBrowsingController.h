#pragma once
#include "../avrcp-core/BrowsingResponse.h"
#include "../avrcp-core/BrowsingTarget.h"
#include "AvrcpController.h"
namespace picow::avrcp {
// One process-lifetime owner. Uses existing AVRCP signalling, never initializes
// or stops the radio. ERTM storage is static and survives owner destruction.
class AvrcpBrowsingController {
public:
  using Config = AvrcpController::Config;
  using Address = BluetoothRuntime::Address;
  struct Binding {
    Address peer{};
    uint32_t epoch = 0, generation = 0;
    uint16_t cid = 0;
  };
  enum class Kind {
    Connected,
    Disconnected,
    Response,
    Error,
    TargetResponseSent
  };
  struct Event {
    Kind kind;
    Binding binding{};
    int status = 0;
    uint8_t pdu = 0;
    const uint8_t *bytes = nullptr;
    uint16_t size = 0;
  };
  bool begin(const Config &, bool enable_target = false);
  ~AvrcpBrowsingController() { stop(); }
  AvrcpBrowsingController() = default;
  AvrcpBrowsingController(const AvrcpBrowsingController &) = delete;
  AvrcpBrowsingController &operator=(const AvrcpBrowsingController &) = delete;
  void setCallback(void (*)(const Event &, void *), void * = nullptr);
  int connect(const Address &);
  int disconnect(const Binding &);
  Binding bindingForPeer(const Address &) const;
  enum { Timeout = -1004 };
  // Budgets for new admissions; zero disables, maximum 0x7fffffff ms.
  // Target budget covers application response and subsequent sends together.
  // Assembly budget covers accepted incoming fragments until valid completion.
  bool setTimeouts(uint32_t connection_ms, uint32_t response_ms,
                   uint32_t target_ms, uint32_t assembly_ms = 0);
  void poll(); // Does not advance deadlines.
  // Monotonic modulo-2^32 milliseconds, intervals < 2^31 ms. First timed
  // poll after admission arms the absolute budget; progress does not renew it.
  void poll(uint32_t now_ms);
  void stop();
  int folderItems(const Binding &, avrcp_browsing_scope_t, uint32_t first,
                  uint32_t last, uint32_t attributes = 0);
  int setBrowsedPlayer(const Binding &, uint16_t);
  int totalItems(const Binding &, avrcp_browsing_scope_t);
  int changePath(const Binding &, bool down, const std::array<uint8_t, 8> &);
  int itemAttributes(const Binding &, const std::array<uint8_t, 8> &,
                     uint16_t counter, uint32_t attributes,
                     avrcp_browsing_scope_t);
  int search(const Binding &, const char *, uint16_t length);

  struct RequestToken {
    Binding binding{};
    uint32_t sequence = 0;
    uint8_t transaction = 0, pdu = 0;
  };
  struct Request {
    RequestToken token{};
    const uint8_t *bytes = nullptr;
    uint16_t size = 0;
  };
  void setRequestCallback(void (*)(const Request &, void *), void * = nullptr);
  // Copy token during request callback; respond later from the main loop.
  // Payload excludes the status byte. All bytes are copied before return.
  int respond(const RequestToken &, uint8_t status, const uint8_t *payload,
              uint16_t size);
  int reject(const RequestToken &, uint8_t status);
  int respondSetBrowsedPlayer(const RequestToken &, uint16_t counter,
                              uint32_t items, uint16_t charset, uint8_t depth,
                              const uint8_t *folders, uint16_t size);
  int respondFolderItems(const RequestToken &, uint16_t counter, uint16_t count,
                         const uint8_t *records, uint16_t size);
  int respondTotalItems(const RequestToken &, uint16_t counter, uint32_t count);
  int respondChangePath(const RequestToken &, uint32_t count);
  int respondItemAttributes(const RequestToken &, uint8_t count,
                            const uint8_t *records, uint16_t size);
  int respondSearch(const RequestToken &, uint16_t counter, uint32_t count);

private:
  void pollImpl(bool timed, uint32_t now_ms);
  uint32_t connection_timeout_ms_ = 0, response_timeout_ms_ = 0,
           target_timeout_ms_ = 0, assembly_timeout_ms_ = 0;
  int respondParts(const RequestToken &, uint8_t, const uint8_t *, uint16_t,
                   const uint8_t *, uint16_t);
  static void connection(uint8_t, uint16_t, uint8_t *, uint16_t);
  static void transport(uint8_t, uint16_t, uint8_t *, uint16_t);
  static void target(uint8_t, uint16_t, uint8_t *, uint16_t);
  static void ignore(uint8_t, uint16_t, uint8_t *, uint16_t) {}
  void emit(const Event &);
  bool allowed(const Address &) const;
  int ready(const Binding &, uint8_t);
  int result(const Binding &, int);
  bool target_enabled_ = false;
  void (*request_callback_)(const Request &, void *) = nullptr;
  void *request_context_ = nullptr;
  Config config_{};
  bool started_ = false, dispatching_ = false;
  void (*callback_)(const Event &, void *) = nullptr;
  void *context_ = nullptr;
};
} // namespace picow::avrcp
