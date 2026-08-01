# ADR-015: プラグインホストは C ABI + `LoadLibraryW` + 無条件 SEH トランポリンで実装し、CoreApi・サンドボックス・署名検証を Phase 8b 以降へ明示的に延期する

- **ステータス:** Accepted
- **決定日:** 2026-08-01 (Phase 8a 実装完了時)
- **関連:** [ADR-008](ADR-008-com-raii-comptr.md)(COM は D3D11/D2D/DXGI 専用に限定)、`docs/design/master_roadmap.md` §8、CLAUDE.md 絶対ルール3(推測実装をしない)・ルール4(巨大クラス/関数の回避)・ルール6(外部ライブラリ追加は最小限)・ルール8(1PR=1責務)

## コンテキスト

`docs/design/master_roadmap.md` §8 は Plugin Engine の完全な v2.0 ビジョン(C ABI SDK・Windows AppContainer/Job Object サンドボックス・別プロセス実行+IPC・`manifest.json5`+Authenticode 署名検証・マーケットプレース連携基盤)を規定しているが、CLAUDE.md §7 の Phase 8 DoD 自体は「サンプル DLL 動作」の一点のみである。全量を一度に実装するのは ADR-011/ADR-012 が Phase 3c/Phase 4a で行った判断(推測実装の回避、CLAUDE.md ルール8「1PR=1責務」)と同じ理由で不適切と判断し、着手前に AskUserQuestion でユーザーへスコープ縮小案を提示、「最小限 PoC」(DLL 読み込み+`onLoad`/`onUnload`呼び出し+SEH クラッシュ隔離のみ)が選ばれた。

着手前調査で以下を確認した(CLAUDE.md ルール3、推測実装をしない):
- `neomifes::platform::ModuleHandle`(`handle_guard.h`、`HandleGuard<HMODULE, FreeLibraryDeleter, nullptr>`)が既に存在し未使用だった — `LoadLibraryW`/`FreeLibrary`に正確に対応済み。
- `src/app/main.cpp`の`enableHighDpi()`が既に`GetProcAddress`+`reinterpret_cast`による関数ポインタ解決の前例を持つ。
- `document::Document`に`getLineText()`や行+桁→オフセット変換が存在せず、roadmap スケッチの`NeoMifesCoreApi`(`insertText`/`getLineText`等)はそのままでは実装できない。
- 本コードベースには`SHARED`/`MODULE`の CMake ターゲットが一つも存在しない(全モジュール STATIC)。

## 選択肢

1. **COM インターフェース(`IUnknown`派生)でプラグイン境界を定義する**
2. **C ABI(`extern "C"` + `__declspec(dllexport)`)+ `LoadLibraryW`/`GetProcAddress`でプラグイン境界を定義する(採用)**
3. **プラグインを別プロセスで実行し、名前付きパイプ等の IPC でホストと通信する**
4. **静的リンク(プラグインもホストと同一バイナリにコンパイル)**

## 決定

**選択肢2を採用する。** エントリポイントは`neomifes_plugin_info()`/`neomifes_plugin_vtable()`の2関数のみ、`GetProcAddress`で名前解決する(序数解決はしない)。SEH トランポリン(`__try`/`__except(EXCEPTION_EXECUTE_HANDLER)`)は**無条件**フィルタを採用し、`original_buffer.cpp`の`scanUtf8Safe()`等が使う`EXCEPTION_IN_PAGE_ERROR`限定フィルタとは意図的に異なる設計にする。

## 根拠

### C ABI + LoadLibraryW を採用する理由(選択肢1・3・4を却下する理由)

- **COM(選択肢1)は不釣り合いなセレモニー。** 本コードベースの COM 利用は ADR-008 の通り D3D11/D2D/DXGI という OS 定義インターフェース専用であり、`IUnknown`の参照カウント・`QueryInterface`・IDL/型ライブラリといった機構は「2関数だけをエクスポートする」今回の要件に対して過剰。`ModuleHandle`が既に存在し未使用だったこと、`enableHighDpi()`に`GetProcAddress`+`reinterpret_cast`の前例があったことから、C ABI は「新規に持ち込む」のではなく「既存の断片を組み合わせる」設計になる。
- **別プロセス実行+IPC(選択肢3)は Phase 8a のスコープを大幅に超える。** 名前付きパイプ設計・シリアライズ・非同期メッセージング・プロセスライフサイクル管理が新規に必要になり、CLAUDE.md ルール8「1PR=1責務」に反する。真の隔離(意図的な悪意あるプラグインからの保護)が必要になった時点で Phase 8b として再検討する。
- **静的リンク(選択肢4)はプラグインエンジンの本質(実行時の動的ロード・アンロード・サードパーティ配布)を満たさない。**

### SEH フィルタを無条件にする理由(既存パターンからの意図的な逸脱)

`original_buffer.cpp`の既存 SEH トランポリンは`EXCEPTION_IN_PAGE_ERROR`のみを捕捉する条件付きフィルタであり、これは「信頼できる自コード内の既知のハードウェア例外(mmap されたファイルの I/O エラー)」を対象にした設計である。プラグインは信頼できない外部コードであり、要件は「プラグインがどんな異常を起こしてもホストプロセスを生かし続ける」ことであるため、`EXCEPTION_EXECUTE_HANDLER`を無条件に返すフィルタを採用した。

**実測による裏付け(本 ADR 採択の根拠として記録):** `tests/integration/plugin_load_test.cpp`の`IsolatesAHardwareFaultInOnLoadWithoutCrashingTheHost`(`crashing_plugin`、null ポインタ書き込みによる`EXCEPTION_ACCESS_VIOLATION`)と`IsolatesAThrownExceptionInOnLoadWithoutCrashingTheHost`(`throwing_plugin`、`std::runtime_error`の throw)の両方が、Debug/Release(MSVC)・ubsan(clang-cl)の全ビルド構成で green であることを確認した。ホストは`/EHsc`でビルドされており、これは通常`extern "C"`リンケージの関数がホスト側から見て「throw しない」ことを前提とするが、`invokePluginCallbackSafe()`(`plugin_host.cpp`)はプラグインの`onLoad`/`onUnload`を**間接関数ポインタ経由**で呼ぶため、コンパイラはこの前提を静的に証明できず、SEH による捕捉が実際に機能することを推測ではなく実測で確認した(CLAUDE.md ルール3)。

**明示的な非対象:** この SEH トランポリンは**セキュリティ境界ではない**。プラグインはホストと同一プロセス・同一アドレス空間で動作するため、意図的に悪意のあるプラグインがホストのメモリへ直接アクセスすることを防げない。あくまで「バグのあるプラグインがホストをクラッシュさせない」という信頼性(reliability)目的の機構であり、真の隔離(AppContainer・Job Object・別プロセス)は Phase 8b 以降のスコープである。

### `NeoMifesPluginContext`を透過的な構造体にする理由(roadmap スケッチからの意図的な逸脱)

roadmap §8 のスケッチは不透明な前方宣言ハンドルを想定していたが、実装では`void* userData`フィールドを持つ透過的な構造体にした。Win32 の`GWLP_USERDATA`、libuv の`void* data`と同種の一般的な C ABI イディオムであり、統合テスト(`plugin_load_test.cpp`)が「`onLoad`/`onUnload`が実際に呼ばれたか」を実 DLL 経由で観測する最小の手段として必要だった。

### `apiVersion`を完全一致のみで判定する理由

`NEOMIFES_PLUGIN_API_VERSION`は現在`1`のみであり、min/max 範囲での互換性判定を今設計するのは、次にバージョンを上げる際の実際の互換性要件(破壊的変更かどうか)が分からない現時点では推測実装になる。次にバージョンを上げるタイミングで再検討する。

### `NeoMifesCoreApi`(insertText/getLineText 等)を延期する理由

`document::Document`には`getLineText()`や行+桁→オフセット変換が存在せず、roadmap スケッチのシグネチャをそのまま実装すると存在しない API を呼ぶことになる。このブリッジ設計自体が独立した検討課題であり、`docs/issues/plugin_core_api_document_gap.md`に記録した。

## 影響

### 実装(`include/neomifes/plugin_sdk.h`, `src/plugin/`, `plugins/samples/`, 新規)
- 配布可能な C ABI ヘッダ(`plugin_sdk.h`、本リポジトリ初のトップレベル`include/`)、`neomifes::plugin::PluginHost`(load/unload、`std::expected`ベースの`PluginExpected<T>`)、`neomifes::plugin::PluginError`/`PluginErrorCode`
- サンプル DLL 4種(`hello_plugin`/`hello_plugin_bad_api_version`/`crashing_plugin`/`throwing_plugin`、本リポジトリ初の`MODULE` CMake ターゲット)

### 実装しない(Phase 8b 以降へ)
- `NeoMifesCoreApi`(insertText/deleteRange/getLineCount/getLineText/registerCommand/showToast/httpRequest/readPluginData/writePluginData) — **2026-08-02 更新:** このうち`insertText`/`deleteRange`/`getLineCount`/`getLineText`の4関数はPhase 8bで実装済み。詳細は[ADR-016](ADR-016-plugin-core-api-bridge.md)参照。残る`registerCommand`/`showToast`/`httpRequest`/`readPluginData`/`writePluginData`は引き続き未実装(ADR-016のスコープ外リスト参照)。
- `permissions`ビットフィールド+権限 UI
- Windows AppContainer・Job Object リソース制限・別プロセス実行+IPC(真のセキュリティ隔離)
- `manifest.json5`パース/スキーマ検証・Authenticode 署名検証
- マーケットプレースクライアント(`src/marketplace/`)
- `onDocumentChanged`+非同期ワーカー配線
- `Ctrl+Shift+X`プラグイン管理 UI・複数プラグイン管理/ホットリロード
- `core::CommandDispatcher`へのプラグインコマンド受け入れ・`src/app/main.cpp`への配線

## 将来の再評価タイミング

- **真のセキュリティ隔離(AppContainer/別プロセス):** マーケットプレース等でユーザーが未検証のサードパーティプラグインを実行する運用が具体化した時点(同一プロセス内 SEH では意図的な悪意には対抗できないため、その時点で再設計必須)
- **`apiVersion`の min/max 範囲判定:** 次に`NEOMIFES_PLUGIN_API_VERSION`を上げる際、破壊的変更かどうかが判明した時点
- **`NeoMifesCoreApi`:** ~~`docs/issues/plugin_core_api_document_gap.md`の完了条件(`Document`側の行+桁⇔オフセット変換 API の設計)を満たした時点~~ → 2026-08-02、Phase 8bで満たされ`insertText`/`deleteRange`/`getLineCount`/`getLineText`を実装済み([ADR-016](ADR-016-plugin-core-api-bridge.md)参照)。`registerCommand`/`showToast`/ネットワーク・ファイルシステム系関数は引き続き未評価のまま。

## 参考
- `docs/design/master_roadmap.md` §8
- `docs/issues/plugin_core_api_document_gap.md`
- `src/platform/include/neomifes/platform/handle_guard.h`(`ModuleHandle`)
- `src/document/src/original_buffer.cpp`(既存 SEH トランポリンパターン、条件付きフィルタとの対比)
