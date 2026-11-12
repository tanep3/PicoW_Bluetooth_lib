// AudioSource / Encoder / Transport / Streamer を、動的に構成・切り替え可能なプラグイン方式でつなぐ司令塔
#include <cstdio>
#include "AudioManager.h"
#include "btstack_run_loop.h"
#include "IAddressConnectable.h"
#include "DebugLog.h"

void AudioManager::addPlugin(IPlugin* plugin) {
    plugins.push_back(plugin);
}

void AudioManager::setupAll() {
    resetConnectIndex();
    for (auto plugin : plugins) {
        plugin->manager_ = this;
        plugin->setup();
    }
    hci_power_control(HCI_POWER_ON);
}

void AudioManager::connectAll() {
    for (auto plugin : plugins) {
        plugin->connect();
    }
}

// void AudioManager::connectAll(bd_addr_t addr) {
//     for (auto plugin : plugins) {
//         if (auto p = dynamic_cast<IAddressConnectable*>(plugin)) {
//             p->connect(addr); // できるやつだけ呼ぶ
//         }
//     }
// }
void AudioManager::connectAllSequential(bd_addr_t addr) {
    // すべて終わったらreturn
    if (connect_idx >= plugins.size()) return;
    char msg[64];
    snprintf(msg, sizeof(msg), "connectAllSequential:%d", (int)connect_idx);
    PWB_LOG_MSG(msg);
    PWB_SLEEP_MS(1000);
    // connect_idx番目がIAddressConnectableならconnect呼ぶ
    if (auto p = dynamic_cast<IAddressConnectable*>(plugins[connect_idx])) {
        p->connect(addr); // 実際の接続
        connect_idx++;
        // このconnectの「完了コールバック」で connectAllSequential(addr) を再度呼ぶ！
    } else {
        // 対象でなければ次へ進める
        connect_idx++;
        connectAllSequential(addr);
    }
}

void AudioManager::resetConnectIndex() { connect_idx = 0; }

void AudioManager::loopAll() {
    for (auto plugin : plugins) {
        plugin->loop();
    }
}

void AudioManager::execute() {
    btstack_run_loop_execute();
}
