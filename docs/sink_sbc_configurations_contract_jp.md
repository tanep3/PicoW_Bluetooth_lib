# 公開SinkのSBC設定契約

公開A2dpSinkReceiverは44.1/48kHz、mono/dual/stereo/joint、4/8/12/16 blocks、4/8 subbands、loudness/SNRの128形式に対応します。任意の16/32kHzや別codecは広告しません。これは現在の実装の契約であり、過去の試験記録を書き換えるものではありません。

能力広告は `3f ff 02 56`、bitpool範囲2..86です。範囲外、上下逆転、合法frameを作れない最小値、未知のenum、channel数不一致を拒否します。選択した上限値でも形式次第で使用できない場合があります。各frameで選択範囲、全形式フィールド、構文上限（mono/dualは16×subbands、stereo/jointは32×subbands）、bitrate上限（mono320kbps、2channel512kbps）を検証します。

frame長は4+subbands×channels/2に、mono/dualならceil(blocks×channels×bitpool/8)、stereo/jointならceil((blocks×bitpool+joint×subbands)/8)を足します。bitrateはframe_bytes×8×rate <= 上限×blocks×subbandsで比較し、切り捨てによる超過受理を防ぎます。今回受理する最大frame長は185bytes。固定storageは構文上限524bytesに余裕を取り、decoder側に16bytesのprefetch padding、再構成側に13bytesの内部RTP headerを別途確保します。コピーpacket上限1024bytes、packet内15frames上限は従来通りです。

PCMはsigned16-bit、native-endian、packed形式です。monoはstride1、2channelはstride2。callbackのframesと返却値は1channelあたりのsample数で16..128（blocks×subbands）です。int16要素数はframes×channels。RTP timestampもpacket内frame数×blocks×subbandsだけ進み、fragment途中では進みません。callback storageはその呼び出し中だけ有効です。既存BoundedPcmSink例はmono/stereo両対応でpull結果にformatを返します。decoder再設定/復旧時のhistory初期化と、fragmentのloss/lifecycle境界は維持しています。

内部Configの4-field aggregateは互換を維持し、追加項目は16blocks/8subbands/loudnessが既定値です。公開snapshotは実際のchannelsとSBC blocks/subbands/allocation/min/max bitpoolを返します。allocationは0=loudness、1=SNR、modeは0=mono、1=dual、2=stereo、3=jointです。未設定snapshot値は交渉成立を意味しません。stereo専用consumerはchannelsを確認し、非対応の場合0framesを返してください。

## 生設定検証の後続対応と残る境界

[生設定guard](sink_raw_configuration_contract.md)はSINGLE_PACKET設定を解析・正規化前に検査し、Sink RECONFIGUREのvalidatorと保持codecの安定buffer参照を提供します。通常のSink CMake helperが固定ハッシュの生成コピーを選びます。手動ソース追加では適用されず、same-peer実験overlayとの合成は非対応です。[callback拒否後の回復](sink_validator_recovery_contract_jp.md)は記載したSET拒否lifecycleを維持します。より広いsignaling lifecycleとsignaling分割処理は未完です。

host上の実SDK round tripやfragment PCM一致は、独立reference vector精度、SBC14-bit適合、無線相互接続、実時間処理、音質、実機性能を保証しません。根拠、式、再現方法は[英語契約](sink_sbc_configurations_contract.md)と試験記録を参照してください。

[検証範囲](validation_jp.md)
