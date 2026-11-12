#pragma once
#include "CoverArtObject.h"

namespace avrcp_sample {
// SDK-independent application flow. Call only on the polling thread, outside
// callbacks. There is no reset/retry: at most one connect and GET per lifetime.
struct SessionBinding {
  std::array<uint8_t, 6> peer{};
  uint32_t epoch = 0, generation = 0;
  uint16_t cid = 0;
  bool operator==(const SessionBinding &b) const {
    return peer == b.peer && epoch == b.epoch && generation == b.generation && cid == b.cid;
  }
  bool valid() const { return peer != std::array<uint8_t, 6>{} && epoch && generation && cid; }
};
struct ArtLive {
  SessionBinding signal{}, browse{};
  ArtBinding cover{};
  bool queue_empty = true, queue_lost = false;
};
enum class FlowState { WaitingSignal, WaitingBrowse, Ready, Queued, Connecting,
                       NeedGet, Receiving, Complete, Failed, Stopped };
enum class FlowFailure { None, Session, Ordering, Connect, Get, Transfer, Timeout,
                         Cancelled, QueueLoss, Browse, Stopped };
enum class InputResult { Accepted, Invalid, NotReady, Duplicate };
struct ArtCommand {
  ArtOperation operation = ArtOperation::Image;
  std::array<char, 8> handle{};
  bool valid() const {
    if (operation != ArtOperation::Thumbnail && operation != ArtOperation::Image &&
        operation != ArtOperation::Properties) return false;
    for (size_t i = 0; i < 7; ++i) if (handle[i] < '0' || handle[i] > '9') return false;
    return handle[7] == '\0'; // Syntax only, NOT proof of metadata provenance.
  }
};
// 32-byte command line; overlong/control-containing lines are discarded whole.
// Caller reads a bounded number of characters per loop, using nonblocking I/O.
class ArtCommandLine {
  std::array<char, 32> line_{};
  size_t used_ = 0;
  bool cr_ = false, bad_ = false, started_ = false, ready_at_start_ = false;
public:
  enum class Result { None, Request, Cancel, Invalid, NotReady };
  Result feed(char c, ArtCommand &out, bool ready = true) {
    if (c == '\r') { if (cr_) bad_ = true; cr_ = true; return Result::None; }
    if (c != '\n') {
      if (cr_) bad_ = true; // CR is permitted only immediately before LF.
      if (!started_) { started_ = true; ready_at_start_ = ready; }
      if (c < ' ' || c > '~' || used_ == line_.size()) bad_ = true;
      else if (!bad_) line_[used_++] = c;
      return Result::None;
    }
    const size_t n = used_; used_ = 0; started_ = false; cr_ = false;
    if (bad_) { bad_ = false; return Result::Invalid; }
    if (n == 6 && std::memcmp(line_.data(), "cancel", 6) == 0) return Result::Cancel;
    const char *names[] = {"art thumbnail ", "art image ", "art properties "};
    const ArtOperation ops[] = {ArtOperation::Thumbnail, ArtOperation::Image, ArtOperation::Properties};
    for (size_t i = 0; i < 3; ++i) {
      const size_t prefix = std::strlen(names[i]);
      if (n != prefix + 7 || std::memcmp(line_.data(), names[i], prefix)) continue;
      ArtCommand candidate; candidate.operation = ops[i];
      std::memcpy(candidate.handle.data(), line_.data() + prefix, 7);
      if (!candidate.valid()) return Result::Invalid;
      if (!ready_at_start_ || !ready) return Result::NotReady;
      out = candidate; return Result::Request;
    }
    return Result::Invalid;
  }
};

template<size_t Capacity> class CoverArtFlow {
  CoverArtObject<Capacity> object_;
  SessionBinding signal_{}, browse_{};
  ArtBinding cover_{};
  ArtCommand command_{};
  FlowState state_ = FlowState::WaitingSignal;
  FlowFailure failure_ = FlowFailure::None;
  uint32_t started_ = 0;
  bool browse_attempted_ = false, connect_attempted_ = false, cancel_requested_ = false;
  int local_status_ = 0, cancel_status_ = 0;
  bool terminal() const { return state_ == FlowState::Failed || state_ == FlowState::Stopped; }
  bool liveSession(const ArtLive &s) const {
    return signal_.valid() && s.signal == signal_ &&
           (!browse_.valid() || s.browse == browse_);
  }
  void fail(FlowFailure f, ArtFailure a = ArtFailure::Error) {
    object_.discard(a);
    if (!terminal()) { failure_ = f; state_ = FlowState::Failed; }
  }
public:
  FlowState state() const { return state_; }
  FlowFailure failure() const { return failure_; }
  const CoverArtObject<Capacity> &object() const { return object_; }
  int localStatus() const { return local_status_; }
  int cancelStatus() const { return cancel_status_; }
  // true exactly once: caller may issue its signalling query. Browsing is
  // deferred until takeBrowseAttempt observes a drained queue and live session.
  bool signalConnected(const SessionBinding &event, const ArtLive &s) {
    if (terminal()) return false;
    if (!event.valid() || !(event == s.signal)) { fail(FlowFailure::Session); return false; }
    if (state_ != FlowState::WaitingSignal) {
      if (!(event == signal_)) fail(FlowFailure::Session);
      return false;
    }
    signal_ = event; state_ = FlowState::WaitingBrowse; return true;
  }
  bool takeBrowseAttempt(const ArtLive &s) {
    if (state_ != FlowState::WaitingBrowse || browse_attempted_) return false;
    if (s.queue_lost) { queueLoss(); return false; }
    if (!liveSession(s)) { fail(FlowFailure::Session); return false; }
    if (!s.queue_empty) return false;
    browse_attempted_ = true; return true;
  }
  void browseAdmission(int status) { if (status) fail(FlowFailure::Browse); }
  bool browseConnected(const SessionBinding &event, const ArtLive &s, int status = 0) {
    if (terminal()) return false;
    if (status || !liveSession(s) || !event.valid() || !(event == s.browse) ||
        event.peer != signal_.peer || event.epoch != signal_.epoch) {
      fail(FlowFailure::Session); return false;
    }
    if (state_ != FlowState::WaitingBrowse) {
      if (!(event == browse_)) fail(FlowFailure::Ordering);
      return false;
    }
    if (!browse_attempted_) { fail(FlowFailure::Ordering); return false; }
    browse_ = event; state_ = FlowState::Ready; return true;
  }
  InputResult submit(const ArtCommand &command, const ArtLive &s) {
    if (!command.valid()) return InputResult::Invalid;
    if (state_ == FlowState::Queued || connect_attempted_ || terminal()) return InputResult::Duplicate;
    if (state_ != FlowState::Ready || !s.queue_empty) return InputResult::NotReady;
    if (s.queue_lost) { queueLoss(); return InputResult::NotReady; }
    if (!liveSession(s) || s.cover.cid) { fail(FlowFailure::Session); return InputResult::NotReady; }
    command_ = command; state_ = FlowState::Queued; return InputResult::Accepted;
  }
  void connected(const ArtBinding &event, int status, const ArtLive &s) {
    if (terminal()) return;
    if (state_ != FlowState::Connecting) { fail(FlowFailure::Ordering); return; }
    if (status || !liveSession(s) || !(event == s.cover) || !event.cid ||
        !event.generation || event.peer != signal_.peer || event.epoch != signal_.epoch ||
        event.avrcp_cid != signal_.cid) { fail(FlowFailure::Session); return; }
    cover_ = event; state_ = FlowState::NeedGet;
  }
  bool data(const ArtBinding &b, ArtOperation op, uint32_t request, uint32_t offset,
            const uint8_t *bytes, size_t size) {
    if (terminal()) return false;
    if (state_ != FlowState::Receiving || !object_.append(b, op, request, offset, bytes, size)) {
      fail(FlowFailure::Transfer); return false;
    }
    return true;
  }
  bool complete(const ArtBinding &b, ArtOperation op, uint32_t request, int status, uint32_t count) {
    if (terminal()) return false;
    if (state_ != FlowState::Receiving || !object_.complete(b, op, request, status, count)) {
      fail(FlowFailure::Transfer); return false;
    }
    state_ = FlowState::Complete; return true;
  }
  void error() { fail(FlowFailure::Transfer); }
  void disconnected() { fail(FlowFailure::Session, ArtFailure::Disconnected); }
  void queueLoss() { fail(FlowFailure::QueueLoss, ArtFailure::QueueLoss); }
  void cancel() { fail(FlowFailure::Cancelled, ArtFailure::Cancelled); }
  void stop() {
    fail(FlowFailure::Stopped, ArtFailure::Cancelled);
    state_ = FlowState::Stopped;
  }
  // IO.connect(peer), IO.get(binding,operation,handle), IO.cancel(peer) are
  // application main-loop calls. Set state/arm BEFORE invoking asynchronous APIs.
  // No other owner may issue Cover Art connect/GET; queue-empty alone cannot
  // prove transport idleness. This one-shot owner never resets or retags intent.
  template<class IO> void tick(uint32_t now, const ArtLive &s, IO &io) {
    if (s.queue_lost) queueLoss();
    if (!terminal() && state_ != FlowState::WaitingSignal && !liveSession(s))
      fail(FlowFailure::Session, ArtFailure::Disconnected);
    if (!terminal() && (state_ == FlowState::NeedGet || state_ == FlowState::Receiving ||
                       state_ == FlowState::Complete) && !(s.cover == cover_))
      fail(FlowFailure::Session, ArtFailure::Disconnected);
    if (!terminal() && ((state_ == FlowState::Connecting || state_ == FlowState::NeedGet) &&
                        uint32_t(now - started_) >= 15000u)) fail(FlowFailure::Timeout);
    if (!terminal() && state_ == FlowState::Receiving && uint32_t(now - started_) >= 30000u)
      fail(FlowFailure::Timeout);
    if (state_ == FlowState::Queued && s.queue_empty) {
      if (s.cover.cid) fail(FlowFailure::Session);
      else {
        state_ = FlowState::Connecting; started_ = now; connect_attempted_ = true;
        local_status_ = io.connect(signal_.peer);
        if (local_status_) fail(FlowFailure::Connect);
      }
    } else if (state_ == FlowState::NeedGet && s.queue_empty) {
      if (!object_.arm(cover_, command_.operation)) fail(FlowFailure::Transfer);
      else {
        state_ = FlowState::Receiving; started_ = now;
        local_status_ = io.get(cover_, command_.operation, command_.handle.data());
        if (local_status_) fail(FlowFailure::Get);
      }
    }
    if (terminal() && connect_attempted_ && !cancel_requested_) {
      cancel_requested_ = true; // Local rejection does NOT trigger a retry.
      cancel_status_ = io.cancel(signal_.peer);
    }
  }
};
} // namespace avrcp_sample
