// IAudioSource.h
// 音声入力（マイク・HFPなど）からの PCMバッファ取得インターフェース
#pragma once
#include <cstdint>
#include <cstddef>

class IAudioSource {
public:
    virtual ~IAudioSource() = default;

    // サンプルが読み取り可能か？
    virtual bool hasSamples() const = 0;

    // PCMバッファ読み込み（samples: サンプル数。戻り値は実際に読めた数）
    virtual size_t read(int16_t* buffer, size_t samples) = 0;
};
