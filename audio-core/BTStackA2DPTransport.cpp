#include "BTStackA2DPTransport.h"
#include "a2dp_source.h"
#include <cstring>
#include <algorithm>

BTStackA2DPTransport::BTStackA2DPTransport(uint16_t cid, uint8_t seid, int mtu, uint32_t timestamp, uint8_t* storage, uint16_t)
    : a2dp_cid_(cid), local_seid_(seid), max_media_payload_size_(mtu), rtp_timestamp_(timestamp), sbc_storage_(storage),
      storage_capacity_(mtu > 0 ? static_cast<size_t>(mtu) : 0), packet_(storage_capacity_ + 12) {}
void BTStackA2DPTransport::configure(uint16_t cid, uint8_t seid, int mtu) {
    setStreaming(false);
    a2dp_cid_ = cid; local_seid_ = seid; max_media_payload_size_ = std::min<int>(mtu, storage_capacity_);
    rtp_timestamp_ = 0; sequence_ = 0; last_status_ = 0;
}
void BTStackA2DPTransport::abandonPending() {
    ++generation_; // Also invalidates a send returning through a reset callback.
    if (fragment_offset_) ++abandoned_partial_frames_;
    fragment_length_ = fragment_offset_ = 0;
}
void BTStackA2DPTransport::setStreaming(bool active) {
    abandonPending();
    ++generation_;
    last_status_ = 0;
    streaming_ = active; requested_ = false; can_send_media_ = false;
}
void BTStackA2DPTransport::requestCanSendNow() {
    if (!streaming_ || !a2dp_cid_ || requested_ || can_send_media_) return;
    requested_ = true;
    a2dp_source_stream_endpoint_request_can_send_now(a2dp_cid_, local_seid_);
}
void BTStackA2DPTransport::onCanSendNow() {
    onCanSendNow(generation_);
}
void BTStackA2DPTransport::onCanSendNow(uint32_t generation) {
    if (generation != generation_) return;
    if (streaming_ && requested_) can_send_media_ = true;
    requested_ = false;
}
bool BTStackA2DPTransport::isReady() const { return streaming_ && can_send_media_; }
bool BTStackA2DPTransport::send(const uint8_t* data, size_t length) {
    // The boolean API cannot represent partial progress: never emit a prefix.
    if (fragment_length_ || max_media_payload_size_ < 2 ||
        length > static_cast<size_t>(max_media_payload_size_ - 1)) return false;
    return sendResult(data, length) == SendResult::Sent;
}
ITransport::SendResult BTStackA2DPTransport::sendResult(const uint8_t* data, size_t length) {
    last_status_ = 0;
    if (!isReady()) return SendResult::Deferred;
    if (!data || !sbc_storage_ || !length || max_media_payload_size_ < 2 ||
        length > 65522) return SendResult::Failed;
    // Parse complete standard SBC frames; RTP timestamp counts samples per channel.
    size_t offset = 0; unsigned frames = 0; uint32_t samples = 0;
    while (offset < length) {
        if (length-offset < 4 || data[offset] != 0x9c || ++frames > 15) return SendResult::Failed;
        const unsigned h = data[offset+1], pool = data[offset+2];
        const unsigned blocks = 4 * (((h >> 4) & 3) + 1);
        const unsigned mode = (h >> 2) & 3;
        const unsigned bands = (h & 1) ? 8 : 4;
        const unsigned channels = mode == 0 ? 1 : 2;
        if (pool < 2 || pool > 250 || pool > (mode < 2 ? 16 : 32)*bands) return SendResult::Failed;
        const unsigned bits = mode < 2 ? blocks*channels*pool : blocks*pool + (mode == 3 ? bands : 0);
        const size_t size = 4 + bands*channels/2 + (bits+7)/8;
        if (size > length-offset) return SendResult::Failed;
        offset += size; samples += blocks*bands;
    }
    const size_t chunk = static_cast<size_t>(max_media_payload_size_ - 1);
    const bool fragmented = length > chunk;
    if (fragment_length_ && (length != fragment_length_ ||
        std::memcmp(data, fragment_frame_.data(), length))) return SendResult::Failed;
    if (fragmented) {
        // Only the player's explicitly supported SBC format, one frame, 2..15 packets.
        if (!fragmentation_enabled_ || frames != 1 || length > fragment_frame_.size() ||
            (data[1] != 0xb9 && data[1] != 0xf9) || data[2] > 53 ||
            (length + chunk - 1) / chunk > 15) return SendResult::Failed;
        if (!fragment_length_) {
            std::memcpy(fragment_frame_.data(), data, length);
            fragment_length_ = length;
        }
    }
    const size_t amount = fragmented ? std::min(chunk, length - fragment_offset_) : length;
    const bool final = !fragmented || fragment_offset_ + amount == length;
    // Use the public raw-packet API. The installed SDK payload_rtp helper
    // reserves the shared buffer and advances its sequence BEFORE checking
    // live credits. l2cap_send (used by this API) checks live readiness first.
    // In the supported CYW43 polling/basic-mode path no callback/yield occurs
    // between that check, reserve and prepared send. Do not generalize this to
    // ERTM/arbitrary asynchronous ports or try to release another user's buffer.
    auto* packet = packet_.data();
    packet[0] = 0x80; packet[1] = 0x60;
    packet[2] = static_cast<uint8_t>(sequence_ >> 8);
    packet[3] = static_cast<uint8_t>(sequence_);
    for (unsigned i = 0; i < 4; ++i)
        packet[4+i] = static_cast<uint8_t>(rtp_timestamp_ >> (24 - 8*i));
    packet[8] = 0x11; packet[9] = 0x22; packet[10] = 0x33; packet[11] = 0x44;
    packet[12] = fragmented
        ? static_cast<uint8_t>(0x80 | (!fragment_offset_ ? 0x40 : 0) | (final ? 0x20 : 0) |
                               ((length - fragment_offset_ + chunk - 1) / chunk))
        : static_cast<uint8_t>(frames);
    std::memmove(packet+13, fragmented ? fragment_frame_.data() + fragment_offset_ : data, amount);
    can_send_media_ = false; // Consume the hint, including on deferral/failure.
    const auto generation = generation_;
    const auto status = a2dp_source_stream_send_media_packet(a2dp_cid_, local_seid_,
        packet, static_cast<uint16_t>(amount+13));
    // A synchronous stop/reset must not mutate the new session's accounting.
    if (generation != generation_) return SendResult::Failed;
    last_status_ = status;
    if (status == BTSTACK_ACL_BUFFERS_FULL) return SendResult::Deferred;
    if (status) return SendResult::Failed;
    ++sent_packets_;
    ++sequence_;
    if (fragmented && !final) {
        fragment_offset_ += amount;
        return SendResult::Progress;
    }
    fragment_length_ = fragment_offset_ = 0;
    rtp_timestamp_ += samples;
    return SendResult::Sent;
}
