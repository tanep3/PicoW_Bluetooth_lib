#pragma once

#include <cstdint>     // uint8_t, uint16_t, uint32_t
#include "btstack.h"   // bd_addr_t  ※必要に応じてbtstack_type.hなどに変更
#include "gap.h"

struct PluginConnectionInfo {
    // ここは必要最小限の汎用フィールド（addressとかroleとか）
    bd_addr_t remote_addr;
    // 追加でプロファイル特有の拡張は、ダウンキャストやvariantで表現
};

struct A2DPConnectInfo : public PluginConnectionInfo {
    uint16_t a2dp_cid;
    uint8_t local_seid;
    int max_media_payload_size;
    uint32_t rtp_timestamp;
    uint8_t* sbc_storage;
    uint16_t sbc_storage_count;
};

struct AVRCPConnectInfo : public PluginConnectionInfo {
    // AVRCP特有のパラメータ
    uint16_t avrcp_cid;
    // ...必要なだけ追加
};

struct HFPConnectInfo : public PluginConnectionInfo {
    uint16_t hfp_cid;
};
