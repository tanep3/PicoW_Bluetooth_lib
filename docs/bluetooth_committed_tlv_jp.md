# 任意選択のTLVスナップショット保存

[詳細な英語契約](bluetooth_committed_tlv.md) · [Classicボンド保存](bluetooth_bond_storage_jp.md)

TLVスナップショットを保存する任意選択の機能です。旧journal媒体の修復・移行は行いません。実flashの信頼性と実運用は未検証です。

## 適用範囲

`picow_enable_persistent_bonds(final_executable, offset, total_size)`で明示的に
選んだ実行ファイルだけが、SDK起動時から新しい汎用TLVを使います。Classicの
ボンドbackend選択は別に必要で、runtimeの既定はRAMのままです。

ハッシュ固定したPico SDK 2.1.1の起動ソースから、保存初期化部分だけを差し替えた
コピーをビルド領域へ生成します。導入済みSDK原本は変更しません。選ばなかった
実行ファイルは元のSDK初期化と、その旧ジャーナルの制限を引き継ぎます。

対応は現在`pico_w`、256-byte page、同容量の4096〜16384-byte bank×2です。
SDKの版・ソース・構成やgeometryが未対応なら拒否します。最終実行target以外への
適用、動的flash offset、Releaseを含むファーム領域重複も拒否します。
filesystem、OTA、他のflash利用者との領域調整や安全なflash実行環境は利用側の責任です。
別のprecompiled SDK保存objectを混ぜたり、元のジャーナル初期化を直接呼んだり、
global TLVを再設定してはいけません。

新TLVはボンド専用ではなく、容量内で任意のuint32 tagと可変長データを扱います。
新形式内ではBTL・BTD・アプリ用tagを共存できますが、旧形式のimportはありません。
BLEの実際のpairing/protocol動作は未試験です。Classic adapterは引き続き
`0x50424b00`〜`0x50424b07`を予約し、controllerに紐付く48-byte recordを最大8件扱います。

## 保存方式

- bank全体を1つのversion付きsnapshotとし、現在有効なbankは更新中に変更しません。
- 別bankへ保持する全tagを書き、内容とCRCを検証してから、独立した最終256-byte
  pageへ完了印を書きます。完了後の公開と再起動時は同じ検証・世代選択を使います。
- 先頭32 bytesにはmagic、bank size、version、uint64世代、payload長、CRCを格納。
  payloadはtag・length・valueの列です。重複tag、不正長、余白の異常を拒否します。
  正確なbyte形式は[英語契約](bluetooth_committed_tlv.md)を参照してください。
- 削除は新snapshotから該当tagを除外する方法で、古いrecordを直接無効化しません。
  無関係なtagはそのままコピーします。長さ0のstoreは削除です。
- tag 0／0xffffffffも扱えます。getのbufferがnullなら長さ、そうでなければ
  コピーしたbyte数を返します。容量不足は媒体を変更せず失敗します。
- 有効な2 bankの世代は隣接した異なる値でなければなりません。世代はwrapしません。
  最終世代の書込みは成功してもEXHAUSTEDとなり、以後は明示的な回復用読取りだけです。
- 4096-byte bankではentry header込みのpayload上限は3808 bytesです。
  8件のボンドrecordは448 bytesを使います。固定RAM bufferを2つ使い、動的確保しません。

## 状態とデータ保護

初期化・再openは読取りのみです。format、移行、erase、中断処理の再開は行いません。

- `BLANK`: 両bankの全byteが0xff。明示的な初期作成前は書けません。
- `READY`: 有効snapshotと空の他bank、または隣接世代の有効snapshotが2つ。
  通常の読書きができます。
- `READ_ONLY`: 旧形式・未知・将来版・破損・途中書込み等を検出。
  有効な片側snapshotがあると`readable == 1`ですが、通常のTLV/認証には使いません。
  `picow_committed_tlv_recovery_get()`という明示APIだけで検証済みの残存データを読めます。
  他bankがより新しい鍵集合を含む可能性があるため、古いsnapshotの妥当性だけでは
  認証への使用を正当化できません。有効snapshotがなければ回復用の読取りも空です。
  このAPIは旧ジャーナルをdecodeしません。
- `AMBIGUOUS`: 2 bankが同じ世代、または非隣接世代。どちらも公開しません。
- `EXHAUSTED`: 世代UINT64_MAX。回復用読取りだけで、通常認証や変更を拒否します。
- `UNAVAILABLE`: HALやgeometryが未対応／未提供です。

途中書込みの残骸により、データを保ったまま通常動作や次の書込みが停止する場合が
あります。これは未知のbyteを消さないための可用性上の制約です。
自動修復・reclaim・import・再format機能はありません。将来の回復作業では元のbyteを
保護し、移行先の所有権と他TLV利用者を含む別途の手順が必要です。2つしかない旧bankの
片方を消して変換する場合、両方の旧imageを保持したとは言えません。
すでに破壊されたデータを復元できる機能でもありません。

## アプリの起動順と失敗処理

sampleの`preparePersistentBondStorage(bool)`と
`configurePersistentBondsBeforeProfiles()`を参照してください。

1. flash領域の所有・安全な実行条件・必要な操作許可を先に確認します。
2. runtime/profile初期化前に`preparePersistentBondStorage(false)`で検査し、
   READYでなければ通常起動を止めます。
3. 意図して用意した新しい空領域に限り、明示承認後に`true`を使えます。
   内部の`picow_committed_tlv_provision_blank()`は両bank全体の空を再検査し、
   eraseせず空snapshotを作ります。旧／未知の内容への消去許可ではありません。
   SDK起動やsample mainが自動で呼ぶことはありません。
4. ボンドbackendを選び、戻り値を確認します。`BluetoothRuntime::initialize()`の成功と
   TLVのREADYを再確認してから、profileのbeginやradio powerOnへ進みます。
5. 所有するmain loopでTLVと`PicoPersistentBonds`のstatusを確認します。
   書込み／容量失敗counterは失敗を可視化します。void deleteの失敗もcounterに残ります。
   失敗時に自動でradioを切る処理はありません。

通常のgeneric readとClassic認証・iteratorはREADYを動的に確認します。
open時だけREADYでも、その後変われば鍵を返しません。回復用のデータを通常認証へ
流し込んではいけません。保存失敗を隠すRAMへの切替や、旧tagが読めないことを理由に
空の成功storeを捏造しないでください。単一main loopでのみ使い、並列アクセス、IRQ、
複数core、外部writerは非対応です。

## 検証と限界

保存モデルにはオフライン故障注入試験があります。実flashを動かした試験ではありません。通常認証と明示的な回復アクセスは別々に検査します。試験の詳細は開発履歴を参照してください。

新実装は整列したpage全体だけを書き、Pico HALの部分page用の周辺byteコピー分岐を
使いません。試験は実HALの実行ではなく、この契約のモデルです。
実証したbank sizeは4096 bytesのみ。読出しが正しく一貫し、故障が対象page／erase bank
内に留まることを仮定します。実際の電圧低下、他pageへの損傷、CRC衝突、不正な読出し、
保持寿命、flash_safe_execute失敗、割込み、複数core、時間・電源条件は未保証です。
HALには書込み／erase結果の返却口がなく、読戻しは物理的な耐電断保証ではありません。

通常更新はほぼ1 bankのerase/programとなり、append journalより寿命と遅延に不利です。
暗号化、真正性、rollback防止、secure erase、複数bondの一括atomic削除はありません。
古いsnapshotに削除済みの鍵byteが残り得ます。実際の鍵・flash・ファーム実行・HCI・無線の
操作や試験は行っていません。

[検証範囲](validation_jp.md)
