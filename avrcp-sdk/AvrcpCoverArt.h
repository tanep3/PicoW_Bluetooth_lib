#pragma once
#include "AvrcpController.h"
#include "classic/avrcp_cover_art_client.h"
namespace picow::avrcp {
// BTstack provides Cover Art Client, not a Cover Art image server.
// One owner, one main loop. All async client, ERTM and request storage persists
// until SDK release. Image bytes stream without allocating a whole image.
class AvrcpCoverArt {
public:
  using Config = AvrcpController::Config;
  using Address = BluetoothRuntime::Address;
  struct Binding {
    Address peer{};
    uint32_t epoch = 0, generation = 0;
    uint16_t cid = 0, avrcp_cid = 0;
  };
  enum class Operation : uint8_t { Thumbnail, Image, Properties };
  enum class Kind : uint8_t { Connected, Disconnected, Complete, Error };
  enum LocalError : int {
    SizeLimit = -1001,
    SinkRejected = -1002,
    Cancelled = -1003,
    Timeout = -1004
  };
  struct Data {
    Binding binding{};
    uint32_t request = 0, offset = 0;
    Operation operation = Operation::Image;
    const uint8_t *bytes = nullptr;
    uint16_t size = 0;
  };
  struct Event {
    Kind kind;
    Binding binding{};
    int status = 0;
    Operation operation = Operation::Image;
    uint32_t request = 0, bytes = 0;
  };
  struct Status {
    bool started = false, stopped = false;
    uint32_t malformed = 0, ignored = 0;
    int last_error = 0;
  };
  AvrcpCoverArt() = default;
  ~AvrcpCoverArt() { stop(); }
  AvrcpCoverArt(const AvrcpCoverArt &) = delete;
  AvrcpCoverArt &operator=(const AvrcpCoverArt &) = delete;
  bool begin(const Config &, uint32_t max_object_bytes = 16u * 1024u * 1024u);
  void setCallback(void (*)(const Event &, void *), void * = nullptr);
  // Borrowed bytes, synchronous. false requests deferred cancel; no pause API.
  void setDataHandler(bool (*)(const Data &, void *), void * = nullptr);
  int connect(const Address &);
  Binding bindingForPeer(const Address &) const;
  int thumbnail(const Binding &, const char *seven_digit_handle);
  int image(const Binding &, const char *seven_digit_handle,
            const char *descriptor = nullptr);
  int properties(const Binding &, const char *seven_digit_handle);
  int disconnect(const Binding &); // Graceful OBEX disconnect, only while idle.
  int cancel(
      const Binding &); // Closes the Cover Art transport, not signalling.
  int cancelPeer(const Address &); // Also cancels a still-pending connection.
  // Budgets for NEW connections/GETs. Zero disables; max 0x7fffffff ms.
  // Absolute elapsed time from the first timed poll after command admission,
  // not inactivity. Data does not extend the budget. Callback mutation fails.
  bool setTimeouts(uint32_t connection_ms, uint32_t object_ms);
  void poll(); // Services cancellation only; does not sample/advance time.
  // Supply monotonic milliseconds modulo 2^32, at intervals < 2^31 ms.
  // Call promptly after commands and regularly while pending.
  void poll(uint32_t now_ms);
  void stop();
  const Status &status() const { return status_; }

private:
  static void packet(uint8_t, uint16_t, uint8_t *, uint16_t);
  int get(const Binding &, Operation, const char *, const char *);
  void emit(const Event &);
  void pollImpl(bool timed, uint32_t now_ms);
  bool allowed(const Address &) const;
  Config config_{};
  uint32_t limit_ = 0;
  uint32_t connection_timeout_ms_ = 0, object_timeout_ms_ = 0;
  Status status_{};
  bool dispatching_ = false;
  void (*callback_)(const Event &, void *) = nullptr;
  void *context_ = nullptr;
  bool (*sink_)(const Data &, void *) = nullptr;
  void *sink_context_ = nullptr;
};
} // namespace picow::avrcp
