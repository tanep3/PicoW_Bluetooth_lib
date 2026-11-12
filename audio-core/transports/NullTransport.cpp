#include "NullTransport.h"
#include <cstdio>

bool NullTransport::send(const uint8_t* data, size_t size) {
    // 送信せずログ出力だけ（任意でバイト数表示）
    printf("[NullTransport] Discarded %zu bytes\n", size);
    return true;
}
