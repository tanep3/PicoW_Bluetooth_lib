#pragma once
#include "A2dpShared.h"
#include "btstack.h"
#include "classic/a2dp.h"

// Pinned to the installed BTstack A2DP/AVDTP adapter contract. Gating must happen
// BEFORE a2dp_config_process: dropping only the application notification still
// lets the SDK auto-discover/configure the wrong role's endpoint.
// This consumes trusted SDK-generated AVDTP meta events, NOT untrusted wire
// bytes. Framing/CID checks are defensive; it is not a generic event parser.
namespace picow::a2dp_shared {
inline bool otherOwnsCid(Role role, uint16_t cid) {
  const auto *connection = avdtp_get_connection_for_avdtp_cid(cid);
  if (!connection) return false;
  Address peer{};
  for (unsigned i = 0; i < peer.size(); ++i) peer[i] = connection->remote_addr[i];
  return otherOwnsPeer(role, peer);
}
inline bool shouldDispatch(Role role, uint8_t type, const uint8_t *packet, uint16_t size) {
  if (type != HCI_EVENT_PACKET || !packet || size < 5 ||
      packet[0] != HCI_EVENT_AVDTP_META || unsigned(packet[1]) + 2u != size) return false;
  switch (packet[2]) {
  case AVDTP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED:
  case AVDTP_SUBEVENT_STREAMING_CONNECTION_ESTABLISHED:
    if (size != 14) return false;
    break;
  case AVDTP_SUBEVENT_SIGNALING_DELAY_REPORT:
  case AVDTP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW:
    if (size != 8) return false;
    break;
  case AVDTP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
    if (size != 5) return false;
    break;
  default: break; // Other payloads are generated/validated by the SDK.
  }
  const auto *connection = avdtp_get_connection_for_avdtp_cid(little_endian_read_16(packet, 3));
  if (!connection) return false;
  Address peer{};
  for (unsigned i = 0; i < peer.size(); ++i) peer[i] = connection->remote_addr[i];
  const auto &owner = owners[static_cast<unsigned>(role)];
  const bool own_peer = owner.registered && peer != Address{} && owner.peer == peer;
  const bool established = packet[2] == AVDTP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED && packet[13] == 0;
  const bool admitted = !established || (owner.admission && owner.admission());
  if (own_peer && admitted) return true;
  // Do not let either SDK role start automatic negotiation for an unknown or
  // unadmitted peer. Only close a successful, fully opened AVDTP signaling
  // connection: cancelling earlier states can synchronously finalize/reenter.
  // SDK changes OPENED to W4_L2CAP_DISCONNECTED, so the other callback does not
  // repeat the close. This does not disconnect the shared HCI ACL.
  bool other_protected = otherOwnsPeer(role, peer);
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
  if (own_peer && other_protected) {
    const auto &other = owners[1u - static_cast<unsigned>(role)];
    // Registration alone cannot accept an unsolicited new connection. Existing
    // SDK work protects the signaling even during local detach/cancellation.
    other_protected = (other.admission && other.admission()) ||
        (other.discovery_admission && other.discovery_admission()) ||
        connection->a2dp_source_config_process.state != A2DP_IDLE ||
        connection->a2dp_sink_config_process.state != A2DP_IDLE;
  }
#endif
  if (established && !other_protected &&
      connection->state == AVDTP_SIGNALING_CONNECTION_OPENED)
    avdtp_disconnect(connection->avdtp_cid);
  return false;
}
// The SDK also resumes its own queue on error/timer paths. Remove cancelled
// queued intent before polling/dispatch, not merely from our resume loop.
inline void pruneQueuedDiscovery() {
  for (unsigned i = 0; i < owners.size(); ++i) {
    const auto &owner = owners[i];
    if (!owner.registered || owner.peer == Address{} ||
        (owner.discovery_admission && owner.discovery_admission())) continue;
    auto peer = owner.peer;
    auto *connection = avdtp_get_connection_for_bd_addr(peer.data());
    if (!connection) continue;
    auto &process = i == 0 ? connection->a2dp_source_config_process : connection->a2dp_sink_config_process;
    process.discover_seps = false;
  }
}
// The pinned SDK has one global SEP-discovery gate. Some success/release
// paths clear it without waking another connection's discover_seps request.
// Re-submit only SDK-marked queued work through its exported entry point; the
// SDK itself decides whether the gate is free. Never reset its private state.
inline void resumeQueuedDiscovery(uint16_t releasing_cid = 0) {
  static bool active = false;
  if (active) return;
  struct Guard { bool &flag; explicit Guard(bool &f) : flag(f) { flag = true; } ~Guard() { flag = false; } } guard(active);
  for (unsigned i = 0; i < owners.size(); ++i) {
    const auto &owner = owners[i];
    if (!owner.registered || owner.peer == Address{} || !owner.discovery_admission || !owner.discovery_admission()) continue;
    auto peer = owner.peer;
    auto *connection = avdtp_get_connection_for_bd_addr(peer.data());
    if (!connection || !connection->avdtp_cid || connection->avdtp_cid == releasing_cid ||
        connection->state != AVDTP_SIGNALING_CONNECTION_OPENED) continue;
    // Same-peer experimental scheduling must not steal the shared initiator
    // before the other role sends/finishes OPEN after its codec configuration.
    // Public same-peer registration remains guarded until full integration.
    if (otherOwnsPeer(static_cast<Role>(i), owner.peer)) {
      if (connection->configuration_state == AVDTP_CONFIGURATION_STATE_LOCAL_INITIATED ||
          connection->configuration_state == AVDTP_CONFIGURATION_STATE_REMOTE_INITIATED) continue;
      const auto &other = i == 0 ? connection->a2dp_sink_config_process : connection->a2dp_source_config_process;
      if (other.state != A2DP_IDLE && other.state != A2DP_CONNECTED &&
          other.state != A2DP_CONFIGURED && other.state != A2DP_STREAMING_OPENED) continue;
    }
    const auto role = i == 0 ? AVDTP_ROLE_SOURCE : AVDTP_ROLE_SINK;
    const auto &process = i == 0 ? connection->a2dp_source_config_process : connection->a2dp_sink_config_process;
    if (process.discover_seps) a2dp_config_process_ready_for_sep_discovery(role, connection);
  }
}
inline void sourcePacket(uint8_t type, uint16_t, uint8_t *packet, uint16_t size) {
  if (!shouldDispatch(Role::Source, type, packet, size)) return;
  pruneQueuedDiscovery();
  switch (packet[2]) {
  case AVDTP_SUBEVENT_SIGNALING_DELAY_REPORT:
    a2dp_replace_subevent_id_and_emit_source(packet, size, A2DP_SUBEVENT_SIGNALING_DELAY_REPORT); break;
  case AVDTP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW:
    a2dp_replace_subevent_id_and_emit_source(packet, size, A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW); break;
  default:
    a2dp_config_process_avdtp_event_handler(AVDTP_ROLE_SOURCE, packet, size);
    resumeQueuedDiscovery(packet[2] == AVDTP_SUBEVENT_SIGNALING_CONNECTION_RELEASED ? little_endian_read_16(packet, 3) : 0);
    break;
  }
}
inline void sinkPacket(uint8_t type, uint16_t, uint8_t *packet, uint16_t size) {
  if (shouldDispatch(Role::Sink, type, packet, size)) {
    pruneQueuedDiscovery();
    a2dp_config_process_avdtp_event_handler(AVDTP_ROLE_SINK, packet, size);
    resumeQueuedDiscovery(packet[2] == AVDTP_SUBEVENT_SIGNALING_CONNECTION_RELEASED ? little_endian_read_16(packet, 3) : 0);
  }
}
inline void install(Role role) {
  if (role == Role::Source) avdtp_source_register_packet_handler(sourcePacket);
  else avdtp_sink_register_packet_handler(sinkPacket);
}
} // namespace picow::a2dp_shared
