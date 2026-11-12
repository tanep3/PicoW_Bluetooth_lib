// IAudioEncoder.h
// PCM→圧縮形式（SBCなど）に変換するインターフェース
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>

class IAudioEncoder {
public:
    virtual ~IAudioEncoder() = default;

    // PCM→圧縮へのエンコード処理
    // エンコード後のバイト数を返す
    virtual std::vector<uint8_t> encode(const int16_t* pcm, size_t samples) = 0;

    // エンコーダが必要とする最小サンプル数（例: SBCは128）
    virtual size_t requiredSamples() const = 0;

    // 出力最大サイズ（バッファ確保に使う）
    virtual size_t maxEncodedSize() const = 0;

    // 追加: 事前確保バッファへのエンコード（既定実装はencode()でフォールバック）
    // 戻り値は実際に書き込んだバイト数
    virtual size_t encode_into(const int16_t* pcm, size_t samples, uint8_t* out, size_t out_cap) {
        auto v = encode(pcm, samples);
        size_t n = v.size() < out_cap ? v.size() : out_cap;
        if (n) std::memcpy(out, v.data(), n);
        return n;
    }
};
