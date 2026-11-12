#include "btstack_config.h" // Added
#include "plugins/BTStack/BTStackA2DPPlugin.h"
#include "AudioPipeline.h"
#include "BTStackA2DPTransport.h"
// #include "btstack_interface.h" // Not directly used, replaced by specific BTstack headers
#include "a2dp_source.h" // For a2dp_source_establish_stream, etc.
#include <memory>

BTStackA2DPPlugin::BTStackA2DPPlugin(IAudioSource* source, IAudioEncoder* encoder, ITransport* transport, btstack_context_t* context)
    : source_(source), encoder_(encoder), transport_(transport), context_(context) {
    // Dependencies are now injected via constructor
}

BTStackA2DPPlugin::~BTStackA2DPPlugin() = default;

void BTStackA2DPPlugin::setup() {
    if (!transport_) {
        // transportがセットされていない場合はセットアップを行わず警告
        // ここでLCDや標準出力、log_xxxなどで「未設定」表示も可
        // 例: printf("BTStackA2DPPlugin::setup(): transport未設定!\n");
        return;
    }
    // Configure the global pipeline
    AudioPipeline::getInstance().configure(source_, encoder_, transport_.get());
}

void BTStackA2DPPlugin::connect() {
    // Connection logic will be handled by a higher-level BTstack initialization module
    // For now, this method does nothing.
}

void BTStackA2DPPlugin::loop() {
    // Drive the audio pipeline when transport is ready
    if (transport_ && source_ && encoder_) {
        AudioPipeline::getInstance().loop();
    }
}

void BTStackA2DPPlugin::setTransport(std::unique_ptr<ITransport> transport) {
    transport_ = std::move(transport);
    // If dependencies are ready, (re)configure pipeline now
    if (transport_ && source_ && encoder_) {
        AudioPipeline::getInstance().configure(source_, encoder_, transport_.get());
    }
}
