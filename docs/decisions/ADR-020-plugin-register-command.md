# ADR-020: `registerCommand` はコールバック本体を後から呼び出せる形の唯一の例外として `NeoMifesCoreApi::registerCommand` を実装し、`ui::CommandPalette` への実配線とプラグインunload時の自動クリーンアップは延期する

- **ステータス:** Accepted
- **決定日:** 2026-08-03 (Phase 8f 実装完了時)
- **関連:** [ADR-016](ADR-016-plugin-core-api-bridge.md)(`NeoMifesCoreApi`橋渡し)、[ADR-018](ADR-018-plugin-permission-model.md)(`permissions`権限モデル)、[ADR-019](ADR-019-plugin-show-toast-headless.md)(`showToast`ヘッドレス実装、`registerCommand`を延期した経緯)、`docs/design/master_roadmap.md` §8.3、CLAUDE.md 絶対ルール3(推測実装をしない)・ルール4(巨大クラス/関数を作らない)・ルール10(過度な先行複雑化を避ける)

## コンテキスト

tree-sitter内部実装調査(根本原因特定+`ts_parser_set_included_ranges()`実機probe検証、不採用)完了後、ユーザーから次のPhaseとして3候補(registerCommand実装/AppContainerサンドボックス/SQL文法対応)を提示し、**registerCommand実装**が選ばれた — ADR-019がPhase 8e(showToast実装)で意図的に延期した唯一の残項目であり、ADR-019自身が「次に着手すべきタイミング」として名指しした条件(`ui::CommandPalette`への実行時登録API相当の設計・SEH保護された遅延呼び出し機構の設計確定)に直接対応する。

**着手前調査(Explore agent + 専用Plan agentによる詳細検証、CLAUDE.mdルール3)で確定した設計方針:**

1. `ui::CommandPalette::create()`は`std::vector<CommandDescriptor>`を1回だけ受け取り、以後追加する手段が無い。
2. `PluginHost`の既存SEHトランポリン(`invokePluginCallbackSafe`、Phase 8a)は`onLoad`/`onUnload`呼び出し専用に`plugin_host.cpp`の無名namespace内に閉じていたが、シグネチャ(`void (*fn)(NeoMifesPluginContext*)`)が`registerCommand`のコールバックと全く同じ形であることが判明し、新規トランポリンを書かず公開昇格して再利用できると分かった。
3. `master_roadmap.md` §8.3のスケッチ(`registerCommand(ctx, id, callback)`)は`title`引数を持たないが、`CommandDescriptor::title`は表示に必須の非オプショナルフィールドであり、スケッチが`CommandDescriptor`確定前に書かれたための逸脱と判断した。
4. Plan agentのレビューで、`plugin_core_api_bridge.cpp`の`registerCommandImpl()`実装案に実際のコンパイルエラー(`util::fromWstringView()`が返す`u16string_view`を`CommandDescriptor::id`/`title`(所有権を持つ`u16string`)へdesignated initializer経由で暗黙変換しようとしていた — `std::basic_string`の`StringViewLike`コンストラクタは`explicit`のためcopy-initializationでは使えない)を実装前に検出した。既存コード6箇所(`find_bar.cpp`/`command_palette.cpp`/`goto_line_bar.cpp`/`grep_bar.cpp`/`codepage_convert.cpp`×2)が全て`std::u16string(util::fromWstringView(...))`という明示的direct-initを踏襲していたことも同時に確認した。

## 選択肢

1. **`registerCommand`を実UIウィジェット(`ui::CommandPalette`への実配線)+`main.cpp`初配線まで含めてフル実装する**
2. **`registerCommand`をヘッドレスな`ui::PluginCommandRegistry`状態層として実装し、`ui::CommandPalette`への実配線・`main.cpp`配線・プラグインunload時の自動クリーンアップは延期する(採用)**
3. **`registerCommand`自体を今回も見送る**

## 決定

**選択肢2を採用する。** `NeoMifesCoreApi::registerCommand`を実装し、実際の消費先として新規`ui::PluginCommandRegistry`(Win32/plugin_sdk非依存、ヘッダオンリーの純粋状態クラス、既存`ui::CommandDescriptor`をそのまま再利用)を新設する。コールバックは登録時ではなく**後で**(ユーザーがコマンドを実行した時点を模した`action()`呼び出しで)実行され、既存の`invokePluginCallbackSafe`(Phase 8a)を公開昇格して再利用したSEHトランポリンで保護する。`ui::CommandPalette`への実配線・`main.cpp`への配線・プラグインunload時の自動`unregisterCommand`は全て次サブフェーズへ延期する。

## 根拠

### `registerCommand`の`callback`だけが「後で呼ばれる」唯一の例外である理由

`plugin_sdk.h`の既存スレッド契約は「`ctx`を受け取ったコールバックが戻った後は呼び出し禁止」だったが、`registerCommand`はまさにこれを覆す機能(コマンドをパレットに登録し、後でユーザーが選んだ時点で実行する)そのものであり、この契約を無条件に守ったままでは実装できない。ADR-019が指摘した2つの前提条件(SEH保護された遅延呼び出し機構・実行時コマンド登録API)を今回で満たした上で、`plugin_sdk.h`のスレッド契約コメントに例外パラグラフを追加し、`registerCommand`の`callback`のみがこの例外に該当することを明記した。

### 既存の`invokePluginCallbackSafe`を公開昇格して再利用した理由

新しいトランポリンを別途書くと、「SEHで無条件に`EXCEPTION_EXECUTE_HANDLER`を使う」という安全性判断の実装が2箇所に分裂し、将来どちらかだけ更新されて挙動がズレるリスクを生む。`registerCommand`のコールバックシグネチャが`onLoad`/`onUnload`と完全に同じ(`void (*)(NeoMifesPluginContext*)`)であることを確認した上で、`plugin_host.cpp`の無名namespaceから`neomifes::plugin`名前空間の公開関数へ昇格するだけで済ませた(本体は一切変更していない)。`registerCommandImpl()`が構築するラムダは`callback`(関数ポインタ)と`ctx`(生ポインタ)のみを値キャプチャし、ラムダの`operator()`自体には`__try`/`__except`を書かない(トランポリンは別関数の中にある)ため、MSVCの「`__try`/`__except`と非トリビアルデストラクタを持つローカルオブジェクトを同居させない」制約にも抵触しない。

### `ui::PluginCommandRegistry`を`ui::CommandDescriptor`そのままの容れ物にした理由

新規のエントリ型を発明せず、既存の`ui::CommandPalette`が保持する型と全く同じ`ui::CommandDescriptor`をそのまま格納する設計にした。これにより将来`ui::CommandPalette`への実配線サブフェーズが来た際、`registry.commands()`をそのまま渡す(あるいは`CommandPalette`側に追加APIを1つ足す)だけで済み、変換層を新設する必要がない。重複id登録は許容する(既存`CommandPalette::m_commands`自体に重複排除ロジックが無いことに合わせた意図的な単純化、CLAUDE.mdルール3)。

### `registerCommand`を権限ゲートしない理由

`showToast`と同じ論法(ADR-019参照): コマンドを1件登録する行為自体はデータの読み書きを一切伴わない低リスクな操作であり、実際の権限境界は登録された`callback`が後で呼ばれた時点で`ctx->coreApi`(既に権限ゲート済み)経由でそのまま働く。`NEOMIFES_PLUGIN_PERMISSION_NONE`のプラグインが登録したコマンドのコールバックが`ctx->coreApi->insertText`を呼んでも、そのポインタは既にNULLのまま(Phase 8dのゲートがそのまま機能する)。新規の権限カテゴリを追加することも検討したが、`showToast`と同様、具体的な必要性が無い状態でカテゴリを増やすのはCLAUDE.mdルール3に反すると判断した。

### `ctx`を第一引数に取る理由(`showToast(sink, message)`との非対称性)

`showToast`はroadmapスケッチから逸脱して`showToast(sink, message)`という「sink直接渡し」の形を採用したが(ADR-019)、`registerCommand`は逆にroadmapスケッチ通り`registerCommand(ctx, id, title, callback)`という「ctx第一引数」の形を採用した。理由: `callback`は後で(`ctx`が指すコンテキストと共に)再実行される必要があり、`ctx`自体を保持しておかなければ`coreApi`/`document`/`toastSink`/`commandRegistry`といった他の能力面に一切アクセスできないコールバックになってしまう。`ctx`はすでに`commandRegistry`フィールドも運んでいるため、`sink`のような別引数を追加で用意する必要も無い。

### `NEOMIFES_CORE_API_VERSION`を`2u`→`3u`へ引き上げる理由

Phase 8eの前例(`1u`→`2u`)と同じく、`NeoMifesCoreApi`構造体へ実際にフィールド(`registerCommand`)が追加されたための意図通りのインクリメント。

### プラグインunload後の「stale」コマンド呼び出しについて、自動クリーンアップを実装しない理由と、その安全性の正確な位置づけ

`PluginHost::unload()`は`m_context.reset()`で`NeoMifesPluginContext`を実際に解放する。もし`ui::PluginCommandRegistry`に残ったエントリの`action()`がunload後に呼ばれた場合、解放済みメモリを指す`ctx`を経由してコールバックへ入り込むことになり、これは**未定義動作**である。SEHトランポリンはこれを「クラッシュする可能性を減らす」ことはできるが「安全である」ことを保証しない — 解放領域がまだ再利用されていなければハードウェア例外が起きずに不定なデータを読んで静かに"成功"する可能性すらある。

このため今回は、どのプラグインがどのコマンドを登録したかを追跡する所有権管理機構(`unload()`時に該当コマンドだけを自動的に`unregisterCommand`する仕組み)を意図的に実装しなかった。`main.cpp`は今も`PluginHost`を一切使っておらず(Phase 8a〜8eいずれも未配線)、「複数プラグインが同時にロード/アンロードされる」という具体的な要求がまだ存在しない状態で先行実装するのはCLAUDE.mdルール3/10に反すると判断した。

**この設計判断は、実装中の自動テストによっても裏付けられた。** 当初、この「unload後の呼び出し」を実際に実行し「プロセスが生存すること」を確認する統合テストを書いたが、`ubsan`プリセット(AddressSanitizer)で実行すると**期待通り確実に失敗した** — ASanが実際のヒープuse-after-freeを正しく検出し報告したためである。これはASanが本来の役目を正しく果たしている証拠であり、`registerCommand`の実装に不具合があるわけではない。CLAUDE.mdの品質ゲート(「Debugビルドで ASan/UBSan走行時のクラッシュ0」)を満たすには、このテストはASanの検出そのものを覆い隠すことになり本末転倒であるため、削除した。「unload後にstaleなコマンドを呼ぶことは安全でない」という事実自体は`plugin_sdk.h`のスレッド契約コメントに明記し、自動テストでは検証しない(検証しようとすると、Debug/Release環境ではメモリが未再利用のため「たまたま」再現せず、ASan環境では確実に検出される、という**ビルド構成依存の不安定なテスト**になってしまうため)。

## 影響

### 実装(`include/`, `src/plugin/`, `src/app/`, `src/ui/`, `plugins/samples/`, `tests/`)
- 新規`src/ui/include/neomifes/ui/plugin_command_registry.h`: ヘッダオンリー`ui::PluginCommandRegistry`クラス(`registerCommand`/`unregisterCommand`/`commands()`)
- `include/neomifes/plugin_sdk.h`: `NeoMifesPluginContext`の前方宣言追加(`NeoMifesCoreApi`より前に必要、Plan agentが検出した必須の機械的追加)、`NeoMifesCommandRegistry`不透明ハンドル、`NEOMIFES_CORE_API_VERSION`を3へ、`NeoMifesCoreApi::registerCommand`、`NeoMifesPluginContext::commandRegistry`追加、スレッド契約コメントへの例外パラグラフ追加、`showToast`コメント中の「hypothetical registerCommand」という記述をADR-020参照へ更新
- `src/plugin/include/neomifes/plugin/plugin_host.h`/`.cpp`: `invokePluginCallbackSafe`を公開昇格(無名namespaceから`neomifes::plugin`名前空間へ、本体は無変更)、`load()`に`commandRegistry`パラメータ追加
- `src/app/include/neomifes/app/plugin_core_api_bridge.h`/`.cpp`: `toNeoMifesCommandRegistry()`+`registerCommandImpl()`追加、`kFullCoreApi`/`kDocumentDeniedCoreApi`双方に設定(ゲート無し)
- `src/app/CMakeLists.txt`: `neomifes_app_input`が新たに`neomifes::plugin`をPRIVATEリンク(`invokePluginCallbackSafe`のシンボル解決に必要)
- 新規`plugins/samples/command_plugin/`: `NEOMIFES_PLUGIN_PERMISSION_NONE`を宣言しつつ`registerCommand`+登録したコマンドから`showToast`を呼び出し、権限ゲートされていないこと・遅延呼び出しが`ctx->coreApi`まで正しく到達することを実証するテスト専用サンプル
- 新規`plugins/samples/crashing_command_plugin/`: 登録したコマンドのコールバックが意図的にクラッシュし、`load()`/`unload()`の呼び出しスタックの外側で起きるクラッシュもSEHで隔離されることを実証するテスト専用サンプル
- `tests/unit/ui_plugin_command_registry_test.cpp`(新規)、`tests/unit/app_plugin_core_api_bridge_test.cpp`(新規7テスト)、`tests/integration/plugin_command_test.cpp`(新規)

### 実装しない(引き続きスコープ外)
- `ui::CommandPalette`への実配線(`registry.commands()`を実際にパレットへ供給する仕組み)
- `src/app/main.cpp`への配線(実アプリは今回もプラグインを一切ロードしない、Phase 8a〜8eと同じ「ヘッドレスのみ」方針を継続)
- プラグインunload時の登録済みコマンドの自動`unregisterCommand`(上記根拠参照、所有権追跡機構が必要)
- プラグイン自身が能動的に呼べる`unregisterCommand`相当のCoreApi関数(roadmapに記載無し、CLAUDE.mdルール3)
- コマンドの重複id検出・拒否

## 将来の再評価タイミング

- **`ui::CommandPalette`への実配線+プラグインunload時の自動クリーンアップ:** `PluginHost`が初めて`main.cpp`へ配線されるサブフェーズ(まだ日程未定)。「複数プラグインが同時にロード/アンロードされる」という具体的な要求が生まれた時点で、所有権追跡の設計を改めて検討する。

## 参考
- `docs/design/master_roadmap.md` §8.3
- `src/ui/include/neomifes/ui/toast_state.h`(Phase 8e、`ui::PluginCommandRegistry`が踏襲したヘッダオンリー純粋状態クラスの前例)
- `src/plugin/src/plugin_host.cpp`(`invokePluginCallbackSafe`の元実装、SEHトランポリンの設計根拠)
