// AudioSource / Encoder / Transport / Streamer を、動的に構成・切り替え可能なプラグイン方式でつなぐ司令塔
#pragma once
#include <cstdio>
#include "IPlugin.h"
#include <vector>
#include <memory>

class AudioManager {
public:
    void addPlugin(IPlugin* plugin);
    void setupAll();
    void connectAll();
    // void connectAll(bd_addr_t addr);
    void connectAllSequential(bd_addr_t addr);
    void resetConnectIndex();
    void loopAll();
    void execute();

private:
    std::vector<IPlugin*> plugins;
    size_t connect_idx;
};
