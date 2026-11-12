#pragma once

#include "IPlugin.h"
#include <memory>

class IAudioSource;
class IAudioEncoder;
class ITransport;

class NullPlugin : public IPlugin {
public:
    NullPlugin(IAudioSource* source, IAudioEncoder* encoder);
    ~NullPlugin() override;

    void setup() override;
    void connect() override;
    void loop() override;

private:
    IAudioSource* source_ = nullptr;
    IAudioEncoder* encoder_ = nullptr;
    std::unique_ptr<ITransport> transport_;
};
