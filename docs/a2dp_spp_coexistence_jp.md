# A2DPとSPPを同じアプリで使う

[English](a2dp_spp_coexistence.md)

A2DPと2本のSPPルートは一つのBluetoothRuntimeと無線資源を共有します。各機能を同じメインループからpollし、SPPのメッセージ区切り、再送、送信量の制御はアプリ側で実装してください。

## 組み込みの入口

[DeveloperSamples](../examples/DeveloperSamples/README_jp.md)のA2DP＋SPP例から始めます。AVRCPも加える場合は[3機能のサンプル](../examples/DeveloperSamples/a2dp_spp_avrcp.cpp)を参考にしてください。既存ACLを使うA2DP接続は明示的な`connectOverExistingAcl()`で行い、通常の`connect()`による探索と区別します。

固定した相手との短時間・一定負荷の共存確認はありますが、すべての負荷での音質、遅延、長時間安定性や再接続を保証しません。送信バイト数の一致やSBCのデコード成功だけから、途切れのない音声や音質を判断しないでください。過去には厳密な音声・継続時間の条件を満たさない試験もあり、後の通信成功でその失敗を取り消すことはできません。[検証範囲](validation_jp.md)も確認してください。

## 送信延期とバッファの所有

CAN_SEND_NOWは、後の送信まで共有HCI bufferを予約する通知ではない。
このSDKのpayload_rtp helperはbuffer予約とRTP連番更新の後に送信するため、
実行時に空きがなくなった場合の再試行にはそのまま使えない。

現adapterは公開raw media packet APIを使う。basic L2CAP経路は実際の空き・remote MTUを
確認してからbufferを予約する。adapterがRTP headerを作り、成功時だけ連番・timestamp・packet数を進める。
RTP 12 byte、SBC payload header 1 byte、完全なSBC frame列を容量計算に含める。

AudioPipelineは未送信のencoded batchを最大1個保持する。延期中に新しいPCMを読み直したり、
同じPCMを再encodeしたりしない。部分的なPCM供給も完成まで保持する。
MediaDeferredは送信数・sample数・pacingを進めず、後のTickで新しい送信可能通知を要求する。
configure、stream disable、世代変更では旧batchを破棄する。
source/encoder/transport callbackからの同期configureは、使用中bufferを解放しないよう変更前に拒否する。

`A2dpSourcePlayer::diagnostics()` は実transportのRTP timestamp、次のsequence、sent packets、
last send status、media deferrals、pending encoded bytesを返す。
公開snapshotのpacket/sample数と別に照合できる。

遅延が20msを超えただけでmedia期限を現在へrebaseすることはしない。
44.1/48kHzの有理数media clockの遅れを保持し、source sampleを飛ばさない。
新しいcreditの下で公開pollあたり最大1packetとし、未送信encoded batchは最大1個のまま。
`next_due_us/debt_us/max_lateness_us/pacing_rebases/pacing_discarded_us` 等で観察する。
平均供給rateの回復と、均一なpacket間隔・連続した受信PCMは別の条件である。
大きな遅延が受信underflowを起こさない保証や、catch-up時の競合がない保証ではない。

## 適用範囲

Pico SDK 2.1.1 / BTstack 1.6.2、単一main loop、CYW43 polling、basic AVDTPの経路を検証対象とする。
raw APIの空き確認からreserve/prepared-sendまで、この構成ではyieldや別ownerによる割込みを挟まない。
SDKが所有するbufferを、単なるreservedフラグだけで独自にreleaseする処理は入れない。
任意transport、ERTM、別core/IRQからの同時呼出しへの保証ではない。

CYW43の実hardware-write errorはSDK assertionなどの終端障害経路であり、通常のresource延期とは異なる。
その全般的な回復は保証しない。SDK callbackには世代番号がなく、同一CID/SEIDを再利用した後の
古いcallbackを完全に識別する保証もない。
