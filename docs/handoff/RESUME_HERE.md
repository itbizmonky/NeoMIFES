# NeoMIFES — 次回セッション再開ガイド

> **最終更新:** 2026-07-19 (Phase 5b1 完了・push 済・CI green 確認済、マスターロードマップ **v2.0** 発行)
> **次回開いたら最初にこのファイルを読むこと。**
> **本ファイルは毎セッション終了時に全文点検し、完了済み手順や重複する次アクションを削除・更新すること** (CLAUDE.md §11 セッション終了時チェックリスト参照)。
> 🗺️ **Phase 4b8・5b2・5b3・5c・6〜12 の実装詳細は [`docs/design/master_roadmap.md`](../design/master_roadmap.md) **v2.0** (2026 行、23 章) に一気通貫で規定済み。ペルソナ 7 種・競合ポジショニング・60 機能継承マトリクス・世界最速の裏付け技術要素・国際化/アクセシビリティ・セキュリティ・リリース・KPI・エコシステム・開発品質基盤 まで網羅。各フェーズ着手時はまず該当章を読んでから Plan Mode で詳細プランを起こす運用に確定。**

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
| Phase 4b3 (ドラッグ選択) | ✅ 完了 |
| Phase 4b4 (ダブルクリック単語選択・トリプルクリック行選択) | ✅ 完了 |
| Phase 4b5a (複数カーソル編集コマンド基盤、core層ヘッドレス) | ✅ 完了 |
| Phase 4b5b (Alt+クリック複数カーソル追加、入力配線) | ✅ 完了 |
| Phase 4b6a (PageUp/PageDown) | ✅ 完了 |
| Phase 4b6b (Ctrl+矢印 単語移動) | ✅ 完了 |
| Phase 4b6c (クリップボードコピー Ctrl+C/X/V) | ✅ 完了 |
| Phase 4b6d (Alt+Shift+クリック/Alt+ドラッグ 選択拡張) | ✅ 完了 |
| Phase 4b7a (複数カーソルの視覚的描画) | ✅ 完了 |
| Phase 4b7b (複数行にまたがる単語移動) | ✅ 完了 |
| Phase 4b7c (複数カーソルを跨いだクリップボード) | ✅ 完了 |
| Phase 4b8 (矩形選択・N対N分配等) | ⏸️ **未着手のまま保留** (ユーザーがPhase 5優先を選択、§3.16参照) |
| Phase 5a (Search Engine基盤: RE2導入 + `SearchService::findAll`) | ✅ 完了 |
| Phase 5b1 (複数行にまたがるマッチ対応) | ✅ 完了 |
| **Phase 5b2 (置換 ReplaceAllCommand、未詳細設計)** | ⏭️ **次回着手** |
| Phase 5b3 (Find bar UI配線、未詳細設計) | ⏸️ 5b2完了後に着手 |

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
- **(2026-07-17 追加)** 通常のローカル検証 (`--preset debug`/`release`) は MSVC のみを使う。`= default` の比較演算子 (`operator==`) を新規に書いたときは、メンバ型全てが同様に比較可能かを確認すること — MSVCは「暗黙的に削除されたdefaulted関数」を無診断で通すが、CIのUBSanジョブが使うclang-clは`-Werror -Wdefaulted-function-deleted`で検出し fail する (Phase 4b5bで実際に発生、`reference_windows_cpp_ci_gotchas.md` 項目6参照)。該当する変更をした場合は `cmake --preset ubsan && cmake --build --preset ubsan` (VS付属LLVMのclang-cl.exeで動作) をpush前に一度実行するとよい

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

### 3.11 Phase 4b3 (ドラッグ選択) 完了記録

**設計上の発見:** Phase 4b2 実装済みの `handleMouseDown(pos, shiftDown=true, ...)` が「anchorを保持しpositionだけ動かす」というドラッグの継続移動に必要な挙動と完全に一致していたため、**新規の core/app ロジックは一切不要だった**。`MainWindow` 側の Win32 状態管理 (`SetCapture`/`WM_MOUSEMOVE`/`WM_LBUTTONUP`) を追加するだけで実現。

**成果物:**
- `MainWindow`: `onMouseDrag` フック新設(shiftDownパラメータなし)。`handleMouseDown()` の先頭で `::SetCapture(m_hwnd)`、新規 `WM_MOUSEMOVE`(`handleMouseMove`、ドラッグ中のみ発火)・`WM_LBUTTONUP`(`handleMouseUp`、`::ReleaseCapture()`)を追加
- `src/app/main.cpp`: `onMouseDrag` は `hitTest()` の後、既存の `handleMouseDown(*hit, /*shiftDown=*/true, ...)` を呼ぶだけ
- テスト数: 189 → 190 (単体+1: ドラッグが依拠する「shiftDown=true繰り返し呼び出しでanchor保持のまま拡張し続ける」挙動のピン留めテスト)
- 実アプリで複数点ドラッグ・Shift+ドラッグ・ウィンドウ境界外へのドラッグ(`SetCapture`効果検証)をシミュレートしクラッシュなし・正常終了を確認

**完了条件:**
- [x] マウスドラッグで選択範囲が連続的に拡張される(ユニットテスト+実アプリでのP/Invokeドラッグシミュレーションでクラッシュなしを確認)
- [x] ドラッグ中にカーソルがウィンドウ境界外に出てもクラッシュしない(`SetCapture`の効果を実機で確認)
- [x] ローカル Debug/Release 全190テスト green、変更 `.cpp` 2ファイルの clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み) 新規警告0
- [ ] ドラッグ中の選択ハイライトの視覚的な正しさは自動検証していない — Phase 4b1/4b2と同じ理由([[reference-no-win32-gui-automation]])、ユーザー自身による目視確認を推奨

**スコープ外 (Phase 4b4 へ持ち越し):** ダブルクリック(単語選択)・トリプルクリック(行選択)、Alt+クリックによる複数カーソル追加、選択範囲のクリップボードコピー、`WM_CAPTURECHANGED` の明示的ハンドリング、ドラッグ中のウィンドウ端オートスクロール

### 3.12 Phase 4b4 (ダブルクリック単語選択 + トリプルクリック行選択) 完了記録

単語境界判定の方式についてユーザーに確認し「簡易文字種ベース」(推奨案)を採用 — ASCII英数字+`_`の連続・CJK文字の連続をそれぞれ1単語、それ以外の記号は1文字ずつ。Unicode UAX #29 準拠は外部ライブラリ導入とADR起票を要するため見送り。

**成果物:**
- 新規 `src/ui/include/neomifes/ui/click_tracking.h`: 純粋関数 `nextClickState()`。`src/render/resize_math.h`/`viewport_math.h` と同じ「ヘッダオンリー・SDK非依存・ユニットテスト可能」パターンを `src/ui/` に初適用 — `MainWindow` のロジックが初めてテスト可能になった部分
- `SelectionModel::selectWordAt()`/`selectLineAt()` 新設。単語境界は簡易文字種ベース、行選択は既存`lineContentEnd()`を再利用し最終行以外は`\n`を含める
- `neomifes::app::handleDoubleClick()`/`handleTripleClick()` 新設(`handleMouseDown`の既存契約は不変)
- `MainWindow::onMouseDown` に `clickCount` パラメータ追加(`WM_LBUTTONDBLCLK`は「3回目」の概念が無いため使わず、`WM_LBUTTONDOWN`単体で手動判定)
- テスト数: 190 → 207 (単体+17、CJK単語選択のテストを含む)
- 実アプリでダブルクリック・トリプルクリック・CJKテキストでのダブルクリックをP/Invokeでシミュレートしクラッシュなしを確認

**完了条件:**
- [x] ダブルクリックで単語が選択される(ASCII/CJK両方でユニットテスト+実アプリ確認)
- [x] トリプルクリックで行が選択される(`\n`込み、ユニットテスト+実アプリ確認)
- [x] ローカル Debug/Release 全207テスト green、変更 `.cpp` 4ファイルの clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み) 新規警告0 (1件検出・修正: `hicpp-use-auto`/`modernize-use-auto`)
- [ ] 単語選択・行選択の視覚的な正しさは自動検証していない — Phase 4b1〜4b3と同じ理由([[reference-no-win32-gui-automation]])、ユーザー自身による目視確認を推奨

**スコープ外 (Phase 4b5 へ持ち越し):** Alt+クリックによる複数カーソル追加(編集コマンドの複数カーソル対応も必要)、選択範囲のクリップボードコピー、ダブルクリック→ドラッグでの単語単位ドラッグ拡張

### 3.13 Phase 4b5a/4b5b (複数カーソル編集コマンド基盤 + Alt+クリック入力配線) 完了記録

Alt+クリックでカーソルを追加できても編集コマンドが複数カーソルに対応していなければ機能として不完全という Phase 4b4 完了時の指摘を受け、調査の結果 core 層のインターフェース変更(`ICommand`)が避けられないと判明したため、CLAUDE.mdルール8に従い **4b5a(core層、ヘッドレス)→4b5b(入力配線)** に分割 (Phase 4a→4b1 と同じ「ヘッドレスcore実装→UI配線」パターン)。

**4b5a 成果物:**
- `ICommand::cursorPositionAfterExecute()`/`cursorPositionAfterUndo()`(単一`TextPos`、全カーソルを1点に強制収束)を `cursorsAfterExecute()`/`cursorsAfterUndo()`(`std::vector<Cursor>`)に置き換え。既存3コマンド(`InsertTextCommand`/`DeleteRangeCommand`/`ReplaceRangeCommand`)は要素数1のvectorを返すだけの機械的変更
- 新規 `MultiCursorEditCommand`(`edit.multiCursor`): N個のカーソルへの同時編集を累積オフセット法(VSCode等と同種の手法)で1回のundoステップとして適用。`PerCursorEdit{range, insertedText}` を`SelectionModel::cursors()`と同じ昇順・非重複の順序で受け取り、昇順1パスで`cumulativeShift`を足し込みながら適用、undoは降順(execute時に捕捉した実位置を使うためシフト再計算不要)。カーソル復元はexecute前のスナップショットをそのまま返す(選択範囲込みで完全復元)
- `SelectionModel::setCursors(std::vector<Cursor>)` 新設。`CommandDispatcher::dispatch()`/`UndoStack::undo()`/`redo()` の `moveAllTo(pos)` 呼び出しを `setCursors(cmd->cursorsAfterExecute()/AfterUndo())` に置き換え
- テスト数: 207 → 213 (単体+6: `MultiCursorEditCommand`4件+`setCursors`2件)

**4b5b 成果物:**
- `neomifes::app::handleAltClick()` 新設 — 既存(Phase 4a)の`SelectionModel::addCursor()`を呼ぶだけの薄い実装
- `editor_input.cpp`の`handleChar`/`applyDeleteKey`を全カーソル対応に書き換え — `selection.cursors()`全件から`PerCursorEdit`を1:1で組み立て`MultiCursorEditCommand`を1回ディスパッチする形に統一(単一/複数カーソルで分岐しない)。境界(文書先頭でのBackspace等)で動けないカーソルは空range/空文字列の"no-op edit"として1エントリを必ず作る(全カーソルがno-opならディスパッチ自体をしない、既存の単一カーソル時の挙動を維持)
- Win32側: `WM_LBUTTONDOWN`のwParamには`MK_ALT`が存在しない(Shift/Ctrlとは非対称)ため`::GetKeyState(VK_MENU)`で都度取得。`MainWindowConfig::onMouseDown`に`bool altDown`追加
- `main.cpp`: `onMouseDown`ラムダの分岐を新規フリー関数`dispatchMouseDown()`に切り出し(altDown追加でcognitive complexity閾値25を超えたため)。`altDown`が最優先分岐
- テスト数: 213 → 217 (単体+4: `handleAltClick`1件、複数カーソル`handleChar`/`handleKeyDown`3件)
- 実アプリでAlt+クリック2箇所→文字入力→Alt+クリック→Backspace→Ctrl+Z/Ctrl+YをP/Invokeでシミュレートしクラッシュなし・応答性維持を確認

**完了条件:**
- [x] Alt+クリックで新しいカーソルが追加される(ユニットテスト+実アプリ確認)
- [x] 複数カーソル状態での文字入力/Backspace/Deleteが全カーソルに反映される(ユニットテスト、累積オフセットの正しさを含む)
- [x] Undo/Redoで複数カーソルの編集前状態(選択範囲込み)が厳密に復元される(ユニットテスト)
- [x] ローカル Debug/Release 全217テスト green、変更ファイルの clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み) 新規警告0 (2件検出・修正: `hicpp-use-auto`/`modernize-use-auto`、`readability-function-cognitive-complexity`)
- [ ] 複数カーソルの視覚的な正しさ(各カーソルの描画位置・全カーソルへの文字挿入の見た目)は自動検証していない — Phase 4b1〜4b4と同じ理由([[reference-no-win32-gui-automation]])、ユーザー自身による目視確認を推奨

**スコープ外 (Phase 4b6 以降へ持ち越し):** Alt+Shift+クリック(追加カーソルの選択範囲拡張)、Alt+ドラッグでの追加カーソルの選択拡張、選択範囲のクリップボードコピー、ダブルクリック→ドラッグでの単語単位ドラッグ拡張、`WM_CAPTURECHANGED`の明示的ハンドリング、ドラッグ中のウィンドウ端オートスクロール、PageUp/PageDown、Ctrl+矢印(単語移動)

### 3.14 Phase 4b6a〜4b6d (PageUp/PageDown・Ctrl+矢印単語移動・クリップボード・Alt+Shift拡張) 完了記録

Phase 4b5b完了後、ユーザーに Phase 4b6 のスコープを確認したところ4項目全て(選択範囲クリップボードコピー、PageUp/PageDown、Ctrl+矢印単語移動、Alt+Shift+クリック/Alt+ドラッグ選択拡張)を選択。CLAUDE.mdルール8「1PR=1責務」に従い、複雑度の低い順に **4b6a→4b6b→4b6c→4b6d** の4サブフェーズに分割。

**4b6a (PageUp/PageDown) 成果物:**
- `MovementKind::PageUp`/`PageDown` 追加。垂直移動の列保持ロジック(`moveVertically`)を「1行分」から「任意行数」に一般化し、既存`Up`/`Down`(delta=±1)と共有
- `SelectionModel::moveAll()`に`pageSize`(デフォルト0)追加。`editor_input.cpp`が`viewport.visibleLines()`からpageSizeを算出
- ページ送り後のスクロールは既存`ensureVisible()`がそのまま「1ページ分スクロール」を実現、新規スクロールロジック不要
- テスト数: 217 → 222

**4b6b (Ctrl+矢印単語移動) 成果物:**
- `MovementKind::WordLeft`/`WordRight` 追加。`selectWordAt()`(Phase 4b4)の`classify()`/`CharKind`を共有ヘルパーに格上げし新規`moveByWord()`で再利用(単語境界の定義を1箇所に保つ)
- **単語移動は現在行内に限定**(行頭/行末で停止、隣接行への越境は次点課題)— `selectWordAt()`と同じ単一行スコープを踏襲
- `editor_input.cpp`の既存`VK_LEFT`/`VK_RIGHT`ケースに`ctrlDown`分岐を追加(`VK_HOME`/`VK_END`と同型)
- テスト数: 222 → 231

**4b6c (クリップボードコピー Ctrl+C/X/V) 成果物:**
- **スコープはプライマリカーソルの選択範囲のみ**(複数カーソルを跨いだコピー/ペーストの分配は次点課題)
- 新規 `src/platform/clipboard.h/.cpp`: `setClipboardText()`/`getClipboardText()`(`GlobalAlloc`/`GlobalLock`/`SetClipboardData`の定番手順)。`editor_input.cpp`はWin32 API呼び出しゼロという既存制約を維持するためこのレイヤに分離
- `editor_input.cpp`に`textToCopy()`/`handlePaste()`追加。Cutはクリップボード書き込み失敗時に選択範囲を削除しない(データ消失防止)
- `main.cpp`: `handleClipboardKey()`新設に加え、**`onKeyDown`ラムダ本体全体**を`handleKeyDownEvent()`という独立関数に切り出し — clang-tidyのcognitive complexityはラムダ本体がwireNormalMode内にインライン定義されていると外側関数に積算されるため、分岐ロジックだけの切り出しでは不十分だった(38→26に減っただけで依然閾値25超過、ラムダ本体そのものを外に出して解消)
- 新規 `tests/integration/platform_clipboard_test.cpp`: 実クリップボードのラウンドトリップ検証(`GTEST_SKIP()`で環境非対応時に緩やかにスキップ、`render_device_smoke_test.cpp`と同じパターン)
- テスト数: 231 → 236。ローカルの`ubsan`(clang-cl)プリセットでも追加検証(Phase 4b5bの教訓を踏まえ)

**4b6d (Alt+Shift+クリック/Alt+ドラッグ 選択拡張) 成果物:**
- 新規 `SelectionModel::moveCursorMatching(identifyingAnchor, newPos)`: anchorが一致する1個のカーソルだけを拡張。`mergeOverlapping()`で添字が不安定なため、拡張中不変な`anchor`を識別キーに採用
- `main.cpp`の`wWinMain`に`std::optional<TextPos> altCursorAnchor`新設(`selectionModel`等と同じ寿命が必要なため`wireNormalMode`外のローカル変数、参照で渡す — `MainWindow::m_isDragging`がメンバ変数である理由と同じ)。プレーンAlt+クリックで設定、Alt+Shift+クリック/Alt+ドラッグで消費、Alt無しクリックでリセット
- テスト数: 236 → 239 (単体+3: `moveCursorMatching`)
- 実アプリでPageUp/PageDown・Ctrl+矢印(通常/Shift拡張)・クリップボードCtrl+C/X/V+Undo/Redo・Alt+クリック/Alt+Shift+クリック/Alt+ドラッグの複合操作をP/Invokeでシミュレートしクラッシュなし・応答性維持を確認

**完了条件:**
- [x] PageUp/PageDownでviewportの表示行数分ジャンプする(列保持含む、ユニットテスト+実アプリ確認)
- [x] Ctrl+矢印で単語境界へ移動する(**Phase 4b7bで複数行対応に拡張済み**、ユニットテスト+実アプリ確認)
- [x] Ctrl+C/X/Vでプライマリカーソルの選択範囲をコピー/切り取り/貼り付けできる(**Phase 4b7cで全カーソル対応に拡張済み**、実クリップボードのラウンドトリップテスト+実アプリ確認)
- [x] Alt+Shift+クリック/Alt+ドラッグで直近のAlt+クリックカーソルの選択範囲を拡張できる(ユニットテスト+実アプリでのクラッシュなし確認)
- [x] ローカル Debug/Release 全239テスト green、変更ファイルの clang-tidy 新規警告0 (4b6cで2件検出・修正: special-member-functions、`bugprone-suspicious-stringview-data-usage`、cognitive-complexity)
- [x] **既知の制限は Phase 4b7a で解消済み:** `RenderPipeline`がプライマリカーソルのキャレット/選択範囲しか保持・描画しなかった制限(Phase 4b5a以降存在)を `setCursorVisuals(std::vector<CursorVisual>)` で解消、詳細は §3.15 参照
- [ ] 単語移動・PageUp/PageDown・選択拡張の視覚的な正しさは自動検証していない — Phase 4b1〜4b5bと同じ理由([[reference-no-win32-gui-automation]])、ユーザー自身による目視確認を推奨(Phase 4b7aでキャレット/選択ハイライトの複数描画自体は実アプリでユーザー確認済み、§3.15参照)

**スコープ外 (Phase 4b6時点、Phase 4b7で一部解消):** 複数行にまたがる単語移動(→Phase 4b7bで解消)、複数カーソルを跨いだクリップボードコピー/ペーストの分配(→Phase 4b7cで一部解消、N対N分配は引き続き対象外)、複数カーソルの視覚的描画(→Phase 4b7aで解消)、ダブルクリック→ドラッグでの単語単位ドラッグ拡張、`WM_CAPTURECHANGED`の明示的ハンドリング、ドラッグ中のウィンドウ端オートスクロール、段落単位移動

### 3.15 Phase 4b7a〜4b7c (複数カーソル視覚描画・複数行単語移動・複数カーソルクリップボード) 完了記録

Phase 4b6d完了後、ユーザーに Phase 4b7 のスコープを確認したところ、以下3項目全てを選択: (1) 複数カーソルの視覚的描画、(2) 複数行にまたがる単語移動、(3) 複数カーソルを跨いだクリップボード。CLAUDE.mdルール8に従い、複雑度と影響度を踏まえ **4b7a(視覚描画、最大規模)→4b7b(複数行単語移動)→4b7c(複数カーソルクリップボード)** の順で分割実装。4b7aを最初にしたのは、RenderPipelineの構造変更を早期検証したいことに加え、Phase 4b5a以降積み残されていた「複数カーソルが実際に画面で見えない」制限を解消することで、それ以降の全機能(4b5b/4b6d等)の効果も遡って視覚確認できるようになる効果を狙ったため。

**4b7a (複数カーソル視覚描画) 成果物:**
- `RenderPipeline::setCaretPosition()`/`setSelectionRange()`(単一値)を`setCursorVisuals(std::vector<CursorVisual>)`に置換。`CursorVisual{position, selectionRange}`は`document::`型のみに依存(`core::Cursor`には依存しない既存制約を維持)
- `FrameState`の`caretPosition`/`selectionRange`を`std::vector<CursorVisual> cursorVisuals`に置換(粗粒度フレームスキップが複数カーソルの変化も検知するよう既存設計意図を維持)
- `drawVisibleLines()`を`computeCaretDraws()`/`drawCaretsOnLine()`/`drawSelectionsOnLine()`の3関数に分割(単一カーソルのループが複数カーソルのループになりcognitive complexityが33に増加、閾値25を超過したため)
- `main.cpp`の`syncRenderStateAndInvalidate()`が`selection.cursors()`全件から`CursorVisual`を組み立てるよう書き換え
- テスト数: 239→244。ローカルDebug/Release/**ubsan(clang-cl)全green**(`CursorVisual`の新規`=default operator==`をPhase 4b5bの教訓に基づき検証、今回は問題なし)
- **実アプリでユーザー自身が視覚確認**: Alt+クリックで追加した複数カーソルのキャレット/選択ハイライトが実際に画面へ複数描画されることを確認(Phase 4b5a以降初めての視覚的検証)

**4b7b (複数行単語移動) 成果物:**
- `moveByWord(forward)`を`moveByWordForward()`/`moveByWordBackward()`(+`skipWhitespaceForward()`/`Backward()`ヘルパー)に一般化。`classify()`が1行内で`'\n'`を空白として扱う性質を、行と行の**境界**(`classify()`が直接見ない場所)まで拡張
- 空行は「改行1個分の空白」として通過(段落区切りとしての明示的停止は別の未実装の関心事)
- 単語間に実際の空白文字が無い行境界は、1行内の単一スペースを1回のCtrl+Rightで飛び越える既存の挙動と一貫して、1回の操作で直接次の単語頭へ着地する(既存の`WordRightFromMidWhitespaceAlsoLandsAtNextWordStart`テストと同じ規則)
- テスト数: 239(4b7a後244)→244(既存の単一行前提テスト`WordLeftRightStayWithinCurrentLine`を`WordRightCrossesLineBoundaryToNextWord`等に置き換え)
- ローカルDebug/Release全green、clang-tidy新規警告0

**4b7c (複数カーソルクリップボード) 成果物:**
- `textToCopy()`が全カーソルのうち選択を持つものを`\n`連結して返すよう一般化。`handlePaste()`が全カーソルへ同一テキストを適用するよう一般化(N個のコピー元とN個の貼り付け先を1対1分配する高度な対応はクリップボードへのメタデータ付与を要するため対象外)
- `handleChar()`と`handlePaste()`が共通ロジックを持つことになったため新規`insertTextAtEveryCursor()`ヘルパーへ共通化
- 新規`deleteAllSelections()`でCtrl+Xが全カーソルの選択を削除するよう一般化。`main.cpp`が直接`DeleteRangeCommand`を組み立てていた最後の箇所を置換、`main.cpp`から`edit_commands.h`への直接依存を解消
- テスト数: 244→250 (単体+6: 複数カーソルでのcopy/paste/cutケース)
- ローカルDebug/Release全green、clang-tidy新規警告0

**完了条件:**
- [x] 複数カーソルのキャレット/選択ハイライトが実際に画面へ描画される(ユーザー自身の目視確認)
- [x] Ctrl+矢印単語移動が行境界を越えて継続する(ユニットテストで複数パターン検証)
- [x] Ctrl+C/X/Vが全カーソル(選択を持つもの)を対象に動作する(ユニットテスト+実アプリ確認)
- [x] ローカル Debug/Release 全250テスト green、変更ファイルの clang-tidy 新規警告0
- [ ] 選択ハイライト・キャレットの正確なピクセル位置は自動検証していない — 既存の制約([[reference-no-win32-gui-automation]])、「複数描画されること」自体はユーザー確認済みだが「位置が正確か」は目視確認の範囲

**スコープ外 (Phase 4b8 以降へ持ち越し):** 複数カーソルを跨いだクリップボードのN対N分配、段落単位移動、ダブルクリック→ドラッグでの単語単位ドラッグ拡張、`WM_CAPTURECHANGED`の明示的ハンドリング、ドラッグ中のウィンドウ端オートスクロール

### 3.16 Phase 5a (Search Engine 基盤: RE2導入 + `SearchService::findAll`) 完了記録

Phase 4b7c完了後、ユーザーから「史上最強のテキストエディタを目指す、機能もデザインも最強に」という大方針が示された。要件定義書とCLAUDE.md §7フェーズ表を突き合わせた棚卸しを行い、AskUserQuestionで次の一手を確認した結果、**検索エンジン(Phase 5)への着手**が選ばれた(Phase 4b8の矩形選択等の残タスクより優先。理由: 要件定義書§8「ログ解析モード」が「本ソフト最大の特徴」と明記されているが、時系列ジャンプ/ERROR抽出/フィルタは検索機能の応用であり、Phase 10はPhase 5に依存するため)。Plan Modeで、Phase 5全体を一度に設計せず**最初のサブフェーズ5a(RE2導入+`SearchService::findAll`、同期・単一行スコープ・ヘッドレス)のみ**を詳細設計する方針を採用(CLAUDE.mdルール8「1PR=1責務」、未着手の後続サブフェーズを先行設計するのは推測実装になるため)。ExitPlanModeでユーザー承認を得て実装着手。

**成果物:**
- **RE2 (ADR-002) + Abseil (LTS 20250814.2) を`cmake/Dependencies.cmake`にFetchContent導入。** テストビルド限定だった`include(Dependencies)`をルート`CMakeLists.txt`で常時includeに変更(検索エンジンはアプリ本体が実行時に必要とするコア依存のため)。GoogleTest/benchmarkのFetchContentは`NEOMIFES_BUILD_TESTS`条件へ移動
- ビルド時に2つの新規CMake問題を発見・解決:
  1. RE2の`install(EXPORT re2Targets ...)`が`ABSL_ENABLE_INSTALL=OFF`と衝突してconfigureが失敗 → `RE2_INSTALL OFF`で該当install()自体を無効化
  2. **ubsanプリセットでのみ**発生するリンクエラー(`_ITERATOR_DEBUG_LEVEL`不一致、re2.lib=0 vs absl_log...lib=2)。原因はAbseil自身のCMakeLists.txtが`ABSL_MSVC_STATIC_RUNTIME`オプション(既定OFF)経由で`CMAKE_MSVC_RUNTIME_LIBRARY`を無条件に上書きしており、ubsanプリセットが指定した値をAbseilの`add_subdirectory()`ツリー配下(何段も下、`get_property(...BUILDSYSTEM_TARGETS)`では捕捉できない深さ)でだけ無視していたことが判明。新規`neomifes_collect_targets_recursive()`ヘルパーで再帰的に全ターゲットを収集し、`MSVC_RUNTIME_LIBRARY`プロパティをこちらの値で強制上書きして解消 (詳細は[[reference-windows-cpp-ci-gotchas]]参照)
- 新規`src/util/utf8_convert.h/.cpp`: `toUtf8WithOffsets(u16string_view) -> Utf8Conversion{utf8, byteToUtf16}`。RE2がUTF-8バイト列を対象とするため、UTF-16内部表現との変換+オフセット対応表を独自実装(`WideCharToMultiByte`は使わず、オフセット表構築が1文字ずつの処理を要するため手書きエンコーダの方が単純)。孤立サロゲートはU+FFFDへ置換
- 新規`src/search/`モジュール: `SearchService::findAll(const Document&, const Query&) -> vector<Match>`(`static`、clang-tidyの`readability-convert-member-functions-to-static`指摘に従った)。リテラル/正規表現検索を**RE2の1本のコードパス**で統一(リテラルは`RE2::QuoteMeta()`でエスケープ)。`wholeWord`はRE2の`\b`(ASCII単語境界のみ、CJK非対応は既知の制限として明記)。**単一行スコープ**(マッチが`'\n'`をまたぐケースは対象外、Phase 4b6bの単語移動と同じ「まず小さく正しく作る」順序)
- テスト数: 250→271 (単体+21: `util_utf8_convert_test.cpp`7件、`search_search_service_test.cpp`14件)。日本語(CJK)テキストでのマッチ位置がUTF-16オフセットとして正しいこと、行をまたぐマッチが検出されないこと、ReDoS的パターンでもRE2の線形時間保証によりハングしないこと、をそれぞれ明示的にテストでピン留め
- 新規`tests/bench/search_find_all_bench.cpp`: 20万行(約10MB相当)の合成ログ風ドキュメントに対する`findAll()`をRelease構成で実測。約60〜66ms(スパースマッチ/無マッチいずれも同程度) — 単純換算で約150MB/s相当。要件定義書§5「数GBファイルでも高速」の達成には非同期化・チャンク並列化(Phase 5b以降のスコープ)が必要になることを示す最初の実測データ

**検証:**
- ローカル **Debug/Release/ubsan(clang-cl) 全green**、全271テストpass。RE2/Abseil導入直後に単独ビルド確認(SearchService実装前)、その後3プリセット通しで再検証
- clang-tidy (`src/.clang-tidy`の`WarningsAsErrors: '*'`込み、新規`.cpp`2ファイル対象) で2件検出・修正: `readability-convert-member-functions-to-static`(`findAll`を`static`化、呼び出し元のテスト/ベンチも`SearchService::findAll()`直接呼び出しに統一)、`readability-math-missing-parentheses`(`text.size() * 3 + 1`に括弧追加)。再スキャンで0警告確認

**完了条件:**
- [x] RE2/Abseilがローカル(Debug/Release/ubsan)でビルド・リンクできる
- [x] 単純文字列検索・正規表現検索・大文字小文字区別・単語単位オプションが動作する(ユニットテスト)
- [x] 日本語テキストでのマッチ位置がUTF-16オフセットとして正しい(ユニットテスト)
- [x] ReDoS的パターンでもハングしない(RE2の線形時間保証、ユニットテストで確認)
- [x] `findAll()`の基礎性能を実測しTIMELINE.mdに記録済み
- [x] Find UI・インクリメンタル検索・置換・Grep・複数行マッチ対応は未着手 (Phase 5b以降) — **複数行マッチ対応は Phase 5b1 で解消済み(§3.17参照)。残りはPhase 5b2以降**

**スコープ外 (Phase 5b 以降へ持ち越し):** Find bar UI配線(新規UI基盤、WC_EDIT子コントロール or 自前描画の設計判断を要する)、インクリメンタル検索(`IncrementalFindService`)、複数行にまたがるマッチ対応(→Phase 5b1で解消)、置換(`ReplaceAllCommand`/`ReplaceInFilesCommand`)、Grep(複数ファイル横断)、巨大ファイルでのチャンク並列走査・SIMD最適化(計測済みの実測値を踏まえて要否判断)。**Phase 4b8**(矩形選択・タブ⇔スペース変換・複数カーソルクリップボードのN対N分配等、§3.15参照)も引き続き保留のまま

**Phase 5a レビュー・修正 (同セッション継続、テスト数 271→274):** `/code-review`(high effort)を実施し、確認済みの正当性バグ4件を修正済み。詳細は`docs/history/TIMELINE.md`の該当セッション参照:
1. ゼロ幅正規表現マッチ(`x*`等)がマルチバイトUTF-8文字(日本語等)付近で重複マッチを生成していた問題 → `findAllInLine()`の走査位置前進をコードポイント境界単位に修正
2. `^$`等「空行にマッチすべき」パターンが常に0件を返していた問題 → 空行を安全に扱う特殊ケースを追加
3. `findAll()`が`BufferSnapshot::extract()`を1行ごとに呼びO(行数×ピース数)になっていた問題 → `LineIndex::build()`と同じ`pieceView()`ベースの単一パス走査(`scanDocument()`)に全面書き換え。副作用として`core`/`search`間の`lineContentEnd()`重複も解消
4. `NEOMIFES_BUILD_TESTS=OFF`でもRE2/Abseilを無条件フェッチしていた問題(アプリ本体は現時点で`neomifes::search`をリンクしていないため矛盾) → `include(Dependencies)`と`add_subdirectory(search)`を`NEOMIFES_BUILD_TESTS`ガードへ戻す(Phase 5bで実際にUIへ配線する際に外す想定)

未修正のPLAUSIBLE所見6件(`MSVC_RUNTIME_LIBRARY`修正の脆弱性、CRLF行末未対応、`decodeOne()`のnoexcept欠如、サロゲート変換ロジックの重複、パターン変換時の無駄なオフセット表構築)は`docs/issues/`に3件のIssueとして起票済み(§3.17参照)。

### 3.17 Phase 5b1 (複数行にまたがるマッチ対応) 完了記録

ユーザーに「Phase 5b着手せよ」と指示されたが、Phase 5bのスコープ自体が未確定だったためAskUserQuestionで確認したところ、Find bar UI配線・複数行マッチ対応・置換(ReplaceAllCommand)・レビュー残台のIssue化の4項目全てが選択された。Issue化は直ちに実施(`docs/issues/`に3件新設: `search_crlf_line_ending.md`・`cmake_msvc_runtime_library_fragility.md`・`search_utf8_convert_minor_cleanup.md`、commit `27147fd`)。残り3項目はCLAUDE.mdルール8に従い**5b1(複数行マッチ対応)→5b2(置換)→5b3(Find bar UI配線)**の順に分割し、Plan Modeで5b1のみを詳細設計(未着手の後続サブフェーズを先行設計するのは推測実装になるため)。Find bar UIの入力方式についてAskUserQuestionで確認し、**WC_EDIT子コントロール**(IME/カーソル点滅をOSに委譲)が選ばれた — この決定は5b3着手時に使う。

**成果物:**
- `SearchService::findAll()`の内部実装(`scanDocument()`)を「1行ごとに検索」から「`pieceView()`で文書全体を1つの`std::u16string`バッファへ連結し1回だけ検索」する方式に書き換え。`findAllInLine()`は`findAllInBuffer()`へ改名(1行専用ではなくなったため)。これによりパターンに`\n`を含むリテラルクエリや`[\s\S]`等の文字クラスを使った複数行マッチが可能になった
- **`^`/`$`のセマンティクス維持が今回の設計上の要点。** RE2は`posix_syntax=false`(本プロジェクトのモード)では`^`/`$`が既定でテキスト全体の先頭/末尾にのみアンカーする(行ごとにアンカーさせるには`(?m)`が必要 — RE2ドキュメントで確認)。Phase 5aは1行を1バッファとして渡していたため`^`/`$`は暗黙的に行アンカーとして機能していたが、文書全体を1バッファ化するとこの暗黙動作が壊れる。`buildPattern()`が生成する最終パターンの先頭に`"(?m)"`を付与することで解消
- `.`は`dot_nl`オプションを既定`false`のままにし、複数行マッチは明示的な`\n`や`[\s\S]`を書いた場合にのみ発生するよう意図的に制限(VSCode等の一般的なエディタの慣習に合わせた)
- テスト数: 279(継続中)。新規6件(複数行にまたがるリテラル/文字クラスマッチ、`^`/`$`が引き続き行アンカーであることの回帰、`\A`/`\z`が文書全体アンカーとして機能することの確認)、既存`MatchDoesNotCrossLineBoundary`を`LiteralQueryWithoutEmbeddedNewlineDoesNotSpanLines`+`DotDoesNotMatchNewlineByDefault`に分割・改名(いずれも変更なしでpassし続けた — `.`がdot_nl=falseである限りこの2パターン自体は複数行にまたがる書き方をしていないため)
- ベンチマーク再実測: 20万行合成ドキュメントで約33〜39ms(Phase 5a時点は約60〜66ms) — 1行ごとのUTF-8変換・RE2呼び出しの繰り返しオーバーヘッドが無くなったことによる改善。単純換算で約260〜300MB/s相当

**検証:**
- ローカル **Debug/Release/ubsan(clang-cl) 全green**、全279テストpass
- clang-tidy (`search_service.cpp`) で新規警告0

**完了条件:**
- [x] パターンに`\n`を含むリテラルクエリ・`[\s\S]`等の文字クラスを使ったパターンが行をまたいでマッチする(ユニットテスト)
- [x] `^`/`$`が引き続き「行の先頭/末尾」を意味する(1バッファ化前の暗黙動作を維持、回帰テストで確認)
- [x] `\A`/`\z`で文書全体の先頭/末尾を明示的に指定できる(ユニットテスト)
- [x] ベンチマーク実測値を`detailed_design.md`/`TIMELINE.md`に記録
- [ ] メモリスケーリング制約(文書全体を1バッファへ連結するため検索1回あたりのメモリが文書サイズに比例)は既知の制約として明記のみ、実際の大規模ファイルでの実測は未実施

**スコープ外 (Phase 5b2/5b3へ持ち越し):** 置換(`ReplaceAllCommand`)、Find bar UI配線、インクリメンタル検索、Grep、巨大ファイルでのチャンク並列走査・SIMD最適化。詳細は本プランのPlan Modeで概要のみ設計済み(`ReplaceAllCommand`は既存`MultiCursorEditCommand`のedit数=カーソル数前提が転用できないため`core::ICommand`直接実装が必要と判明、Find bar UIはWC_EDIT子コントロール使用が決定済み)。

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
  - **マスターロードマップ (Phase 4b8/5b2/5b3/6-12 の実装詳細一気通貫): [`docs/design/master_roadmap.md`](../design/master_roadmap.md)** (v1.0、2026-07-19)
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
master_roadmap.md §4 (Phase 5b2 — 置換 ReplaceAllCommand) の詳細設計を
Plan Mode で起こしてから実装に着手せよ
(Phase 4b8 の残タスクに戻る可能性も含めて確認すること — master_roadmap.md §3 参照)。
着手前に:
1. master_roadmap.md §4 全文 (影響ファイル・データ構造・キャプチャグループ対応まで)
2. detailed_design.md §7 の Phase 5b1 実装済み範囲(§7.1')
3. 本ファイル §3.17 末尾のスコープ外一覧
これら3点を読むこと。
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

**Phase 4b3 (ドラッグ選択) は完了済み (§3.11 参照)。** Phase 4b2 の `handleMouseDown(pos, shiftDown=true, ...)` がドラッグの継続移動に必要な挙動と完全に一致していたため、新規の core/app ロジックは不要で、`MainWindow` の `SetCapture`/`WM_MOUSEMOVE`/`WM_LBUTTONUP` 配線のみで実現した。

**Phase 4b4 (ダブルクリック単語選択・トリプルクリック行選択) は完了済み (§3.12 参照)。** 単語境界はユーザー確認済みの簡易文字種ベース。クリック回数判定はヘッダオンリーの純粋関数 `click_tracking.h`(`src/render/`のmath系ヘッダと同じパターン)で実装し、`MainWindow`のロジックが初めてユニットテスト可能になった。

**Phase 4b5a (複数カーソル編集コマンド基盤) / Phase 4b5b (Alt+クリック複数カーソル追加) は完了済み (§3.13 参照)。** `ICommand::cursorsAfterExecute()`/`cursorsAfterUndo()`(`std::vector<Cursor>`)+新規 `MultiCursorEditCommand`(累積オフセット法)で編集コマンドが複数カーソルに対応し、`handleAltClick()`(`SelectionModel::addCursor()`を呼ぶだけ)でAlt+クリックからカーソルを追加できるようになった。`editor_input.cpp`の`handleChar`/`applyDeleteKey`は単一/複数カーソルを区別せず`MultiCursorEditCommand`経由で統一的に処理する。

**Phase 4b6a〜4b6d (PageUp/PageDown・Ctrl+矢印単語移動・クリップボードコピー・Alt+Shift拡張) は完了済み (§3.14 参照)。** `MovementKind`に`PageUp`/`PageDown`/`WordLeft`/`WordRight`を追加(既存の列保持ロジック・`classify()`を一般化・再利用)。新規`src/platform/clipboard.h/.cpp`でCtrl+C/X/V(当時はプライマリカーソルの選択範囲のみ)を実装。新規`SelectionModel::moveCursorMatching()`でAlt+Shift+クリック/Alt+ドラッグによる特定カーソルの選択拡張を実装。

**Phase 4b7a〜4b7c (複数カーソル視覚描画・複数行単語移動・複数カーソルクリップボード) は完了済み (§3.15 参照)。** `RenderPipeline::setCursorVisuals(std::vector<CursorVisual>)`で複数カーソルのキャレット/選択ハイライトが実際に画面へ描画されるようになった(Phase 4b5a以降の既知の制限を解消、ユーザー自身が実アプリで視覚確認済み)。`moveByWordForward()`/`moveByWordBackward()`が行境界を越えて継続するようになった。`textToCopy()`/`handlePaste()`/新規`deleteAllSelections()`が全カーソル(選択を持つもの)を対象に動作するようになった。

**Phase 4b8 は未着手のまま保留。** ユーザーが「史上最強のテキストエディタを目指す」大方針のもと、検索エンジン(Phase 5)を優先する判断をしたため、矩形選択・タブ⇔スペース変換・複数カーソルクリップボードのN対N分配等(§3.15末尾のスコープ外一覧)は後回しになっている。次にPhase 5系のスコープが一段落したら、着手候補として再度提示すること。

**Phase 5a (Search Engine基盤: RE2導入 + `SearchService::findAll`) は完了済み (§3.16 参照)。** ADR-002で決定済みのRE2を`cmake/Dependencies.cmake`に導入(常時ビルド対象化)、新規`src/search/`モジュールで同期・単一行スコープの`SearchService::findAll(Document, Query)`を実装。リテラル/正規表現検索をRE2の1本のコードパスで統一。新規`src/util/utf8_convert.h`でUTF-16⇔UTF-8変換+オフセットマッピングを実装(日本語テキストでのマッチ位置精度を担保)。ビルド中に判明した2件のCMake問題(RE2の`install(EXPORT)`衝突、ubsanプリセットでのAbseil起因`_ITERATOR_DEBUG_LEVEL`不一致)を解決済み。20万行合成ドキュメントでの`findAll()`実測値(約60〜66ms、約150MB/s相当)を記録し、要件定義書の「数GBファイルでも高速」達成には今後の非同期化・チャンク並列化が必要になることを実証。コードレビュー(`/code-review`)で確認された4件の正当性バグを修正済み(§3.16末尾参照)。

**Phase 5b1 (複数行にまたがるマッチ対応) は完了済み (§3.17 参照)。** `scanDocument()`を「1行ずつ検索」から「文書全体を1バッファ化して1回検索」する方式に書き換え、`\n`を含むリテラルクエリや`[\s\S]`等の文字クラスで行をまたぐマッチが可能になった。`buildPattern()`に`"(?m)"`を付与し`^`/`$`の行アンカー動作を維持。ベンチマークも改善(約60〜66ms→約33〜39ms、単一ピース文書)。

**Phase 5b2 (次回) 着手時に確認すること:**
1. 置換(`ReplaceAllCommand`)の詳細設計から着手 — Explore調査で判明済みの制約: 既存`MultiCursorEditCommand`は「edit数とカーソル数が1:1」前提のため転用不可、`core::ICommand`を直接実装する新規クラスが必要(累積オフセット適用のアルゴリズム自体は`MultiCursorEditCommand::execute()`/`undo()`のパターンを再利用可能)。詳細は§3.17末尾参照
2. `docs/design/detailed_design.md` §7 に Phase 5b1 実装済み範囲(§7.1')と元スケッチ(§7.1'')の対比を明記済み — まずここを読む
3. Phase 5b2完了後、Phase 5b3(Find bar UI配線)に進む。**WC_EDIT子コントロール**を使う設計判断は済み(§3.17参照) — `src/ui/`に既存のダイアログ/子コントロール前例が無いため、着手時に改めてExplore調査してから詳細設計すること
4. **保留中のPhase 4b8**(§3.15末尾参照)に戻る選択肢もあわせて次セッション開始時にユーザーへ提示すること
5. 対話的な1行単位編集が実現したら、[ADR-011](../decisions/ADR-011-phase3c-render-cache-scope.md) の再評価トリガーに従い細粒度 DamageTracker の要否を判断する (Phase 4b1〜5b1 では未判断のまま)
6. `docs/issues/undo_stack_unbounded_memory.md` — Phase 4b1 で約1,350件規模の初回実測を追記済みだが、100万件規模の実測はまだ無い。編集量が増える機能が加わったら再実測を検討
5. RE2/Abseilの`MSVC_RUNTIME_LIBRARY`強制上書き(`cmake/Dependencies.cmake`の`neomifes_collect_targets_recursive()`)は、Abseil/RE2のバージョンを更新する際に同じロジックが引き続き妥当か再確認すること(Abseilの`ABSL_MSVC_STATIC_RUNTIME`オプションの挙動が変わっていないか)

## 7. 履歴を辿りたいとき
[`docs/history/TIMELINE.md`](../history/TIMELINE.md) にセッション単位で全ての意思決定と成果物を時系列に記録。「なぜこう決めたか」を後追いする際の一次資料。

## 8. セッション終了時に必ず確認すること
[`CLAUDE.md`](../../CLAUDE.md) §11 の「セッション終了時チェックリスト」を実行してから作業を締めること。2026-07-15 の包括レビューでドキュメント鮮度の不整合 (本ファイルの `git init` 指示残留、Issue チェックボックス未更新、ベンチ実測値の未確認等) が多数見つかった反省に基づく恒久ルール。
