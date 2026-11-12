#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace avrcp_sample {
// Application bytes only: no SDK, decoder, authentication, allocation or I/O.
struct ArtBinding {
  std::array<uint8_t, 6> peer{};
  uint32_t epoch = 0, generation = 0;
  uint16_t cid = 0, avrcp_cid = 0;
  bool operator==(const ArtBinding &b) const {
    return peer == b.peer && epoch == b.epoch && generation == b.generation &&
           cid == b.cid && avrcp_cid == b.avrcp_cid;
  }
};
enum class ArtOperation : uint8_t { Thumbnail, Image, Properties };
enum class ArtState { Idle, Armed, Receiving, Complete, Discarded };
enum class ArtFailure { None, State, Binding, Operation, Request, Offset,
                        Empty, Null, Capacity, Status, Count, Cancelled,
                        Disconnected, Error, QueueLoss };
// One-shot application-lifetime gate, independent of object reset/reuse.
// take() only permits a main-loop request; it proves no transport outcome.
class ArtCancellationGate {
  bool requested_ = false;
public:
  bool take() {
    if (requested_) return false;
    requested_ = true;
    return true;
  }
};
template<size_t Capacity> class CoverArtObject {
  static_assert(Capacity > 0 && Capacity <= UINT32_MAX, "32-bit nonempty budget");
  std::array<uint8_t, Capacity> bytes_{};
  ArtBinding binding_{};
  ArtOperation operation_{};
  uint32_t request_ = 0;
  size_t used_ = 0;
  ArtState state_ = ArtState::Idle;
  ArtFailure failure_ = ArtFailure::None;
  bool fail(ArtFailure f) { discard(f); return false; }
  bool matches(const ArtBinding &b, ArtOperation op) {
    if (!(binding_ == b)) return fail(ArtFailure::Binding);
    if (operation_ != op) return fail(ArtFailure::Operation);
    return true;
  }
public:
  // No rearming over queued old records: caller must first drain/discard them.
  void reset() {
    bytes_.fill(0); binding_ = {}; operation_ = {}; request_ = 0; used_ = 0;
    state_ = ArtState::Idle; failure_ = ArtFailure::None;
  }
  bool arm(const ArtBinding &b, ArtOperation op) {
    if (state_ != ArtState::Idle) return fail(ArtFailure::State);
    if (!b.epoch || !b.generation || !b.cid || !b.avrcp_cid ||
        b.peer == std::array<uint8_t, 6>{}) return fail(ArtFailure::Binding);
    if (op != ArtOperation::Thumbnail && op != ArtOperation::Image &&
        op != ArtOperation::Properties) return fail(ArtFailure::Operation);
    binding_ = b; operation_ = op; state_ = ArtState::Armed; return true;
  }
  bool append(const ArtBinding &b, ArtOperation op, uint32_t request,
              uint32_t offset, const uint8_t *data, size_t size) {
    if (state_ != ArtState::Armed && state_ != ArtState::Receiving)
      return fail(ArtFailure::State);
    if (!matches(b, op)) return false;
    if (!request || (state_ == ArtState::Receiving && request != request_))
      return fail(ArtFailure::Request);
    if (offset != used_) return fail(ArtFailure::Offset);
    if (!size) return fail(ArtFailure::Empty); // Strict sample policy.
    if (!data) return fail(ArtFailure::Null);
    if (size > Capacity - used_) return fail(ArtFailure::Capacity);
    std::memcpy(bytes_.data() + used_, data, size);
    used_ += size; request_ = request; state_ = ArtState::Receiving; return true;
  }
  bool complete(const ArtBinding &b, ArtOperation op, uint32_t request,
                int status, uint32_t count) {
    if (state_ != ArtState::Receiving) return fail(ArtFailure::State);
    if (!matches(b, op)) return false;
    if (request != request_) return fail(ArtFailure::Request);
    if (status != 0) return fail(ArtFailure::Status);
    if (count != used_) return fail(ArtFailure::Count);
    state_ = ArtState::Complete; return true;
  }
  // Fail closed even for unrelated terminal errors; never promotes partial data.
  void discard(ArtFailure why) {
    bytes_.fill(0); used_ = 0; request_ = 0;
    if (state_ != ArtState::Discarded) failure_ = why;
    state_ = ArtState::Discarded;
  }
  ArtState state() const { return state_; }
  ArtFailure failure() const { return failure_; }
  size_t received() const { return used_; }
  // View expires on ANY mutating call. Never expose uncompleted bytes.
  const uint8_t *data() const { return state_ == ArtState::Complete ? bytes_.data() : nullptr; }
  size_t size() const { return state_ == ArtState::Complete ? used_ : 0; }
};
} // namespace avrcp_sample
