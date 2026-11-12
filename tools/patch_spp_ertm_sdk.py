#!/usr/bin/env python3
"""Generate pinned L2CAP/RFCOMM safety overlays without modifying the SDK.

Upstream BlueKitchen attribution and license are preserved verbatim. Unknown
source bytes fail closed. This generator is repository-authored modification
logic, not a replacement license or a claim of Bluetooth certification.
"""
import hashlib
from pathlib import Path
import sys

REVISION = "picow-spp-ertm-overlay-v3"
EXPECTED = {
    "l2cap": "76654c73dd8c02792099fc4edbb2d467ee3cb7284b8def33202bf07cd715d190",
    "rfcomm": "91f3f07d1de269c00cc3a2edc17fd6e072cf569a965b3024fefdd838b75e7a72",
}


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError("Expected one audited source anchor: " + old[:90])
    return source.replace(old, new, 1)


def l2cap(source):
    source = replace_once(source,
        "#define L2CAP_SIG_ID_INVALID 0",
        """#define L2CAP_SIG_ID_INVALID 0

/* PicoW: bit 14 is unused by this pinned SDK. Remember the mode of the
 * outstanding request, independently of an optional switch to Basic caused
 * by the peer's crossed request. No SDK header or channel ABI is changed. */
#define PICOW_L2CAP_CONF_REQ_ERTM (1u << 14)
""")
    source = replace_once(source,
        "    // get num free tx buffers\n    int num_free_tx_buffers",
        """    /* PicoW: never divide by zero or fragment below the SDU header size. */
    if (channel->local_mps < 3 || channel->remote_mps < 3 ||
        channel->num_tx_buffers == 0 ||
        channel->num_stored_tx_frames > channel->num_tx_buffers) return false;
    // get num free tx buffers
    int num_free_tx_buffers""")
    source = replace_once(source,
        "    pos += channel->num_rx_buffers * channel->remote_mps;",
        "    pos += channel->num_tx_buffers * channel->remote_mps;")
    # Both request and response must validate the complete TLV envelope before
    # accessing its header or payload. The signaling dispatcher validates the
    # enclosing command length before either parser is called.
    start = source.index("static void l2cap_signaling_handle_configure_request(")
    end = source.index("static int l2cap_channel_ready_for_open(", start)
    parsers = source[start:end]
    parsers = parsers.replace("uint16_t end_pos = 4 +", "uint32_t end_pos = 4u +")
    parsers = parsers.replace("uint16_t pos     =", "uint32_t pos     =")
    anchor = "    while (pos < end_pos){\n"
    assert parsers.count(anchor) == 2
    parsers = parsers.replace(anchor, anchor + """        /* PicoW: reject truncated option headers/payloads before any read. */
        if (end_pos - pos < 2u || command[pos + 1u] > end_pos - pos - 2u){
            channel->state = L2CAP_STATE_WILL_SEND_DISCONNECT_REQUEST;
            return;
        }
""")
    anchor = "        // Retransmission and Flow Control Option\n"
    assert parsers.count(anchor) == 2
    parsers = parsers.replace(anchor, anchor + """        if (option_type == L2CAP_CONFIG_OPTION_TYPE_RETRANSMISSION_AND_FLOW_CONTROL && length != 9){
            channel->state = L2CAP_STATE_WILL_SEND_DISCONNECT_REQUEST;
            return;
        }
""")
    parsers = replace_once(parsers,
        "            channel->remote_mtu = little_endian_read_16(command, pos);",
        """            if (little_endian_read_16(command, pos) < 48){
                channel->state = L2CAP_STATE_WILL_SEND_DISCONNECT_REQUEST;
                return;
            }
            channel->remote_mtu = little_endian_read_16(command, pos);""")
    parsers = replace_once(parsers,
        """                case L2CAP_CHANNEL_MODE_ENHANCED_RETRANSMISSION:
                    // Store remote config""",
        """                case L2CAP_CHANNEL_MODE_ENHANCED_RETRANSMISSION:
                    /* PicoW: BASIC options carry no usable ERTM parameters.
                     * Accept optional Basic immediately: counteroffering ERTM
                     * makes Basic-only peers close before our retry arrives. */
                    if (mode != L2CAP_CHANNEL_MODE_ENHANCED_RETRANSMISSION){
                        if (channel->ertm_mandatory){
                            channel->state = L2CAP_STATE_WILL_SEND_DISCONNECT_REQUEST;
                        } else if (mode == L2CAP_CHANNEL_MODE_BASIC){
                            channel->mode = L2CAP_CHANNEL_MODE_BASIC;
                            channelStateVarClearFlag(channel, L2CAP_CHANNEL_STATE_VAR_SEND_CONF_RSP_REJECTED);
                            channelStateVarClearFlag(channel, L2CAP_CHANNEL_STATE_VAR_SEND_CONF_RSP_ERTM);
                            channelStateVarClearFlag(channel, L2CAP_CHANNEL_STATE_VAR_RCVD_CONF_RSP);
                            /* Await a crossed ERTM response before reusing its
                             * identifier. If already answered, request Basic. */
                            if (!(channel->state_var & L2CAP_CHANNEL_STATE_VAR_SENT_CONF_REQ)){
                                channelStateVarSetFlag(channel, L2CAP_CHANNEL_STATE_VAR_SEND_CONF_REQ);
                            }
                            l2cap_emit_simple_event_with_cid(channel, L2CAP_EVENT_ERTM_BUFFER_RELEASED);
                        } else {
                            channel->remote_tx_window_size = btstack_min(channel->num_tx_buffers, 63u);
                            channel->remote_max_transmit = channel->local_max_transmit;
                            channelStateVarSetFlag(channel, L2CAP_CHANNEL_STATE_VAR_SEND_CONF_RSP_REJECTED);
                            channelStateVarSetFlag(channel, L2CAP_CHANNEL_STATE_VAR_SEND_CONF_RSP_ERTM);
                        }
                        break;
                    }
                    /* Standard enhanced control uses six-bit sequence numbers.
                     * MPS below 3 cannot carry this implementation's SDU header
                     * plus payload. Reject, do not silently round peer limits. */
                    if (command[pos + 1] == 0 || command[pos + 1] > 63 ||
                        little_endian_read_16(command, pos + 7) < 3){
                        channel->state = L2CAP_STATE_WILL_SEND_DISCONNECT_REQUEST;
                        return;
                    }
                    // Store remote config""")
    parsers = replace_once(parsers,
        "uint16_t num_bytes_per_tx_buffer_before = sizeof(l2cap_ertm_tx_packet_state_t) + channel->remote_mps;",
        "uint32_t num_bytes_per_tx_buffer_before = sizeof(l2cap_ertm_tx_packet_state_t) + channel->remote_mps;")
    parsers = replace_once(parsers,
        "uint16_t tx_storage = channel->num_tx_buffers * num_bytes_per_tx_buffer_before;",
        "uint32_t tx_storage = channel->num_tx_buffers * num_bytes_per_tx_buffer_before;")
    parsers = replace_once(parsers,
        """                            uint16_t num_bytes_per_tx_buffer_now = sizeof(l2cap_ertm_tx_packet_state_t) + channel->remote_mps;
                            channel->num_tx_buffers = tx_storage / num_bytes_per_tx_buffer_now;""",
        """                            uint32_t num_bytes_per_tx_buffer_now = sizeof(l2cap_ertm_tx_packet_state_t) + channel->remote_mps;
                            /* PicoW: retain a representable six-bit TX window;
                             * calculate before narrowing to uint8_t. */
                            uint32_t num_tx_buffers = tx_storage / num_bytes_per_tx_buffer_now;
                            channel->num_tx_buffers = (uint8_t) btstack_min(num_tx_buffers, 63u);""")
    parsers = replace_once(parsers,
        """                        uint16_t effective_mtu = channel->remote_mps * channel->num_tx_buffers - 2;
                        channel->remote_mtu    = btstack_min( effective_mtu, channel->remote_mtu);""",
        """                        uint32_t effective_mps = btstack_min(channel->remote_mps, channel->local_mps);
                        uint32_t effective_mtu = channel->num_tx_buffers == 1 ? effective_mps :
                                                effective_mps * channel->num_tx_buffers - 2u;
                        if (effective_mtu < 48u){
                            channel->state = L2CAP_STATE_WILL_SEND_DISCONNECT_REQUEST;
                            return;
                        }
                        channel->remote_mtu = (uint16_t) btstack_min(effective_mtu, channel->remote_mtu);""")
    source = source[:start] + parsers + source[end:]
    source = replace_once(source,
        """                channelStateVarSetFlag(channel, L2CAP_CHANNEL_STATE_VAR_SENT_CONF_REQ);
                channel->local_sig_id = l2cap_next_sig_id();""",
        """                channelStateVarSetFlag(channel, L2CAP_CHANNEL_STATE_VAR_SENT_CONF_REQ);
#ifdef ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
                channelStateVarClearFlag(channel, PICOW_L2CAP_CONF_REQ_ERTM);
                if (channel->mode == L2CAP_CHANNEL_MODE_ENHANCED_RETRANSMISSION){
                    channelStateVarSetFlag(channel, PICOW_L2CAP_CONF_REQ_ERTM);
                }
#endif
                channel->local_sig_id = l2cap_next_sig_id();""")
    source = replace_once(source,
        """                    if (result == L2CAP_CONF_RESULT_UNACCEPTABLE_PARAMETERS){
                        // On 'Reject - Unacceptable Parameters' to our Basic mode request, disconnect""",
        """                    if (result == L2CAP_CONF_RESULT_UNACCEPTABLE_PARAMETERS &&
                        !(channel->state_var & PICOW_L2CAP_CONF_REQ_ERTM)){
                        // On 'Reject - Unacceptable Parameters' to our Basic mode request, disconnect""")
    source = replace_once(source,
        """                    result = little_endian_read_16 (command, L2CAP_SIGNALING_COMMAND_DATA_OFFSET+4);
                    l2cap_stop_rtx(channel);
                    l2cap_signaling_handle_configure_response(channel, result, command);""",
        """                    /* PicoW: ignore stale/duplicate responses, including
                     * duplicates received before the Basic retry is sent. */
                    if (identifier != channel->local_sig_id ||
                        !(channel->state_var & L2CAP_CHANNEL_STATE_VAR_SENT_CONF_REQ)) break;
                    result = little_endian_read_16 (command, L2CAP_SIGNALING_COMMAND_DATA_OFFSET+4);
                    l2cap_stop_rtx(channel);
                    l2cap_signaling_handle_configure_response(channel, result, command);
                    if (result != L2CAP_CONF_RESULT_PENDING){
                        channelStateVarClearFlag(channel, L2CAP_CHANNEL_STATE_VAR_SENT_CONF_REQ);
                    }""")
    source = replace_once(source,
        """                        case 0: // success
                            channelStateVarSetFlag(channel, L2CAP_CHANNEL_STATE_VAR_RCVD_CONF_RSP);""",
        """                        case 0: // success
#ifdef ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
                            /* A crossed Basic request can supersede our ERTM
                             * proposal even when its response was positive. */
                            if (channel->mode == L2CAP_CHANNEL_MODE_BASIC &&
                                (channel->state_var & PICOW_L2CAP_CONF_REQ_ERTM)){
                                channelStateVarSetFlag(channel, L2CAP_CHANNEL_STATE_VAR_SEND_CONF_REQ);
                                break;
                            }
#endif
                            channelStateVarSetFlag(channel, L2CAP_CHANNEL_STATE_VAR_RCVD_CONF_RSP);""")
    source = replace_once(source,
        """                            // retry on negative result
                            channelStateVarSetFlag(channel, L2CAP_CHANNEL_STATE_VAR_SEND_CONF_REQ);
                            break;
                    }
                    break;""",
        """                            // retry on negative result
                            channelStateVarSetFlag(channel, L2CAP_CHANNEL_STATE_VAR_SEND_CONF_REQ);
                            break;
                    }
#ifdef ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
                    if (result != L2CAP_CONF_RESULT_PENDING){
                        channelStateVarClearFlag(channel, PICOW_L2CAP_CONF_REQ_ERTM);
                    }
#endif
                    break;""")
    source = replace_once(source,
        """                    uint16_t effective_mps = btstack_min(channel->remote_mps, channel->local_mps);
                    uint16_t usable_mtu = channel->num_tx_buffers == 1 ? effective_mps : channel->num_tx_buffers * effective_mps - 2;""",
        """                    uint32_t effective_mps = btstack_min(channel->remote_mps, channel->local_mps);
                    uint32_t usable_mtu = channel->num_tx_buffers == 1 ? effective_mps : channel->num_tx_buffers * effective_mps - 2u;""")
    # A parser's fail-closed transition must not be overwritten by stale flags
    # from an earlier round of the configuration exchange.
    source = replace_once(source,
        "    if (channel->state == L2CAP_STATE_OPEN) return 0;",
        "    if (channel->state != L2CAP_STATE_CONFIG) return 0;")
    return source + r'''
/* PicoW overlay extension: inspect the actual negotiated transport, never a
 * requested configuration or stale ERTM lease ID. Read-only, single-loop API.
 * 0 = BASIC, 3 = ERTM, 0xff = unknown/not an open Classic channel. */
uint8_t picow_l2cap_channel_mode(uint16_t local_cid);
uint8_t picow_l2cap_channel_mode(uint16_t local_cid){
#ifdef ENABLE_CLASSIC
    l2cap_channel_t * channel = l2cap_get_channel_for_local_cid(local_cid);
    if (channel == NULL || channel->channel_type != L2CAP_CHANNEL_TYPE_CLASSIC ||
        channel->state != L2CAP_STATE_OPEN) return 0xffu;
#ifdef ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
    if (channel->mode != L2CAP_CHANNEL_MODE_BASIC &&
        channel->mode != L2CAP_CHANNEL_MODE_ENHANCED_RETRANSMISSION) return 0xffu;
    return (uint8_t) channel->mode;
#else
    return 0u;
#endif
#else
    UNUSED(local_cid);
    return 0xffu;
#endif
}
'''


def rfcomm(source):
    source = replace_once(source,
        "modem_status_event[1] = sizeof(event) - 2;",
        "modem_status_event[1] = sizeof(modem_status_event) - 2;")
    source = replace_once(source,
        """            // free newly created multiplexer
            btstack_memory_rfcomm_multiplexer_free(multiplexer);""",
        """            // PicoW: unlink the published multiplexer before freeing it.
            rfcomm_multiplexer_free(multiplexer);""")
    source = replace_once(source,
        """            if (status) {
                btstack_memory_rfcomm_multiplexer_free(multiplexer);
                btstack_memory_rfcomm_channel_free(channel);
                return status;
            }""",
        """            if (status) {
                /* PicoW: no L2CAP channel exists on synchronous failure, so
                 * neither a close event nor an ERTM release event will arrive.
                 * Unpublish both objects before releasing the caller's lease. */
#ifdef RFCOMM_USE_ERTM
                const uint16_t ertm_id = multiplexer->ertm_id;
#endif
                rfcomm_channel_finalize(channel);
                rfcomm_multiplexer_free(multiplexer);
                if (out_rfcomm_cid) *out_rfcomm_cid = 0;
#ifdef RFCOMM_USE_ERTM
                if (ertm_id && rfcomm_ertm_released_callback){
                    (*rfcomm_ertm_released_callback)(ertm_id);
                }
#endif
                return status;
            }""")
    return source + r'''
/* PicoW overlay extension; the L2CAP counterpart belongs to the same generated
 * overlay pair. These explicit C prototypes require no installed SDK edit. */
uint8_t picow_l2cap_channel_mode(uint16_t local_cid);
uint8_t picow_rfcomm_channel_mode(uint16_t rfcomm_cid);
uint8_t picow_rfcomm_channel_mode(uint16_t rfcomm_cid){
    rfcomm_channel_t * channel = rfcomm_channel_for_rfcomm_cid(rfcomm_cid);
    if (channel == NULL || channel->state != RFCOMM_CHANNEL_OPEN ||
        channel->multiplexer == NULL ||
        channel->multiplexer->state != RFCOMM_MULTIPLEXER_OPEN) return 0xffu;
    return picow_l2cap_channel_mode(channel->multiplexer->l2cap_cid);
}
'''


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in EXPECTED:
        raise SystemExit("usage: patch_spp_ertm_sdk.py {l2cap|rfcomm} INPUT OUTPUT")
    kind, original, output = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
    if original.resolve() == output.resolve() or (output.exists() and output.samefile(original)):
        raise SystemExit("Input and output must differ; SDK originals are never edited.")
    raw = original.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED[kind]:
        raise SystemExit(f"Unsupported {kind} source SHA256 {digest}; expected {EXPECTED[kind]}. No output written.")
    result = globals()[kind](raw.decode("utf-8"))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(result, encoding="utf-8")
    print(f"{REVISION} {kind}: {digest} -> {hashlib.sha256(result.encode()).hexdigest()}")


if __name__ == "__main__":
    main()
