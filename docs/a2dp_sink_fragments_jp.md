# SBC 分割フレームの有界受信

[English](a2dp_sink_fragments.md) · [PCM の受け取り方](a2dp_sink_pcm_jp.md)

`A2dpSinkReceiver` の本番受信経路に SBC 分割フレームの再構成を追加しました。新しいアプリ API は不要です。従来どおり `poll` を呼び、`IAudioSink::write` 中に借用 PCM をコピーします。全フレームの構造と SDK の CRC/デコード検査が通るまで PCM は出力しません。

## 受信契約

現在の受信機は[拡張SBC設定契約](sink_sbc_configurations_contract_jp.md)に従い、44.1/48 kHz、4種類のchannel mode、4/8/12/16 blocks、4/8 subbands、loudness/SNRに対応します。bitpool広告範囲は2–86ですが、交渉値と形式ごとの制限を適用します。受理する符号化フレームは最大185 bytes、再構成用固定領域は余裕を持たせた524 bytesです。これは**実装全体のRAM使用量ではなく**、RTP/完成packet、decoder padding、欠落管理情報も別に必要です。従来の119-byte限定形式の証拠は過去のcheckpointとして保持します。

一次資料は Bluetooth SIG の [A2DP 1.4.1 §4.3.3–4.3.4](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/a2dp_v1-4-1_1752513648/A2DP_v1.4.1/out/en/index-en.html) と [RFC 3550 §5.1](https://www.rfc-editor.org/rfc/rfc3550.html#section-5.1) です。同じフレームの fragment は同じ timestamp、下位 nibble は現在を含む残数、S/L は先頭/末尾、末尾以外の総 packet size は同一です。RTP sequence は packet ごとに進みます。新 fragment 経路は dynamic PT、M=0、X=0 を要求し、SBC 予約 bit4 は A2DP §1.5.2 に従い無視します。

以下は一般的な規格要件と区別した、この受信機の**有界ポリシー**です。

- 保持する未完成フレームは一つ。空でない 2–15 fragments、16-bit wrap を含む連続 sequence、1 ずつ減る残数を要求します。
- 同じ群の SSRC/PT/CSRC list は不変。RTP padding は拒否し、最終 packet は先頭より大きくできません。
- 再構成領域の上限は524 bytes。完成時に交渉済み形式・bitrate制限を満たすSBC一フレーム（現状最大185 bytes）と厳密一致する必要があり、余剰bytesも拒否します。新しい先頭または非分割 packet は古い未完成データを捨てます。
- 不正 packet、sequence 拒否、古い queue 世代/状態、再設定、START/SUSPEND/STOP/RELEASE、明示 disconnect、HCI 終了で未完成データを破棄します。
- queue overflow/oversize は後続コピー packet に欠落世代を記録します。欠落前に queue に入った先頭を処理し直しても、欠落後の末尾とは結合しません。
- 並べ替え、再送、部分デコード、補間、期限 timer、送信元への backpressure はありません。未完成データは次の packet または状態境界まで固定容量内で残る場合があります。

既存の非分割 parser の予約 bit/RTP extension 方針は変更していません。A2DP/RTP 全体の適合性は主張しません。対応 codec のフレーム上限を超える入力は拒否します。ホストの合成 packet 試験は、実機 MTU 交渉や相手機器の分割動作の確認ではありません。

## 所有権と診断値

BTstack callback の bytes は bounded queue にコピーし、組み立てと完成 packet は受信機の固定領域に保持します。decoder も padding 付き領域へコピーします。PCM ポインタは同期 `write` 中だけの借用です。codec reset 時には外部 synthesis buffer も消去し、前 stream のフィルタ履歴を持ち越しません。

`received_packets` は callback 数、`processed_packets` は未完成 fragment を含む受理 RTP packet 数です。`decoded_frames` は完成・デコード成功時だけ増え、一つの符号化フレームは各channelにつきblocks×subbands（16–128）のPCM framesとなり、channel数は1または2です。timestampの期待値は最後のfragmentでだけそのframe数だけ進め、中間は sequence だけ進めます。不正 fragment 群は既存 `parse_errors`、queue 欠落は queue counters を使います。不正または孤立 fragment の欠落が直ちに `missing_packets` に計上されるとは限りません。sequence は受理 packet によって更新されます。

## 検証と制限

旧限定形式の証跡は公開 facade 診断、本番 callback/queue と実 SDK codec の ASan/UBSan 試験、非ゼロ PCM 全 sample の非分割入力との一致、異常/状態境界、到達可能な enabled Pico W ARM build を含みます。この断片再構成checkpointは、実peerの分割動作、音質、この経路の実機受入を確認したものではありません。実行コマンドは英語版を参照してください。再実行時には毎回新しい build directory 名を使い、過去の object が入ったディレクトリを再利用しないでください。

新しい形式の検証は拡張設定の証跡と[現行契約](sink_sbc_configurations_contract_jp.md)を参照してください。旧checkpointを新形式の証拠として流用しません。

[検証範囲](validation_jp.md)
