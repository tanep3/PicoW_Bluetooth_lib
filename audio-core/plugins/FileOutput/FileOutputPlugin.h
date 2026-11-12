#pragma once

#include "IPlugin.h"
#include "IAudioSource.h"
#include "IAudioEncoder.h"
#include "ITransport.h"
#include "transports/FileTransport.h"
#include <string>
#include <memory>

class FileOutputPlugin : public IPlugin {
public:
    FileOutputPlugin();
    ~FileOutputPlugin() override;

    void setup(IAudioSource* source, IAudioEncoder* encoder, const std::string& path);
    void connect() override;
    void loop() override;

private:
    IAudioSource* source_ = nullptr;
    IAudioEncoder* encoder_ = nullptr;
    std::unique_ptr<ITransport> transport_;
};
