#include "plugins/FileOutput/FileOutputPlugin.h"
#include "transports/FileTransport.h"
#include "AudioPipeline.h"
#include <stdexcept>
#include <memory> // std::unique_ptr のために必要

// make_unique_base ヘルパー関数
template <typename Base, typename Derived, typename... Args>
std::unique_ptr<Base> make_unique_base(Args&&... args) {
    return std::unique_ptr<Base>(static_cast<Base*>(new Derived(std::forward<Args>(args)...)));
}

FileOutputPlugin::FileOutputPlugin() = default;

FileOutputPlugin::~FileOutputPlugin() = default;

void FileOutputPlugin::setup(IAudioSource* source, IAudioEncoder* encoder, const std::string& path) {
    if (!source || !encoder || path.empty()) {
        throw std::runtime_error("[FileOutputPlugin] Invalid arguments for setup.");
    }
    source_ = source;
    encoder_ = encoder;
    transport_ = make_unique_base<ITransport, FileTransport>(path, FileTransport::Mode::WriteBinary);

    // Configure the global pipeline
    AudioPipeline::getInstance().configure(source_, encoder_, transport_.get());
}

void FileOutputPlugin::connect() {
    // No-op for file transport
}

void FileOutputPlugin::loop() {
    AudioPipeline::getInstance().loop();
}
