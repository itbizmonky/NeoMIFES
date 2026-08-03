# ADR-019: `NeoMifesCoreApi::showToast` はヘッドレスな `ui::ToastState` 状態層のみ実装し、`registerCommand` と実UIウィジェットは延期する

- **ステータス:** Accepted
- **決定日:** 2026-08-02 (Phase 8e 実装完了時)
- **関連:** [ADR-016](ADR-016-plugin-core-api-bridge.md)(`NeoMifesCoreApi`橋渡し、`registerCommand`/`showToast`を「UI側の受け皿が無い」として延期)、[ADR-018](ADR-018-plugin-permission-model.md)(`permissions`権限モデル)、`docs/design/master_roadmap.md` §8.3、CLAUDE.md 絶対ルール3(推測実装をしない)・ルール4(巨大クラス/関数を作らない)・ルール10(過度な先行複雑化を避ける)

## コンテキスト

Phase 8d(`permissions`権限モデル)完了後、ユーザーから次のPhaseとして4候補(`registerCommand`・`showToast`実装/AppContainerサンドボックス/tree-sitter内部実装調査/SQL文法対応)を提示し、**`registerCommand`・`showToast`実装**が選ばれた — ADR-016/017/018が3フェーズ連続で「UI側の受け皿が無いため実装できない」と明記してきた前提条件であり、Phase 8dで権限モデルが整った今、新規CoreApi機能を最初から権限ビットでゲートできる状態になった。

**着手前調査(Explore agent、CLAUDE.mdルール3)で判明した重大な事実:** `registerCommand`と`showToast`は実装の重さが本質的に異なる。roadmapスケッチの`showToast(ctx, message)`は`onLoad`/`onUnload`中に同期的に1回呼ばれるだけで完結し、`plugin_sdk.h`の既存スレッド契約(「`ctx`を受け取ったコールバックが戻った後は呼び出し禁止」)の範囲内にそのまま収まる。一方`registerCommand(ctx, id, callback)`は「コールバックを保存し、後で(ユーザーがコマンドパレットから選択した時点で)安全に呼び出す」という既存のスレッド契約が明示的に禁止しているパターンを必要とし、新しい安全性契約の策定・SEH保護された遅延呼び出し機構・実行時コマンド登録API(現状`ui::CommandPalette`は`create()`時に渡された`std::vector<CommandDescriptor>`を後から追加する手段が無い)が必要になる。さらに、本コードベースには**トースト/通知UIが一切存在せず**(`src/`/`include/`/`docs/`全体を検索して確認済み、3箇所のコメントで「no error-toast UI exists in this codebase」と明記)、`PluginHost`は**未だかつて`main.cpp`/`wWinMain`へ配線されたことが無い**(実アプリは今回もプラグインを一切ロードしない、Phase 8a〜8dと同じ「ヘッドレスのみ」を継続する既存路線)。

この状況をAskUserQuestionで再提示し、**「showToastのみ、ヘッドレス実装」**が選ばれた。

## 選択肢

1. **`registerCommand`と`showToast`の両方を、実UIウィジェット+`main.cpp`初配線まで含めてフル実装する**
2. **`showToast`のみ、ヘッドレスな状態層として実装する(採用)**
3. **`registerCommand`のみ実装する**
4. **両方をヘッドレスで実装する(`registerCommand`用の遅延呼び出し機構含む)**

## 決定

**選択肢2を採用する。** `NeoMifesCoreApi::showToast`を実装し、実際の消費先として新規`ui::ToastState`(Win32非依存、ヘッダオンリーの純粋状態クラス)を新設する。`registerCommand`・実Win32トーストウィジェット(ポップアップウィンドウ・自動消滅タイマー)・`main.cpp`への配線は全て次サブフェーズへ延期する。

## 根拠

### `showToast`と`registerCommand`を分離する理由

`showToast`はroadmapスケッチ通り同期呼び出しで完結し、既存のスレッド契約を一切変更せずに実装できる。一方`registerCommand`は本質的に別種の機構(コールバックの保存+後からの安全な呼び出し)を必要とし、これを同じフェーズで扱うとスコープが大きく膨らみCLAUDE.mdルール8(1PR=1責務)に反する。両者の実装難易度の非対称性は着手前調査で初めて判明した事実であり、推測でスコープを決めなかった(CLAUDE.mdルール3)。

### `ui::ToastState`をヘッダオンリーの純粋状態クラスとした理由

本コードベースの既存UIウィジェット(`FindBar`/`GrepBar`/`GotoLineBar`/`CommandPalette`)はいずれも実HWNDを持つ`.h`+`.cpp`クラスだが、**そのいずれも単体・統合テストの対象になっておらず**、正しさの検証は「実アプリでの視覚確認」のみに依存してきた(このコードベースはWin32ウィジェットの自動テスト機構を持たない)。今回のスコープは`main.cpp`を無改修のまま(Phase 8a〜8dと同じ方針)実装を検証する必要があるため、実HWNDを持つウィジェットを新設すると検証手段が無くなってしまう。そこで、「現在表示すべきメッセージ1件」だけを保持する最小限の状態層(`show()`/`hide()`/`isVisible()`/`message()`)として設計し、実DLL経由の統合テストで正しさを実証できる形にした。複数メッセージのキューイングは意図的に実装しない(CLAUDE.mdルール10、実際の消費者(実UIウィジェット)がまだ無い状態で先行実装しない)。実際のWin32ポップアップウィンドウは、将来`main.cpp`が本クラスの実インスタンスを保持し描画する段階で新設する。

### `showToast`を権限ゲートしない理由

roadmap原案の5予約カテゴリ(Network/Filesystem/Subprocess/Registry/Clipboard)のいずれも「トースト表示」という能力に意味的に合致せず、ドキュメントアクセスとも無関係(`NEOMIFES_PLUGIN_PERMISSION_DOCUMENT`でゲートする理由が無い)。新規の権限カテゴリを追加することも検討したが、表示のみでデータ読み書き能力を持たない低リスクな機能に対して新カテゴリを追加するのはCLAUDE.mdルール3(推測実装をしない)に反すると判断した。`buildPluginCoreApi()`が返す`kFullCoreApi`/`kDocumentDeniedCoreApi`の両方に同じ`showToastImpl`を設定することで、「常に非NULL」を直接表現した。実際に他のUI関数(将来の`registerCommand`等)が増えてから、共通するカテゴリが本当に必要か再評価する。

### `NEOMIFES_CORE_API_VERSION`を`1u`→`2u`へ引き上げる理由

Phase 8b導入時のコメントが「CoreApi surfaceは独自のペースで成長する、バージョン1では何も変化していない」と明記していた通り、今回が初めてCoreApi構造体に実際にフィールドが追加される変更であり、意図通りの初回インクリメントとなる。

### `PluginHost::load()`への`toastSink`パラメータ追加

`NeoMifesToastSink`という新規不透明ハンドルを`NeoMifesDocument`と全く同じパターンで追加し、`load()`に第4引数`NeoMifesToastSink* toastSink = nullptr`を追加した(既存の`document`パラメータと同じ扱い、デフォルトnullptrで既存呼び出し元は無改修)。`neomifes::app::toNeoMifesToastSink(ui::ToastState&)`が`reinterpret_cast`を`plugin_core_api_bridge.cpp`内に閉じ込める(`toNeoMifesDocument()`と同じ設計)。`neomifes::plugin`は引き続き`neomifes::document`/`neomifes::ui`のいずれにも依存しない(レイヤリング規則、ADR-016)。

## 影響

### 実装(`include/`, `src/plugin/`, `src/app/`, `src/ui/`, `plugins/samples/`, `tests/`)
- 新規`src/ui/include/neomifes/ui/toast_state.h`: ヘッダオンリー`ui::ToastState`クラス
- `include/neomifes/plugin_sdk.h`: `NEOMIFES_CORE_API_VERSION`を2へ、`NeoMifesToastSink`不透明ハンドル、`NeoMifesCoreApi::showToast`、`NeoMifesPluginContext::toastSink`追加
- `src/plugin/include/neomifes/plugin/plugin_host.h`/`.cpp`: `load()`に`toastSink`パラメータ追加
- `src/app/include/neomifes/app/plugin_core_api_bridge.h`/`.cpp`: `toNeoMifesToastSink()`+`showToastImpl()`追加、`kFullCoreApi`/`kDocumentDeniedCoreApi`双方に設定
- `src/app/CMakeLists.txt`: `neomifes_app_input`が新たに`neomifes::ui`をPUBLICリンク
- 新規`plugins/samples/toast_plugin/`: `NEOMIFES_PLUGIN_PERMISSION_NONE`を宣言しつつ`showToast`を呼び出し、権限ゲートされていないことを実証するテスト専用サンプル
- `tests/unit/ui_toast_state_test.cpp`(新規)、`tests/unit/app_plugin_core_api_bridge_test.cpp`(新規3テスト)、`tests/integration/plugin_toast_test.cpp`(新規)

### 実装しない(引き続きスコープ外)
- `registerCommand`(コールバックを保存し後で安全に呼び出す新しい契約・SEH保護された遅延呼び出し機構・`ui::CommandPalette`への実行時登録APIが必要、上記根拠参照)
- 実Win32トーストウィジェット(ポップアップウィンドウ・自動消滅タイマー・スタイリング — `ui::ToastState`はその手前の状態層のみ)
- `src/app/main.cpp`への配線(実アプリは今回もプラグインを一切ロードしない、Phase 8a〜8dと同じ「ヘッドレスのみ」方針を継続)
- 複数トーストのキューイング

## 将来の再評価タイミング

- **`registerCommand`:** `ui::CommandPalette`に実行時登録API(`addCommand()`相当)が用意された時点、およびSEH保護された遅延コールバック呼び出し機構の設計が固まった時点。
- **実Win32トーストウィジェット+`main.cpp`配線:** `PluginHost`が初めて`main.cpp`へ配線されるサブフェーズ(まだ日程未定)。

## 参考
- `docs/design/master_roadmap.md` §8.3
- `src/ui/include/neomifes/ui/goto_line_bar.h`(既存Win32ウィジェットの典型例、対照として)
- `src/ui/include/neomifes/ui/command_palette.h`(`m_commands`が`create()`時1回のみ設定され実行時追加APIが無いことの確認元)
