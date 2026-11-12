#pragma once
#include "ITransport.h"
#include <string>

class WebSocketTransport : public ITransport {
public:
    WebSocketTransport(const std::string& uri);
    bool isReady() const override;
    bool send(const uint8_t* data, size_t size) override;

private:
    std::string uri_;
    // WebSocket接続・CBOR処理（後で実装）
};
