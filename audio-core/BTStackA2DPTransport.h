#pragma once
#include "ITransport.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>

// All methods run on the BTstack run loop. Storage capacity must be >= MTU.
// Retry safety is scoped to the SDK's CYW43 polling/basic AVDTP path.
class BTStackA2DPTransport : public ITransport {
public:
    BTStackA2DPTransport(uint16_t cid, uint8_t seid, int mtu, uint32_t timestamp, uint8_t* storage, uint16_t count);
    void configure(uint16_t cid, uint8_t seid, int mtu);
    void setStreaming(bool active);
    void requestCanSendNow();
    // Caller gates SDK callbacks by CID/SEID and outstanding request. Raw SDK
    // events have no generation: a delayed event after ID reuse is ambiguous.
    void onCanSendNow();
    void onCanSendNow(uint32_t generation);
    void clearCanSend() { can_send_media_ = false; }
    bool isReady() const override;
    bool send(const uint8_t* data, size_t length) override;
    SendResult sendResult(const uint8_t* data, size_t length) override;
    // Opt in only for sendResult consumers that handle Progress. Call while stopped.
    void enableSbcFragmentation(bool enabled) { abandonPending(); fragmentation_enabled_ = enabled; }
    void abandonPending() override;
    uint32_t abandonedPartialFrames() const { return abandoned_partial_frames_; }
    uint32_t generation() const override { return generation_; }
    uint16_t sequenceNumber() const { return sequence_; }
    uint32_t sentPackets() const { return sent_packets_; }
    uint8_t lastSendStatus() const { return last_status_; }
    uint32_t timestamp() const { return rtp_timestamp_; }
private:
    uint16_t a2dp_cid_;
    uint8_t local_seid_;
    int max_media_payload_size_;
    uint32_t rtp_timestamp_;
    uint8_t* sbc_storage_;
    const size_t storage_capacity_;
    // One bounded wire packet, including RTP. Allocated only at construction.
    std::vector<uint8_t> packet_;
    uint16_t sequence_ = 0;
    uint32_t generation_ = 0;
    uint32_t sent_packets_ = 0;
    uint8_t last_status_ = 0;
    // One supported frame; no allocations in pumping and no borrowed byte pointers.
    std::array<uint8_t, 118> fragment_frame_{};
    size_t fragment_length_ = 0, fragment_offset_ = 0;
    uint32_t abandoned_partial_frames_ = 0;
    bool fragmentation_enabled_ = false;
    bool streaming_ = false;
    bool requested_ = false;
    bool can_send_media_ = false;
};
