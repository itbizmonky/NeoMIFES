# ADR-017: プラグインの資源制限は Windows Job Object の `JOB_OBJECT_LIMIT_ACTIVE_PROCESS=1` のみを有効化し、メモリ/CPU時間制限と AppContainer 化は見送る

- **ステータス:** Accepted
- **決定日:** 2026-08-02 (Phase 8c 実装完了時)
- **関連:** [ADR-015](ADR-015-plugin-host-c-abi-seh.md)(C ABI + SEH クラッシュ隔離の前提設計)、[ADR-016](ADR-016-plugin-core-api-bridge.md)(`NeoMifesCoreApi`橋渡し)、`docs/design/master_roadmap.md` §17.1、CLAUDE.md 絶対ルール3(推測実装をしない)・ルール10(性能改善は必ずベンチマーク結果を根拠とする)

## コンテキスト

Phase 8b(`NeoMifesCoreApi`橋渡し実装)完了後、ユーザーから次のPhaseとしてAppContainerサンドボックス(master_roadmap.md §17.1「レベル3」)が選ばれた。

**着手前調査(Explore agent + Microsoft Learn直接確認、CLAUDE.mdルール3)で判明した重大な事実:** AppContainerは既存の「同一プロセス内`LoadLibraryW`」アーキテクチャへ後付けできない。AppContainerはプロセス生成時にのみ付与できるセキュリティトークン機構(`CreateAppContainerProfile()`+`PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES`+`CreateProcess()`)であり、既に起動済みの通常プロセスへ遡って適用するWin32 APIは存在しない。適用にはプラグインを別プロセスとして起動し直す必要があり、これは`PluginHost`の全面再設計・`NeoMifesCoreApi`のRPC化・本リポジトリに現状ゼロのIPC基盤の新規構築を意味する — まさにADR-015が「Phase 8aのスコープを大幅に超える」として一度却下した「選択肢3(別プロセス+IPC)」そのものである。

この状況をユーザーへ再提示し、**「Job Object資源制限のみに縮小」**が選ばれた — §17.1の3段階モデルのうち「レベル2」(Job Objectでリソース制限)のみを実装し、「レベル3」(AppContainer完全隔離)は据え置く。

## 選択肢

1. **メモリ/CPU時間のJob Object制限を有効化する**(§17.1のスケッチ「メモリ・CPU時間・ハンドル数の上限」通り)
2. **`JOB_OBJECT_LIMIT_ACTIVE_PROCESS=1`のみを有効化する(採用)**
3. **Job Objectサンドボックス自体を見送る**

## 決定

**選択肢2を採用する。** プロセス全体(ホスト本体+ロード中の全プラグイン)を1つの無名Job Objectへ自己登録し、`JOB_OBJECT_LIMIT_ACTIVE_PROCESS`(`ActiveProcessLimit=1`)のみを設定する。メモリ/CPU時間の上限、および`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`は意図的に設定しない。

## 根拠

### メモリ/CPU時間制限(選択肢1)を採用しない理由

プラグインは現状ホストと同一プロセスで動作するため(ADR-015)、「プラグインだけの」メモリ・CPU使用量を個別に計測する手段が無い — プロセス分離無しには構造的に不可能である。したがって、プロセス全体にメモリ/CPU時間の上限を掛けると、**本プロジェクトが掲げる中核価値「10GBファイル対応」(CLAUDE.md)と正面衝突する。** Phase 7aの実測(100万行の完全tree-sitter再解析で約6.6秒のCPU時間)のような正当な重い処理中に、OSがプロセスごと強制終了しかねない。これは実測データの無いまま恣意的な数値を決め打ちすることになり(CLAUDE.mdルール10)、セキュリティ向上どころか通常利用時の致命的な機能後退になる。ハンドル数についても、Win32 Job ObjectのAPI(`JOBOBJECT_BASIC_LIMIT_INFORMATION`/`JOBOBJECT_EXTENDED_LIMIT_INFORMATION`、Microsoft Learn直接確認済み)にはそもそもハンドル数上限を表す`LimitFlags`ビットが存在しない(§17.1のスケッチ自体が実装不可能な項目を含んでいた)。

### `ActiveProcessLimit=1`のみを採用する理由

Microsoft Learn(`JOBOBJECT_BASIC_LIMIT_INFORMATION`)で仕様を直接確認済み: `JOB_OBJECT_LIMIT_ACTIVE_PROCESS`は「Job内の同時アクティブプロセス数の上限」を課す。ホスト本体もロード中のプラグインも現状一切子プロセスを起動しないため(`CreateProcess`/`ShellExecute`等の呼び出しが本コードベースに存在しないことを確認済み)、この制限は今日の正当な機能を一切損なわずに「悪意あるプラグインが`cmd.exe`/`powershell.exe`等の子プロセスを起動する」という攻撃カテゴリ全体を塞ぐ、実質的で無害な防御になる。

**実測による裏付け(本ADR採択の根拠として記録、CLAUDE.mdルール3):** `tests/integration/plugin_sandbox_test.cpp`の`ChildProcessCreationFailsOnceSandboxedAndCallerSurvives`で、`ensureProcessSandboxed()`成功後に`CreateProcessW`を試みると失敗し(呼び出し元プロセスは`ERROR_NOT_ENOUGH_QUOTA`相当のエラーを受け取るのみ)、**呼び出し元プロセス自身は生存し続けて後続のアサーションを実行できる**ことをローカル実機(Debug/Release/ubsan全構成)で確認した。Plan Mode段階ではMicrosoft Learnの文面+コミュニティ報告からの推定に留まっていたが、実装フェーズで初めて実機検証により裏付けられた。

### `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`を設定しない理由

本設計はホスト自身のプロセスが自分自身の作成したJob Objectへ自己登録する構成である。同フラグ本来のユースケース(別プロセスがJobハンドルを保持し、そのコントローラがクラッシュした際に道連れで子プロセスを終了させる)が本設計には存在しない。有効化すると、Jobハンドルを誤って早期にクローズした場合の自己終了リスクだけが増え、利益が無い。

### `PluginHost::load()`へ自動フックしない理由

`ensureProcessSandboxed()`(`AssignProcessToJobObject`)は**プロセスの生存期間中ずっと後戻りできない片道操作**である(Win32に「Jobから外れる」APIは無い)。本リポジトリの単体テストは約40ファイルが`tests/unit/CMakeLists.txt`の1つの`neomifes_unit_tests.exe`プロセスに同居しており、`PluginHost::load()`から無条件に自動呼び出しすると、既存の`plugin_plugin_host_test.cpp`の失敗系テスト(`LoadOfNonexistentPathFailsWithLoadLibraryFailed`)が走った瞬間、そのテストバイナリプロセス全体が以後二度と子プロセスを起動できなくなるという、無関係なテストへの重大な副作用を生む。このため`ensureProcessSandboxed()`は独立したAPI(`plugin_sandbox.h`)とし、`PluginHost::load()`からは呼ばない。実際の呼び出し元は将来`src/app/main.cpp`が起動時に1回呼ぶ想定だが、Phase 8a/8bと同じ「main.cpp無改修、テストのみで証明する」方針を今回も継続し、実際の配線はスコープ外とする。

### 失敗を非致命的だが必ず観測可能にする設計

`AssignProcessToJobObject`は、呼び出し元プロセスが既にネスト不可のJobへ所属している環境(一部のCI/コンテナ/ターミナルラッパーが子プロセスをJobで囲うことがある)で失敗しうる。失敗してもプラグインのロード自体は継続できなければならない(`outline.cpp`の空`SymbolTable`と同じ「安全な劣化」方針)が、`PluginExpected<void>`型を通じて必ず観測可能にし、黙って握り潰さない(CLAUDE.mdルール3)。`tests/integration/plugin_sandbox_test.cpp`の各テストは`ensureProcessSandboxed()`失敗時に`GTEST_SKIP()`する(既存の`platform_clipboard_test.cpp`等と同じ確立済みイディオム) — このローカル開発機・CI(windows-2022ランナー)いずれでも実際にはスキップされず全テストが実行されたことを確認済み。

## 影響

### 実装(`src/plugin/`, `tests/integration/`)
- 新規`neomifes::plugin::ensureProcessSandboxed()`/`queryActiveJobLimits()`(`plugin_sandbox.h`/`.cpp`)
- `plugin_error.h`へ`PluginErrorCode::SandboxSetupFailed`を1件追加(`CreateJobObjectW`/`SetInformationJobObject`/`AssignProcessToJobObject`いずれの失敗も同じコードへ集約、`win32Error`+`describe()`で診断)
- 新規`tests/integration/plugin_sandbox_test.cpp`(専用exe — `AssignProcessToJobObject`が片道操作であるため、既存の他テストバイナリへ混在させない)

### 実装しない(引き続きスコープ外)
- Windows AppContainer本体(§17.1レベル3、別プロセス+IPC全面再設計が前提)
- メモリ/CPU時間/ハンドル数のJob Object制限(プロセス全体を巻き込むため不採用、上記根拠参照)
- `permissions`マニフェストビットフィールド・`manifest.json5`+Authenticode署名検証・マーケットプレース
- `registerCommand`/`showToast`/ネットワーク・ファイルシステム系`NeoMifesCoreApi`関数(権限モデルが無い)
- `Ctrl+Shift+X`プラグイン管理UI(`ensureProcessSandboxed()`の結果を将来表示する想定の消費者)
- `src/app/main.cpp`への配線(`ensureProcessSandboxed()`もPhase 8a/8bと同じくヘッドレスのまま)

## 将来の再評価タイミング

- **`ActiveProcessLimit=1`の緩和:** roadmap §11.2(LSP統合)は言語サーバーを子プロセスとして起動する設計であり、Phase 11着手時に本制限が正面衝突する。その時点で緩和方式(上限値の引き上げ、あるいはプラグインロード後の一時的な制限適用など)を再設計する必要がある。なお§11.1(Git統合)はlibgit2をリンクライブラリとして使う設計であり子プロセスを起動しないため、本制限と衝突しないことを確認済み。
- **真のセキュリティ隔離(AppContainer/別プロセス):** ADR-015/016と同じ再評価条件(マーケットプレース等で未検証のサードパーティプラグインを実行する運用が具体化した時点)。

## 参考
- `docs/design/master_roadmap.md` §17.1
- `src/platform/include/neomifes/platform/handle_guard.h`(`KernelHandle`)
- Microsoft Learn: `JOBOBJECT_BASIC_LIMIT_INFORMATION`/`JOBOBJECT_EXTENDED_LIMIT_INFORMATION`/`CreateJobObjectW`/`AssignProcessToJobObject`
