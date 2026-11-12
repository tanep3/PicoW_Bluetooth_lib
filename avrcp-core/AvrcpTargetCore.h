#pragma once
// Allocation-free AVRCP pass-through state, independent of Pico and BTstack.
// Single-loop API. Callbacks may inspect state, but must not mutate this object.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace picow { namespace avrcp {
using Address = std::array<uint8_t, 6>;
constexpr std::size_t PeerCapacity = 2;
struct Binding {
  Address peer{};
  uint32_t epoch = 0; // Shared runtime ACL incarnation, never zero.
  uint16_t cid = 0;
  uint32_t generation = 0; // Assigned by connect; survives reset monotonically.
};
inline bool operator==(const Binding &a, const Binding &b) {
  return a.peer == b.peer && a.epoch == b.epoch && a.cid == b.cid &&
         a.generation == b.generation;
}
enum class Result : uint8_t {
  Ok, InvalidInput, NotConnected, StaleBinding, Capacity, Duplicate,
  Unsupported, InvalidSequence, Exhausted
};
enum class Kind : uint8_t { Connected, Disconnected, Operation, Reset, Rejected };
struct Event {
  Kind kind = Kind::Rejected;
  Binding binding{};
  uint8_t operation = 0;
  bool pressed = false;
  Result result = Result::Ok;
};
struct Status {
  uint64_t connections = 0, disconnections = 0, operations = 0;
  uint64_t rejected = 0, resets = 0;
  std::size_t active = 0;
  Result last_result = Result::Ok;
};
class AvrcpTargetCore {
public:
  using Callback = void (*)(const Event &, void *);
  AvrcpTargetCore() {
    // AVRCP operation IDs: volume +/-, play, stop, pause, rewind,
    // fast forward, forward and backward. No application-specific mapping.
    for (uint8_t op : {0x41, 0x42, 0x44, 0x45, 0x46, 0x48, 0x49, 0x4b, 0x4c})
      supported_[op] = true;
  }
  void setCallback(Callback callback, void *context = nullptr) {
    callback_ = callback;
    context_ = context;
  }
  const Status &status() const { return status_; }
  bool supported(uint8_t operation) const {
    return operation < supported_.size() && supported_[operation];
  }
  Result setSupported(uint8_t operation, bool enabled) {
    if (operation >= supported_.size()) return reject({}, Result::InvalidInput);
    if (status_.active) return reject({}, Result::InvalidSequence);
    supported_[operation] = enabled;
    return success();
  }
  Binding bindingForCid(uint16_t cid) const {
    for (const auto &slot : slots_)
      if (slot.active && slot.binding.cid == cid) return slot.binding;
    return {};
  }
  Binding bindingForPeer(const Address &peer) const {
    for (const auto &slot : slots_)
      if (slot.active && slot.binding.peer == peer) return slot.binding;
    return {};
  }
  bool isPressed(const Binding &binding, uint8_t operation) const {
    if (operation >= supported_.size()) return false;
    for (const auto &slot : slots_)
      if (slot.active && slot.binding == binding) return slot.pressed[operation];
    return false;
  }
  Result connect(const Binding &requested, Binding *accepted = nullptr) {
    if (accepted) *accepted = {};
    if (!requested.cid || !requested.epoch || requested.peer == Address{})
      return reject(requested, Result::InvalidInput);
    Slot *free = nullptr;
    for (auto &slot : slots_) {
      if (!slot.active) { if (!free) free = &slot; continue; }
      // One AVRCP control channel per peer; never replace an active binding.
      if (slot.binding.cid == requested.cid || slot.binding.peer == requested.peer)
        return reject(requested, Result::Duplicate);
    }
    if (!free) return reject(requested, Result::Capacity);
    if (generation_ == std::numeric_limits<uint32_t>::max())
      return reject(requested, Result::Exhausted);
    *free = {};
    free->binding = requested;
    free->binding.generation = ++generation_;
    free->active = true;
    ++status_.active;
    ++status_.connections;
    success();
    if (accepted) *accepted = free->binding;
    emit({Kind::Connected, free->binding, 0, false, Result::Ok});
    return Result::Ok;
  }
  Result disconnect(const Binding &binding) {
    Slot *slot = nullptr;
    const auto result = resolve(binding, slot);
    if (result != Result::Ok) return reject(binding, result);
    *slot = {};
    --status_.active;
    ++status_.disconnections;
    success();
    // No invented release commands: Disconnected tells the app to cancel holds.
    emit({Kind::Disconnected, binding, 0, false, Result::Ok});
    return Result::Ok;
  }
  // Read-only validation is useful to adapters that must acknowledge commands
  // before publishing them. BTstack may already acknowledge pass-through itself.
  Result validateOperation(const Binding &binding, uint8_t operation,
                           bool pressed) const {
    if (!binding.cid || !binding.epoch || !binding.generation || binding.peer == Address{})
      return Result::InvalidInput;
    const Slot *found = nullptr;
    for (const auto &slot : slots_) {
      if (!slot.active) continue;
      if (slot.binding == binding) { found = &slot; break; }
      if (slot.binding.cid == binding.cid || slot.binding.peer == binding.peer)
        return Result::StaleBinding;
    }
    if (!found) return Result::NotConnected;
    if (operation >= supported_.size()) return Result::InvalidInput;
    if (!supported_[operation]) return Result::Unsupported;
    if (found->pressed[operation] == pressed) return Result::InvalidSequence;
    return Result::Ok;
  }
  Result operation(const Binding &binding, uint8_t operation, bool pressed) {
    const auto result = validateOperation(binding, operation, pressed);
    if (result != Result::Ok) return reject(binding, result, operation, pressed);
    Slot *slot = nullptr;
    resolve(binding, slot);
    slot->pressed[operation] = pressed;
    ++status_.operations;
    success();
    emit({Kind::Operation, binding, operation, pressed, Result::Ok});
    return Result::Ok;
  }
  void reset() {
    // Preserve counters, support policy and generation to reject old handles.
    slots_ = {};
    status_.active = 0;
    ++status_.resets;
    success();
    emit({Kind::Reset, {}, 0, false, Result::Ok});
  }
private:
  struct Slot {
    Binding binding{};
    std::array<bool, 128> pressed{};
    bool active = false;
  };
  Result resolve(const Binding &binding, Slot *&found) {
    if (!binding.cid || !binding.epoch || !binding.generation || binding.peer == Address{})
      return Result::InvalidInput;
    for (auto &slot : slots_) {
      if (!slot.active) continue;
      if (slot.binding == binding) { found = &slot; return Result::Ok; }
      if (slot.binding.cid == binding.cid || slot.binding.peer == binding.peer)
        return Result::StaleBinding;
    }
    return Result::NotConnected;
  }
  Result success() { status_.last_result = Result::Ok; return Result::Ok; }
  Result reject(const Binding &binding, Result result, uint8_t operation = 0,
                bool pressed = false) {
    ++status_.rejected;
    status_.last_result = result;
    emit({Kind::Rejected, binding, operation, pressed, result});
    return result;
  }
  void emit(const Event &event) { if (callback_) callback_(event, context_); }
  std::array<Slot, PeerCapacity> slots_{};
  std::array<bool, 128> supported_{};
  Status status_{};
  uint32_t generation_ = 0;
  Callback callback_ = nullptr;
  void *context_ = nullptr;
};
}} // namespace picow::avrcp
