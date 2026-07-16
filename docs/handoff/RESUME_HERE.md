# NeoMIFES — 次回セッション再開ガイド

> **最終更新:** 2026-07-17 (Phase 4b2 完了後)
> **次回開いたら最初にこのファイルを読むこと。**
> **本ファイルは毎セッション終了時に全文点検し、完了済み手順や重複する次アクションを削除・更新すること** (CLAUDE.md §11 セッション終了時チェックリスト参照)。

---

## 1. 現在の状態 (一目)

| 項目 | 状態 |
|---|---|
| Phase 0 (要件確認・設計書・自己レビュー) | ✅ 完了 |
| Phase 0.5 (ビルド基盤 / CI / 静的解析) | ✅ 完了 (CI green 達成) |
| Phase 1 (Win32 骨組み + 起動 0.3s/20MB PoC) | ✅ 完了 (CI 実測 22ms) |
| Phase 2a (Document Engine API + MVP 実装 + テスト網羅) | ✅ 完了 |
| Phase 2b1 (B-1 pieceView + B-2 AddBuffer チャンク化) | ✅ 完了 |
| Phase 2b2 Step 1+2 (PieceTree insert/split/erase、PieceTable 差し替え) | ✅ 完了 |
| Phase 2b3 Step 1+2 (mmap+Lazy Decode + SEH + bench + Phase 2b 完了報告) | ✅ 完了 |
| Phase 3 着手前レビュー (設計書のADR-007同期) | ✅ 完了 |
| Phase 3 着手前ハウスキーピング (WarningsAsErrors/Named Mutex/UBSan CI) | ✅ 完了 |
| Phase 3a (D2D/DXGI/COM 基盤配線) | ✅ 完了 |
| Phase 3b (DirectWrite テキストレイアウト + Document実描画) | ✅ 完了 |
| Phase 3c (TextLayoutCache + 粗粒度フレームスキップ + `--measure-frame`) | ✅ 完了 |
| **Phase 3 全体 (60fps スクロール確認 DoD 達成、[`phase_3_report.md`](../phase_reports/phase_3_report.md) 発行済み)** | ✅ **完了** |
| Phase 4a (Cursor/SelectionModel/Command/UndoStack、ヘッドレス、100万Undo DoD 実測) | ✅ 完了 (ADR-012) |
| Phase 4b1 (キーボード入力配線・キャレット描画・マウスホイールスクロール) | ✅ 完了 |
| Phase 4b2 (マウスクリック位置特定・選択範囲ハイライト描画) | ✅ 完了 |
| **Phase 4b3 (ドラッグ選択・ダブル/トリプルクリック・複数カーソル)** | ⏭️ **次回着手** |

---

## 2. ビルド検証について

**訂正 (2026-07-15):** 過去のセッションで「この環境には MSVC が無い」と誤って記録・運用していたが、実際には **Visual Studio Community 2026 (v18.2.1、MSVC 19.50/14.50.35717) がインストール済み**で、実機ビルド・テスト・clang-tidy がローカルで実行可能。今後は **push 前に必ずローカル検証すること**。CI 専用ワークフローに逆戻りしない。

Bash では `cl`/`cmake`/`ninja` に PATH が通っていない (`which` で見つからない) が、これは不在を意味しない。**PowerShell + `Enter-VsDevShell` を使うこと** (環境変数はコマンド間で持続しないため、1回の呼び出し内で完結させる):

```powershell
$vsPath = "C:\Program Files\Microsoft Visual Studio\18\Community"
Import-Module "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll" -ErrorAction Stop
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
Set-Location "D:\IDE\Claude\NeoMIFES"
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

Release + ベンチ実測値の取得も同様 (`--preset release`、`.\build\release\tests\bench\neomifes_document_bench.exe --benchmark_min_time=0.2s`)。

clang-tidy (LLVM 20.1.8 が VS にバンドル) — **変更したファイルだけを個別に**実行すること (全ファイル一括だと数分でタイムアウトすることがある):
```powershell
$tidy = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
& $tidy -p build\debug --quiet --extra-arg=-Wno-unused-command-line-argument <変更したファイル>
```

`Enter-VsDevShell` 実行時に出る `'vswhere.exe' は、内部コマンドまたは...` という警告は無害 (`-VsInstallPath` を明示しているため実害なし)。

- **リポジトリは初期化・push 済み。** `git init` は不要 (Session 7 で完了、以降 main ブランチへの push を継続)
- 変更を加えてローカル検証が green になったら `git add` → `git commit` → **ユーザーに `git push` を依頼** (エージェントは push しない方針)
- push 後も `gh run list --limit 3` で CI 結果を確認する。**ローカルとCIでMSVCバージョンが異なりうる** (CI は 14.44、ローカルは 14.44/14.50 両方) ため、ローカル green は「ほぼ確実」であって「絶対」ではない
- CI が失敗した場合は `gh run view <id> --log-failed` でログを取得。Windows/MSVC/clang-tidy 特有の落とし穴は Claude のメモリ機能内 `reference_windows_cpp_ci_gotchas.md` に集約済み

---

## 3. Phase 2b (全 Step) 完了記録

### 3.1 参照した意思決定
1. [**ADR-007**](../decisions/ADR-007-piece-tree-mutable-rb.md) — Mutable RB-Tree + Piece-Vector Snapshot (Phase 2b2 で採用)
2. [`docs/issues/lazy_decode_mmap.md`](../issues/lazy_decode_mmap.md) — mmap + Lazy Decode の設計・完了条件・実測値
3. [`docs/issues/line_index_o_log_n.md`](../issues/line_index_o_log_n.md) — LineIndex O(log n) 化の撤回理由 (参考、対応不要)
4. [`docs/phase_reports/phase_2b_report.md`](../phase_reports/phase_2b_report.md) — 2b1/2b2/2b3 統合レポート (最新の完了状況はここが一次資料)

### 3.2 Phase 2b1〜2b3 完了サマリ
- ✅ `BufferSnapshot::pieceView` + AddBuffer チャンク化 (Phase 2b1)
- ✅ `PieceTree` (insert/split/erase, CLRS 13.3+13.4) + `PieceTable` 内部差し替え、プロパティテスト 20,000 反復化 (Phase 2b2)
- ✅ `OriginalBuffer` を mmap + Lazy Decode に全面再設計、64KiB チェックポイント索引、on-demand decode + キャッシュ (Phase 2b3 Step 1)
- ✅ SEH (`__try`/`__except`, `EXCEPTION_IN_PAGE_ERROR`) をスキャン/デコードの両経路に配線、100MB/1GB load ベンチ新設、実測値取得、`docs/phase_reports/phase_2b_report.md` 発行 (Phase 2b3 Step 2)
- テスト数: 80 (Phase 2b2 完了時) → 93 (Phase 2b3 Step 2 完了時)

### 3.3 Phase 2b 全体の完了条件 (最終)
- [x] `PieceTable::insert` / `erase` が O(log n) (tree 経由で達成)
- [x] `PieceTable::insert` (small edit) < 500ns 中央値 — CI 実測 243〜276ns
- [x] ~~`PieceTable::snapshot` 100K piece で ≤1ms~~ — 実測 1.196ms (CI) / 1.481ms (ローカル)、約20〜48%超過。低優先度の残タスクとして受容 ([`piece_table_rb_tree.md`](../issues/piece_table_rb_tree.md))
- [x] ~~1GB UTF-8 load ≤ 2s~~ — ローカル Release 実測 2031ms、目標に対し約1.5%超過。ディスクI/O律速でありデコード戦略非依存、低優先度で受容 ([`lazy_decode_mmap.md`](../issues/lazy_decode_mmap.md))
- [x] Working Set 増分 ≤ 30MB — `private_working_set_delta` で実測 0.46MB (1GB) / 0.078MB (100MB)、目標を大幅クリア (総 Working Set が file size 相当になる件の解釈は issue doc 参照)
- [x] 既存単体テスト + プロパティテスト (20,000 反復) 全 green を維持 (93/93)
- [x] RB invariant テスト (root black / no red-red / uniform black height / aggregate 整合) 追加済
- [x] OriginalBuffer の mmap + Lazy Decode コア実装・テスト
- [x] SEH によるネットワークドライブ例外対策

### 3.4 Phase 3 着手前ハウスキーピング (2026-07-15 レビューで期限確定 → 2026-07-16 に全3件完了)

Phase 0.5/1 から「次のフェーズで」と繰り返し先送りされてきた技術的負債3件、全て解消済み。

1. ✅ **Named Mutex 単一インスタンス化** (basic_design §2.3)。`src/app/main.cpp` に `claimSingleInstance()` を追加 — `CreateMutexW` で多重起動を検出し、既存ウィンドウを `FindWindowW`+`SetForegroundWindow` でフォアグラウンド化する。**IPC 経由のコマンドライン引数委譲は未実装** (basic_design §2.3 が想定する完全な形は SessionManager が必要で Phase 4+ まで存在しないため、今回は意図的に見送り — 投機的実装をしない CLAUDE.md ルール#3 に基づく判断)。`--measure-startup`/`--measure-memory` モードはこのチェックの対象外。ローカルで実プロセス2重起動を確認済み
2. ✅ **CI に clang-cl UBSan ジョブ追加** (self-review R4)。clang-cl 用の CMake 設定 (`ubsan` プリセット新設、CRT を `/MT` static release に切替、`-fno-sanitize=alignment` で Microsoft STL/UCRT 由来の誤検知を除外) を追加。ローカルで clang-cl ビルド+93テスト全pass を確認してから `.github/workflows/ci.yml` に `ubsan` ジョブを追加。詳細は [`cmake/Sanitizers.cmake`](../../cmake/Sanitizers.cmake) のコメント参照
3. ✅ **`.clang-tidy` の `WarningsAsErrors` 有効化** (Phase 0.5 P05-4)。実態調査の結果 `src/`47件・`tests/`276件の既存警告が判明したため、ユーザーと相談の上 **`src/` (本番コード) のみ先に有効化**する方針に確定:
   - `src/` の47件は全て個別修正 (const化・designated initializer化・`std::ranges`化・実質バグではない項目は理由付き `NOLINT`) — 0件まで削減
   - 新設 [`src/.clang-tidy`](../../src/.clang-tidy) (`InheritParentConfig: true` + `WarningsAsErrors: '*'`) で src/ のみ有効化。ルートの `.clang-tidy` は `WarningsAsErrors: ''` のまま (tests/ はこちらが適用される)
   - **注意:** `InheritParentConfig` は `WarningsAsErrors` をカンマ結合でマージするため、「ルート='\*' + 子='\''」による無効化は機能しない (`'*,'` になり実質 '*' のまま)。逆に「ルート='' + 子='\*'」の一方向加算アプローチが正しく機能することを確認した上で採用
   - `tests/` の276件 (主に Google Benchmark マクロ由来の構造的警告) は別途の優先度低いフォローアップとして残す。将来対応する場合は `tests/.clang-tidy` で個別チェックを無効化する方式を検討 (上記の理由で単純な `WarningsAsErrors` オーバーライドは使えない)
   - ローカルで CI 相当の全31ファイルスキャンを実施し ALL PASS を確認

### 3.5 Phase 3a (D2D/DXGI/COM 基盤配線) 完了記録

**参照した意思決定:** [ADR-008](../decisions/ADR-008-com-raii-comptr.md) (ComPtr採用) / [ADR-009](../decisions/ADR-009-deferred-device-init.md) (デバイス生成タイミング)。詳細な設計判断はいずれもADR本文とTIMELINE.md Session 18参照。

**成果物:**
- 新規 `src/render/` レイヤ: `resize_math.h` (純粋関数)、`render_error.h/cpp` (`RenderExpected<T> = std::expected<T,RenderError>` — プロジェクト初のstd::expected採用)、`d2d_factories.h/cpp` (プロセス単位シングルトン)、`render_device.h/cpp` (D3D11+D2D+DXGIデバイスグラフのRAII所有、HARDWARE→WARPフォールバック)、`render_pipeline.h/cpp` (attach/resize/renderファサード、デバイスロスト時の全体再生成)
- `MainWindow`: `onDeferredInit`/`onResize`/`setPaintHandler()`追加、`WM_SIZE`/`WM_DPICHANGED`ハンドリング新設。GDIプレースホルダーはレンダラ未アタッチ時のフォールバックとして温存
- `main.cpp`: `LaunchMode::Normal`時のみ`RenderPipeline`配線 (ADR-009により`--measure-startup`/`--measure-memory`のタイミング契約は構造的に無傷)
- テスト数: 93 → 109 (単体+11、統合+1 `render_device_smoke_test.cpp`)

**完了条件:**
- [x] GDIプレースホルダー描画がD2Dクリア描画に置き換わる (実アプリでd2d1.dll/d3d11.dll/dxgi.dllのロードを確認)
- [x] リサイズ・DPI変更に耐える (4段階リサイズでクラッシュなし、スクリーンショットで表示崩れなしを確認)
- [x] デバイスロスト時の再生成ロジック実装 (`RenderPipeline::recreateDevice`、実機での強制デバイスロスト誘発テストは未実施 — 通常操作では発生しないため統合テストでは検証していない、既知の限界)
- [x] 起動時間退化なし (ローカル実測 firstPaintNs=33.16ms、目標300msの11%)
- [x] ローカル Debug/Release 全109テスト green、clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み) 新規警告0
- [ ] mmapビュー数上限のLRU追い出し等は対象外 (Phase 3aのスコープ外、そもそもRenderingでは無関係)

**スコープ外 (Phase 3b以降に持ち越し):** DirectWriteテキストレイアウト・Document内容の実描画、TextLayoutCache/GlyphCache/DamageTracker、60fps計測ハーネス、Line Gutter・テーマ・日本語フォントフォールバック・IME

### 3.6 Phase 3b (DirectWrite テキストレイアウト + Document実描画) 完了記録

**参照した意思決定:** [ADR-010](../decisions/ADR-010-render-depends-on-document.md) (Rendering Engine → Document Engine 直接依存)。着手前に洗い出した4件の設計課題は `detailed_design.md` §4.4 で全て解決済みマーク済み。

**成果物:**
- `RenderDevice`: `clearAndPresent()` を `beginFrame()`/`endFrame()` に分解 (DC を非所有ポインタで貸し出し、`m_frameOpen` で誤用ガード)。`setDpi()` 追加
- `Document`: `version()` カウンタ追加 (mutator 3箇所で `++m_version`)。`offsetToLine`/`lineToOffset` を `mutable` キャッシュ経由の `const` メソッドに変更 (`RenderPipeline` が `const Document*` 越しに呼べるようにするため)
- `RenderPipeline`: `setDocument()`/`setTopLine()`/`topLine()` 追加。`refreshDocumentCacheIfStale()` が `Document::version()` を比較して `snapshot()` を呼ぶ唯一の箇所 (§4.3 ガードレールの実装)。`ensureTextFormat()` (Consolas 14pt、`DWRITE_WORD_WRAPPING_NO_WRAP` 必須)・`ensureTextBrush()`・`drawVisibleLines()` (可視範囲を1回の `extract()` で取得し `\n` 分割して `DrawText`) を追加。`resize()` に `dpiScale` 引数追加
- 新規 `src/render/include/neomifes/render/viewport_math.h`: `computeVisibleLineCount()` (純粋関数)
- `main.cpp`: `--open <path>` 引数追加 (`loadUtf8File` 失敗時は空 `Document` にフォールバック、起動をブロックしない)。`Document` を `window`/`renderPipeline` より前に宣言し生存期間を保証
- `src/render/CMakeLists.txt`: `neomifes::document` を `PUBLIC` 追加 (ADR-010)
- テスト数: 109 → 123 (単体+7: `render_viewport_math_test.cpp`/`document_document_test.cpp`、統合+2: `render_device_smoke_test.cpp` に誤用ガード2件追加、新規 `render_text_smoke_test.cpp`)

**完了条件:**
- [x] Document の内容が DirectWrite で実描画される (`--open <file>` で実ファイルを開き、複数行・タブ含めて正しく表示されることをスクリーンショットで確認)
- [x] リサイズで崩れ・クラッシュがない (600x400→1400x900→300x200→1000x650 の4段階リサイズで確認)
- [x] `snapshot()` がフレームごとに呼ばれない (`refreshDocumentCacheIfStale()` のバージョン比較で保証、統合テストで再取得/再利用の両経路をカバー)
- [x] ローカル Debug/Release 全123テスト green、clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み) 新規警告0
- [ ] ピクセル単位の描画正しさ検証は対象外 (キャプチャ機構が Phase 3c/計測ハーネス側の関心事のため、統合テストは「クラッシュ・エラーなく描画される」のみを保証)

**スコープ外 (Phase 3c以降に持ち越し):** `TextLayoutCache`/`GlyphCache` (LRU)、`DamageTracker`/ダーティ矩形部分描画、60fps計測ハーネス (`--measure-frame`)、Line Gutter・テーマ・日本語フォントフォールバック・IME、対話的スクロール入力 (Phase 4 Editor Core)

### 3.7 Phase 3c (TextLayoutCache + 粗粒度フレームスキップ + `--measure-frame`) 完了記録 — Phase 3 全体完了

**参照した意思決定:** [ADR-011](../decisions/ADR-011-phase3c-render-cache-scope.md) (TextLayoutCache のみ実装、GlyphCache・細粒度 DamageTracker は延期)。詳細レポート: [`docs/phase_reports/phase_3_report.md`](../phase_reports/phase_3_report.md) (3a/3b/3c 統合)。

**成果物:**
- 新規 `src/render/text_layout_cache.{h,cpp}`: 行番号キーの `IDWriteTextLayout` キャッシュ。`Document::version()` 変化時の wholesale `clear()` のみで無効化 (LRU 無し、無制限成長は [`text_layout_cache_unbounded_growth.md`](../issues/text_layout_cache_unbounded_growth.md) に tripwire として記録)
- `RenderPipeline`: `drawVisibleLines()` を `TextLayoutCache::getOrCreate()` + `DrawTextLayout` に変更。`FrameState`/`captureFrameState()` による粗粒度フレームスキップ (`render()` が前回成功フレームと完全一致なら描画を丸ごとスキップ、`FLIP_DISCARD`+DWM合成下で安全)。`layoutCacheStats()` アクセサ追加
- 新規 `src/app/frame_profile.{h,cpp}` + `main.cpp` の `--measure-frame <out.json>`: 合成ドキュメント (5万行) または `--open` の実ファイルで300フレーム連続スクロールを計測、min/max/avg/p50/p95 + キャッシュ統計を JSON 出力
- 新規 `tests/bench/render_text_layout_cache_bench.cpp`: デバイス/vsync を介さない TextLayoutCache 単体のCPUコスト計測
- `.github/workflows/ci.yml`: 「Frame PoC (report only, no hard fail)」ステップ追加
- テスト数: 123 → 129 (単体+6: `render_text_layout_cache_test.cpp`、統合+2: `render_text_smoke_test.cpp` にキャッシュ/スキップ検証3件追加・新規 `frame_measure_test.cpp`)

**完了条件 (= Phase 3 全体 DoD):**
- [x] 60fps スクロール確認: `--measure-frame` 実測 (50,000行合成ドキュメント、300フレーム、Release) avg 5.52ms / p50 5.56ms / p95 5.66ms / max 8.11ms — 全フレームが16.6ms予算内
- [x] TextLayoutCache miss < 50µs: 実測 532ns (約94倍のマージン)
- [x] TextLayoutCache hit < 5µs: 実測 4.34ns (約1152倍のマージン)
- [x] ローカル Debug/Release 全129テスト green、clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み) 新規警告0
- [ ] GlyphCache・細粒度 DamageTracker は意図的延期 (ADR-011、再評価トリガー明記済み) — 未実装であって未対応ではない

**スコープ外 (Phase 4 以降に持ち越し):** GlyphCache、細粒度 DamageTracker、対話的スクロール入力、Line Gutter・テーマ・日本語フォントフォールバック・IME (旧「Phase 3d」検討事項、Phase 4 とは独立の将来フェーズとして再スコープ確認予定)

### 3.8 Phase 4a (Command/Undo/Selection、ヘッドレス) 完了記録

**参照した意思決定:** [ADR-012](../decisions/ADR-012-phase4a-editor-core-scope.md) (Command/Undo/Selection のヘッドレス基盤のみ実装、UI配線・圧縮/ディスクスワップ・矩形選択を延期)。

**成果物:**
- 新規 `src/core/` レイヤ (`neomifes::core`, `neomifes::document` にのみ PUBLIC 依存、`neomifes::render` には依存しない):
  - `cursor.h`: `Cursor{position, anchor, isPrimary}`(フラット `TextPos`、design doc §5.1 のまま)
  - `command.h`: `ICommand`/`ExecutionContext`(新規グルー、`Document&`+`SelectionModel&` を保持)
  - `selection_model.h/.cpp`: `SelectionModel`(8種の `MovementKind`: Left/Right/Up/Down/LineStart/LineEnd/DocumentStart/DocumentEnd。複数カーソル対応、範囲重複マージ)
  - `edit_commands.h/.cpp`: `InsertTextCommand`/`DeleteRangeCommand`/`ReplaceRangeCommand`
  - `undo_stack.h/.cpp`: `UndoStack`(バケット化/圧縮/ディスクスワップなしのシンプル2スタック実装)
  - `command_dispatcher.h/.cpp`: `CommandDispatcher`(execute→push を1呼び出しにまとめる新規グルー)
  - `viewport.h/.cpp`: `Viewport`(`scrollTo`/`ensureVisible`/`visibleLines`。`FoldingMap` は未実装)
- 新規 `tests/bench/core_undo_stack_bench.cpp` + `neomifes_core_bench` ターゲット: 100万コマンドの push/undo を実測
- テスト数: 129 → 164 (単体+35: `core_selection_model_test.cpp`/`core_edit_commands_test.cpp`/`core_undo_stack_test.cpp`/`core_command_dispatcher_test.cpp`/`core_viewport_test.cpp`)

**完了条件 (= Phase 4 DoD「100万Undo達成」):**
- [x] 100万コマンドの push が完了する: 実測 352ms (Release)
- [x] 100万コマンドの undo が完了する: 実測 174ms (Release)
- [x] ローカル Debug/Release 全164テスト green、`src/core/*.cpp` の clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み) 新規警告0
- [ ] UndoStack のメモリ使用量は未計測 (時間面のDoDは満たすが、メモリ面は [`undo_stack_unbounded_memory.md`](../issues/undo_stack_unbounded_memory.md) の tripwire として記録、Phase 4b の対話的編集セッションで実測予定)

**スコープ外 (Phase 4b 以降に持ち越し):** キーボード/マウス入力の `MainWindow` 配線、キャレット/選択範囲のレンダリング、矩形選択・縦編集コマンド群、`UndoStack` のバケット化/zstd圧縮/ディスクスワップ、`tryMerge` 連続入力パッキング、`MovementUnit`(単語/段落単位移動)、Search/Encoding/Plugin/AI 依存の標準コマンド群、`Viewport` の `FoldingMap`、`RenderPipeline::setTopLine()` への実配線

### 3.9 Phase 4b1 (キーボード入力配線 + キャレット描画 + マウスホイールスクロール) 完了記録

Phase 4b をさらに 4b1/4b2 に分割 (Phase 3 の 3a/3b/3c 分割と同じ理由 — 3層 (ui/app/render) にまたがりマウスクリック位置特定という新規設計判断を含む一括実装は CLAUDE.md ルール8「1PR=1責務」に反するため)。分割の判断・スコープの根拠は本ファイルの計画段階での分析を参照 (ADR新規起票はせず、ADR-012 が既に想定していた「Phase 4b で詳細設計する」範囲内の実装と位置づけた)。

**成果物:**
- `SelectionModel::moveAllTo(TextPos)` 新設 — 編集/Undo/Redo後にキャレットを絶対位置へ飛ばす手段が Phase 4a に無かったギャップを解消
- `ICommand::cursorPositionAfterExecute()`/`cursorPositionAfterUndo()` 新設、3編集コマンドに実装。`CommandDispatcher::dispatch()`/`UndoStack::undo()`/`redo()` が自動的に `SelectionModel::moveAllTo()` を呼ぶよう配線 — Phase 4a のコードレビューで指摘された「`ExecutionContext` が `SelectionModel&` を保持するが未使用」というギャップを埋めた
- `MainWindow`: `onKeyDown`/`onChar`/`onMouseWheel` フック新設、`WM_KEYDOWN`/`WM_CHAR`/`WM_MOUSEWHEEL` 処理を追加 (`WM_LBUTTONDOWN` は Phase 4b2 へ)
- 新規ライブラリ `neomifes::app_input` (`src/app/include/neomifes/app/editor_input.h` + `editor_input.cpp`): Win32非依存の `handleKeyDown`/`handleChar`/`applyMouseWheelScroll`。`tests/unit/app_editor_input_test.cpp` でヘッドレステスト
- `RenderPipeline`: `setCaretPosition(TextPos)` 新設、`drawVisibleLines()` 内でキャレット行に `HitTestTextPosition`+`FillRectangle` で描画。`FrameState` に `caretPosition` を追加し、キャレット単独移動が Phase 3c の粗粒度フレームスキップに飲み込まれる不整合を修正 (統合テストで検証: `RenderTextSmokeTest.CaretOnlyMovementForcesRedrawInsteadOfFrameSkip`)
- `src/app/main.cpp`: `SelectionModel`/`CommandDispatcher`/`Viewport` を `LaunchMode::Normal` で配線、`Viewport::topLine()`→`RenderPipeline::setTopLine()` のブリッジを実装 (Phase 4a で「Phase 4b の仕事」と明記されていた箇所)
- レビューで発見した副次バグ修正: `Viewport::ensureVisible()` の誤った `noexcept` 宣言 (`Document::offsetToLine()` が allocate しうるため noexcept ではない) を削除
- テスト数: 164 → 185 (単体+20: `app_editor_input_test.cpp` 18件新設 + `core_selection_model_test.cpp`/`core_command_dispatcher_test.cpp`へのケース追加、統合+1: キャレット×フレームスキップ)

**完了条件:**
- [x] キーボードで矢印移動・Home/End(+Ctrl)・Backspace/Delete・文字入力・Enter/Tab・Ctrl+Z/Ctrl+Y が動作する (ヘッドレスユニットテスト20件 + 実アプリでの `SendKeys` 経由の対話的操作でクラッシュなしを確認)
- [x] マウスホイールでスクロールする (`applyMouseWheelScroll` の単体テストで境界値・符号を検証。実アプリでのホイール入力の自動検証は未実施 — `SendKeys` はホイールイベントを送れないため、コード変更なしの純粋関数としてユニットテストのみで検証)
- [x] キャレットが編集/移動のたびに正しく再描画される (FrameState拡張の統合テストで検証)
- [x] ローカル Debug/Release 全185テスト green、変更/新規 `.cpp` 全8ファイルの clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み) 新規警告0 (2件検出・修正: `readability-redundant-casting`、`hicpp-use-auto`/`modernize-use-auto`)
- [x] `docs/issues/undo_stack_unbounded_memory.md` に実アプリでの初回メモリ実測(約1,350件編集、WorkingSet増分約3MB)を追記。100万件規模には未到達のため issue はOpenのまま維持
- [ ] キャレットの視覚的な描画位置の正しさ(ピクセル単位)は自動検証していない — このセッションにはネイティブ Win32 ウィンドウのスクリーンショット/GUI自動化ツールが無く、`SendKeys` によるクラッシュ検知のみ実施。ユーザー自身による目視確認を推奨

**スコープ外 (Phase 4b2 へ持ち越し):** マウスクリックでのカーソル位置特定 (`WM_LBUTTONDOWN` + `IDWriteTextLayout::HitTestPoint`)、選択範囲のハイライト描画、複数カーソルの入力経路 (Alt+Click等)、PageUp/PageDown、Ctrl+矢印 (単語移動)、クリップボード、IME、`tryMerge`

### 3.10 Phase 4b2 (マウスクリック位置特定 + 選択範囲ハイライト描画) 完了記録

**成果物:**
- `SelectionModel::moveAllTo(TextPos, bool extendSelection = false)` — デフォルト引数で既存呼び出し(`CommandDispatcher`/`UndoStack`)を変更せず後方互換を維持しつつ、Shift+クリックでのanchor保持に対応
- `RenderPipeline::hitTest(xPx, yPx) -> optional<TextPos>` 新設 — このコードベース初の `IDWriteTextLayout::HitTestPoint` 使用。既存の `TextLayoutCache`/DPI変換/`m_topLine` 計算(`drawVisibleLines()`が確立済み)を再利用
- 選択範囲ハイライト描画: `RenderPipeline::setSelectionRange(TextRange)` 新設、`FrameState`に`selectionRange`追加(caretPosition追加と同じ理由でフレームスキップとの不整合を予防)、新規`m_selectionBrush`(半透明青)、`drawSelectionOnLine()`を`drawVisibleLines()`ループ内で`DrawTextLayout`より前に描画
- `neomifes::app::handleMouseDown(TextPos, bool shiftDown, ...)` 新設 — ヒットテスト済みの`TextPos`を受け取るだけで、座標変換自体は`RenderPipeline`(レイアウト情報を持つレンダー層)が担い、`editor_input`のWin32/レンダー非依存という既存制約を維持
- `MainWindow`: `onMouseDown`フック新設、`WM_LBUTTONDOWN`処理を追加(`<windowsx.h>`の`GET_X_LPARAM`/`GET_Y_LPARAM`、`wParam & MK_SHIFT`でShift状態取得)
- テスト数: 185 → 189 (単体+4: `moveAllTo`のextendケース2件+`handleMouseDown`2件、統合+2: `hitTest`の境界値検証+選択ハイライトのフレームスキップ検証)

**完了条件:**
- [x] クリックでカーソルが移動し既存の選択が解除される(ユニットテスト+実アプリでの`SetCursorPos`+`mouse_event`によるクリックシミュレーションでクラッシュなしを確認)
- [x] Shift+クリックでanchorを保持したまま選択範囲が拡張される(ユニットテストで検証、実アプリでも`keybd_event`でShift保持状態を再現しクラッシュなしを確認)
- [x] 選択範囲変更のみのフレームがフレームスキップに飲まれない(統合テスト`SelectionRangeRendersWithoutErrorAndForcesRedraw`で検証)
- [x] ローカル Debug/Release 全189テスト green、変更 `.cpp` 5ファイルの clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み) 新規警告0 (1件検出・修正: `readability-isolate-declaration`)
- [ ] キャレット・選択ハイライトの視覚的な描画位置の正しさ(ピクセル単位)は自動検証していない — Phase 4b1と同じ理由([[reference-no-win32-gui-automation]])、ユーザー自身による目視確認を推奨

**スコープ外 (Phase 4b3 へ持ち越し):** ドラッグ選択 (`WM_MOUSEMOVE`+`SetCapture`/`ReleaseCapture`)、ダブルクリック(単語選択)・トリプルクリック(行選択)、Alt+クリックによる複数カーソル追加、選択範囲のクリップボードコピー

---

## 4. Phase 2a のコンテキスト圧縮版

### 4.1 意図的な MVP 縮退 (Phase 2b で解消したもの / まだ残るもの)
| 縮退項目 | 現状 | 状態 |
|---|---|---|
| Piece コンテナ | RB-tree + 順序統計 (`PieceTree`) | ✅ 解消済み (Phase 2b2) |
| snapshot | vector 全コピー O(n) | 意図的に維持 (ADR-007。O(1) 化は将来の再評価事項) |
| Original | mmap + 64KiB チェックポイント + on-demand decode (evict なし) | ✅ 解消済み (Phase 2b3 Step 1) |
| LineIndex | mutation ごとに O(N) 再スキャン | 意図的に維持 (tree 集約では原理的に O(log n) 化不可、`line_index_o_log_n.md` 参照) |
| Encoding | UTF-8 のみ | Phase 6 の Encoding Engine 側で拡張予定 |
| Loader | 同期 | Worker で非同期化は将来検討 |

**公開ヘッダは Phase 2b で 1 行も変えない** という当初方針は Step 1 完了時点まで完全に守られている (実装差し替えのみで完了)。

### 4.2 変わっていないもの (継続確定事項)
- 内部文字型: `char16_t` / `std::u16string` (util の `wchar_cast.h` で境界処理)
- 内部位置単位: **UTF-16 CU** (`TextPos`)
- ADR 群 (CMake/RE2/TextMate/WinHTTP/VS 17.13+) はそのまま

---

## 5. ドキュメント地図

- 運用ガイド: [`CLAUDE.md`](../../CLAUDE.md)
- 要件: [`NeoMIFES_要件定義書.md`](../../NeoMIFES_要件定義書.md)
- 設計:
  - 基本: [`docs/design/basic_design.md`](../design/basic_design.md)
  - 詳細: [`docs/design/detailed_design.md`](../design/detailed_design.md)
  - レビュー: [`docs/design/self_review.md`](../design/self_review.md)
- 意思決定: [`docs/decisions/README.md`](../decisions/README.md)
- Issue (Phase 2b/3c/4a 引継ぎ): [`docs/issues/`](../issues/)
- フェーズ報告:
  - [Phase 0.5](../phase_reports/phase_0.5_report.md)
  - [Phase 1](../phase_reports/phase_1_report.md)
  - [Phase 2a](../phase_reports/phase_2a_report.md)
  - [Phase 2b (2b1/2b2/2b3 統合)](../phase_reports/phase_2b_report.md)
  - [Phase 3 (3a/3b/3c 統合)](../phase_reports/phase_3_report.md)

---

## 6. 次回の推奨最初のプロンプト例

```
RESUME_HERE.md を読んで現在の状態を把握し、
Phase 4b3 (ドラッグ選択・ダブル/トリプルクリック・複数カーソル) に着手せよ。
着手前に detailed_design.md §5.3 の Phase 4b1/4b2 実装済み範囲と、
本ファイル §3.10 のスコープ外一覧を確認すること。
```

**Phase 3 全体ロードマップ (完了、2026-07-16):**

| サブフェーズ | 内容 | 状態 |
|---|---|---|
| 3a | D2D/DXGI/COM 基盤配線 | ✅ 完了 (§3.5 参照) |
| 3b | DirectWrite テキストレイアウト、Document内容の実描画、最小スクロール状態 | ✅ 完了 (§3.6 参照) |
| 3c | TextLayoutCache + 粗粒度フレームスキップ + `--measure-frame` 計測ハーネス | ✅ 完了 (§3.7 参照) |

Phase 3 は [`docs/phase_reports/phase_3_report.md`](../phase_reports/phase_3_report.md) として統合レポート発行済み。旧「Phase 3d」(Line Gutter・テーマ・日本語フォントフォールバック・IME) は Phase 3 の DoD (60fpsスクロール確認) に必須でないため対象外とし、Phase 4 とは独立の将来フェーズとして扱う方針をユーザーと確認済み。

**Phase 4a (Command/Undo/Selection、ヘッドレス) は完了済み (§3.8 参照、ADR-012)。** `src/core/` に `Cursor`/`SelectionModel`/`ICommand`/`InsertTextCommand`/`DeleteRangeCommand`/`ReplaceRangeCommand`/`UndoStack`/`CommandDispatcher`/`Viewport` を実装し、`tests/bench/core_undo_stack_bench.cpp` の実測 (Release: push 352ms / undo 174ms、100万コマンド) で CLAUDE.md Phase 4 DoD「100万Undo達成」を満たした。

**Phase 4b1 (キーボード入力配線・キャレット描画・マウスホイールスクロール) は完了済み (§3.9 参照)。** `neomifes::app_input` ライブラリで `WM_KEYDOWN`/`WM_CHAR`/`WM_MOUSEWHEEL` を Editor Core に配線し、実アプリで対話的な編集・移動・Undo/Redo・スクロールが動作する状態になった。キャレットも `RenderPipeline` に描画される。

**Phase 4b2 (マウスクリック位置特定・選択範囲ハイライト描画) は完了済み (§3.10 参照)。** `RenderPipeline::hitTest()` でクリック座標を `TextPos` に変換し、`SelectionModel::moveAllTo(pos, shiftDown)` でクリック/Shift+クリックによるカーソル移動・選択拡張を実装。選択範囲は半透明の矩形でハイライト描画される。

**Phase 4b3 (次回) 着手時に確認すること:**
1. ドラッグ選択 (`WM_MOUSEMOVE` + `SetCapture`/`ReleaseCapture`)。ボタン押下状態をまたぐ新規の状態管理 (`MainWindow`にmouseDown中フラグを持たせるか、`WM_LBUTTONDOWN`〜`WM_LBUTTONUP`間の座標追跡方法) の設計が必要。`RenderPipeline::hitTest()` (Phase 4b2実装済み) はそのまま再利用できる想定
2. ダブルクリック(単語選択)・トリプルクリック(行選択)。`WM_LBUTTONDBLCLK` の配線 + 単語境界判定 (`MovementUnit` 未実装、Phase 4a/ADR-012 で延期された論点と同じ課題に直面する見込み)
3. Alt+クリックによる複数カーソル追加。`SelectionModel::addCursor()`/`moveAll()` は Phase 4a から複数カーソルに対応済みだが、UI からカーソルを追加する入力経路がまだ無い。編集コマンド (`InsertTextCommand`等) が複数カーソルを考慮していない点 (Phase 4aレビューのPLAUSIBLE指摘) も併せて要検討
4. `docs/design/detailed_design.md` §5.3 に Phase 4b1/4b2 の実装内容と Phase 4b3 へのスコープ外一覧を明記済み — まずここを読む
5. 対話的な1行単位編集が実現したら、[ADR-011](../decisions/ADR-011-phase3c-render-cache-scope.md) の再評価トリガーに従い細粒度 DamageTracker の要否を判断する (Phase 4b1/4b2 では未判断のまま)
6. `docs/issues/undo_stack_unbounded_memory.md` — Phase 4b1 で約1,350件規模の初回実測を追記済みだが、100万件規模の実測はまだ無い。編集量が増える機能が加わったら再実測を検討

## 7. 履歴を辿りたいとき
[`docs/history/TIMELINE.md`](../history/TIMELINE.md) にセッション単位で全ての意思決定と成果物を時系列に記録。「なぜこう決めたか」を後追いする際の一次資料。

## 8. セッション終了時に必ず確認すること
[`CLAUDE.md`](../../CLAUDE.md) §11 の「セッション終了時チェックリスト」を実行してから作業を締めること。2026-07-15 の包括レビューでドキュメント鮮度の不整合 (本ファイルの `git init` 指示残留、Issue チェックボックス未更新、ベンチ実測値の未確認等) が多数見つかった反省に基づく恒久ルール。
