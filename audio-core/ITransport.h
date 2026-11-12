// ITransport.h
// エンコード済みデータをどこかに送信するための送信インターフェース
#pragma once
#include <cstdint>
#include <cstddef>

class ITransport {
public:
    enum class SendResult { Sent, Deferred, Failed, Progress };
    virtual ~ITransport() = default;

    // バッファ送信処理（true: 成功）
    virtual bool send(const uint8_t* data, size_t length) = 0;

    // Deferred means no bytes accepted by this call; retry the identical batch.
    // Progress means one packet accepted, but the batch is not complete.
    // Both retain ownership with the caller until Sent or abandonment.
    // Legacy transports keep their existing fatal-failure semantics.
    virtual SendResult sendResult(const uint8_t* data, size_t length) {
        return send(data, length) ? SendResult::Sent : SendResult::Failed;
    }
    // Changes invalidate any retained batch from the previous stream/session.
    virtual uint32_t generation() const { return 0; }
    virtual void abandonPending() {}

    // 今送信可能かどうか（ペイロード制限などの都合）
    virtual bool isReady() const = 0;
};
