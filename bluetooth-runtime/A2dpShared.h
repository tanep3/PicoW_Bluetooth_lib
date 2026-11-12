#pragma once
#include <array>
#include <cstdint>

// Application-side role ownership. SDK 1.6.2 broadcasts signaling to both
// roles, but keeps only one configured media stream per peer connection.
// Distinct-peer dual role is permitted. Same-peer dual media needs SDK work;
// reject that registration rather than letting either role tear down the other.
namespace picow::a2dp_shared {
using Address = std::array<uint8_t, 6>;
enum class Role : uint8_t { Source, Sink };
struct Owner { bool registered = false; Address peer{}; bool (*admission)() = nullptr; bool (*discovery_admission)() = nullptr; };
inline std::array<Owner, 2> owners{};
inline bool otherOwnsPeer(Role role, const Address &peer) {
  const auto &other = owners[1u - static_cast<unsigned>(role)];
  return peer != Address{} && other.registered && other.peer == peer;
}
inline bool canRegister(Role role, const Address &peer) {
  if (owners[static_cast<unsigned>(role)].registered) return false;
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
  // Only explicit version-pinned experiment targets may exercise shared peers.
  (void)peer;
  return true;
#else
  return !otherOwnsPeer(role, peer);
#endif
}
inline void registerRole(Role role, const Address &peer) {
  owners[static_cast<unsigned>(role)] = {true, peer, nullptr, nullptr};
}
inline void setAdmission(Role role, bool (*admission)()) {
  owners[static_cast<unsigned>(role)].admission = admission;
}
inline void setDiscoveryAdmission(Role role, bool (*admission)()) {
  owners[static_cast<unsigned>(role)].discovery_admission = admission;
}
} // namespace picow::a2dp_shared
