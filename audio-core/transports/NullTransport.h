#pragma once
#include "ITransport.h"

class NullTransport : public ITransport {
public:
    bool isReady() const override { return true; }
    bool send(const uint8_t* data, size_t size) override;
};
