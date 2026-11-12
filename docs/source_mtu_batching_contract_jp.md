# A2DP Source の MTU 対応・完全フレーム集約

**過去の実装checkpointを記録した資料です。**

**過去の完全frame checkpointです。** 以下はその時点の契約と証跡を保持しています。現在は[Source SBC断片送信の契約](source_sbc_fragmentation_contract_jp.md)が、1 frameが収まらない場合の拒否とSource固有の最小payload設定を更新しました。1〜4完全frameの経路は維持します。旧証跡・入力manifestは変更せず、現在のrevision2結果と区別してください。

`A2dpSourcePlayer` 本体が、実際のメディア payload に収まる SBC 完全フレームを1〜4個まとめます。サンプルだけの機能ではありません。SBC の断片化や対応コーデック形式の追加は行いません。

対応する stereo・16 blocks・8 subbands では、1フレームは `12 + 2 × negotiated_max_bitpool` byte、各チャンネル128 sample です。44.1/48 kHz でサイズ式は共通です。SDK の media payload 上限と本体の1024 byte上限の小さい方を使用します。この payload は12 byteの RTP header を除き、1 byteの SBC payload header を含みます。個数は `min(4, floor((payload − 1) / frame_bytes))` です。1フレーム＋headerも入らない場合は PCM 読み取り前に失敗します。bitpool53の境界は119/237/355/473 byte、bitpool2なら17 byteから有効です。

## 設定順序とバッファ

- 最初のcodec設定でencoderの正確なサイズを決めます。MTU確定前のバッファは最大4フレームです。
- stream確立時に実payloadから個数を選択し、transport設定とpipelineバッファ再設定を行ってからOpenに移ります。
- codec再設定時は、既存payloadと新しいframeサイズで再計算します。ローカル再設定は最終ACKまで反映しません。停止中に適用し、自動再開は追加しません。
- pipelineが所有するのは途中のPCMバッチと未送信の符号化バッチ各1つです。既存の余剰容量を含むPCM領域は最大4096 byte、符号化領域は最大472 byteです。transportの1024 byte領域と1036 byte packet vectorは固定です。これらは単一構成のbuffer上限で、全体やpeakの上限ではありません。設定変更では旧領域の解放前に新領域を確保するため、一時的に新旧2組が共存します。送信処理の外で設定し、設定変更やresetで古いPCM・未送信データを破棄します。
- Busy時は同一の符号化byte列を保持し、次のpollと新しいcreditで再試行します。再試行のためにPCMを追加消費したり、再符号化したりしません。成功するまではpacket数、RTP sequence/timestamp、受理sample数、時刻を進めません。
- pauseは未送信データを破棄します。成功済みのRTP sequence/timestampはpause・再設定を跨いで維持します。

送信成功時、実際のバッチの各チャンネルsample数を明示してreducerへ渡します。時刻は `samples × 1000000 / rate` と剰余で進めます。適応経路で512を固定加算しません。transportは従来どおり完全フレームを独立して解析し、RTP用sample数を計算します。

## 互換性と範囲

固定4フレームの既存engine consumer向けに `Config.minimum_payload` の既定473を維持します。適応実装を持つplayerだけ17を指定し、実codecサイズも検査します。`Event.accepted_samples` は既存fieldの末尾に追加し、既定512を維持します。適応executorは実数を明示します。成功時に許可する値は128/256/384/512、Deferred時はこの値を使いません。header変更後は再buildが必要で、binary ABI互換の保証はありません。

形式制約、main loop制約、admission/security、同一peer方針、Sinkは変更しません。実機相互運用の合格を意味せず、広範な受入groupを完了扱いにしません。

## 限定検証

検証範囲は固定6ケースです。player/pipeline/transportと実BTstack SBC encoderを使い、runtimeとSDK eventはstubです。compile前に非system推移依存のhashを固定し、buildとlogを保持します。ASanを適用し、project C++にはUBSanも適用します。固定小数点のsigned shift等を使う上流SBC CはUBSanのみ除外し、実行環境の制約でleak検査は無効です。

1〜4frame、サイズ不足、低bitpoolの最小サイズ、wire/RTP/sample/有理数時刻、同一byte列のDeferred再試行を検査します。237 byteケースではpauseによる未送信破棄と、44.1kHz・bitpool2へ再設定後の4frameも確認します。全組合せ探索、不正入力群、旧Source lifecycle一括検査、無線試験は実行しません。offline ARM compile/linkもbuild根拠のみで、flashや実機Bluetooth実行はしません。


同梱サンプルのビルドと実行可能な検査手順は[ビルドガイド](build_guide_jp.md#compile-only)を参照してください。上記の機能別検証範囲をすべて再実行する手順ではありません。
