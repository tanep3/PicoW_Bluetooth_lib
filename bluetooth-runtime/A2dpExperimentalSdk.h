#pragma once
#include "btstack.h"
// Only the version-pinned experimental SDK overlay exports these symbols.
// Normal builds retain the same-peer registration guard. No stability promise.
#ifdef PICOW_A2DP_SAME_PEER_EXPERIMENTAL
extern "C" {
uint8_t picow_a2dp_join_existing_role(avdtp_role_t role, uint16_t cid);
uint8_t picow_a2dp_close_role_media(avdtp_role_t role, uint16_t cid, uint8_t seid);
// Request returns success when accepted; poll success means fully retired.
// COMMAND_DISALLOWED from poll means drain still pending. Pre-configuration only.
uint8_t picow_a2dp_cancel_role_discovery(avdtp_role_t role, uint16_t cid);
uint8_t picow_a2dp_poll_role_cancel(avdtp_role_t role, uint16_t cid);
uint8_t picow_a2dp_retire_role(avdtp_role_t role, uint16_t cid, uint8_t seid);
}
#endif
