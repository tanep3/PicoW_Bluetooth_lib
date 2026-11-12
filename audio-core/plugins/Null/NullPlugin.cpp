#include "plugins/Null/NullPlugin.h"
#include "IAudioSource.h"
#include "IAudioEncoder.h"
#include "transports/NullTransport.h"
#include "AudioPipeline.h"
#include <stdexcept>
#include <iostream>
#include <cstdio> 
#include "pico/stdlib.h"

NullPlugin::NullPlugin(IAudioSource* source, IAudioEncoder* encoder)
    : source_(source), encoder_(encoder) {
    if (!source_ || !encoder_) {
        throw std::runtime_error("[NullPlugin] Source or encoder cannot be null.");
    }
}

NullPlugin::~NullPlugin() = default;

void NullPlugin::setup() {
    // Create and hold the NullTransport
    transport_ = std::make_unique<NullTransport>();

    // Configure the global pipeline with the transport we own
    AudioPipeline::getInstance().configure(source_, encoder_, transport_.get());
    printf("[NullPlugin] setup() complete.\n");
}

void NullPlugin::connect() {
    printf("[NullPlugin] connect() called (no-op).\n");
}

void NullPlugin::loop() {
    AudioPipeline::getInstance().loop();
}
