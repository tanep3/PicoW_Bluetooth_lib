// 音声伝送の中核。
// 「入力 → エンコード → 送信」 の連携を自動的に行う制御クラス。
#pragma once
#include "IAudioSource.h"
#include "IAudioEncoder.h"
#include "ITransport.h"

class AudioPipeline {
public:
    // シングルトンインスタンスを取得
    static AudioPipeline& getInstance();

    // Configure outside loop/callbacks; throws std::logic_error on reentry.
    // Borrowed objects must be alive while used. With pending encoded bytes,
    // the old transport must survive replacement so its batch can be abandoned.
    // With no pending batch configure never touches the old transport.
    // reset/loop still require the currently borrowed transport to be alive.
    void configure(IAudioSource* source, IAudioEncoder* encoder, ITransport* transport);

    enum class Result { Idle, Sent, Deferred, Failed, Progress };
    Result loop();  // 定期的に呼び出す（処理トリガー）

    void reset(); // Discard partial PCM and the retained encoded batch, including partial sends.
    size_t pendingBytes() const { return pending_bytes_; }
    bool hasPendingBatch() const { return pending_bytes_ != 0; }

    // メモリ解放
    ~AudioPipeline();

private:
    // シングルトンパターン
    AudioPipeline();
    AudioPipeline(const AudioPipeline&) = delete;
    AudioPipeline& operator=(const AudioPipeline&) = delete;

    IAudioSource* source_ = nullptr;
    IAudioEncoder* encoder_ = nullptr;
    ITransport* transport_ = nullptr;

    int16_t* pcm_buffer_ = nullptr;
    uint8_t* encoded_buffer_ = nullptr;
    size_t pcm_buffer_samples_ = 0;
    size_t encoded_buffer_size_ = 0;
    size_t pending_bytes_ = 0, pcm_samples_ = 0;
    uint32_t transport_generation_ = 0, configuration_generation_ = 0;
    bool pumping_ = false;
};
