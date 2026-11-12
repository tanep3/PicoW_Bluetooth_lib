#pragma once

#include "IPlugin.h"
#include <memory>
#include <string>

class IAudioSource;
class IAudioEncoder;
class ITransport;

class WebSocketOutputPlugin : public IPlugin {
public:
    WebSocketOutputPlugin();
    ~WebSocketOutputPlugin() override;

    void setup(IAudioSource* source, IAudioEncoder* encoder, const std::string& uri);
    void connect() override;
    void loop() override;

private:
    IAudioSource* source_ = nullptr;
    IAudioEncoder* encoder_ = nullptr;
    std::unique_ptr<ITransport> transport_;
};
