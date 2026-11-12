// 音声伝送の中核（実装）。
// 「入力 → エンコード → 送信」 の連携を自動的に行う制御クラス。

#include "AudioPipeline.h"
#include <memory>
#include <stdexcept>

// シングルトンインスタンスの取得
AudioPipeline& AudioPipeline::getInstance() {
    static AudioPipeline instance; // Guaranteed to be destroyed, instantiated on first use.
    return instance;
}

// プライベートコンストラクタ
AudioPipeline::AudioPipeline() = default;

// デストラクタでメモリ解放
AudioPipeline::~AudioPipeline() {
    delete[] pcm_buffer_;
    delete[] encoded_buffer_;
}

void AudioPipeline::configure(IAudioSource* source, IAudioEncoder* encoder, ITransport* transport) {
    // Callbacks may recursively pump, but must not replace storage still in use
    // by the source, encoder or transport. Fail before mutating configuration.
    if (pumping_) throw std::logic_error("AudioPipeline: Cannot configure while pumping.");
    if (!source || !encoder || !transport) {
        throw std::runtime_error("AudioPipeline: Null pointer passed to configure.");
    }

    // Allocate before replacing the live configuration (including on failure).
    const auto samples = encoder->requiredSamples();
    const auto bytes = encoder->maxEncodedSize();
    std::unique_ptr<int16_t[]> pcm(new int16_t[samples * 2]);
    std::unique_ptr<uint8_t[]> encoded(new uint8_t[bytes]);
    // A completed old transport may already have been destroyed. Only a
    // retained encoded batch requires its still-live owner to be abandoned.
    if (pending_bytes_ && transport_) transport_->abandonPending();
    delete[] pcm_buffer_;
    delete[] encoded_buffer_;
    source_ = source;
    encoder_ = encoder;
    transport_ = transport;
    pcm_buffer_samples_ = samples;
    encoded_buffer_size_ = bytes;
    pcm_buffer_ = pcm.release();
    encoded_buffer_ = encoded.release();
    reset();
}

void AudioPipeline::reset() {
    if (transport_) transport_->abandonPending();
    pending_bytes_ = pcm_samples_ = 0;
    transport_generation_ = transport_ ? transport_->generation() : 0;
    ++configuration_generation_;
}

AudioPipeline::Result AudioPipeline::loop() {
    if (pumping_ || !source_ || !encoder_ || !transport_) return Result::Idle;
    struct Guard {
        bool& flag;
        explicit Guard(bool& value) : flag(value) { flag = true; }
        ~Guard() { flag = false; }
    } guard(pumping_);
    if (transport_generation_ != transport_->generation()) reset();
    const auto generation = configuration_generation_;
    auto invalidated = [&] {
        if (generation == configuration_generation_ && transport_generation_ == transport_->generation())
            return false;
        reset();
        return true;
    };
    if (!transport_->isReady()) return Result::Deferred;
    if (!pending_bytes_) {
        const bool available = source_->hasSamples();
        if (invalidated() || !available) return Result::Idle;
        const auto read = source_->read(pcm_buffer_ + pcm_samples_, pcm_buffer_samples_ - pcm_samples_);
        if (invalidated()) return Result::Idle;
        if (read > pcm_buffer_samples_ - pcm_samples_) { reset(); return Result::Failed; }
        pcm_samples_ += read;
        if (pcm_samples_ < pcm_buffer_samples_) return Result::Idle;
        const auto written = encoder_->encode_into(pcm_buffer_, pcm_samples_, encoded_buffer_, encoded_buffer_size_);
        pcm_samples_ = 0;
        if (invalidated()) return Result::Idle;
        pending_bytes_ = written;
        if (!pending_bytes_ || pending_bytes_ > encoded_buffer_size_) { reset(); return Result::Failed; }
    }
    const auto result = transport_->sendResult(encoded_buffer_, pending_bytes_);
    if (invalidated()) return Result::Idle;
    if (result == ITransport::SendResult::Deferred) return Result::Deferred;
    if (result == ITransport::SendResult::Progress) return Result::Progress;
    if (result == ITransport::SendResult::Failed) { reset(); return Result::Failed; }
    pending_bytes_ = 0;
    return Result::Sent;
}
