# ADR-016: `NeoMifesCoreApi` はドキュメント操作4関数のみを`src/app/`のブリッジ層で実装し、`neomifes::plugin`自体はDocument Engineに依存させない

- **ステータス:** Accepted
- **決定日:** 2026-08-02 (Phase 8b 実装完了時)
- **関連:** [ADR-015](ADR-015-plugin-host-c-abi-seh.md)(C ABI + SEH クラッシュ隔離の前提設計)、`docs/design/master_roadmap.md` §8.3、`docs/issues/plugin_core_api_document_gap.md`、CLAUDE.md §3(レイヤードアーキテクチャ、上位は下位のみに依存)・絶対ルール3(推測実装をしない)・ルール10(ベンチマーク根拠のない先行複雑化を避ける)

## コンテキスト

Phase 8a(プラグインエンジン最小限 PoC)は`NeoMifesCoreApi`(roadmap §8.3 スケッチの`insertText`/`deleteRange`/`getLineCount`/`getLineText`等)の実装を丸ごと延期した。理由は`document::Document`に「行番号→テキスト取得」「行+桁→オフセット変換」のいずれの API も存在せず、このブリッジ設計自体が独立した検討課題だったため(`docs/issues/plugin_core_api_document_gap.md`)。

Phase 8b はこの前提条件を満たし、`NeoMifesCoreApi`を実装する。着手前調査で以下を確認した(CLAUDE.md ルール3、推測実装をしない):

- `document::Document::offsetToLine()`/`lineToOffset()`は範囲外入力に対して既に安全にクランプする実装(`LineIndex`、`upper_bound`ベース)。
- `PieceTable::insert()`は`pos`を文書長にクランプするが、`PieceTree::eraseRange()`(`piece_tree.cpp:522`)は`range.start >= end`ガードにより**反転レンジ(start>end)を安全な no-op として無視する**(Plan agentが実装を追跡して発見、メモリ破壊ではなく「意図した削除が黙って起きない」という正しさの問題)。
- CLAUDE.md §3 のレイヤードアーキテクチャ図は`Document Engine → Search/Encoding Engine → Plugin Engine → AI Plugin`の順に依存が下向きに伸びており、Plugin Engine は Document Engine に依存してはならない。
- `src/app/`(`neomifes_app_input`、`document_open.h`/`outline_bridge.h`)は既に document:: と他エンジンを橋渡しする「糊付け層」として確立済みで、Win32 非依存のままヘッドレスにテスト可能。

## 選択肢 (`coreApi`/`document`をプラグインへどう渡すか)

1. **`NeoMifesPluginVTable::onLoad`/`onUnload`のシグネチャに`const NeoMifesCoreApi*`引数を追加する**
2. **`NeoMifesPluginContext`に`coreApi`/`document`フィールドを追加する(採用)**
3. **グローバルなシングルトン`NeoMifesCoreApi`をプラグイン側が自前でルックアップする**

## 決定

**選択肢2を採用する。** `NeoMifesPluginContext`へ`const NeoMifesCoreApi* coreApi`と`NeoMifesDocument* document`の2フィールドを追加し、`PluginHost::load(dllPath, coreApi = nullptr, document = nullptr)`が呼び出し元から受け取ってそのまま転送する。実際に`NeoMifesCoreApi`のインスタンスを構築し`document::Document`へブリッジする実装(`neomifes::app::buildPluginCoreApi()`/`toNeoMifesDocument()`)は`src/app/plugin_core_api_bridge.h`/`.cpp`(`neomifes_app_input`ターゲット)に置き、`neomifes::plugin`(`PluginHost`)自体は`document::Document`型を一切知らないまま据え置く。

## 根拠

### `NeoMifesPluginContext`フィールド経由で渡す理由(選択肢1・3を却下する理由)

- **選択肢1(vtableシグネチャ変更)はPhase 8aの4サンプルプラグイン全ての互換性を壊す。** `hello_plugin`/`hello_plugin_bad_api_version`/`crashing_plugin`/`throwing_plugin`はいずれも`onLoad(NeoMifesPluginContext* ctx)`という現行シグネチャでビルド済みであり、ここへ引数を追加すると全て再コンパイルが必要になる(ソース非互換)。`NeoMifesPluginContext`は既に`userData`という「透過的フィールド越しにホスト→プラグインへ情報を渡す」パターン(ADR-015で確立)を持っており、`coreApi`/`document`を同じ構造体へ追加するのは既存パターンの単純な拡張であり、vtable自体には一切触れない。
- **選択肢3(グローバルシングルトン)はスレッド安全性契約を暗黙にし、複数ドキュメント/複数プラグインホストへの将来拡張を阻害する。** `NeoMifesCoreApi`の関数自体は`buildPluginCoreApi()`が返す単一の静的インスタンス(状態を持たないstateless関数ポインタの集まり、ドキュメントは各呼び出しで明示的に渡される)で問題ないが、「今どのドキュメントに対して呼ばれているか」をグローバル変数で持たせると、将来複数`PluginHost`が異なる`document::Document`を同時に扱うようになった際に破綻する。`NeoMifesPluginContext::document`という呼び出しごとの明示的なハンドルにすることで、この将来拡張を阻害しない。

### `neomifes::plugin`を`document::Document`非依存に保つ理由(レイヤリング)

CLAUDE.md §3 の依存方向ルール(上位は下位のみに依存、下位は上位を知らない)に従うと、図の並び上 Plugin Engine は Document Engine より下位に位置し、Plugin Engine が Document Engine へ依存することは許されない。`PluginHost::load()`の新規パラメータは`const NeoMifesCoreApi*`/`NeoMifesDocument*`という`plugin_sdk.h`が既に定義するC ABI型(`document::Document`型は一切登場しない、`NeoMifesDocument`は本ヘッダ内で完結する不透明forward宣言)であるため、`src/plugin/CMakeLists.txt`は`neomifes::document`への依存を一切追加せずに済む。

実際に`NeoMifesDocument*`を`document::Document*`へ`reinterpret_cast`し、本物の`insertText()`/`eraseRange()`/`lineCount()`/`lineText()`を呼ぶ実装(`plugin_core_api_bridge.cpp`)は、`neomifes::document`と`neomifes::plugin_sdk`の双方に依存できる`src/app/`(既存の`document_open.h`/`outline_bridge.h`と同じ「エンジン間の糊付け層」の役割)に置く。この結果`neomifes::plugin`のCMake依存関係は完全に無変更のままとなった(`neomifes::plugin_sdk`+`neomifes::platform`のみ、Phase 8aから不変)。

### `document::Document`への追加APIを最小限に留める理由

`lineText(LineNumber) -> std::u16string`と`lineColumnToOffset(LineNumber, uint32_t) -> TextPos`の2メソッドのみを追加した。

- `lineText()`は`RenderPipeline::extractLineText()`(Phase 7o)と実質同じロジックだが、意図的に実装を共有しない。`RenderPipeline`版は`m_cachedSnapshot`(Phase 3c/ADR-010の「毎フレーム`snapshot()`を呼ばない」最適化)を読むのに対し、`Document::lineText()`は毎回`snapshot()`を取り直す。呼び出し頻度の性質(毎フレーム vs. プラグインコールバック内の低頻度呼び出し)が異なる2つの実装を無理に1つへ統合するのは、間接性を増すだけで実質的な重複削減にならない(CLAUDE.mdルール10)。
- `lineColumnToOffset()`は`min(lineToOffset(line) + column, length())`という最小限のクランプに留めた。**out-of-range な`line`は`lineToOffset()`自身の既存クランプ(最終行の「開始位置」)を継承し、out-of-range な`column`は文書全体の「終端」にクランプする** — この2つの挙動は非対称である(前者は行の境界、後者は文書全体の境界)が、いずれも「クラッシュしない・文書破損しない」という最低限の安全性のみを保証する設計判断であり、「意図通りの行に必ず収まる」精密さは保証しない。これは`NeoMifesCoreApi`が現時点で信頼していない外部入力(プラグイン)に対する最小限の境界防御であり、それ以上の精度(グラフェムクラスタ考慮、行内クランプ等)は実際の需要が生じてから設計する。

### `deleteRange`が反転レンジを正規化する理由

`lineColumnToOffset()`で解決した`(lineStart, columnStart)`→`(lineEnd, columnEnd)`のオフセットペアが逆転している場合(例: プラグインの計算バグ)、ブリッジは`std::swap()`で正規化してから`Document::eraseRange()`へ渡す。理由は前述の通り`PieceTree::eraseRange()`が`start>=end`を安全な no-op として扱うため、正規化しないと「プラグインが明らかに削除しようとした範囲が黙って何も削除されない」という意図と異なる挙動になる。`Document::eraseRange()`自体は変更しない(このAPIの既存契約——呼び出し側が正規化済みレンジを渡す前提——を壊さない、境界防御は信頼できない入力を扱うブリッジ層の責務であって`Document`自身の責務ではない、CLAUDE.md「境界でのみ検証する」原則)。

### `getLineText`の境界チェック付きコピー契約

roadmap §8.3 スケッチは`void getLineText(doc, line, buffer, bufferLen)`だが、実装は`unsigned`を返す(コピーした文字数、末尾null文字を含まない)。Win32の多くのAPI(`GetWindowTextW`等)と同じ「バッファに収まらない分は切り詰め、必ずnull終端する」契約にした。「収まりきらなかった場合の必要バッファ長を返す」ような追加APIは、現時点で実際の利用者(サードパーティプラグイン)がまだ存在せず要求も無いため、CLAUDE.mdルール10に従い今は追加しない。

### `apiVersion`をCoreApi専用に独立させる理由

`NeoMifesCoreApi::apiVersion`は`NEOMIFES_PLUGIN_API_VERSION`を再利用せず、新規`NEOMIFES_CORE_API_VERSION`とした。CoreApi面(`registerCommand`/`showToast`/ネットワーク関数等が今後追加されうる、ADR-015の延期リスト参照)とプラグイン基本契約(`onLoad`/`onUnload`/`apiVersion`)は将来の成長速度が異なると見込まれるため、別々のバージョンカウンタで管理する。

### セキュリティ境界・Undo非対応という既知のギャップ

**`NeoMifesCoreApi`はセキュリティ境界ではない。** ADR-015が明記した通りPhase 8時点ではプラグインに対する権限モデル(`permissions`ビットフィールド)が存在せず、ロードに成功した任意のプラグインは`NeoMifesCoreApi`経由で無制限にドキュメントを編集できる。これはADR-015の「SEH隔離は信頼性目的でありセキュリティ境界ではない」という既存の制約の実害範囲が「クラッシュしない」から「任意の文書編集が可能」へ広がったことを意味する。真の権限ゲートはPhase 8のサブフェーズとして別途必要になる。

**プラグイン発の編集は`core::CommandDispatcher`/`UndoStack`を経由しない。** `plugin_core_api_bridge.cpp`は`document::Document::insertText()`/`eraseRange()`を直接呼ぶため、`Ctrl+Z`でプラグインの編集を取り消すことはできない。これはroadmapスケッチ自体も`NeoMifesDocument*`への直接操作という同じ形を示しており、`CommandDispatcher`統合には生きた`SelectionModel`/ディスパッチャインスタンスが必要でヘッドレスなブリッジの範囲を超える(Phase 8bのスコープ外)。

## 影響

### 実装(`src/document/`, `include/neomifes/plugin_sdk.h`, `src/plugin/`, `src/app/`, `plugins/samples/document_editing_plugin/`)
- `document::Document::lineText()`/`lineColumnToOffset()`(2メソッド追加)
- `plugin_sdk.h`: `NEOMIFES_CORE_API_VERSION`、`NeoMifesDocument`(opaque)、`NeoMifesCoreApi`構造体(`insertText`/`deleteRange`/`getLineCount`/`getLineText`の4関数のみ)、`NeoMifesPluginContext`への`coreApi`/`document`フィールド追加
- `neomifes::plugin::PluginHost::load()`: `coreApi`/`document`のデフォルトnullptr引数2つを追加(既存呼び出しは無改修でコンパイル継続)
- 新規`neomifes::app::buildPluginCoreApi()`/`toNeoMifesDocument()`(`src/app/plugin_core_api_bridge.h`/`.cpp`、`neomifes_app_input`ターゲット)
- 新規サンプルプラグイン`document_editing_plugin`(`onLoad`が`ctx->coreApi->insertText()`を実際に呼びDLL境界越しの往復を実証)

### 実装しない(Phase 8以降のさらに別サブフェーズへ)
- `registerCommand`/`showToast`(UI側のプラグイン向け公開機構が未整備)
- `httpRequest`/`readPluginData`/`writePluginData`+`permissions`ビットフィールド(権限モデルが無い)
- `onDocumentChanged`+非同期配線
- `Ctrl+Shift+X`プラグイン管理UI・`manifest.json5`+Authenticode署名検証・マーケットプレースクライアント
- Windows AppContainer/Job Objectサンドボックス・別プロセスIPC(真のセキュリティ隔離、ADR-015が既に延期)
- `src/app/main.cpp`への配線(実アプリは今回もプラグインを一切ロードしない)
- プラグイン発の編集を`core::CommandDispatcher`/`UndoStack`経由にしてUndo可能にすること

## 将来の再評価タイミング

- **権限モデル・真のセキュリティ隔離:** ADR-015と同じ再評価条件(マーケットプレース等で未検証のサードパーティプラグインを実行する運用が具体化した時点)
- **プラグイン編集のUndo対応:** 実際にプラグインがドキュメントを編集するユースケースが本格化し、Undo不可が実用上の問題として顕在化した時点
- **`getLineText`の「必要バッファ長を返す」拡張:** 実際のプラグイン利用者から要求が出た時点
- **`registerCommand`/`showToast`:** UI側にプラグイン向け公開機構(コマンド登録・トースト表示)が用意された時点

## 参考
- `docs/design/master_roadmap.md` §8.3
- `docs/issues/plugin_core_api_document_gap.md`
- `src/document/src/piece_tree.cpp`(`eraseRange()`の`start>=end`ガード)
- `src/app/include/neomifes/app/document_open.h`(`src/app/`の既存「エンジン間の糊付け層」パターン)
