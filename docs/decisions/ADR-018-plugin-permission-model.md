# ADR-018: プラグインの `permissions` は自己申告ビットフィールド + NULL 関数ポインタ・ゲートで実装し、マニフェスト検証・署名検証・確認ダイアログは見送る

- **ステータス:** Accepted
- **決定日:** 2026-08-02 (Phase 8d 実装完了時)
- **関連:** [ADR-015](ADR-015-plugin-host-c-abi-seh.md)(C ABI + SEH クラッシュ隔離)、[ADR-016](ADR-016-plugin-core-api-bridge.md)(`NeoMifesCoreApi`橋渡し、「真の権限ゲートは Phase 8 のサブフェーズとして別途必要」と予告)、[ADR-017](ADR-017-plugin-job-object-sandbox.md)(Job Object資源制限)、`docs/design/master_roadmap.md` §8.3・§17.1、CLAUDE.md 絶対ルール3(推測実装をしない)・ルール4(巨大クラス/関数を作らない)

## コンテキスト

Phase 8c(Job Objectによるプラグイン資源制限)完了後、ユーザーから次のPhaseとして4候補(`permissions`権限モデル/`registerCommand`・`showToast`実装/tree-sitter内部実装調査/SQL文法対応)を提示し、**`permissions`権限モデル**が選ばれた。ADR-015/016/017が3フェーズ連続で「権限モデルが無いため実装できない」と明記してきた前提条件であり、ADR-016は特に「真の権限ゲートはPhase 8のサブフェーズとして別途必要になる」と名指しで予告していた。

**着手前調査(既存コード・roadmap・ADR-015/016/017全文の直接読解、CLAUDE.mdルール3)で判明した重大な事実:** roadmap §8.3が示す`permissions`ビットフィールドの原案は`Network | Filesystem | Subprocess | Registry | Clipboard`の5カテゴリのみで構成されており、`Document`(文書読み書き)は含まれていない。ところが実際にPhase 8bで実装済みの`NeoMifesCoreApi`(`insertText`/`deleteRange`/`getLineCount`/`getLineText`)は完全にドキュメント操作のみであり、Network/Filesystem/Subprocess/Registry/Clipboardに対応するCoreApi関数(`httpRequest`/`readPluginData`/`writePluginData`等)は1つも存在しない。roadmap原案のカテゴリをそのまま実装しても、ゲートする対象が何も無い「意味のないビットフィールド」になってしまうと判明した。

## 選択肢

1. **roadmap原案の5カテゴリのみを実装する** — 実装済みのCoreApi関数を一切ゲートできない
2. **roadmap原案の5カテゴリに加え、新規`Document`カテゴリを追加し、これのみを実際にゲートする(採用)**
3. **`manifest.json5`+Authenticode署名検証+ユーザー確認ダイアログまでフルスコープで実装する**

## 決定

**選択肢2を採用する。** `NeoMifesPluginInfo`に`unsigned int permissions`フィールドを追加し、roadmap原案の5カテゴリ(`NEOMIFES_PLUGIN_PERMISSION_NETWORK`/`FILESYSTEM`/`SUBPROCESS`/`REGISTRY`/`CLIPBOARD`、いずれも未使用の予約ビット)に加え、新規`NEOMIFES_PLUGIN_PERMISSION_DOCUMENT`を追加する。`neomifes::app::buildPluginCoreApi(unsigned int grantedPermissions)`が`Document`ビットの有無で「4関数全て実装済みのCoreApi」と「4関数全てNULLのCoreApi」のどちらかを返す。`manifest.json5`・Authenticode署名検証・未署名プラグインの確認ダイアログは全て見送る。

## 根拠

### `Document`カテゴリを新規追加する理由

roadmap原案のカテゴリはいずれも「まだ存在しないCoreApi関数」に対応しており、今回そのまま実装しても実効性が無い。実際に存在する唯一の能力面(ドキュメント読み書き)をゲートしなければ、`permissions`という概念自体が名目だけのものになる。roadmap原案からの意図的な逸脱であり、他の5カテゴリは将来`httpRequest`/`readPluginData`/`writePluginData`等が実装された時点で初めて意味を持つ予約ビットとしてそのまま残す(名前の後方互換性を保つため、今のうちに確保しておく)。

### enforcement機構としてNULL関数ポインタを採用し、新規エラーコードを追加しない理由

roadmap §8.3が唯一示している実施方法(「`// v2.0: 権限が付与されている場合のみ非 NULL`」というコメント)は、ホスト側が明示的に「権限チェック→拒否」する呼び出しゲートではなく、権限が無ければ対応するCoreApi関数ポインタをNULLにするというゲート方式だった。これは`PluginHost`の既存の無条件SEHトランポリン(`invokePluginCallbackSafe`、Phase 8a)と組み合わせると好都合であることが判明した: NULL関数ポインタ経由の呼び出しはハードウェア例外として発生し、既存のSEHトランポリンがそのまま捕捉して`PluginErrorCode::OnLoadCrashed`として報告する。「権限の無いAPIを呼び出した」結果は「クラッシュしたが隔離された」という、Phase 8aが実機検証済みの経路にそのまま帰着するため、新規の`PluginErrorCode`もチェックロジックも一切不要になった。`tests/integration/plugin_document_editing_test.cpp`の`PluginWithoutDocumentPermissionCrashesOnNullInsertTextAndLeavesDocumentUntouched`で、新規サンプル`permission_denied_plugin`(`NEOMIFES_PLUGIN_PERMISSION_NONE`を宣言しつつ`insertText`を無条件呼び出し)を使い、ローカル実機(Debug/Release/ubsan全934件green)でこの経路を実測検証した。

### `manifest.json5`・Authenticode署名検証・確認ダイアログを見送る理由

- プラグインの発見・インストール・配置ディレクトリという概念自体がまだ存在しない。現状`PluginHost::load()`はテストコードが直接DLLパスを渡すのみで、`%APPDATA%\NeoMIFES\plugins\<id>\`のような構造は無く、マニフェストファイルを置く場所が無い。
- `nlohmann/json`(ADR-013で導入済み、`neomifes_core`のみPRIVATEリンク)はJSON5ではなくJSONのみをパースできる。ADR-013自身が「roadmap原案は`search_history.json5`だったが機械生成/機械読み取り専用ファイルにJSON5の付加機能は不要と判断しJSONへ意図的にdivergeした」という前例を残しているが、今回はそもそも読み込むファイル自体が無いため議論の対象にすらならない。
- UI側の確認ダイアログ・トースト機構はまだ存在しない。

本フェーズの`permissions`は「プラグイン自身が`NeoMifesPluginInfo::permissions`ビットフィールドで自己申告し、ホストがそれをそのまま信頼して読む」設計に限定した。マニフェスト・署名・ユーザー同意という「信頼性を検証する」レイヤーは今回一切実装していない。`plugin_manifest.{h,cpp}`(roadmap §8.6が示す新規ファイル)は、読み込む対象ファイル形式が無い状態で新設すると空のスキャフォールドになり、CLAUDE.mdルール3(推測実装をしない)に反するため作成しなかった。

### セキュリティ境界としての限界(ADR-015/016と同じ免責)

プラグインDLLは今も同一プロセス・同一アドレス空間で実行されるため(ADR-015)、プラグイン自身が`permissions`フィールドに任意の値を偽って申告することを技術的に防ぐ手段は無い。**本フェーズが実際に提供する価値は2つ:** (a) 透明性の下地(将来UIで「このプラグインはX権限を要求しています」と表示する際のデータソースになる)、(b) 悪意ではなく事故的な誤用に対する多層防御(本来ドキュメントを触るはずのない純粋UIプラグインが、バグで誤って`insertText`を呼んでしまっても、`Document`権限を申告していなければNULL経由でクラッシュ隔離されるだけで実害が出ない)。

「自己申告を信頼する」設計が悪意への対策にならない以上、既存のJob Object制限(ADR-017、`ActiveProcessLimit=1`)を`permissions`に応じて条件付き適用へ緩めることはしない。roadmap §17.1の原案は「Network権限を要求するプラグインにのみJob Object適用」だったが、ADR-017は既に「権限モデルが無いため全プラグイン一律適用」と確定させており、今回権限モデルができたとしても「自己申告は信頼できない」という前提は変わらないため、無条件適用のまま据え置く — 緩和による具体的な利益が無く、リスクだけが増える。

### `PluginHost::load()`のシグネチャ変更(`const NeoMifesCoreApi*` → `CoreApiFactory`関数ポインタ)

`permissions`はプラグインDLL内の`NeoMifesPluginInfo`にあり、`load()`が`neomifes_plugin_info()`を呼んで初めて判明する。ところが従来の設計は呼び出し元(`src/app/`)が`load()`を呼ぶ**前**に`buildPluginCoreApi()`を呼んで`coreApi`を構築し渡していたため、権限が分かった時点(`load()`内部)ではもう手遅れだった。そこで`load()`の`coreApi`引数を、事前に構築済みの`const NeoMifesCoreApi*`から、権限を受け取って`CoreApi`を構築する関数ポインタ(`using CoreApiFactory = const NeoMifesCoreApi* (*)(unsigned int) noexcept;`)へ変更し、`load()`内部で`info->permissions`を読んだ直後に呼び出す形にした。Phase 7uが「ホットパスでは`std::function`ではなく生の関数ポインタ+payload」を採用した前例があるが、今回は`load()`ごとに1回しか呼ばれない非ホットパスであり、かつ`app::buildPluginCoreApi`自身が既にstateless free functionなので、生の関数ポインタ型をそのまま採用した(`std::function`より軽量、`<functional>`不要、`plugin_sdk.h`自体のC ABI関数ポインタ流儀とも一致)。`neomifes::plugin`は引き続き`document::Document`を一切知らない(レイヤリング規則、ADR-016)。

## 影響

### 実装(`include/`, `src/plugin/`, `src/app/`, `plugins/samples/`, `tests/`)
- `include/neomifes/plugin_sdk.h`: `NEOMIFES_PLUGIN_PERMISSION_*`6マクロ追加、`NeoMifesPluginInfo::permissions`フィールド追加
- `src/plugin/include/neomifes/plugin/plugin_host.h`/`.cpp`: `load()`の`coreApi`引数を`CoreApiFactory`関数ポインタへ変更、新規`grantedPermissions()`アクセサ追加
- `src/app/include/neomifes/app/plugin_core_api_bridge.h`/`.cpp`: `buildPluginCoreApi()`のシグネチャを`(unsigned int) -> const NeoMifesCoreApi*`へ変更、`kFullCoreApi`/`kDocumentDeniedCoreApi`の2定数切替
- 新規`plugins/samples/permission_denied_plugin/`: `NEOMIFES_PLUGIN_PERMISSION_NONE`を宣言しつつ`insertText`を無条件呼び出しし、NULL関数ポインタ経由のクラッシュ隔離を実測証明するテスト専用サンプル
- 既存5サンプルプラグイン(`hello_plugin`/`hello_plugin_bad_api_version`/`crashing_plugin`/`throwing_plugin`/`document_editing_plugin`)の`NeoMifesPluginInfo`へ`.permissions`フィールドを明示追加
- `tests/unit/app_plugin_core_api_bridge_test.cpp`: 既存20テストを`buildPluginCoreApi(NEOMIFES_PLUGIN_PERMISSION_DOCUMENT)`呼び出しへ更新、新規2テスト追加
- `tests/integration/plugin_document_editing_test.cpp`: 既存テストに`grantedPermissions()`検証追加、新規テストケース1件追加(`permission_denied_plugin`経由)
- `tests/integration/plugin_load_test.cpp`: `grantedPermissions()`検証1行追加

### 実装しない(引き続きスコープ外)
- `manifest.json5`パース(プラグイン発見・インストールディレクトリ構造自体が未実装)
- Authenticode署名検証、未署名プラグインの確認ダイアログ・Enterprise設定による無効化
- `registerCommand`/`showToast`/`httpRequest`/`readPluginData`/`writePluginData`(まだ存在しないCoreApi関数 — 対応する`Network`/`Filesystem`/`Subprocess`/`Registry`/`Clipboard`ビットは今回は予約のみで未エンフォース)
- Job Object制限(ADR-017)の権限連動化(上記根拠参照、意図的に見送り)
- `src/app/main.cpp`への配線(Phase 8a〜8cと同じ「ヘッドレスのみ」方針を継続)
- `plugin_manifest.{h,cpp}`(roadmap §8.6が示す新規ファイル、読み込む対象ファイル形式が無いため作成せず)

## 将来の再評価タイミング

- **`manifest.json5`+署名検証+確認ダイアログ:** プラグインの発見・インストールディレクトリ構造(マーケットプレース等)が具体化した時点。ADR-015/016/017と同じ再評価条件。
- **`Network`/`Filesystem`/`Subprocess`/`Registry`/`Clipboard`ビットの実エンフォース化:** 対応するCoreApi関数(`httpRequest`/`readPluginData`/`writePluginData`等)が実装される時点。
- **Job Object制限の権限連動化:** 自己申告を信頼できる仕組み(署名検証・マニフェスト検証)が整った時点で再検討。

## 参考
- `docs/design/master_roadmap.md` §8.3・§17.1
- `docs/decisions/ADR-013-json-library.md`(JSON5→JSONへの意図的乖離の前例)
