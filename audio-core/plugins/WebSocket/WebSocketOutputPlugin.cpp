#include "plugins/WebSocket/WebSocketOutputPlugin.h"
#include "IAudioSource.h"
#include "IAudioEncoder.h"
#include "transports/WebSocketTransport.h"
#include "AudioPipeline.h"
#include <stdexcept>

WebSocketOutputPlugin::WebSocketOutputPlugin() = default;

WebSocketOutputPlugin::~WebSocketOutputPlugin() = default;

void WebSocketOutputPlugin::setup(IAudioSource* source, IAudioEncoder* encoder, const std::string& uri) {
    if (!source || !encoder || uri.empty()) {
        throw std::runtime_error("[WebSocketOutputPlugin] Invalid arguments for setup.");
    }
    source_ = source;
    encoder_ = encoder;
    transport_ = std::make_unique<WebSocketTransport>(uri);

    // Configure the global pipeline
    AudioPipeline::getInstance().configure(source_, encoder_, transport_.get());
}

void WebSocketOutputPlugin::connect() {
    // The WebSocketTransport itself should handle the connection logic.
}

void WebSocketOutputPlugin::loop() {
    AudioPipeline::getInstance().loop();
}
