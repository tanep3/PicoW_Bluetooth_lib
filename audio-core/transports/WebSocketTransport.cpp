#include "WebSocketTransport.h"
#include <cstdio>

WebSocketTransport::WebSocketTransport(const std::string& uri)
    : uri_(uri)
{
    // 接続はあとで
}

bool WebSocketTransport::isReady() const {
    // 仮に true を返す（実装予定）
    return true;
}

bool WebSocketTransport::send(const uint8_t* data, size_t size) {
    // ダミー出力
    printf("[WebSocketTransport] Would send %zu bytes to %s\n", size, uri_.c_str());
    return true;
}
