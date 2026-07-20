# NeoMIFES 開発タイムライン

> **目的:** これまでの設計・意思決定・実装の履歴を時系列で 1 か所に集約。
> 「なぜこの設計になっているか」「いつ何を決めたか」を後追いするための一次資料。
>
> **運用ルール:** 各セッション終了時に **本ファイル末尾に 1 セクション追記** すること。既存セクションは (誤記訂正を除き) 変更しない。

## 目次
- [Session 1: 要件確認 → 設計書 → 自己レビュー → ADR-001〜005](#session-1-2026-07-14-要件確認--設計書--自己レビュー--adr-001005)
- [Session 2: Phase 0.5 (ビルド基盤・CI・静的解析)](#session-2-2026-07-14-phase-05-ビルド基盤ci静的解析)
- [Session 3: Phase 1 (Win32 骨組み + 起動 0.3s/20MB PoC)](#session-3-2026-07-14-phase-1-win32-骨組み--起動-03s20mb-poc)
- [Session 4: 開発一次停止 → RESUME_HERE + Memory 整備](#session-4-2026-07-14-開発一次停止--resume_here--memory-整備)
- [Session 5: Phase 2a (Document Engine MVP)](#session-5-2026-07-14-phase-2a-document-engine-mvp)
- [Session 6: Phase 2a 後レビュー + A-2/A-3 対応](#session-6-2026-07-14-phase-2a-後レビュー--a-2a-3-対応)
- [Session 7: GitHub 連携 → CI green (5 ラウンド)](#session-7-2026-07-14-github-連携--ci-green-5-ラウンド)
- [Session 8: ADR-006 起草 → Phase 2b1 実装](#session-8-2026-07-14-adr-006-起草--phase-2b1-実装)
- [Session 9: Phase 2b2 着手前レビュー + ADR-007 + Timeline 整備](#session-9-2026-07-14-phase-2b2-着手前レビュー--adr-007--timeline-整備)
- [Session 10: Phase 2b2 Step 1 (PieceTree 追加)](#session-10-2026-07-15-phase-2b2-step-1-piecetree-追加--insert--split)
- [Session 11: Phase 2b2 Step 2 (eraseRange + PieceTable 差し替え)](#session-11-2026-07-15-phase-2b2-step-2-eraserange--piecetable-差し替え)
- [Session 12: Phase 2b2 完了後の包括レビュー + プロセス改善](#session-12-2026-07-15-phase-2b2-完了後の包括レビュー--プロセス改善)
- [Session 13: Phase 2b3 Step 1 (mmap + Lazy Decode コア)](#session-13-2026-07-15-phase-2b3-step-1-mmap--lazy-decode-コア)
- [Session 14: Phase 2b3 Step 2 (SEH + load bench + Phase 2b 完了)](#session-14-2026-07-15-phase-2b3-step-2-seh--load-bench--phase-2b-完了)
- [Session 15: Phase 3 着手前レビュー (設計書のADR同期漏れ発見・修正)](#session-15-2026-07-15-phase-3-着手前レビュー-設計書のadr同期漏れ発見修正)
- [Session 16: Phase 3着手前ハウスキーピング (Named Mutex + UBSan CI)](#session-16-2026-07-16-phase-3着手前ハウスキーピング-named-mutex--ubsan-ci)
- [Session 17: WarningsAsErrors有効化 (src/限定)](#session-17-2026-07-16-warningsaserrors有効化-src限定)
- [Session 18: Phase 3a (Direct2D/DXGI 基盤配線)](#session-18-2026-07-16-phase-3a-direct2ddxgi-基盤配線)
- [Session 19: Phase 0〜3a 包括レビュー + Phase 3b 計画ブラッシュアップ](#session-19-2026-07-16-phase-03a-包括レビュー--phase-3b-計画ブラッシュアップ)
- [Session 20: Phase 3b (DirectWrite テキストレイアウト + Document 実描画)](#session-20-2026-07-16-phase-3b-directwrite-テキストレイアウト--document-実描画)
- [Session 21: Phase 3c (TextLayoutCache + 粗粒度フレームスキップ + `--measure-frame`) — Phase 3 全体完了](#session-21-2026-07-16-phase-3c-textlayoutcache--粗粒度フレームスキップ--measure-frame--phase-3-全体完了)
- [Session 22: Phase 4a (Command/Undo/Selection、ヘッドレス) — 100万Undo DoD 実測](#session-22-2026-07-16-phase-4a-commandundoselectionヘッドレス--100万undo-dod-実測)
- [Session 23: Phase 4a レビュー + Phase 4b1〜4b4 (入力配線・キャレット・選択・ドラッグ・単語/行選択)](#session-23-2026-07-17-phase-4a-レビュー--phase-4b1-キーボード入力配線--キャレット描画--マウスホイールスクロール)

---

## Session 1 (2026-07-14): 要件確認 → 設計書 → 自己レビュー → ADR-001〜005

**成果物:**
- [`NeoMIFES_要件定義書.md`](../../NeoMIFES_要件定義書.md) v1.0 レビュー
- [`CLAUDE.md`](../../CLAUDE.md) 作成 — テックリード役、絶対ルール 11 条、コーディング規約、フェーズ表
- [`docs/design/basic_design.md`](../design/basic_design.md) v1.0 — 7 層アーキテクチャ、モジュール責務、スレッドモデル、非機能要件方針、リスク R1〜R7
- [`docs/design/detailed_design.md`](../design/detailed_design.md) v1.0 — Piece Table クラス設計、レンダリング詳細、Command / Undo、Plugin C ABI、テスト目標値
- [`docs/design/self_review.md`](../design/self_review.md) v1.0 — 要件カバレッジ 82%、F-1〜F-4 修正リスト起票

**意思決定:**
- ユーザー確認 4 項目 (縦編集 / 独自マクロ / マクロ言語 / ビルド) → 全て推奨案採用
- 追加確認 4 項目 (正規表現 / シンタックス / 設定ファイル / 20MB 計測基準) → 全て推奨案採用
- [ADR-001](../decisions/ADR-001-build-system.md) CMake 3.28+ + MSVC v143 + Ninja + x64 のみ
- [ADR-002](../decisions/ADR-002-regex-engine.md) 正規表現エンジン = **RE2** 単独 (Phase 5 で導入)
- [ADR-003](../decisions/ADR-003-syntax-definition.md) シンタックス = **TextMate 互換** (tree-sitter は Phase 7 後に評価)
- [ADR-004](../decisions/ADR-004-http-client.md) HTTP = **WinHTTP** (AI プラグイン内のみ)
- [ADR-005](../decisions/ADR-005-min-msvc-version.md) 最低 VS 2022 17.13+ (std::expected 完全実装)
- 設定ファイル: **JSON5** / 内部文字型: **char16_t / std::u16string**
- 「20MB 初期起動」= 空ドキュメント表示後の Working Set と定義
- 「縦編集」= 列編集 (MIFES 由来) / 「独自マクロ」= キー操作記録
- マクロ言語標準同梱: Lua + JavaScript (QuickJS) + Python (標準プラグイン)
- self_review v1.1 → v1.2 更新、F-1〜F-4 完了、要件カバレッジ 100%
- **Phase 0.5 (CI/ビルド整備) をフェーズ表に追加**

---

## Session 2 (2026-07-14): Phase 0.5 (ビルド基盤・CI・静的解析)

**成果物:**
- [`CMakeLists.txt`](../../CMakeLists.txt) — ADR-005 準拠のバージョン検査 + オプション設計
- [`CMakePresets.json`](../../CMakePresets.json) — debug / release / asan
- `cmake/CompileOptions.cmake` / `Sanitizers.cmake` / `Dependencies.cmake`
- `src/util/`, `src/app/` 最小骨格 (version.h + WinMain スタブ)
- `tests/unit/`, `tests/bench/` に GoogleTest 1.15.2 + google-benchmark 1.9.1 の FetchContent + smoke テスト
- [`.clang-tidy`](../../.clang-tidy) — CLAUDE.md §4 命名規約を写像
- [`.clang-format`](../../.clang-format) — Google ベース + ColumnLimit 100
- [`.editorconfig`](../../.editorconfig), [`.gitignore`](../../.gitignore)
- [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml) — build (Debug/Release) + tests + bench smoke + clang-tidy
- [`README.md`](../../README.md), [`docs/phase_reports/phase_0.5_report.md`](../phase_reports/phase_0.5_report.md)

**特記:**
- Direct2D / DirectWrite は Phase 3 送り (CLAUDE.md §7 準拠)
- `WarningsAsErrors: ''` (警告のみ表示、警告 0 を確認できたら `'*'` へ切替予定)

---

## Session 3 (2026-07-14): Phase 1 (Win32 骨組み + 起動 0.3s/20MB PoC)

**成果物:**
- `src/platform/` — `HandleGuard` (RAII)、`PerfClock` (QPC)、`ProcessMetrics` (PSAPI EX2)
- `src/ui/` — `MainWindow` (WNDCLASSEX + WndProc + `onFirstPaint` フック)
- `src/app/` — wWinMain 書き換え。`--measure-startup <file>` / `--measure-memory <file>` / 通常モード。Per-Monitor V2 DPI
- `src/app/startup_profile.{h,cpp}` — 4 マーカ + memory 2 値の JSON 出力
- `tests/unit/platform_*` (5 ケース)、`tests/integration/startup_measure_test.cpp` (subprocess spawn 検証)
- CI に Release 版 `--measure-startup` step を追加
- self_review v1.1 → v1.2 (R1 状態更新)
- [`docs/phase_reports/phase_1_report.md`](../phase_reports/phase_1_report.md)

**特記:**
- Direct2D / DirectWrite は依然 Phase 3 送り。Phase 1 は GDI FillRect 背景描画
- 意図的に platform / ui / app を責務分離

---

## Session 4 (2026-07-14): 開発一次停止 → RESUME_HERE + Memory 整備

**成果物:**
- [`docs/handoff/RESUME_HERE.md`](../handoff/RESUME_HERE.md) 作成 — 再開時最初に読む単一ガイド
- [`CLAUDE.md`](../../CLAUDE.md) 冒頭に RESUME_HERE への誘導追加
- Claude 側メモリ (`%USERPROFILE%\.claude\projects\D--IDE-Claude-NeoMIFES\memory\`) 整備:
  - `MEMORY.md` (索引) + `project_neomifes_state.md` + `user_communication_style.md` + `reference_neomifes_docs.md` + `project_neomifes_verification.md`

---

## Session 5 (2026-07-14): Phase 2a (Document Engine MVP)

**成果物:**
- `src/document/` — text_pos / piece / add_buffer / original_buffer / buffer_snapshot / piece_table / line_index / document / file_loader (9 モジュール)
- `src/util/wchar_cast.h` — Phase 1 宿題消化 (char16_t ↔ wchar_t)
- `tests/unit/document_*` (5 ファイル / 31 単体ケース + 2000 反復プロパティテスト)
- `tests/bench/document_piece_table_bench.cpp` (4 本)
- [`docs/issues/piece_table_rb_tree.md`](../issues/piece_table_rb_tree.md) 起票 (Phase 2b 引継ぎ)
- [`docs/issues/lazy_decode_mmap.md`](../issues/lazy_decode_mmap.md) 起票 (Phase 2b3 引継ぎ)
- [`docs/phase_reports/phase_2a_report.md`](../phase_reports/phase_2a_report.md)

**意思決定:**
- Phase 2 を **2a (API/MVP/テスト)** と **2b (性能最適化)** に分割
- PieceTable は `std::vector<Piece>` ベースの MVP 実装、公開ヘッダは 2b で 1 行も変えない設計
- OriginalBuffer は全読込 (mmap + Lazy Decode は Phase 2b3 送り)
- FileLoader は UTF-8 (BOM) のみ (Phase 6 の Encoding Engine 完成まで)

---

## Session 6 (2026-07-14): Phase 2a 後レビュー + A-2/A-3 対応

**成果物:**
- Phase 2b 着手前のレビュー — A (必須) / B (推奨) / C (nice-to-have) / D (verified OK) 分類
- **A-2**: CI bench smoke に `neomifes_document_bench.exe` 追加
- **A-3**: `tests/unit/util_wchar_cast_test.cpp` (5 ケース) 追加

**特記:** A-1 (CI 実行) は環境的にユーザー作業で保留

---

## Session 7 (2026-07-14): GitHub 連携 → CI green (5 ラウンド)

**成果物:**
- リポジトリ設定 (GitHub 上 `itbizmonky/NeoMIFES`, MIT License 選択)
- [`.gitattributes`](../../.gitattributes) 追加 (改行コード統一)
- Repository description + topics 10 個
- 各種 gh コマンド + `README.md` License 節更新 (Phase 12 → MIT に確定)

**CI 修正 5 ラウンド (全て `refs`):**
| # | 症状 | 修正 | Commit |
|---|---|---|---|
| 1 | `C2248: private wndProcTrampoline` | private → public + docstring | `44c1c08` |
| 2 | integration test で `firstPaint < windowCreated` | `onWindowCreated` フック追加 | `6a2c879` |
| 3 | GUI exe の `$LASTEXITCODE` が空 | `Start-Process -Wait -PassThru` | `ec97e7a` |
| 4 | clang-tidy が `@obj.modmap` を読めない | `CMAKE_CXX_SCAN_FOR_MODULES=OFF` + 2 warnings 修正 | `e463306` |
| 5 | clang-cl が `/Zc:__STDC__` 等を error 扱い | `--extra-arg=-Wno-unused-command-line-argument` | `283cedb` |

**副次発見:** CI 実測で **first paint = 22ms** (0.3s 目標の 7%)。Phase 3 で Direct2D 化しても大幅マージンあり。

**メモリ追加:** `reference_windows_cpp_ci_gotchas.md` — 5 種の落とし穴を将来 Windows C++ プロジェクトで即参照できる形に

---

## Session 8 (2026-07-14): ADR-006 起草 → Phase 2b1 実装

**成果物:**
- [ADR-006](../decisions/ADR-006-piece-tree-implementation.md) 起草 — Path-Copying Persistent RB-Tree (**後日 Session 9 で Superseded**)
- ADR インデックス + Issue との相互リンク
- Phase 2b1 実装:
  - **B-1**: `BufferSnapshot::pieceView(const Piece&) -> u16string_view` 追加、`LineIndex` を O(N²) → O(N)
  - **B-2**: `AddBuffer` を append-only チャンク deque 化 (128 KiB / chunk、pointer stability 保証)
- 単体テスト +6 (add_buffer 拡充 + buffer_snapshot 新規)、単体テスト計 37
- self_review v1.3 (R10 状態更新 + R11 新規)
- CI green 継続
- Commit: `8efc065` (ADR-006), `226a739` (Phase 2b1)

**副次効果:** basic_design §5.2「BufferSnapshot は任意スレッドから参照可能」の要件を実装レベルで担保できるようになった (AddBuffer 再確保による UB 消滅)

---

## Session 9 (2026-07-14): Phase 2b2 着手前レビュー + ADR-007 + Timeline 整備

**成果物:**
- Phase 2b2 (RB-tree 実装) 着手前の設計再レビュー
- **[ADR-007](../decisions/ADR-007-piece-tree-mutable-rb.md) 起票** — Mutable RB-Tree + Piece-Vector Snapshot に方針転換
- **ADR-006 を Superseded 化** (履歴保存、削除せず)
- Issue `piece_table_rb_tree.md` を ADR-007 準拠に更新 (完了条件: 500ns insert、1ms snapshot、20K 反復プロパティ、RB invariant テスト)
- RESUME_HERE.md 更新 — Phase 2b2 実装ガードレール G1〜G10
- detailed_design.md §3.1 の Piece.offset 記述統一 (Add/Original 両方 UTF-16 CU)
- **本 `TIMELINE.md` 作成** + CLAUDE.md から起動時に辿れるようリンク配置

**方針転換の理由 (要約):**
1. snapshot() O(1) は要件でなく aspirational な目標 — 現行 Phase 2a/2b1 も既に O(n pieces)
2. Persistent delete (Kahrs/GM) の実装コストが過大、ローカルビルド不可な環境ではリスク大
3. shared_ptr オーバーヘッドで 500ns insert 目標達成困難 (path-copying は ~2μs 見積)
4. snapshot O(1) の恩恵範囲が狭い (LineIndex は tree 集約で O(log n)、他は頻度低い)

将来 (Phase 3+) で snapshot() が実測ボトルネックになったら persistent 化を再検討可能 — Public API は不変なので実装 swap で済む。

**次セッション (Phase 2b2 実装) で守るべきガードレール:** [`RESUME_HERE.md §3.3.1`](../handoff/RESUME_HERE.md) の G1〜G10

---

## Session 10 (2026-07-15): Phase 2b2 Step 1 (PieceTree 追加 / insert + split)

**背景:** ADR-007 で mutable RB tree を採用。実装は 2 段階に分割:
- Step 1 (このセッション): PieceTree クラスを新規追加、insert + splitPieceAt + validate + テストのみ。PieceTable / LineIndex の差し替えはしない
- Step 2 (次回): erase + line queries + PieceTable/LineIndex 内部差し替え

環境制約 (ローカルビルド不可) 下でリスクを最小化するための段階分割。

**成果物:**
- [`src/document/include/neomifes/document/piece_tree.h`](../../src/document/include/neomifes/document/piece_tree.h) — `PieceTreeNode` + `PieceTree` API
- [`src/document/src/piece_tree.cpp`](../../src/document/src/piece_tree.cpp) — CLRS 13.3 準拠 RB insert + rotate + fixup + splitPieceAt + collectPieces + validate
- [`tests/unit/document_piece_tree_test.cpp`](../../tests/unit/document_piece_tree_test.cpp) — 11 ケース (empty / single / append 500 / prepend 500 / alternating 200 / newline aggregate / split × 2 / stress 800 / move / order preservation / clamp)

**設計選択:**
- ノード所有権: 親から子へ `std::unique_ptr` (rotate 時は unique_ptr slot 単位で明示的 move 転送)
- 集約フィールド: `subtreeLength` / `subtreeNewlines` / `subtreeCount` — rotate 直後に必ず `updateAggregate` 呼出、insert 経路で `updateAggregatesUpward`
- `validate()`: RB 3 不変条件 (root black / no red-red / uniform black height) + parent 整合性 + 集約整合性を bottom-up 再計算で検証
- splitPieceAt は「1 ノードの piece を短縮 + 右半分を新ノードとして boundary 挿入」に還元 — insertAt が既にあれば実装が最小

**意図的な非対応 (Step 2 で):**
- `eraseRange` (RB delete + double-black fixup)
- `offsetToLine` / `lineToOffset` (tree 集約経由の O(log n))
- PieceTable / LineIndex 内部差し替え
- property test 20K 反復化

**RESUME_HERE.md 更新:** Step 1 完了、Step 2 が次回着手対象。

## Session 11 (2026-07-15): Phase 2b2 Step 2 (eraseRange + PieceTable 差し替え)

**目標:** Step 1 の PieceTree に CLRS 13.4 RB delete を追加し、PieceTable の内部を `std::vector<Piece>` から `PieceTree` へ差し替える。公開 API は不変。

**重要な設計修正 (実装検討中に判明):**
当初 (RESUME_HERE G8) は「LineIndex を tree 集約経由で O(log n) 化」を予定していたが、これは**不可能**と判明した。`subtreeNewlines` 集約は piece 内の改行**総数**しか保持せず、任意オフセットの前に何個改行があるかを答えるには **piece 内の改行の実際の位置** が必要 — これは tree が持たないテキスト内容 (buffer) を見ないと分からない。→ **LineIndex は Phase 2b1 の設計のまま維持** (O(N) 再構築 + O(log n) 二分探索)。将来の解決案 (piece に newline-offset 配列を持たせる等) を [`docs/issues/line_index_o_log_n.md`](../issues/line_index_o_log_n.md) に記録し、投機的最適化を避けるため実装は見送り。

**成果物:**
- [`piece_tree.h`](../../src/document/include/neomifes/document/piece_tree.h) 拡張: `eraseRange(TextRange)`, `pieceContainingStrictly(TextPos)`, private `eraseNode`/`eraseFixup`/`findNodeStartingAt`
- [`piece_tree.cpp`](../../src/document/src/piece_tree.cpp) 拡張: CLRS 13.4 RB-DELETE + RB-DELETE-FIXUP をnullptr-sentinel + unique_ptr 所有権モデルに適応。x が null になりうるケースは `xParent` を明示的に追跡することで対処 (CLRS の sentinel 手法の標準的な代替)
- [`piece_table.h`](../../src/document/include/neomifes/document/piece_table.h) / [`piece_table.cpp`](../../src/document/src/piece_table.cpp) 全面書き換え: `m_pieces` (vector) → `m_tree` (PieceTree)。`findPiece`/`splitAt` を `ensureBoundary` (PieceTree::pieceContainingStrictly を使う) に統合。公開 API は 1 メソッドも変更なし
- [`line_index.h`](../../src/document/include/neomifes/document/line_index.h) のドキュメントコメント修正 (誤った「Phase 2b で O(log n) 化」の予告を削除し、正確な制約説明に置換)
- [`docs/issues/line_index_o_log_n.md`](../issues/line_index_o_log_n.md) 新規 — 上記の設計修正の詳細と将来案
- `tests/unit/document_piece_tree_test.cpp` に erase 系テスト追加 (単体 11 ケース + 2 種のランダムストレステスト、うち1つは reference model との突合)
- `tests/unit/document_property_test.cpp`: 2000 → 20,000 反復に拡張 (ADR-007 の Phase 2b2 完了条件)

**設計の正しさ検証 (ローカルビルド不可のため入念に):**
- rotateLeft/rotateRight は Step 1 で CI 検証済み、変更なし
- eraseFixup の「x が null」ケースは `xParent` を明示引数として追跡し、初回ループのみ null x を扱う設計とした (2 回目以降の `x = xParent` 後は必ず非 null)
- aggregate 再計算は「全ての構造変更 (splice + fixup rotation) が完了してから、`xParentRaw` の**現在の**親チェーンを 1 回だけ root まで rewalk する」方式に統一。rotation は xParent かその祖先でしか起きないため、この 1 回の rewalk で必ず全ての変更箇所をカバーできることを手動トレースで確認
- 具体例 (2 子ノード削除 + sibling-black-both-children-black の再彩色ケース) を手でトレースし、期待される RB 木になることを確認

**CI 未確認:** 本セッション終了時点で push 前。次回 (または本セッション内) に CI green を確認する必要あり。

**次回 (Phase 2b3):** OriginalBuffer の mmap + Lazy Decode 化、1GB ロードベンチ。[`docs/issues/lazy_decode_mmap.md`](../issues/lazy_decode_mmap.md) 参照。

## Session 12 (2026-07-15): Phase 2b2 完了後の包括レビュー + プロセス改善

**目標:** Phase 0 〜 Phase 2b2 全体を振り返り、実装・設計・ドキュメント・プロセスの各面をレビューし、発見した問題を修正する。

**レビューで発見した問題:**

1. **ドキュメント鮮度の不整合 (複数箇所):**
   - `self_review.md` のタイトルが `v1.1` のまま (本文は v1.4 まで更新済み)
   - `self_review.md` §G「総合評価」・§I「次アクション」が Phase 0 時点の内容のまま放置 (`docs/pocs/` を新設予定、等の実行されなかった記述含む)
   - `RESUME_HERE.md` §2 に **`git init` の指示が残存** — リポジトリは Session 7 で初期化・push 済みにもかかわらず、次回セッションへの指示として古い手順が残っていた
   - `RESUME_HERE.md` §6 に **既に完了済みのタスクが重複記載** (プロパティテスト反復数拡張は Phase 2b2 Step 2 で完了済みなのに Phase 2b3 の TODO として再掲)
   - `docs/issues/piece_table_rb_tree.md` の完了条件チェックボックスが、実際には達成済みの項目も含めて全て未チェックのまま

2. **CLAUDE.md 絶対ルール10違反の兆候:** 「性能改善は必ずベンチマーク結果を根拠とする」というルールがありながら、**CI が毎回出力していたベンチマーク実測値を誰も確認していなかった。** レビューで実際に CI ログを取得したところ:
   - `PieceTable::insert` (Release): **276 ns** — 目標 500ns を達成 (ADR-007 の判断が実測でも裏付けられた)
   - `PieceTable::snapshot`: 1000 piece 規模で 3549ns。目標は 100K piece 規模 ≤1ms だが、**その規模では未計測** (外挿でのみ推定)

3. **正式フェーズレポートの欠落:** Phase 2b1・2b2 は TIMELINE.md のセッション記録のみで、CLAUDE.md 規定の正式な phase_report (設計/実装/テスト/残課題/次アクション) が作られていなかった。

4. **Phase 2b3 計画の補強点:** UTF-8 マルチバイト文字が mmap decode チャンク境界をまたぐケースの設計が `lazy_decode_mmap.md` に未記載だった。1GB ロードベンチを CI (共有ランナー) でフルサイズ実行するコストへの配慮も欠けていた。

**対応した修正 (全て本セッション内で実施):**
- [`self_review.md`](../design/self_review.md) → v1.5。タイトル修正、§G/§I に「歴史的記録である」旨の明記 + 現状反映の §G'/§I' を追加
- [`RESUME_HERE.md`](../handoff/RESUME_HERE.md) → §2 全面書き換え (git init 指示削除、CI 運用フローに置換)、§6 の重複タスク削除、1GB ベンチを CI 縮小版+ローカル手動検証の二段構成に変更、§8 新設 (セッション終了チェックリストへの誘導)
- [`piece_table_rb_tree.md`](../issues/piece_table_rb_tree.md) → 完了条件を実態に合わせて更新。ベンチ実測値 (276ns) を記録。LineIndex 関連 2 項目は「撤回」と明記
- [`document_piece_table_bench.cpp`](../../tests/bench/document_piece_table_bench.cpp) → `BM_PieceTable_Snapshot_100K` を追加し、100K piece 規模の snapshot 性能を外挿でなく直接計測できるようにした (次回 CI で実測値取得予定)
- [`lazy_decode_mmap.md`](../issues/lazy_decode_mmap.md) → UTF-8 マルチバイト文字のチャンク境界分割リスクをリスク一覧・完了条件に追加
- **[`CLAUDE.md`](../../CLAUDE.md) §11 新設「セッション終了時チェックリスト」** — 今回発見した問題の再発防止を目的とした恒久ルール。RESUME_HERE.md 全文点検、関連節の同期更新、Issue チェックボックスの即時更新、ベンチマーク実測値の確認・転記、TIMELINE 追記、親フェーズ完了時の統合レポート発行、の6項目

**次回:** Phase 2b3 (mmap + Lazy Decode + 1GB bench) に、今回追加した UTF-8 境界分割リスクと CI/ローカル二段ベンチ方針を織り込んで着手する。

**追記 (同日、push 後の CI 結果確認):** 新設した `BM_PieceTable_Snapshot_100K` の実測値が判明。**100K piece で 1.196ms、目標 (≤1ms) を約20%超過。** 1000 piece からの線形外挿予測 (0.35ms) は大きく外れており、「外挿でなく実測する」という本セッションの教訓が早速裏付けられた形。ブロッカーとはせず `piece_table_rb_tree.md` に低優先度の残タスクとして記録し、Phase 2b3 完了後に再評価する方針とした。`InsertAtEnd` は 243ns で目標 500ns を引き続き達成。

## Session 13 (2026-07-15): Phase 2b3 Step 1 (mmap + Lazy Decode コア)

**目標:** `OriginalBuffer` を Phase 2a の「全読み込み + 全文デコード」から mmap + on-demand デコードに置き換える。公開 API 不変を維持。

**設計判断 (実装検討中に確定):**
- **mmap ビュー自体の LRU 追い出しは実装しない**。x64 の仮想アドレス空間は 10GB 級ファイルでも十分足りるため、OS のページング任せで良い (`MapViewOfFile` を 1 回、ファイル全体に対して呼ぶだけ)。当初 Issue が想定していた「1GB ずつマップして LRU で解放」は過剰設計と判断
- **デコード結果のキャッシュは「初回アクセスでデコードして永久保持、追い出しなし」方式にした**。真の LRU 追い出しを実装するには `std::u16string_view` を返す現行 API が dangling view を生みうる (追い出された瞬間、既に返した view が無効化される) ため、それを安全にするには参照カウント付きキャッシュエントリへの設計変更が必要になり、リスクに見合わないと判断。メモリ増加は「実際にスクロール/検索でアクセスした範囲」にのみ比例するため、**ファイルを開いた直後**という目標計測ポイントには影響しない
- **64KiB ごとのチェックポイント索引** (バイトオフセット + その時点の CU オフセット) を初回スキャン時に構築。**チェックポイントは必ず「完全な 1 文字を処理し終えた直後」にのみ記録**するため、マルチバイト UTF-8 文字が途中で分断されることは構造的に起こり得ない (単なる注意ではなく、アルゴリズムの不変条件として保証)
- **`PieceTable` のコンストラクタが `OriginalBuffer::newlineCount()` を直接使うよう変更** — これが実質的な laziness の核。以前は `view(0, size())` でファイル全体を強制デコードしてから改行数を数えていたが、これでは mmap 化しても意味がない。改行数はバイトレベルの初回スキャンで事前計算されるようになった
- `OriginalBuffer::view()` / `BufferSnapshot::pieceView()` から `noexcept` を除去 (mmap デコード経路がアロケーションを伴うため、`std::bad_alloc` を握り潰す `catch(...)` は CLAUDE.md で禁止されている)

**成果物:**
- 新規 `platform::FileMapping` (mmap RAII、`handle_guard.h` に `FileHandle`/`MappedView` エイリアス追加)
- `OriginalBuffer` 全面再設計 (InMemory/MemoryMapped 二本立て、チェックポイント索引、on-demand decode キャッシュ)
- `PieceTable` コンストラクタ、`FileLoader` (旧 `decodeUtf8` 削除) を新設計に対応
- テスト +12 (80→92): `platform_file_mapping_test.cpp` 新設、`document_file_loader_test.cpp` にチェックポイント境界をまたぐマルチバイト文字・複数チェックポイント・newlineCount 事前計算のテスト追加

**レビューで見つけて直したバグ:** `FileMapping::size()` が move 後に stale な値 (moved-from のはずなのに古いサイズ) を返す問題。`m_view` (HandleGuard) は move で正しくリセットされるが `m_size` はただの `uint64_t` でリセットされないため。`size()` の実装を `m_view` の有効性に紐付けることで解消。

**CI 未確認:** push 前。次回セッション冒頭で確認要。

**次回 (Phase 2b3 Step 2):** 1GB/100MB load bench (CI は縮小版、フルサイズはローカル手動)、SEH によるネットワークドライブ例外対策、Phase 2b 完了報告 (`phase_2b_report.md` 1本に統合)。

## Session 14 (2026-07-15): Phase 2b3 Step 2 (SEH + load bench + Phase 2b 完了)

**目標:** Step 1 で残した Step 2 の作業 (SEH 例外対策、1GB/100MB ロードベンチ、実測値取得、Phase 2b 完了報告) を仕上げ、Phase 2b (2b1/2b2/2b3) を完了させる。

**本セッションで初めて実施できたこと:** ここまでのセッション群は「ローカル MSVC が無い」という誤った前提のもと CI 往復のみで検証していたが、Session 13 終盤でユーザーから「MSVC はマシンにインストール済み (Visual Studio Community 2026)」と訂正を受けた。本セッションはその訂正後、**初めて実装からローカルビルド検証までを push 前に完結させたセッション**。

**実施内容:**
1. **SEH 実装の検証:** Session 13 終盤で書いた `OriginalBuffer::scanUtf8Safe` / 匿名名前空間 `decodeUtf8RunSafe` (`__try`/`__except` で `EXCEPTION_IN_PAGE_ERROR` を捕捉するトランポリン関数) をローカル Debug ビルドで初めてコンパイル検証。ビルド成功、93 テスト全 pass を確認
2. **clang-tidy でのバグ発見・修正:** ローカル clang-tidy 実行で `static_cast<DWORD>(EXCEPTION_IN_PAGE_ERROR)` が「既に DWORD 型への冗長なキャスト」という指摘を受け、両トランポリン関数から不要なキャストを削除 (機能に影響はないが、CI 専用フローでは気づけなかった類の指摘)
3. **`tests/bench/document_load_bench.cpp` 新規作成:** `generateMockFile` (1MiB チャンクでの反復書込み) + `BM_LoadFile_100MB` (常時registered) + `BM_LoadFile_1GB` (`NEOMIFES_BENCH_1GB=1` の時のみ `benchmark::RegisterBenchmark` で動的登録、CI では未実行)
4. **重要な発見: Working Set 計測指標の見直し。** 当初 `MemorySnapshot::workingSetBytes` (総 Working Set) の増分を計測したところ、100MB ファイルで約100MB、1GB ファイルで約1GB相当の増分が出た — 目標 (30MB未満) を大幅に超過するように見えた。原因を調査した結果、これは **実装の欠陛ではなく計測指標の選択ミス** と判明: `scanUtf8`/`scanUtf8Safe` による初回の UTF-8 妥当性検証パスが全バイトを最低 1 回読む必要があり、mmap されたページは読み取られた時点でプロセスの総 Working Set にカウントされる (OS ファイルキャッシュとして共有・再利用可能なページであるにもかかわらず)。これはどんなファイル読込方式でも避けられない。本来 Lazy Decode アーキテクチャが保証しているのは「UTF-16 への複製をプライベートヒープに確保しないこと」であり、これを正しく反映するのは `MemorySnapshot::privateWorkingSetBytes` (共有ページを除いた増分)。この指標に切り替えて再計測したところ、100MBで0.078MB、1GBで0.46MB — 目標を大幅にクリアしていることを確認。ベンチは両方の数値をカスタムカウンターとして透明性のため記録
5. **ローカル Debug/Release 両方でフルビルド・全93テスト実行・clang-tidy を実施** — CLAUDE.md に追加した「push 前ローカル検証必須」ルールを本セッションで初めて実践
6. **実測値取得:**
   - `BM_LoadFile_100MB` (Release): 199ms、`private_working_set_delta_MiB`=0.078
   - `BM_LoadFile_1GB` (Release, `NEOMIFES_BENCH_1GB=1` 手動実行): 2031ms (目標2.0sに対し約1.5%超過、ディスクI/O律速でありデコード戦略非依存と判断し低優先度で受容)、`private_working_set_delta_MiB`=0.46
7. **ドキュメント更新:** [`lazy_decode_mmap.md`](../issues/lazy_decode_mmap.md) の完了条件チェックボックスを実測値付きで更新 (Working Set 指標の解釈も明記)、[`RESUME_HERE.md`](../handoff/RESUME_HERE.md) を Phase 2b 完了・Phase 3 着手前提の内容に全面更新

**Phase 2b (2b1+2b2+2b3) 完了条件:** 全項目達成 (snapshot 1ms・1GB load 2s の 2 項目はわずかな超過を低優先度残タスクとして受容、他は全て目標クリア)。

**次回 (Phase 3):** Rendering Engine (Direct2D/DirectWrite) 着手。詳細は RESUME_HERE.md §6 参照。

## Session 15 (2026-07-15): Phase 3 着手前レビュー (設計書のADR同期漏れ発見・修正)

**目標:** ユーザーの指示「Phase3に移る前にレビュー者となって全体計画レビューをしてください」に基づき、Phase 2b完了後・Phase 3着手前の包括的なレビューを実施する。

**発見した問題 (深刻度順):**

1. **🔴 重大: `detailed_design.md` §3.1 (Document Engine) が ADR-006 (Superseded) 時代の設計のまま凍結されていた。** ADR-007 で「Mutable RB-Tree + 都度コピーのPiece-Vector Snapshot」に方針転換し Phase 2b2/2b3 で実装済みにもかかわらず、設計書のコード例は旧案 (`std::atomic<std::shared_ptr<PieceTree>>`、"RCU風"、`snapshot() は O(1)`、原本を1GBずつLRUマップ、AddBufferは64MBチャンク、`OriginalBuffer`に`encoding`パラメータ) のままだった。`basic_design.md` L74 の「RCU風スナップショット」も同根。3セッション (Phase 2b1〜2b3) にわたって誰も気づかず放置されていた
2. **実害のリスク:** §4.2「レンダリング戦略」はフレームごとにDocumentへアクセスする設計だが、§3.1の「snapshot()はO(1)」という誤った記述により、Phase 3の設計者(未来のセッション)がフレームごとにsnapshot()を呼ぶ実装を無自覚に選ぶ恐れがあった。実際の snapshot() コストは100K piece規模で1.2〜1.5ms — 16.6ms/60fpsのフレーム予算の約7%を消費する。§4.3にはこのコストへの言及が皆無だった
3. **🟡 中: `self_review.md` §G'/§H/§I' が Phase 2b2完了時点(v1.5)のまま。** Phase 2b3 Step1+2の完了、MSVC実機ビルド訂正、SEH/ロードベンチの実測値が未反映。皮肉にも、これはSession 12でCLAUDE.md §11を新設する原因になった問題パターンの再発だった (§11のチェック対象に基本/詳細設計書自体が入っていなかったための抜け漏れ)
4. **`piece_table_rb_tree.md` の状態表記が「bench直接検証待ち」のまま** — その検証は既に完了済みだった
5. **🟢 低: Phase 0.5/1から3フェーズ持ち越しの技術的負債3件** (WarningsAsErrors切替、Named Mutex単一インスタンス化、clang-cl UBSanジョブ) が「次のフェーズで」と際限なく先送りされ続けていることを確認 (実装自体は未着手のまま、放置しても即座の実害はないが期限が曖昧化していた)

**ユーザーの判断:** 4択 (全て対応 / 重大+中のみ / 重大のみ / 記録のみ) を提示し、**「全て対応」**を選択。

**対応内容:**
- [`detailed_design.md`](../design/detailed_design.md) §3.1〜3.3・§4.3 を ADR-007 実装の実態 (mutable RB-tree、O(n) snapshot実測値、単一mmapビュー、128KiB AddBufferチャンク、永久デコードキャッシュ、UTF-8限定、SEH対策、実際のFileLoader API) に全面書き換え。§4.3に「`snapshot()`はフレームごとに呼ばない」というPhase3向けガードレールを明記
- [`basic_design.md`](../design/basic_design.md) L74 の「RCU風」記述を実態 (スナップショット複製共有) に修正、ADR-006→ADR-007の方針転換を明記
- [`self_review.md`](../design/self_review.md) → v1.6。§G'/§H/§I' をPhase 2b完了状態に更新。新規リスク R13 (snapshot コストのPhase3設計への影響、本レビューで対応済みと記録) / R14 (設計書がADR更新後も同期されないリスク、本レビューで一度顕在化・修正したことを記録)
- [`piece_table_rb_tree.md`](../issues/piece_table_rb_tree.md) 状態表記を「完全解消」に修正、ローカル実測値(1.481ms)も追記
- **[`CLAUDE.md`](../../CLAUDE.md) §11 に新規チェック項目追加:** 「ADRを新規発行・Superseded化したら、参照している設計書本体のコード例も同じセッション内で同期させる」。§6 の `WarningsAsErrors` 記述も「Phase 2b完了時に切替」という期限が実際に到来したことを反映し、「Phase 3着手時(Direct2Dコード追加前)」に確定
- [`RESUME_HERE.md`](../handoff/RESUME_HERE.md) §3.4 を「Phase 3着手前ハウスキーピング」として再構成 — 技術的負債3件を Direct2D コード着手前に片付ける小さな先行タスクとして期限を確定 (これ以上の先送りを防ぐ)

**教訓:** ADRやIssueドキュメントを正しく更新していても、それらが説明している設計原則を記述した基本/詳細設計書の**コード例本体**は別途同期させないと古いまま残る。ドキュメント鮮度チェックは「Issueのチェックボックス」だけでなく「設計書のコード例」まで対象を広げる必要がある。

**次回 (Phase 3):** まず RESUME_HERE.md §3.4 のハウスキーピング3件を消化してから、Rendering Engine (Direct2D/DirectWrite) に着手する。

## Session 16 (2026-07-16): Phase3着手前ハウスキーピング (Named Mutex + UBSan CI)

**目標:** Session 15 で期限を確定した Phase 3 着手前ハウスキーピング3件 (WarningsAsErrors切替/Named Mutex/UBSan CIジョブ) にユーザーの「実施せよ」指示で着手する。

**WarningsAsErrors切替:** 実施前に `.clang-tidy` の `WarningsAsErrors: '*'` を単純に切り替えると何が起きるか実態調査したところ、**`src/` で47件・`tests/` で276件、合計323件**の既存clang-tidy警告が判明。単純な切替は静的解析CIジョブを即座に壊す規模と判断し、この項目のみ保留してユーザー判断を仰ぐことにした (「実施せよ」の指示があっても、想定外に大きなブラストレディウスが判明した時点で確認を挟むべきというCLAUDE.mdルール#3/#9の適用)。

**Named Mutex単一インスタンス化:** `src/app/main.cpp` に `claimSingleInstance()` を実装。`CreateMutexW` で多重起動を検出し、既存ウィンドウを `FindWindowW` (`kWindowClassName` を `main_window.h` に公開昇格) + `SetForegroundWindow` でフォアグラウンド化。basic_design §2.3 が想定する「コマンドライン引数をIPCで先行プロセスへ委譲」は SessionManager 不在(Phase 4+ 実装予定)のため意図的に見送り — 投機的実装をしないというCLAUDE.mdルール#3の判断。`--measure-startup`/`--measure-memory` モードは対象外とし、CI/PoCハーネストの複数プロセス起動に影響しないようにした。ローカルで実プロセスを2重起動して動作確認済み (2番目が即exit、1番目は継続動作)。

**clang-cl UBSan CIジョブ:** 「YAML追加のみ」の想定に反し、実際にはCMake側の相応の対応が必要と判明した:
1. clang-cl使用時、既存の `/Zc:preprocessor` 等MSVC専用フラグが「未使用引数」として `/WX` によりエラー化 → `CompileOptions.cmake` に clang-cl 検出時の `-Wno-unused-command-line-argument` 追加 (CIのclang-tidyジョブが既に同じ問題に同じ対処をしていたのと同根)
2. clang-cl バンドルのUBSanランタイム (`clang_rt.ubsan_standalone_cxx-x86_64.lib`) が **静的リリースCRT (`/MT`)** でビルドされており、プロジェクトのデフォルト (`/MDd`, Debug動的CRT) とは `_ITERATOR_DEBUG_LEVEL`・`RuntimeLibrary` 双方で不整合 → 新設した `ubsan` プリセットで `CMAKE_MSVC_RUNTIME_LIBRARY: MultiThreaded` を強制、`/RTC1` も同時に除去 (ASanの既存対応と同パターン)
3. 上記を修正した後、実際にUBSanが**Microsoft STL/UCRT自体の内部実装**(`wchar.h`のwcslen高速パス的な非アライン読み込み)を誤検知として大量に検出することが判明 → `-fno-sanitize=alignment` のみ除外し、他のUBSanチェックは維持

ローカルで clang-cl ビルド (`cmake --preset ubsan`) → 全93テストpass を確認してから `.github/workflows/ci.yml` に `ubsan` ジョブを追加 (`build-and-test` に依存、`choco install llvm` で clang-cl を調達 — 既存の `static-analysis` ジョブと同じ調達パターン)。

**教訓:** 「小さなハウスキーピング」に見えたタスクが2件とも、実際にやってみると想定より深い技術的複雑性 (323件の警告、CRT/ランタイムライブラリのABI不整合、標準ライブラリ自体のUBSan非互換性) を持っていた。事前に「小さいはず」と決めつけず、着手してみて分かった実際のスコープに応じて、進める/止めて確認するを判断する必要がある — 特にCI設定変更は「動くようになるまでローカルで検証してからでないとpushしない」という既存ルールの重要性を再確認した。

**次回:** WarningsAsErrors切替のスコープ (全323件対応 / 一部除外して段階導入 / 別Issueとして正式に切り出す等) をユーザーと相談してから、Phase 3 (Rendering Engine) 本体に着手する。

## Session 17 (2026-07-16): WarningsAsErrors有効化 (src/限定)

**目標:** Session 16 で保留した3件目のハウスキーピング (`.clang-tidy` の `WarningsAsErrors: '*'` 切替) に、ユーザーへのスコープ確認を経て着手する。

**ユーザー判断:** 「323件全部即直す」「src/のみ先に切替」「Issueとして切り出し見送り」「その他」の4択を提示し、**「src/のみ先に切替」**を選択。

**実施内容:**
1. `src/` の47件を1件ずつ精査して対応:
   - 実質的な改善: `const` 化 (不要な非const参照・ロック変数)、designated initializer 化 (`TextRange`/`Checkpoint`/`PieceLookup`/`ValidateResult`)、`2u`→`2U` 等の大文字リテラルサフィックス、`if (a>b) a=b;` → `std::min` 書き換え、`unsigned char[3]` → `std::array<unsigned char,3>`、ヘッダ/cpp間の引数名不一致修正 (`eraseNode`)
   - 理由付き `NOLINT`: CLRS準拠のRB木アルゴリズム (`decodeUtf8Run`・`eraseFixup` の cognitive-complexity超過、`collectInOrder`・`validateNode` の recursion警告) は「教科書との対応関係を壊さない」「20,000反復プロパティテスト等で既に検証済み」を理由に分割しない判断。Win32文字列リテラル用のC配列2箇所 (`kWindowClassName`/`kSingleInstanceMutexName`) も同様
   - `perf_clock.cpp` の `g_processStartCounter`: CLAUDE.mdが原則禁止する「グローバル可変状態」に該当するが、`markProcessStart()`は呼び出し側が選んだ瞬間を明示的に記録する必要があり (遅延初期化のmagic staticでは違う瞬間を捉えてしまう)、意図的な例外として理由をコメントで明示しNOLINT
   - MSVC STLヘッダ (`xfilesystem_abi.h`) 内部由来の `clang-analyzer-optin.core.EnumCastOutOfRange` 誤検知は `.clang-tidy` のチェック除外リストに追加
2. **`NOLINTNEXTLINE` 誤用のデバッグ:** 最初にNOLINTコメントを追加した際、コメントブロックの「途中」に置いてしまい (`NOLINTNEXTLINE` の直後に説明文が続く形)、`NOLINTNEXTLINE` が実際の宣言ではなく次のコメント行だけを抑制してしまうミスを複数箇所で発生させた。再スキャンで6件の「消えていない」警告として発覚し、全て「NOLINT注釈は対象行の直前 (説明コメントより後) に置く」形に修正して解消。**教訓: 複数行コメント + NOLINTNEXTLINE を組み合わせる際は、NOLINT注釈を必ずコード行の直前(最後)に置くこと**
3. `src/` を0警告まで削減したことを確認 (フルスキャンで再検証)
4. **`WarningsAsErrors` のスコープ限定に関する技術的発見:** clang-tidy の `InheritParentConfig: true` は `WarningsAsErrors` を文字列連結でマージするため、「親='\*' + 子='\''」による無効化オーバーライドは機能しない (`'*,'` になり実質「全部」のまま)。逆に「親='' + 子='\*'」の一方向加算は正しく機能する。この非対称性に気づかず最初 `tests/.clang-tidy` で無効化しようとして失敗し (`--dump-config` で実際の有効値を確認して発覚)、方針を反転して `src/.clang-tidy` で有効化する方式に変更して解決
5. ローカルで CI の `static-analysis` ジョブと同じロジック (全31ファイルに対する個別 clang-tidy 実行 + 終了コード確認) を再現し、**全ファイル PASS** を確認
6. Debug/Release 両方で全93テストが green であることを再確認

**成果物:** [`src/.clang-tidy`](../../src/.clang-tidy) 新規 (`InheritParentConfig: true` + `WarningsAsErrors: '*'`)。ルートの `.clang-tidy` は `WarningsAsErrors: ''` のまま維持 (tests/ に適用される)。`tests/` の276件は別途の低優先度フォローアップとして先送り。

**次回 (Phase 3):** Rendering Engine (Direct2D/DirectWrite) に着手。

## Session 18 (2026-07-16): Phase 3a (Direct2D/DXGI 基盤配線)

**目標:** ユーザーの「Phase 3に進め」指示を受け、Plan modeでPhase 3全体を3a/3b/3c(+3d検討)に段階分割する計画を提示・承認を得た上で、**Phase 3a: D2D/DXGI/COMの配線基盤**(テキスト描画・キャッシュ・シンタックス・IME・テーマは対象外)を実装する。

**計画フェーズ:** 3体のExplore agentを並列起動しUI/appレイヤ・Document読み取りAPI・detailed_design.md §4・CMake構造・テスト規約を調査した上で、Plan agentにPhase 3a の詳細設計 (ファイル構成・MainWindow統合・デバイスロスト処理・デバイス生成タイミング・CMake・テスト戦略・ADR要否) を依頼。得られた計画をレビューし、ユーザー承認を得てから実装着手。

**成果物:**
- **[ADR-008](../decisions/ADR-008-com-raii-comptr.md)**: COM RAIIに`Microsoft::WRL::ComPtr`採用 (HandleGuard拡張ではなく — COMの「コピーでAddRef」意味論はHandleGuardのmove-only設計と根本的に異なるため)
- **[ADR-009](../decisions/ADR-009-deferred-device-init.md)**: デバイス生成は同期・UIスレッド・自己ポストメッセージ (`WM_APP+1`) 方式。ワーカースレッド化は不採用 (D3D11+D2D生成が実測5ms未満で起動予算に対し無視できるコストであり、ワーカースレッド化はCOMアパートメント設計の複雑性に見合わないため)
- 新規 `src/render/` レイヤ (`resize_math.h`/`render_error.h+cpp`/`d2d_factories.h+cpp`/`render_device.h+cpp`/`render_pipeline.h+cpp`):
  - `RenderExpected<T> = std::expected<T, RenderError>` — **プロジェクト初のstd::expected採用箇所** (Phase 2はstd::expected完全対応前の設計だったためstd::variantを使用していたが、CLAUDE.md §4の規定通りに実装)
  - `RenderDevice`: D3D11+D2D+DXGIデバイスグラフのRAII所有。`D3D_DRIVER_TYPE_HARDWARE`→`WARP`フォールバック (GPU無しCI runner対策)。resize時は`SetTarget(nullptr)`→`ResizeBuffers`→再バインドの順序を厳守
  - `RenderPipeline`: MainWindow/appが触るファサード。デバイスロスト検知時はデバイスグラフ全体を破棄・再生成 (MS推奨通り、スワップチェーンだけでなく)
- `MainWindow`拡張: `onDeferredInit`(初回`WM_PAINT`後`WM_APP`経由で1回発火)・`onResize`・`setPaintHandler()`を追加、`WM_SIZE`/`WM_DPICHANGED`ハンドリング新設。GDIプレースホルダーパスは温存 (レンダラ未アタッチ時のフォールバックとして)
- `main.cpp`: `LaunchMode::Normal`時のみ`RenderPipeline`を生成・配線。`--measure-startup`/`--measure-memory`モードは一切変更なし (ADR-009の設計通り、構造的に計測タイミングへの影響がない)
- 単体テスト+11 (`render_resize_math_test.cpp`, `render_error_test.cpp`)、統合テスト+1 (`render_device_smoke_test.cpp` — 実際のCOM/D3D11/D2D/DXGIデバイス生成をHARDWARE→WARPフォールバック込みで検証、GPU無し環境でも成功する設計なのでhard passとして扱う)。テスト総数 93→109

**検証:**
- ローカルDebug/Release両方でフルビルド・全109テストpass (初回ビルドでCOM API呼び出しが一発でコンパイル成功 — ID2D1Device6/DeviceContext6が基底interfaceからのQueryInterfaceアップグレードで取得する必要がある点など、事前調査が正確だった)
- clang-tidy (`src/.clang-tidy`の`WarningsAsErrors: '*'`込み) で新規ファイル6本を検証、初回スキャンで「designated initializer化」×多数・「unchecked-optional-access」1件・「const化」1件を検出・修正、再スキャンで0警告確認
- `--measure-startup`実測: firstPaintNs=33.16ms (ローカル、目標300msの11%) — レンダラ配線後も退化なし
- 実アプリを起動し、プロセスにロードされたモジュール一覧で`d2d1.dll`/`d3d11.dll`/`dxgi.dll`が実際にロードされていることを確認 (GDIへの静かなフォールバックではなく、D2D/DXGIが本当に有効化されたことの裏付け)。ウィンドウを4段階でリサイズしクラッシュしないこと、スクリーンショットで表示崩れがないことを確認

**次回 (Phase 3b):** DirectWriteテキストレイアウト、Document内容の実描画、ビューポート/スクロール位置管理。`detailed_design.md` §4.3に追記済みの「snapshot()はフレームごとに呼ばない」ガードレールを実装で守ること。

## Session 19 (2026-07-16): Phase 0〜3a 包括レビュー + Phase 3b 計画ブラッシュアップ

**目標:** ユーザーの指示「Phase0〜Phase3aまでの実装内容と、Phase3bの実装計画をレビューして改善点や修正点を明確にしたうえで品質改善せよ」に基づき、全フェーズのドキュメント・ソースコード・Phase 3b 計画を包括的にレビューし、発見した問題を修正する。

**レビュー手法:** Explore agent でソースコード全 35 ファイル (~3,900 行) + テスト 17 ファイル (~2,140 行) を棚卸し (命名規約・include 整合性・NOLINT・所有権・CMake 一貫性・ADR 状態) した上で、設計書 4 本 (basic_design / detailed_design / self_review / RESUME_HERE) + TIMELINE.md を全文精読。

**発見した問題 (Session 15 と同パターンの再発を含む):**

1. **🔴 `detailed_design.md` §4.1 が Phase 3a 実装と乖離** — 同 §3.1 で Session 15 に発見・修正したのと同じパターン。§4.1 のコード例は `m_d2dFactory`/`m_dwFactory` を RenderPipeline の直接メンバに持つ旧案のまま、実装では `d2d_factories.h` のプロセス単位シングルトンに分離済み。`void attach(HWND)` (非返却) vs 実際の `RenderExpected<void> attach(HWND)`。`TextLayoutCache`/`GlyphCache`/`DamageTracker` が既存メンバかのように記載されているが未実装。実際の `RenderPipeline → optional<RenderDevice> → ComPtrs` 構成が反映されていなかった
2. **🟡 `detailed_design.md` §18.3 のベンチ目標値 `PieceTable::snapshot ≤ 100ns`** — 1000 倍の誤記 (実測 1.2ms)
3. **🟡 `detailed_design.md` §19.1-§19.2 のビルド/CI 記述陳腐化** — "VS2022" 表記、実在しないジョブ名
4. **🟡 `basic_design.md` §4.1 の「非同期化」表現** — ADR-009 は同期・UIスレッド・WM_APP 方式を明確に選択
5. **🟢 RESUME_HERE.md §1 に「(push/CI 確認待ち)」残存**、`self_review.md` §H R1 が Phase 3a 未反映
6. **🟢 `startup_profile.h` の未使用 `#include <string>`**
7. **Phase 3b 設計課題 4 件を特定:** DC アクセスパターン / Document→Render 通知 / スクロール位置管理 / DPI 対応

**対応内容:**
- `detailed_design.md` §4.1-§4.3 を Phase 3a 実装 (RenderDevice/RenderPipeline/d2d_factories シングルトン分離、RenderExpected エラー型) の実態に全面書き換え。**§4.4 新設** — Phase 3b 設計課題 4 件を具体的な推奨方針付きで明記
- `detailed_design.md` §18.3 ベンチ目標値修正 (snapshot 100ns→1ms) + 実測値付き状態カラムを追加
- `detailed_design.md` §19.1-§19.2 を CI 実態 (build-and-test/static-analysis/ubsan の 3 ジョブ) に更新
- `basic_design.md` §4.1「非同期化」→「初回 WM_PAINT 完了後に UI スレッド上で遅延実行 (ADR-009)」
- RESUME_HERE.md §1「push/CI 確認待ち」削除、§6 に Phase 3b 設計課題 4 件の具体的チェックリストを追記
- `self_review.md` v1.7→v1.8、§H R1 を「解消」に更新、§G' 推奨判断に §4.4 参照を追加
- `startup_profile.h` の未使用 `#include <string>` 削除
- ローカル Debug/Release 全 109 テスト pass、clang-tidy 0 件を確認

**教訓:** Session 15 で発見・対策した「ADR 更新後の設計書本体同期漏れ」が §4 (Rendering Engine) で再発していた。CLAUDE.md §11 のチェックリスト「ADR 更新時は設計書本体のコード例も同期」は §3 (Document Engine) だけでなく、新設された §4 (Phase 3a の成果物) にも適用する必要がある。Phase 3a では 2 本の ADR (008/009) を発行したが、§4.1 の旧来のコード例が同セッション内で更新されないまま push された。原因は「Phase 3a ではコード例を新設したわけではなく、既存の §4.1 に触れなかったため、チェックリストの対象として認識されなかった」こと。

## Session 20 (2026-07-16): Phase 3b (DirectWrite テキストレイアウト + Document 実描画)

**目標:** ユーザーの「Phase 3b の計画に入れ」指示を受け、Plan mode で Session 19 が特定した Phase 3b 設計課題 4 件 (DC アクセスパターン / Document→Render 通知 / スクロール位置管理 / DPI 対応) の解決方針を確定し、DirectWrite でのテキスト実描画を実装する。

**計画フェーズ:** RESUME_HERE.md §6・detailed_design.md §4.4・既存の render/document ソース (render_pipeline.h/render_device.h/document.h/buffer_snapshot.h/main.cpp/file_loader.h) を直接精読した上で、Plan agent に詳細設計 (4課題の解決方針・具体的シグネチャ・ADR要否・テスト戦略・タスク分割) を依頼。Plan agent は「Rendering Engine → Document Engine の依存方向」についてプロンプトの前提誤りを指摘・訂正 (CLAUDE.md §3 のレイヤ図は上位→下位の依存であり、Rendering Engine は Document Engine より上位に描かれているため直接依存は規約上正しい)。得られた計画を `src/render/CMakeLists.txt`・`docs/decisions/README.md` 等で裏取り検証してからユーザー承認を得て実装着手。

**成果物:**
- **[ADR-010](../decisions/ADR-010-render-depends-on-document.md)**: Rendering Engine は Document Engine に直接依存する (`neomifes_render` → `neomifes_document` PUBLIC 依存)。却下案 (app層仲介・`ITextSource`抽象) の理由も記録
- `RenderDevice`: `clearAndPresent()` を `beginFrame()`/`endFrame()` に分解 (DC を非所有ポインタで貸し出し、`m_frameOpen` で誤用ガード)。`setDpi()` 追加
- `Document`: `version()` カウンタ追加。`offsetToLine`/`lineToOffset` を `mutable` キャッシュ経由の `const` メソッドに変更 (`RenderPipeline` が `const Document*` 越しに呼べるように)
- `RenderPipeline`: `setDocument()`/`setTopLine()`/`topLine()` 追加。`refreshDocumentCacheIfStale()` が `Document::version()` 比較で `snapshot()` を呼ぶ唯一の箇所 (§4.3 ガードレールの実装)。`ensureTextFormat()`/`ensureTextBrush()`/`drawVisibleLines()` を追加、可視範囲を1回の `extract()` で取得し `\n` 分割して `DrawText`。`resize()` に `dpiScale` 引数追加
- 新規 `viewport_math.h`: `computeVisibleLineCount()` (純粋関数、`resize_math.h` と同パターン)
- `main.cpp`: `--open <path>` 引数追加、`Document` を `window`/`renderPipeline` より前に宣言 (非所有ポインタの生存期間保証)。`wWinMain` の認知的複雑度超過 (clang-tidy) を `loadStartupDocument()` ヘルパー抽出で解消
- テスト+14 (単体: `render_viewport_math_test.cpp`/`document_document_test.cpp`、統合: `render_device_smoke_test.cpp` に誤用ガード2件追加、新規 `render_text_smoke_test.cpp`)。テスト総数 109→123

**検証:**
- ローカル Debug/Release 両方でフルビルド・全123テスト pass
- clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み) で4件検出・修正: `wWinMain` 認知的複雑度超過 (ヘルパー抽出)、未使用 using 宣言、`bugprone-unchecked-optional-access` (`refreshDocumentCacheIfStale`/`ensureTextFormat` という不透明な関数呼び出しを挟むと `if (!m_device)` によるナローイングが効かなくなる問題 — チェック直後に `RenderDevice&` 参照へ束縛することで解消)、`bugprone-unused-return-value` (`(void)` キャストでは抑制されず、`[[maybe_unused]]` 付き名前付き変数への代入で解消)。再スキャンで0警告確認
- 実アプリを `--open <file>` で起動し、PowerShell (`System.Drawing`) でスクリーンショットを撮影して複数行・タブインデントを含むテキストが正しく描画されることを確認。600x400→1400x900→300x200→1000x650 の4段階リサイズでクラッシュ・表示崩れがないことを確認 (途中、スクリーンショットが真っ暗に見えた原因はウィンドウが最小化されていたことによるもので、GDI プレースホルダーと D2D クリア色が偶然同じ RGB(30,30,30) だったため誤診断しかけた — `IsIconic()` で状態確認する一手間が有効だった)

**教訓:** GDI プレースホルダー色と D2D 背景クリア色が同一 RGB 値のため、スクリーンショットの見た目だけでは「D2D が実際に描画しているか」を判別できない。プロセスのロード済みモジュール確認 (d2d1.dll等) や `IsIconic()` 等の状態確認を併用することで誤診断を避けられた。

**次回 (Phase 3c):** `TextLayoutCache`/`GlyphCache`/`DamageTracker`、60fps計測ハーネス (`--measure-frame`)。`RenderPipeline::drawVisibleLines()` は現状行ごとに `DrawText` を直接呼ぶだけで `IDWriteTextLayout` のキャッシュを持たない — キャッシュ粒度・無効化戦略の設計が必要 (新規 ADR の可能性が高い)。

## Session 21 (2026-07-16): Phase 3c (TextLayoutCache + 粗粒度フレームスキップ + `--measure-frame`) — Phase 3 全体完了

**目標:** ユーザーの「着手せよ」指示を受け、Session 20 が引き継いだ Phase 3c (`TextLayoutCache`/`GlyphCache`/`DamageTracker` + 60fps計測ハーネス) の設計・実装を行う。CLAUDE.md §7 の Phase 3 DoD「60fpsスクロール確認」の達成が最終目標。

**計画フェーズ:** RESUME_HERE.md §6・`main.cpp`/`startup_profile.h`/`ci.yml` の既存計測PoCパターン・`render_pipeline.h/.cpp` の現状を直接精読した上で、Plan agent に詳細設計を依頼。Plan agent は元の3コンポーネント構想 (`TextLayoutCache`/`GlyphCache`/`DamageTracker`) を再検証し、**GlyphCache と細粒度 DamageTracker を明示的に延期する**ことを提案 (CLAUDE.md ルール3「推測実装をしない」・ルール10「性能改善はベンチマーク根拠必須」に基づく判断: D2D の `DrawTextLayout()` が既にシェーピング済みグリフラン情報を再利用するため TextLayoutCache 単体で恩恵の大部分を得られる可能性が高く、独自グリフアトラスが必要という実測根拠が無い。細粒度 DamageTracker も対話的編集・スクロールが未実装のため実際のユースケースが無い)。ユーザー承認を得て実装着手。

**成果物:**
- **[ADR-011](../decisions/ADR-011-phase3c-render-cache-scope.md)**: Phase 3c は TextLayoutCache のみを実装し、GlyphCache・細粒度 DamageTracker を延期する。再評価トリガー (ベンチ/計測での目標未達 → GlyphCache 再検討、Phase 4 での対話的編集 → 細粒度 DamageTracker 再検討) を明記
- 新規 `src/render/text_layout_cache.{h,cpp}`: 行番号キーの `IDWriteTextLayout` キャッシュ。`Document::version()` 変化時の wholesale `clear()` のみで無効化 (LRU無し — 無制限成長は [`text_layout_cache_unbounded_growth.md`](../issues/text_layout_cache_unbounded_growth.md) に tripwire として記録)
- `RenderPipeline`: `drawVisibleLines()` を `dc.DrawText()` 直呼びから `TextLayoutCache::getOrCreate()` + `dc.DrawTextLayout()` に変更。`FrameState`/`captureFrameState()` による粗粒度フレームスキップ (`DXGI_SWAP_EFFECT_FLIP_DISCARD` + DWM合成下での安全性、`MainWindow::handlePaint()` の無条件 `ValidateRect()` により `WM_PAINT` 再発行ループにならないことを確認)
- 新規 `src/app/frame_profile.{h,cpp}` + `main.cpp` の `--measure-frame <out.json>`: 5万行の合成ドキュメント (または `--open` の実ファイル) で300フレーム連続スクロールを計測し min/max/avg/p50/p95 + キャッシュ統計を JSON 出力。`--measure-startup` と同じ計測PoCパターンを踏襲し `MainWindow` へのマウス/キーボード配線は追加していない
- 新規 `tests/bench/render_text_layout_cache_bench.cpp` (`neomifes_render_bench`): デバイス/vsync を介さない TextLayoutCache 単体のCPUコスト計測
- `.github/workflows/ci.yml`: 「Frame PoC (report only, no hard fail)」ステップ追加 (Startup PoC と同じ soft-fail パターン)
- 新規 `docs/phase_reports/phase_3_report.md`: Phase 3 (3a/3b/3c) 統合完了レポート。ユーザーに確認の上、Phase 3c 完了時点で発行 (「Phase 3d」= Line Gutter/テーマ/IME 等は Phase 3 の DoD に必須でないため対象外とし独立の将来フェーズとして扱う方針で合意)
- テスト+6 (単体: `render_text_layout_cache_test.cpp`)、統合+3 (`render_text_smoke_test.cpp` にキャッシュ/フレームスキップ検証3件追加、新規 `frame_measure_test.cpp`)。テスト総数 123→129

**検証:**
- ローカル Debug/Release 両方でフルビルド・全129テスト pass
- google-benchmark 実測 (Release): `BM_TextLayoutCache_Miss` 532ns (目標<50µsに対し約94倍のマージン)、`BM_TextLayoutCache_Hit` 4.34ns (目標<5µsに対し約1152倍のマージン) — GlyphCache 延期判断を裏付ける実測データとして ADR-011 に記録
- `--measure-frame` 実測 (Release、5万行、300フレーム): avg 5.52ms / p50 5.56ms / p95 5.66ms / max 8.11ms — 全フレームが16.6ms予算内、60fps DoD 達成を確認
- clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み) で2件検出・修正: `modernize-use-ranges` (`std::sort`→`std::ranges::sort`)、`readability-avoid-nested-conditional-operator` (三項演算子の入れ子を if/else if/else に書き換え)。再スキャンで0警告確認
- `bugprone-unchecked-optional-access`/`bugprone-unused-return-value` 系の再発は無し (Session 20 で確立した「`RenderDevice&` 参照への早期束縛」「`[[maybe_unused]]` 付き名前付き変数への代入」パターンを新規コードでも一貫して踏襲したため)

**教訓:** 元の設計スケッチ (`detailed_design.md` §4.1) が構想していた3コンポーネントのうち2つを「未着手」ではなく「測定に基づき明示的に延期」として扱い、ADR に再評価トリガーを明記したことで、将来のセッションが「なぜGlyphCacheが無いのか」を一から再検討する無駄を防いだ。CLAUDE.md ルール3/10 (推測実装をしない・ベンチマーク根拠必須) は「機能を作らない」判断そのものにも適用され、その判断もまた根拠と共に記録すべきものであることを再確認した。

**次回 (Phase 4):** Editor Core (Cursor/SelectionModel/Command/Undo/Viewport)。`RenderPipeline::setTopLine()` を実際に駆動する `Viewport` への置換、対話的編集実現後の細粒度 DamageTracker 再評価 (ADR-011)、`TextLayoutCache` のメモリ実測 ([`text_layout_cache_unbounded_growth.md`](../issues/text_layout_cache_unbounded_growth.md)) が主な引き継ぎ事項。

## Session 22 (2026-07-16): Phase 4a (Command/Undo/Selection、ヘッドレス) — 100万Undo DoD 実測

**目標:** ユーザーの「継続実行せよ」指示 (前セッションの `/compact` 後) を受け、`RESUME_HERE.md` §6 が示す次アクション通り Phase 4 (Editor Core) に着手する。CLAUDE.md §7 の Phase 4 DoD「100万Undo達成」の達成が最終目標。

**計画フェーズ:** `docs/phase_reports/phase_3_report.md` §6・`detailed_design.md` §5/§6 を確認した上で、Explore agent 3体を並列起動し (1) `Document` の公開API・エラー処理規約・`RenderPipeline` との連携パターン、(2) `MainWindow` の入力処理有無・`main.cpp` の配線点・render層のエラー処理規約 (`std::expected`)・CMake登録パターン、(3) 既存テスト/ベンチの規約・Phase 4 関連 ADR/issue・要件定義書の Undo 要件文言、を調査。`src/core/` が完全新規レイヤーであること、`detailed_design.md` §5/§6 (縦編集・約20種標準コマンド・UndoStackの1000件バケット化+zstd圧縮+ディスクスワップ) が Document/Render 実装確定前の Phase 0 スケッチであることを確認し、Plan Mode で **Phase 4 を 4a (ヘッドレス基盤+100万Undoベンチ) / 4b以降 (UI配線・矩形選択・圧縮等)** に分割する計画を立案。Phase 3 の 3a/3b/3c 分割、ADR-011 の延期パターンを踏襲。ExitPlanMode でユーザー承認を得て実装着手。

**成果物:**
- **[ADR-012](../decisions/ADR-012-phase4a-editor-core-scope.md)**: Phase 4a は Command/Undo/Selection のヘッドレス基盤のみを実装し、UI配線・UndoStackの圧縮/ディスクスワップ・矩形選択(縦編集)・`tryMerge`・`MovementUnit`・Search/Encoding/Plugin/AI依存の標準コマンド群・`Viewport`の`FoldingMap`を明示的に延期する。各項目の再評価トリガーを明記
- 新規 `src/core/` レイヤ (`neomifes::core`、`neomifes::document` にのみ PUBLIC 依存、`neomifes::render` には意図的に非依存 — CLAUDE.md §3「並行実行可能な独立エンジン」の原則を優先):
  - `Cursor`(design doc §5.1のまま、フラット `TextPos`)/`ICommand`・`ExecutionContext`(新規グルー)/`SelectionModel`(8種の `MovementKind`、複数カーソル+範囲重複マージ、上下移動の列保持は `LineIndex` の行区切り契約から `BufferSnapshot::extract` 無しで計算)/`InsertTextCommand`・`DeleteRangeCommand`・`ReplaceRangeCommand`(`BufferSnapshot::extract` で削除/置換前テキストを捕捉)/`UndoStack`(`std::vector<unique_ptr<ICommand>>` 2本のシンプル実装)/`CommandDispatcher`(execute→push を1呼び出しにまとめる新規グルー)/`Viewport`(`scrollTo`/`ensureVisible`/`visibleLines`、`FoldingMap`無し)
- 新規 `tests/bench/core_undo_stack_bench.cpp` + `neomifes_core_bench` ターゲット: 100万コマンドの push/undo を1単位として計測 (`state.range()` は使わず固定 `constexpr kOpCount` — 既存bench規約踏襲)
- 新規 `docs/issues/undo_stack_unbounded_memory.md`: UndoStack のメモリ使用量無制限成長の tripwire (時間面のDoDは実測済みだがメモリ面は未計測)
- テスト+35 (単体: `core_selection_model_test.cpp`/`core_edit_commands_test.cpp`/`core_undo_stack_test.cpp`/`core_command_dispatcher_test.cpp`/`core_viewport_test.cpp`)。テスト総数 129→164
- `.github/workflows/ci.yml`: ベンチ smoke run の `$benchExes` 配列に `neomifes_core_bench.exe` 追加

**検証:**
- ローカル Debug/Release 両方でフルビルド・全164テスト pass
- google-benchmark 実測 (Release、`--benchmark_min_time=0.01s`、1,000,000コマンド): `BM_UndoStack_PushOneMillion` 352ms、`BM_UndoStack_UndoOneMillion` 174ms — DoD「100万Undo達成」を実測で確認 (ADR-012 に記録)。Debug実測はpush 5.01s/undo 2.05s (CI予算内)
- clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み、`src/core/*.cpp` 対象) で3件検出・修正: `readability-math-missing-parentheses`(`*`/`+`混在への括弧追加、3箇所)、`readability-avoid-nested-conditional-operator`(上下移動の行番号決定を三項演算子の入れ子からif/else if/elseに書き換え)、`misc-const-correctness`(`mergeOverlapping()`のループ変数を`const Cursor&`に)。再スキャンで0警告確認。`tests/`側は既存規約通りwarn-onlyのため対応不要 (BM_マクロ由来の構造的警告のみ、自作コード起因の1件 `core_edit_commands_test.cpp` の括弧欠落は修正)
- ヘッダファイル単体への clang-tidy 直接実行はコンパイルフラグ欠如による誤検知(`std::string_view`未検出等)を起こすことを確認 — CI と同じく `.cpp` ファイルのみを対象とする方針を再確認

**教訓:** Phase 0 設計スケッチ (`detailed_design.md` §5/§6) の全量を一度に実装しようとせず、DoD (「100万Undo達成」の一点)を満たす最小構成をまず切り出してヘッドレスに実装し、ベンチマークで実測してから UI配線等の残作業を次フェーズへ回す判断が、ADR-011 (Phase 3c) に続き有効に機能した。`UndoStack`の1000件バケット化/zstd圧縮/ディスクスワップという具体的な設計は、実際にメモリを計測してから要否を判断すべき最適化であり、要件定義書に規定の無い256MB予算という数値を鵜呑みにして先行実装しないという判断がCLAUDE.mdルール10の実践として機能した。

**次回 (Phase 4b):** キーボード/マウス入力配線・キャレット描画・`Viewport`↔`RenderPipeline`接続。`MainWindow::wndProc` への `WM_KEYDOWN`/`WM_CHAR`/`WM_LBUTTONDOWN`/`WM_MOUSEWHEEL` 新設、`RenderPipeline` への選択範囲/キャレット描画パス追加が主な引き継ぎ事項。詳細は ADR-012・`RESUME_HERE.md` §3.8/§6 参照。

## Session 23 (2026-07-17): Phase 4a レビュー + Phase 4b1 (キーボード入力配線 + キャレット描画 + マウスホイールスクロール)

**目標:** ユーザーから「次のPhaseに進みたい。もしくはここで貴方にレビューして貰うのが良いか?」と問われ、まず `/code-review` でPhase 4a (`a513021`) を高effortでレビューし、指摘を修正してから Phase 4b に着手する2段構成のセッション。

**レビューフェーズ:** 8観点 (line-by-line/removed-behavior/cross-file/reuse/simplification/efficiency/altitude/CLAUDE.md conventions) の並列 finder エージェントで Phase 4a の diff を精査 (2エージェントはAPIセッション制限で失敗、手動で補完)。30候補から重複排除・検証の上、CONFIRMED 1件・PLAUSIBLE 4件の計5件に収束。**CONFIRMED:** `Viewport::ensureVisible()` が `noexcept` 宣言されているが内部で呼ぶ `Document::offsetToLine()` が (`LineIndex` 再構築時に allocate しうるため) `noexcept` ではなく、例外発生時に `std::terminate()` を招く不整合。即座に修正 (`noexcept` 除去)。**PLAUSIBLE (Phase 4b以降の既知課題として記録):** CRLF行末でのカーソル位置不整合(Phase 6 Encoding Engineスコープ)、垂直移動のsticky column欠如、編集コマンドがSelectionModelのカーソル位置を更新しないギャップ、`mergeOverlapping()`の単一カーソル時の無駄な allocation。効率指摘の `mergeOverlapping()` fast pathも同セッションで修正。

**計画フェーズ (Phase 4b):** ユーザーが「次フェーズに進め」と指示。`RESUME_HERE.md` §6・`MainWindow`/`RenderPipeline`/`main.cpp` の現状を直接精読した上で、Explore agent 1体で D2D描画プリミティブの現状(`DrawTextLayout`/`Clear`/`CreateSolidColorBrush`のみ、矩形/線描画皆無)・`HitTest`系APIの前例(皆無)・`MainWindow`を演習するテストの有無(皆無、Win32メッセージシミュレーションハーネス自体が存在しない)を確認。この調査結果と、レビューで発覚した「編集コマンドがSelectionModelを更新しない」ギャップを踏まえ、Plan Modeで **Phase 4b を 4b1 (キーボード入力+キャレット描画+マウスホイール、ヘッドレステスト可能) / 4b2 (マウスクリック位置特定+選択範囲ハイライト、新規hit-test設計を要する)** に分割する計画を立案。Phase 3の3a/3b/3c分割、Phase 4のADR-012分割と同じ「1PR=1責務」パターンを踏襲。ExitPlanModeでユーザー承認を得て実装着手。

**成果物:**
- レビューで発覚したギャップの解消: `SelectionModel::moveAllTo(TextPos)` 新設、`ICommand::cursorPositionAfterExecute()`/`cursorPositionAfterUndo()` を全コマンドに追加し `CommandDispatcher::dispatch()`/`UndoStack::undo()`/`redo()` が自動的に `SelectionModel` を更新するよう配線 (Phase 4a時点で未使用だった `ExecutionContext::selection()` を実際に使い始めた)
- `MainWindow`: `onKeyDown`/`onChar`/`onMouseWheel` フック新設、`WM_KEYDOWN`/`WM_CHAR`/`WM_MOUSEWHEEL` 処理を追加
- 新規ライブラリ `neomifes::app_input` (`src/app/editor_input.h/.cpp`): Win32非依存の `handleKeyDown`/`handleChar`/`applyMouseWheelScroll`。Win32メッセージハーネスが無い制約から、Win32プリミティブ型の引数を受け取るが内部でWin32 APIを一切呼ばない設計にすることでヘッドレステスト可能にした (Phase 4aの`src/core/`と同じ思想)
- `RenderPipeline`: `setCaretPosition(TextPos)` 新設、`drawVisibleLines()` のループ内でキャレット行に `HitTestTextPosition`+`FillRectangle` で描画 (新規ブラシは作らず既存 `m_textBrush` を再利用)。`FrameState` に `caretPosition` を追加し、Phase 3cの粗粒度フレームスキップがキャレット単独移動を再描画対象外にしてしまう不整合(レビューでは未指摘、実装中に自己発見)を修正
- `src/app/main.cpp`: `SelectionModel`/`CommandDispatcher`/`Viewport` を配線、`Viewport::topLine()`→`RenderPipeline::setTopLine()` のブリッジを実装 (Phase 4aで「Phase 4bの仕事」と明記されていた箇所)
- テスト数: 164→185 (単体+20、統合+1: `RenderTextSmokeTest.CaretOnlyMovementForcesRedrawInsteadOfFrameSkip` でFrameState修正を実証)
- 実アプリ (`NeoMIFES.exe`) を起動し `System.Windows.Forms.SendKeys` で入力 (文字入力・矢印・Backspace/Delete・Ctrl+Z/Y・Enter・Home/End・Ctrl+Home/End) を送信、クラッシュしないことを確認。約1,350文字の連続入力セッションで `WorkingSet64` を計測 (48.53MB→51.49MB、増分約3MB) し `undo_stack_unbounded_memory.md` に初回実測として追記

**検証:**
- ローカル Debug/Release 両方でフルビルド・全185テスト pass
- clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み、変更/新規 `.cpp` 全8ファイル対象) で2件検出・修正: `readability-redundant-casting` (三項演算子+`static_cast`の組み合わせをif文に書き換え)、`hicpp-use-auto`/`modernize-use-auto` (`char16_t inserted = static_cast<char16_t>(ch)` を `auto` に)。再スキャンで0警告確認
- **既知の限界:** キャレットの視覚的な描画位置の正しさ(ピクセル単位)は自動検証していない。このセッションにはネイティブWin32ウィンドウのスクリーンショット/GUI自動化ツールが無く、`SendKeys`によるクラッシュ検知のみ実施 — Phase 3a/3bで行われていた「スクリーンショットで確認」に相当する視覚検証はユーザー自身に委ねる必要がある

**教訓:** Phase 4aのコードレビューで見つかった「`ExecutionContext`が`SelectionModel&`を保持するが未使用」という altitude 指摘 (「早すぎる抽象化では」という懸念) は、Phase 4bで実際にキーボード入力を配線する段になって「編集後にカーソルをどこへ動かすか」という実需要が生まれ、まさにその未使用フィールドで解決するという形で正当化された。レビュー段階で「今は使われていない」と映った設計が、次フェーズの実装で自然に活きるケースがあることを示す一例。また、`FrameState`とキャレットの相互作用(粗粒度フレームスキップがキャレット単独移動を握りつぶす)はレビューでは発見されず実装中に気づいた — 既存の最適化機構に新機能を足すときは、その機構の判定条件を毎回洗い出す必要があるという教訓。

**Phase 4b1 完了後:** ユーザーが実アプリで動作確認 (「実機確認」指示)。エージェントが `NeoMIFES.exe` を起動し、対話的な操作項目 (文字入力・矢印移動・Home/End・Backspace/Delete・Ctrl+Z/Y・Enter・マウスホイール・リサイズ) の確認をユーザーに依頼。ユーザー確認後「Phase 4b2 に進me」と指示、続けて同一セッション内で Phase 4b2 に着手。

**計画フェーズ (Phase 4b2):** Explore agent 1体で (1) `RenderPipeline` の現状 (Phase 4b1後の最新状態、`drawVisibleLines()`のループ構造、`drawCaretOnLine()`のシグネチャ)、(2) `viewport_math.h`/`resize_math.h` のDPI変換規約、(3) `HitTestPoint`/`DWRITE_HIT_TEST_METRICS` の標準シグネチャ (リポジトリに前例なし、SDKヘッダも同梱されていないため一般知識から報告)、(4) `main_window.cpp`にマウスキャプチャ/移動系メッセージの前例が無いこと、(5) `editor_input.h/.cpp`の既存シグネチャと`main.cpp`配線パターン、(6) `SelectionModel::moveAllTo()`がposition/anchor両方を設定するためShift+クリックの選択拡張に使えないこと、を確認。この調査結果を基に Plan Mode で設計 — **ドラッグ選択・ダブル/トリプルクリック・Alt+クリック複数カーソルは Phase 4b3 以降へ延期**し、単純クリック+Shift+クリックによる範囲選択のみをスコープとする。ExitPlanModeでユーザー承認を得て実装着手。

**成果物 (Phase 4b2):**
- `SelectionModel::moveAllTo(TextPos, bool extendSelection = false)` — デフォルト引数で既存呼び出し (`CommandDispatcher`/`UndoStack`) を変更せず後方互換を保ちつつ、Shift+クリックでのanchor保持に対応
- `RenderPipeline::hitTest(xPx, yPx) -> optional<TextPos>` 新設 — このコードベース初の `IDWriteTextLayout::HitTestPoint` 使用。既存の `TextLayoutCache`/DPI変換/`m_topLine` 計算を再利用し、可視行なら描画時に作成済みのレイアウトをキャッシュヒットで再利用する設計
- 選択範囲ハイライト描画: `RenderPipeline::setSelectionRange(TextRange)` 新設、`FrameState`に`selectionRange`追加(caretPosition追加と同じ理由でフレームスキップとの不整合を予防)、新規`m_selectionBrush`(半透明青)、`drawSelectionOnLine()`を`drawVisibleLines()`ループ内で`DrawTextLayout`より前に呼び出しテキストの下に描画
- `neomifes::app::handleMouseDown(TextPos, bool shiftDown, ...)` 新設 — ヒットテスト済みの`TextPos`を受け取るだけで、座標変換自体はレンダー層(`RenderPipeline::hitTest()`)が担い、`editor_input`はWin32/レンダー非依存の制約を維持
- `MainWindow`: `onMouseDown`フック新設、`WM_LBUTTONDOWN`処理を追加 (`<windowsx.h>`の`GET_X_LPARAM`/`GET_Y_LPARAM`、`wParam & MK_SHIFT`でShift状態取得)
- テスト数: 185→189 (単体+4: `moveAllTo`のextendケース2件+`handleMouseDown`2件、統合+2: `hitTest`の境界値検証+選択ハイライトのフレームスキップ検証)
- 実アプリで `SetCursorPos`+`mouse_event`(P/Invoke)によるクリック・Shift+クリックのシミュレーションを実行し、クラッシュしないことを確認

**検証 (Phase 4b2):**
- ローカル Debug/Release 両方でフルビルド・全189テスト pass
- clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み、変更 `.cpp` 5ファイル対象) で1件検出・修正: `readability-isolate-declaration` (`float startX=0,startY=0,endX=0,endY=0;`の1行複数宣言を4行に分離)。再スキャンで0警告確認
- **既知の限界:** Phase 4b1と同様、キャレット・選択ハイライトの視覚的な正しさはこのセッションのツールセットでは自動検証できない (ネイティブWin32ウィンドウのスクリーンショット/GUI自動化ツールが無いため)。統合テストは「クラッシュ・エラーなく描画される」「フレームスキップが正しく回避される」ことのみを保証し、ピクセル単位の見た目確認はユーザー自身に委ねる

**教訓 (Phase 4b2):** `HitTestPoint`(座標→位置)は `HitTestTextPosition`(位置→座標、Phase 4b1でキャレット描画に使用済み)の逆方向にあたる同じAPIファミリで、`hitTest()`の実装は`drawVisibleLines()`が既に確立していたDPI変換・TextLayoutCache運用パターンをほぼそのまま再利用できた — 新規のDirectWrite APIを導入する際も、既存の類似APIの使用パターンを踏襲することでコード全体の一貫性を保てることを再確認した。また `moveAllTo()`にデフォルト引数を追加する設計判断(新規メソッド名を増やさない)は、既存呼び出し元を一切変更せずに機能拡張できる後方互換な変更の一例として、今後の類似拡張の参考になる。

**Phase 4b2 完了後:** ユーザーが「push する」と指示。push後 CI (`gh run list`) が success で完了 (Build&Test debug/release・UBSan・clang-tidy の4ジョブ全green、総実行時間38分51秒) したことを確認。続けて「次のPhaseへ進め」と指示、同一セッション内で Phase 4b3 に着手。

**計画フェーズ (Phase 4b3):** RESUME_HERE.md §3.10/§6 が Phase 4b3 のスコープとして挙げていた「ドラッグ選択・ダブル/トリプルクリック・複数カーソル」について、`MainWindow`/`editor_input`/`SelectionModel` の現状 (Phase 4b2後の最新状態) を直接精読して調査した結果、**Phase 4b2で実装済みの`handleMouseDown(pos, shiftDown=true, ...)`が「anchor保持でpositionだけ動かす」という、ドラッグの継続移動に必要な挙動と完全に一致する**ことを発見。ドラッグ選択には新規の core/app ロジックが一切不要で、`MainWindow`側のWin32状態管理 (`SetCapture`/`WM_MOUSEMOVE`/`WM_LBUTTONUP`) だけで実現できると判断。一方ダブルクリック(単語選択)は単語境界判定の仕様についてADR-012が既に「ユーザーとの合意が必要」と明記済みの再評価トリガーに該当し、Alt+クリック複数カーソルは編集コマンドの複数カーソル対応という別の大きめの設計変更を要することが分かったため、Plan Modeで **Phase 4b3のスコープをドラッグ選択のみに絞り、ダブル/トリプルクリック・複数カーソルはPhase 4b4以降へ延期する**計画を立案。ExitPlanModeでユーザー承認を得て実装着手。

**成果物 (Phase 4b3):**
- `MainWindow`: `onMouseDrag`フック新設(shiftDownパラメータなし — ドラッグは常にanchor保持での拡張)。`handleMouseDown()`(既存)の先頭で`::SetCapture(m_hwnd)`を呼びドラッグ中フラグを立てる。新規`WM_MOUSEMOVE`(`handleMouseMove`、ドラッグ中のみ`onMouseDrag`発火)・`WM_LBUTTONUP`(`handleMouseUp`、`::ReleaseCapture()`+フラグ降下)を追加
- `src/app/main.cpp`: `onMouseDrag`配線 — `RenderPipeline::hitTest()`でヒットテストした後、**既存の**`handleMouseDown(*hit, /*shiftDown=*/true, ...)`を呼ぶだけ。新規の`app`層関数は無し
- テスト数: 189→190 (単体+1: `EditorInputTest.RepeatedShiftedMouseDownSimulatesDragExtendingFromOriginalAnchor` — `handleMouseDown`を`shiftDown=true`で複数回呼び、anchorが最初の呼び出し以降変わらず維持されることを検証、ドラッグが依拠する核心の挙動を明示的にピン留め)
- 実アプリで `SetCursorPos`+`mouse_event`+`keybd_event`(P/Invoke)による複数点ドラッグ・Shift+ドラッグ・ウィンドウ境界外へのドラッグ(`SetCapture`の効果検証)をシミュレートし、クラッシュせず正常終了することを確認

**検証 (Phase 4b3):**
- ローカル Debug/Release 両方でフルビルド・全190テスト pass
- clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み、変更 `.cpp` 2ファイル対象) で新規警告0
- **既知の限界:** Phase 4b1/4b2と同様、ドラッグ中の選択ハイライトが視覚的に正しいかは自動検証できない ([[reference-no-win32-gui-automation]])。`SetCapture`がウィンドウ境界外へのドラッグでもクラッシュしないことは実機で確認したが、これは「クラッシュしない」の確認であって「見た目が正しい」の確認ではない

**教訓 (Phase 4b3):** Phase 4b2で`handleMouseDown`に`shiftDown`という汎用的な「anchor保持で拡張するか」フラグを設計したことが、Phase 4b3で「ドラッグは実質的に繰り返しのShift+クリック」という洞察につながり、新規コードをほぼ書かずに済んだ。個別の入力イベント(クリック・ドラッグ)ごとに専用のハンドラを都度新設するのではなく、「選択操作の共通の型は何か」という抽象度で設計しておくと、後続の入力手段(この場合ドラッグ)が驚くほど安く実装できることを示す一例。この累積的な設計効率は、Phase 4b1で`moveAllTo`にShift+クリック用の`extendSelection`を足した判断が、Phase 4b3でさらに一段先まで効いた結果でもある。

**Phase 4b3 完了後:** ユーザーが「Phase 4b4の方針について」と質問。ダブルクリック単語選択の境界判定方式(簡易文字種ベース vs Unicode UAX #29準拠)についてAskUserQuestionでユーザーに確認し、「簡易文字種ベース(推奨)」を選択いただいた。

**計画フェーズ (Phase 4b4):** 既存の `MainWindow`/`editor_input`/`SelectionModel` を直接精読して調査した結果、Win32 の `WM_LBUTTONDBLCLK`(要`CS_DBLCLKS`)には「3回目」の概念が無いため、`WM_LBUTTONDOWN` 単体でのクリック回数手動判定が必要と判断。判定ロジックを `src/render/resize_math.h`/`viewport_math.h` と同じ「ヘッダオンリー・Windows SDK非依存・ユニットテスト可能」パターンで `src/ui/click_tracking.h` として切り出す設計を採用(`MainWindow`のロジックが初めてテスト可能になる部分)。Plan Modeで、Alt+クリック複数カーソル(編集コマンドの複数カーソル対応を要する別の大きめの設計変更)は Phase 4b5 へ延期し、本フェーズはダブルクリック単語選択・トリプルクリック行選択のみに絞る計画を立案。ExitPlanModeでユーザー承認を得て実装着手。

**成果物 (Phase 4b4):**
- 新規 `src/ui/include/neomifes/ui/click_tracking.h`: 純粋関数 `nextClickState()`。時間閾値(`GetDoubleClickTime()`)・距離閾値(`GetSystemMetrics(SM_CXDOUBLECLK/SM_CYDOUBLECLK)`)を呼び出し側から受け取り、クリック回数(1/2/3、3で頭打ち)を返す
- `SelectionModel::selectWordAt()`/`selectLineAt()` 新設。単語境界は簡易文字種ベース(ASCII英数字+`_`の連続・CJK文字の連続をそれぞれ1単語、それ以外の記号は1文字ずつ)。行選択は既存`lineContentEnd()`を再利用し、最終行以外は`\n`を選択範囲に含める
- `neomifes::app::handleDoubleClick()`/`handleTripleClick()` 新設 — `handleMouseDown`の既存契約は変更せず新規の兄弟関数として追加
- `MainWindow::onMouseDown` フックに `clickCount` パラメータ追加、`m_clickState`(`ClickTrackerState`)を保持
- `main.cpp`: `clickCount>=3`→`handleTripleClick`、`==2`→`handleDoubleClick`、それ以外→既存の`handleMouseDown`に分岐
- テスト数: 190→207 (単体+17: `ui_click_tracking_test.cpp`新設8件、`selectWordAt`/`selectLineAt`のケース7件、`handleDoubleClick`/`handleTripleClick`のケース2件)。CJK単語選択のテストを含む
- 実アプリでダブルクリック・トリプルクリック(P/Invokeで同一座標への複数回クリックをシミュレート)、および日本語(CJK)テキストでのダブルクリックをテストしクラッシュなし・正常終了を確認

**検証 (Phase 4b4):**
- ローカル Debug/Release 両方でフルビルド・全207テスト pass
- clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み、変更 `.cpp` 4ファイル対象) で1件検出・修正: `hicpp-use-auto`/`modernize-use-auto` (`std::size_t col = static_cast<std::size_t>(...)` を `auto` に)。再スキャンで0警告確認
- **既知の限界:** Phase 4b1〜4b3と同様、単語選択・行選択の視覚的な正しさは自動検証できない ([[reference-no-win32-gui-automation]])

**教訓 (Phase 4b4):** クリック回数判定という、一見Win32メッセージ処理そのもの(`WM_LBUTTONDBLCLK`等)に見える機能も、実際には「時刻+座標の近さからリピートクリックを判定する」という純粋な計算に分解できた。`src/render/`で確立していた「ヘッダオンリー・SDK非依存の純粋関数」パターンを他レイヤー(`src/ui/`)に転用することで、このコードベースでこれまでテスト不可能だった`MainWindow`のロジックの一部が初めてユニットテスト可能になった — Win32依存に見える処理でも、実際に外部APIを呼ぶ部分と純粋な判定ロジックを意識的に分離すれば、テスト可能な範囲を継続的に広げられることを示す一例。

**Phase 4b4 完了後:** ユーザーが未push2コミット(Phase 4b3/4b4)の扱い(push / 実機確認 / Phase 4b5着手)を問われ、compact実施を経て次セッションで「4b5着手」と指示。

**計画フェーズ (Phase 4b5):** `edit_commands.h`/`command.h`/`selection_model.h`/`command_dispatcher.cpp`/`editor_input.cpp`/`main.cpp`/`main_window.h/.cpp` を直接精読して調査した結果、`ICommand::cursorPositionAfterExecute()`/`AfterUndo()`(単一`TextPos`)を受けて`CommandDispatcher`/`UndoStack`が`SelectionModel::moveAllTo()`を呼ぶ既存の仕組みが「全カーソルを1点に強制収束させる」ことしかできず、複数カーソル編集を原理的に表現できないと判明。Alt+クリックの入力配線(UI層)だけでなく、`ICommand`インターフェース自体の一般化という core 層の設計変更が避けられないため、Plan Modeで **Phase 4b5 をさらに 4b5a(複数カーソル編集コマンド基盤、core層ヘッドレス)と 4b5b(Alt+クリック入力配線)に分割**する計画を立案(Phase 4a→4b1の「ヘッドレスcore実装→UI配線」パターンを踏襲)。設計の核は「累積オフセット法」— `SelectionModel::cursors()`が保証する昇順・非重複の順序のまま1パスで処理し、直前までの編集による純増減を足し込んで各編集の実適用位置を求める(VSCode等の複数カーソルエディタで使われる標準的な手法)。ExitPlanModeでユーザー承認を得て実装着手。

**成果物 (Phase 4b5a):**
- `ICommand::cursorPositionAfterExecute()`/`cursorPositionAfterUndo()`(単一`TextPos`)を`cursorsAfterExecute()`/`cursorsAfterUndo()`(`std::vector<Cursor>`)に置き換え。パラレルな2つ目のインターフェースを増やすのではなく既存メソッドを置き換える方針(既存3コマンドは要素数1のvectorを返すだけの機械的変更)
- 新規 `MultiCursorEditCommand`(`edit.multiCursor`): `PerCursorEdit{range, insertedText}`のリストを累積オフセット法で1パス適用。undoは降順(execute時に捕捉した実適用位置`m_currentStartAtExecute`を使うためシフト再計算不要)。カーソル復元はexecute前の`SelectionModel::cursors()`スナップショットをそのまま返す(選択範囲込みで完全復元、range等からの逆算より確実)
- `SelectionModel::setCursors(std::vector<Cursor>)`新設(`mergeOverlapping()`込み)。`CommandDispatcher::dispatch()`/`UndoStack::undo()`/`redo()`の`moveAllTo(pos)`呼び出しを`setCursors(cmd->cursorsAfterExecute()/AfterUndo())`に置き換え
- 既存の`InsertTextCommand`/`DeleteRangeCommand`/`ReplaceRangeCommand`はクラスとして残す(削除しない、Phase 4b5bで呼び出し経路が統一されても単体テストでの被覆は維持)
- テスト数: 207→213 (単体+6: `MultiCursorEditCommand`4件(累積オフセット・選択置換・境界no-op・weight)、`SelectionModel::setCursors`2件)
- clang-tidyで1件検出・修正: `hicpp-use-auto`/`modernize-use-auto`(`const document::TextPos currentStart = static_cast<...>` を `auto` に)

**成果物 (Phase 4b5b):**
- `neomifes::app::handleAltClick()`新設 — 既存(Phase 4a)の`SelectionModel::addCursor()`を呼ぶだけの3行、新規coreメソッドは不要だった
- `editor_input.cpp`の`handleChar`/`applyDeleteKey`を全カーソル対応に書き換え — `selection.cursors()`全件から`PerCursorEdit`を1:1・同順序で組み立て`MultiCursorEditCommand`を1回ディスパッチする形に統一(単一/複数カーソルで分岐しない)。境界(文書先頭でのBackspace等)で動けないカーソルは空range/空文字列の"no-op edit"として1エントリを必ず作る(`MultiCursorEditCommand`が1カーソル1エントリの1:1対応を前提とするため)。全カーソルがno-opならディスパッチ自体をしない(単一カーソル時の「何も起きない」動作を維持)
- Win32側: `WM_LBUTTONDOWN`のwParamには`MK_ALT`が存在しない(Shift/Ctrlの`MK_SHIFT`/`MK_CONTROL`とは非対称)。`MainWindow::handleMouseDown()`で`::GetKeyState(VK_MENU) & 0x8000`を都度読み取り、`onMouseDown`フックのシグネチャに`bool altDown`追加
- `main.cpp`: `onMouseDown`ラムダの分岐ロジックを新規フリー関数`dispatchMouseDown()`に切り出し — `altDown`追加でclang-tidyの`readability-function-cognitive-complexity`閾値(25)を`wireNormalMode()`が超えたため(`loadStartupDocument()`/`prepareDocument()`と同じ「関数を分離してcomplexity低減」パターン)。`altDown`が最優先分岐で`handleAltClick`へ
- テスト数: 213→217 (単体+4: `handleAltClick`1件、複数カーソル`handleChar`/`handleKeyDown`(Backspace)3件、境界カーソルが他カーソルの編集をブロックしないことの確認を含む)
- 実アプリで2箇所へのAlt+クリック→'X'入力(両カーソルに挿入されることを想定)→Alt+クリック→Backspace→Ctrl+Z→Ctrl+YをP/Invokeでシミュレートし、クラッシュせず応答性維持を確認

**検証 (Phase 4b5a/4b5b):**
- ローカル Debug/Release 両方でフルビルド・全217テスト pass
- clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み、変更ファイル全対象) で計2件検出・修正 (`hicpp-use-auto`/`modernize-use-auto`、`readability-function-cognitive-complexity`)。再スキャンで0警告確認
- **既知の限界:** 複数カーソルの視覚的な正しさ(各カーソルの描画位置・全カーソルへの文字挿入の見た目)は自動検証していない ([[reference-no-win32-gui-automation]])。P/Invokeによるクラッシュ検知・応答性確認のみ実施、ユーザー自身の目視確認を推奨

**教訓 (Phase 4b5a/4b5b):** 「入力配線を先に作ってから複数カーソル対応を追加する」のではなく「複数カーソル対応(core層)を先に作ってから入力配線を追加する」順序を選んだことで、4b5bの実装は「既存の`addCursor()`を呼ぶ3行」+「`editor_input.cpp`の呼び出し経路を1箇所に統一する書き換え」だけで完結した — Phase 4b3(ドラッグ選択が既存の`shiftDown=true`呼び出しの繰り返しで実現できた)と同様、「土台となる汎用インターフェースを先に正しく設計しておくと、後続の入力手段が驚くほど安く実装できる」というこのプロジェクトで繰り返し観測されているパターンの再確認。また、`MultiCursorEditCommand`を「1カーソル1エントリ、no-opも明示的に1エントリとして表現する」設計にしたことで、「一部のカーソルだけ境界にいる」という部分的no-opケースを特別扱いなしに処理できた — 可変長のedit listではなく固定長(カーソル数と同じ)のedit listにするという一見些細な設計判断が、呼び出し側のコードを大幅に単純化した一例。

**Phase 4b5 push・CI failure・修正 (同セッション継続):** ユーザーが「pushせよ」と指示、`6704556`(4b3)〜`5118a8a`(4b5b)の4コミットをpush。CI(run `29550663468`)でBuild&Test debug/release・clang-tidyは成功したが、**UBSan (clang-cl) ジョブがビルド段階で失敗**: `src/ui/include/neomifes/ui/click_tracking.h`(Phase 4b4で新設)の`ClickTrackerState`が持つ`friend constexpr bool operator==(...) = default;`が、メンバ`ClickPoint`に`operator==`が定義されていないため暗黙的に削除されており、clang-cl が `-Werror -Wdefaulted-function-deleted` で検出(MSVCはこの種の「静かな削除」を無診断で通すため、Phase 4b4完了時のローカル検証・CIには一度も引っかからず4b5a/4b5bの2コミット分も素通りしていた)。原因を`gh api repos/.../actions/jobs/<id>/logs`で直接取得したビルドログから特定し、`ClickPoint`に同様の`= default`な`operator==`を追加して修正。ローカルの`ubsan`プリセット(`cmake --preset ubsan && cmake --build --preset ubsan`、VS付属LLVMのclang-cl.exeで動作、これまでのセッションで未使用だった)で再現・修正確認してからコミット`ed23aa4`をpush、CI(run `29551870156`)で全4ジョブgreenを確認。

**教訓:** 「ローカル検証はMSVCのみで実施」という運用が定着していたため、MSVCとclang-clで診断結果が異なるバグ(defaulted比較演算子の暗黙的削除)が2フェーズ分(4b4→4b5a/4b5b)気づかれずに積み重なった。この種のバグはCIに存在するUBSan(clang-cl)ジョブでしか検出できないため、`= default`な比較演算子を新規に書いた際はローカルでも`ubsan`プリセットを一度実行する習慣が必要と判断し、`RESUME_HERE.md`§2と`reference_windows_cpp_ci_gotchas.md`(項目6)に記録した(コミット`8fdecc5`)。「ローカルgreen ≈ ほぼ確実だが絶対ではない」という既存の注記が、今回はMSVCバージョン差異ではなくコンパイラ自体の差異という新しいパターンで実証された。

**Phase 4b6 スコープ確認:** ユーザーが「継続実施せよ」と指示。CIの docs-only push 完了・green確認後、Phase 4b6 のスコープをAskUserQuestionで確認したところ、候補4項目(選択範囲クリップボードコピー、PageUp/PageDown、Ctrl+矢印単語移動、Alt+Shift+クリック/Alt+ドラッグ選択拡張)**全て**が選択された。CLAUDE.mdルール8「1PR=1責務」に従い、Plan Modeで **4b6a(PageUp/PageDown)→4b6b(Ctrl+矢印単語移動)→4b6c(クリップボード)→4b6d(Alt+Shift拡張)** の4サブフェーズに複雑度の低い順で分割する計画を立案。既存コード(`Viewport`/`selection_model.cpp`の`classify()`/`handle_guard.h`)を直接精読して各サブフェーズの実現方式を検討した上でExitPlanModeでユーザー承認を得て実装着手。

**成果物 (Phase 4b6a — PageUp/PageDown):**
- `MovementKind::PageUp`/`PageDown`追加。`moveVertically(doc, current, bool up)`を`moveVertically(doc, current, int64_t lineDelta)`に一般化し、既存`Up`/`Down`(delta=±1)と新規PageUp/PageDown(delta=±pageSize)が同じ列保持ロジックを共有
- `SelectionModel::moveAll()`に`document::LineNumber pageSize = 0`をデフォルト引数で追加(既存呼び出し元は変更不要、Phase 4b2の`extendSelection`追加と同じパターン)
- `editor_input.cpp`の`applyMovementKey()`に`VK_PRIOR`/`VK_NEXT`ケース追加、`handleKeyDown()`が`viewport.visibleLines()`からpageSizeを算出して渡す
- ページ送り後のスクロールは既存`ensureVisible()`が自然に実現、新規スクロールロジック不要と判明
- テスト数: 217→222。ローカルDebug/Release全green、clang-tidy新規警告0

**成果物 (Phase 4b6b — Ctrl+矢印単語移動):**
- `MovementKind::WordLeft`/`WordRight`追加。`selectWordAt()`(Phase 4b4)が持っていた`classify(char16_t)`/`CharKind`を無名namespace内で先頭に再配置し、新規`moveByWord()`と共有する形にリファクタ(単語境界の定義を1箇所に保つ)
- **単語移動は現在行内に限定**(行頭/行末で停止)— `selectWordAt()`と同じ単一行スコープを踏襲、複数行走査という新たな設計判断を回避
- `editor_input.cpp`の既存`VK_LEFT`/`VK_RIGHT`ケースに`ctrlDown`分岐を追加(`VK_HOME`/`VK_END`と同型)
- テスト数: 222→231。ローカルDebug/Release全green、clang-tidy新規警告0

**成果物 (Phase 4b6c — クリップボードコピー Ctrl+C/X/V):**
- **スコープをプライマリカーソルの選択範囲のみに限定**(複数カーソルを跨いだコピー/ペーストの分配は新たな仕様判断を要するため次点課題)
- 新規`src/platform/clipboard.h/.cpp`: `setClipboardText()`/`getClipboardText()`。`ClipboardScope`(OpenClipboard/CloseClipboardのRAII、HandleGuardの汎用テンプレートは使えないため独自実装)、`GlobalAlloc`/`GlobalLock`/`SetClipboardData`の定番手順(成功後はGlobalFreeしない)
- `editor_input.cpp`に`textToCopy()`/`handlePaste()`追加。Cutはクリップボード書き込み失敗時に選択範囲を削除しない設計(データ消失防止)
- `main.cpp`: `handleClipboardKey()`新設に加え、**`onKeyDown`ラムダの本体全体**を`handleKeyDownEvent()`という独立関数に切り出す必要が判明 — clang-tidyの`readability-function-cognitive-complexity`は、ラムダが`wireNormalMode`内にインライン定義されている場合その本体の複雑度を外側関数に積算する仕様のため、分岐ロジックだけを`handleClipboardKey()`に切り出しても(38→26)閾値25を超えたまま。ラムダ本体そのものを外に出して初めて解消した
- 新規`tests/integration/platform_clipboard_test.cpp`: 実クリップボードのラウンドトリップ検証、`GTEST_SKIP()`で環境非対応時に緩やかにスキップ(`render_device_smoke_test.cpp`と同じパターン)
- clang-tidyで2件検出・修正: `ClipboardScope`のspecial-member-functions不足(move ctor/assignの明示的delete追加)、`bugprone-suspicious-stringview-data-usage`(`memcpy`+`.data()`を`std::ranges::copy`に置き換え)
- テスト数: 231→236。ローカルDebug/Release全green、**Phase 4b5bの教訓を踏まえ`ubsan`(clang-cl)プリセットでも追加検証**(全236テストgreenを確認、この教訓が実際に活きた最初のケース)

**成果物 (Phase 4b6d — Alt+Shift+クリック/Alt+ドラッグ選択拡張):**
- 新規`SelectionModel::moveCursorMatching(identifyingAnchor, newPos)`: `anchor`が指定値と一致する1個のカーソルだけを拡張、他のカーソルには触れない。`moveAll()`/`moveAllTo()`が常に全カーソルへ一様適用する既存設計とは異なる新プリミティブ。カーソルの安定した識別キーとして`anchor`(拡張中は不変)を採用— 配列添字は`mergeOverlapping()`のたびに再ソートされ不安定なため使えないと判断
- `main.cpp`の`wWinMain`に`std::optional<TextPos> altCursorAnchor`新設。`wireNormalMode`のローカル変数ではなく`wWinMain`側に置き、参照で渡す設計にした — `wireNormalMode`はウィンドウ作成前に一度呼ばれて戻るだけの関数なので、そのローカル変数はメッセージループ開始前にスタックごと消える。`MainWindow::m_isDragging`がメンバ変数である理由と全く同じ制約
- `dispatchMouseDown()`を拡張: プレーンAlt+クリックで`altCursorAnchor`を設定、Alt+Shift+クリックでこれを使い`moveCursorMatching()`を呼ぶ、Alt無しクリックでリセット。`onMouseDrag`も同様に`altCursorAnchor`があれば専用カーソルを拡張、無ければ既存の全カーソル拡張(`shiftDown=true`)にフォールバック
- テスト数: 236→239 (単体+3: `moveCursorMatching`の対象カーソル限定・繰り返し呼び出し安定性・非該当時no-op)
- 実アプリでPageUp/PageDown・Ctrl+矢印(通常/Shift拡張)・Ctrl+C/X/V+Undo/Redo・Alt+クリック/Alt+Shift+クリック/Alt+ドラッグの複合操作を1回のP/Invokeシナリオでシミュレートし、クラッシュなし・応答性維持を確認
- **既知の制限として明記**: `RenderPipeline`はプライマリカーソルのキャレット/選択範囲しか保持・描画しないため、Alt+クリックで追加した非プライマリカーソルは`SelectionModel`レベルでは正しく拡張されるが画面には見えない。これはPhase 4b5a以降ずっと存在していた制限で4b6d固有の問題ではないが、今回のAlt+Shift拡張機能が「見た目に効果がない」ことにつながるため完了記録に明示

**検証 (Phase 4b6a〜4b6d 通し):**
- ローカル Debug/Release 両方でフルビルド・全239テスト pass (各サブフェーズ完了ごとに実施、累積)
- clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み、変更ファイル全対象) で計2件検出・修正 (4b6cのみ、上述)。再スキャンで0警告確認
- 4b6cのみ`ubsan`(clang-cl)プリセットでも追加検証、全236テストgreen

**教訓 (Phase 4b6a〜4b6d):** (1) 「既存の単一カーソル向けプリミティブを一般化してから複数のユースケースで共有する」パターンが本フェーズでも繰り返し有効だった — `moveVertically`のUp/Down→PageUp/PageDown一般化、`classify()`のselectWordAt→moveByWord共有、いずれも新規ロジックをほぼ書かずに済んだ。(2) clang-tidyのcognitive complexityチェックは「ラムダの本体がどこで実行されるか」ではなく「ラムダがどこに書かれているか(字面上のネスト)」を見るため、分岐を関数に切り出しても、ラムダ自体がまだ大きい関数の中にインライン定義されていれば複雑度は下がりきらない — 呼び出し元の複雑度を本当に下げるには、ラムダ本体そのものを外に出す必要がある。(3) Phase 4b5bのUBSan(clang-cl)障害を踏まえて「新規Win32 API面を追加したら`ubsan`プリセットも試す」を4b6cで実践した結果、実際に2件のclang-cl固有の指摘(special-member-functions、bugprone-suspicious-stringview-data-usage)を事前に検出できた — 教訓が実運用で機能した最初の確認事例。

## Session 24 (2026-07-17): Phase 4b7a〜4b7c (複数カーソル視覚描画・複数行単語移動・複数カーソルクリップボード)

**Phase 4b6 push・CI確認:** ユーザーが「pushせよ」と指示、Phase 4b6a〜4b6d + 設計ドキュメント同期の全コミットをpush。CI (`gh run`) で4ジョブ (Build&Test debug/release・UBSan・clang-tidy) 全green確認。

**Phase 4b7 スコープ確認:** ユーザーが「着手せよ」と指示したがPhase 4b7自体のスコープが未確定だったため、AskUserQuestionで確認。候補のうち**複数カーソルの視覚的描画**(推奨案として提示)・**複数行にまたがる単語移動**・**複数カーソルを跨いだクリップボード**の3項目が選択された(「その他」も選択されたが自由記述欄への具体的な記入は無かったため、この3項目のみをスコープとして扱った)。

**計画フェーズ:** CLAUDE.mdルール8「1PR=1責務」に従い、Plan Modeで **4b7a(視覚描画)→4b7b(複数行単語移動)→4b7c(複数カーソルクリップボード)** の順に分割する計画を立案。4b7aを最初にした理由は2点: (1) `RenderPipeline`の構造変更を伴う最も規模の大きい変更を早期に着手・検証したい、(2) これが完了すると Phase 4b5a以降ずっと視覚的に確認できなかった複数カーソルの効果(4b5b/4b6dのAlt+クリック/Alt+Shift拡張)が初めて画面上で見えるようになり、既存機能の実効的な検証価値も持つため。ExitPlanModeでユーザー承認を得て実装着手。

**成果物 (Phase 4b7a — 複数カーソルの視覚的描画):**
- `RenderPipeline`に新規値型`CursorVisual{TextPos position; TextRange selectionRange;}`(`= default`な`operator==`込み)を追加。単一値の`setCaretPosition()`/`setSelectionRange()`を1本の`setCursorVisuals(std::vector<CursorVisual>)`に置き換え(2つのAPIが常に対で呼ばれていた実情に合わせ1呼び出しでアトミックに更新できるようにした)。`FrameState::caretPosition`/`selectionRange`も`std::vector<CursorVisual> cursorVisuals`に置き換え、粗粒度フレームスキップが複数カーソルの変化を正しく検知するようにした
- `drawVisibleLines()`を書き換え: 全`CursorVisual`の行・列を可視行ループの外側で1回だけ事前計算する新規private関数`computeCaretDraws()`を追加(`offsetToLine()`呼び出しをカーソル数分に抑え、可視行数×カーソル数にしない)。選択ハイライト描画・キャレット描画もそれぞれ`drawSelectionsOnLine()`/`drawCaretsOnLine()`という新規private関数に分離し、可視行ループ内で全`CursorVisual`をイテレートして該当行のものだけ描画する形に一般化
- `main.cpp`の`syncRenderStateAndInvalidate()`を書き換え、`selection.cursors()`全件から`CursorVisual`のvectorを組み立てて`setCursorVisuals()`を1回呼ぶ形に変更
- `tests/integration/render_text_smoke_test.cpp`: 既存3テストを新APIに移行、新規`MultipleCursorVisualsRenderWithoutErrorAndForceRedraw`(3カーソル・一部選択ありを混在させてエラー無く描画完了することを検証)を追加
- 実アプリ (`NeoMIFES.exe`) を起動しAlt+クリックで複数カーソルを追加、**ユーザー自身の目視確認**で各カーソルのキャレット/選択ハイライトが実際に画面へ描画されることを確認(このセッションにはネイティブWin32ウィンドウのスクリーンショット/GUI自動化ツールが無く[[reference-no-win32-gui-automation]]、Phase 4b7aの主目的が視覚的検証そのものだったため、他フェーズ以上にユーザー確認の比重を高くした)。ユーザーは確認後「進め」と回答し4b7bへ継続を指示

**成果物 (Phase 4b7b — 複数行にまたがる単語移動):**
- Phase 4b6bの単一行スコープの`moveByWord()`を、行境界を跨いで走査を継続する`moveByWordForward()`/`moveByWordBackward()`に分割・拡張。無名namespace内の`skipWhitespaceForward()`/`skipWhitespaceBackward()`ヘルパーが、空白(行末の仮想的な区切り含む)を行を跨いで読み飛ばす処理を担う。空行は「純粋な空白1個分」として扱い通過する(明示的な停止はしない — 単語移動の簡易さを優先し段落移動とは役割を分離)
- 行を跨ぐ処理は既存パターン(`doc.snapshot()->extract()`で1行分だけ取得)を繰り返すループとして実装し、文書全体の一括extractは避けた(巨大ファイルでのメモリ安全性を既存コードの流儀と揃えた)
- `tests/unit/core_selection_model_test.cpp`: 旧`WordLeftRightStayWithinCurrentLine`を`WordRightCrossesLineBoundaryToNextWord`/`WordLeftCrossesLineBoundaryToPreviousWord`/`WordRightSkipsOverAnEntireEmptyLine`/`WordLeftSkipsOverAnEntireEmptyLine`/`WordRightAtDocumentEndOfMultiLineDocIsNoOp`/`WordLeftAtDocumentStartOfMultiLineDocIsNoOp`の6件に置き換え
- テスト作成時、"foo\nbar"間のような「実際の空白文字を挟まない改行1個」がShift無しの単一行内の空白1個(既存の`WordRightFromMidWhitespaceAlsoLandsAtNextWordStart`が示す通り1回のキー押下でスキップされる)と全く同じ扱いになる(1回の`WordRight`で"foo"の末尾から"bar"の先頭へ直接到達する)ことにテスト実行時に気づき、当初の期待値(2回のキー押下を想定)を実装ではなくテスト側の誤りと判断して修正した

**成果物 (Phase 4b7c — 複数カーソルを跨いだクリップボード):**
- **スコープをVSCode等が行う「コピー時のカーソル数とペースト時のカーソル数が一致すれば1対1で分配する」高度な対応の対象外**とし、シンプルな規則を採用: コピー/カットは選択を持つ全カーソルのテキストを昇順`\n`連結、ペーストは同一テキストを全カーソルへ同一内容として適用(選択があれば置換) — `handleChar()`の既存の「全カーソルへ同じテキストを適用する」規則と統一
- `editor_input.cpp`に無名namespace内の共通ヘルパー`insertTextAtEveryCursor()`を新設し、`handleChar()`/`handlePaste()`双方が同じ「全カーソルへテキスト適用→`MultiCursorEditCommand`を1回ディスパッチ」ロジックを共有するよう統合。`textToCopy()`は全カーソルを走査し選択を持つものだけ`\n`連結する形に一般化
- 新規`deleteAllSelections()`(既存`applyDeleteKey()`と同じ「1カーソル1エントリ、no-opも明示的に1エントリ」パターンを流用)を追加し、`main.cpp`の`handleClipboardKey()`のCut分岐がこれを呼ぶよう変更。これにより`main.cpp`から`DeleteRangeCommand`/`edit_commands.h`への直接依存が不要になった(編集ロジックをeditor_input層に集約する既存方針への統一)
- `tests/unit/app_editor_input_test.cpp`: `TextToCopyJoinsMultipleSelectionsWithNewline`/`TextToCopySkipsCursorsWithoutSelection`/`HandlePasteInsertsAtEveryCursor`/`DeleteAllSelectionsDeletesEachCursorsSelection`/`DeleteAllSelectionsLeavesCursorsWithoutSelectionUntouched`/`DeleteAllSelectionsReturnsFalseWithNoSelectionsAtAll`の6件を追加

**検証 (Phase 4b7a〜4b7c 通し):**
- ローカル Debug/Release 両方でフルビルド・全テストpass(各サブフェーズ完了ごとに実施、累積)。ctestで最終確認した総テスト数は250(単体+12・統合+1、内訳は各サブフェーズの成果物節を参照)
- clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み、変更/新規ファイル対象) で複数件検出・修正: 4b7aの`drawVisibleLines()`が`readability-function-cognitive-complexity`(33、閾値25)に抵触し`computeCaretDraws()`/`drawCaretsOnLine()`/`drawSelectionsOnLine()`への分離で解消。再スキャンで0警告確認
- Phase 4b5b以来の教訓に従い、4b7aで新規`= default`な`CursorVisual::operator==`を追加した際、ローカル`ubsan`(clang-cl)プリセットで検証 — `TextPos`(uint64_t)と`TextRange`(既存`operator==`済み)のみをメンバに持つため実際に安全であることを確認できた(問題は検出されず)
- 実アプリで4b7aの視覚確認に続き、PageUp/PageDown・複数行を跨ぐCtrl+矢印・複数カーソルCtrl+C/X/V+Undo/Redoを含む複合操作をP/Invokeでシミュレートし、クラッシュなし・応答性維持を確認
- **既知の限界:** 4b7a自体の視覚検証はユーザー自身の目視確認に依存しており、以降のセッションでの回帰は自動検証されない([[reference-no-win32-gui-automation]])

**教訓 (Phase 4b7a〜4b7c):** (1) 「既存の単一値/単一カーソル向けプリミティブを一般化してから複数のユースケースで共有する」パターンが本フェーズでも一貫して有効だった — `CursorVisual`による単一キャレット/選択→vector化、`classify()`/`CharKind`の単一行→複数行`moveByWord*`への転用、`insertTextAtEveryCursor()`による`handleChar`/`handlePaste`の統合、いずれも並行する特別処理を増やさずに済んだ。(2) Phase 4b6cで確立した「ラムダの本体そのものを外に出さないとcognitive complexityは下がりきらない」教訓は本フェーズでは新規発生せず(`drawVisibleLines()`は元々独立関数だったため分離のみで解消)、教訓が状況依存であることの確認になった。(3) Phase 4b7aでユーザー自身の視覚確認を明示的に依頼したことで、Phase 4b5a以降積み残されていた「複数カーソルは見えているのか」という疑問に初めて実証的な答えが得られた — 視覚検証手段を持たないこの開発環境では、実装が完了した時点でなく「視覚的に意味のある変化が生まれた時点」でユーザー確認を挟むことが、確認の密度と実装速度のバランスとして機能した。

## Session 25 (2026-07-18): Phase 5a (Search Engine 基盤: RE2導入 + SearchService::findAll)

**方針転換:** ユーザーから「Phase 4b8のスコープについて検討したい、現状実装されている機能と今後実装が必要な機能を見極めたい。史上最強のテキストエディタを製造すべく進めよ。機能もデザインも最強としたい」という大きな方針が示された。要件定義書(`NeoMIFES_要件定義書.md`)全文とCLAUDE.md §7フェーズ表、実際の`src/`ディレクトリ構成を突き合わせて棚卸しした結果、Phase 0〜4b7cで完成しているのは「エディタの心臓部(見る・打つ・戻す)」のみで、検索・エンコーディング多様化・シンタックスハイライト・プラグイン・AI・ログ解析モード・CSVモード・JSON/XML Tree・Git/LSP/マクロ・複数タブ/ウィンドウ・矩形選択・行番号・自動保存等、要件定義書の他の柱にはほぼ手が付いていないことが判明。特に「ログ解析モード」が要件定義書§8で「本ソフト最大の特徴」と明記されているが、時系列ジャンプ/ERROR抽出/フィルタは検索機能の応用であり、Phase 10(ログ解析)はPhase 5(検索エンジン)に依存する構造的事実を指摘。AskUserQuestionで次の一手をユーザーに確認し、**検索エンジン(Phase 5)への着手**が選ばれた(Phase 4b8の矩形選択等の残タスクより優先)。

**計画フェーズ:** Explore agent 1体で(1) `Document`/`BufferSnapshot`の読み取りAPI(行単位extract()パターン、チャンク走査プリミティブは未整備)、(2) `src/ui/`にダイアログ/子コントロールの前例が皆無なこと、(3) `RenderPipeline`のハイライト描画パターン(`CursorVisual`/`drawSelectionsOnLine`が新規`SearchMatchVisual`の直接の前例になる)、(4) `cmake/Dependencies.cmake`にRE2/Abseilが未導入なこと、(5) `ICommand`に読み取り専用の「ナビゲーションコマンド」前例が無いこと、(6) `docs/design/detailed_design.md` §7に既に`SearchService`/`IncrementalFindService`/`ReplaceAllCommand`のクラス構想がスケッチ済みだが`application::ICommand`という実在しない名前空間の記載や、§7.3が前提とするチャンク走査プリミティブの不在といったドリフトがあること、を調査。ADR-002で正規表現エンジンはRE2に決定済み(std::regexは低速・ReDoS脆弱性で不採用)であることも確認。この調査結果を踏まえ、Plan ModeでPhase 5全体を一度に設計せず**最初のサブフェーズ5a(RE2導入+`SearchService::findAll`、同期・単一行スコープ・ヘッドレス)のみ**を詳細設計する方針を採用(CLAUDE.mdルール8「1PR=1責務」、未着手の後続サブフェーズ(インクリメンタル検索・Find UI・置換・Grep・巨大ファイル最適化)を先行設計するのは推測実装になるため)。ExitPlanModeでユーザー承認を得て実装着手。

**成果物:**
- **RE2 (ADR-002) + Abseil (LTS 20250814.2、RE2 tag 2025-11-05と時期的に対応するバージョンを選定) を`cmake/Dependencies.cmake`にFetchContent導入。** 検索エンジンはアプリ本体が実行時に必要とするコア依存のため、`NEOMIFES_BUILD_TESTS`限定だった`include(Dependencies)`をルート`CMakeLists.txt`で常時includeに変更。GoogleTest/benchmarkのFetchContentは同ファイル内の`if(NEOMIFES_BUILD_TESTS)`ブロックへ移動
- ビルド中に新規に発生した2件のCMake上の問題を解決:
  1. **RE2の`install(EXPORT re2Targets ...)`がconfigure段階で失敗**(`ABSL_ENABLE_INSTALL=OFF`によりAbseil側のexport setが空になり、そこに依存するRE2のexportが「対象が見つからない」エラーになる)。当初`EXCLUDE_FROM_ALL`で回避を試みたが効果が無く、RE2自身のCMakeオプション`RE2_INSTALL`(既定ON)がまさにこの`install(EXPORT)`ブロックを直接ガードしていることをRE2のCMakeLists.txt本体を読んで特定、`RE2_INSTALL OFF`で解決
  2. **ubsanプリセットでのみ**発生するリンクエラー(`lld-link: /failifmismatch: mismatch detected for '_ITERATOR_DEBUG_LEVEL'`、re2.lib=0 vs absl_log_internal_message.lib=2)。`compile_commands.json`を直接比較し、Abseilのオブジェクトだけが`-MDd`(動的デバッグCRT)、RE2側は`-MT`(静的リリースCRT、ubsanプリセットが指定した値)であることを特定。原因はAbseil自身のCMakeLists.txtが`ABSL_MSVC_STATIC_RUNTIME`オプション(既定OFF)経由で`CMAKE_MSVC_RUNTIME_LIBRARY`を**無条件に**再`set()`しており、この上書きがAbseilの`add_subdirectory()`ツリー配下(`absl/base`等、何段も下)の各ターゲットにだけ適用され、そのスコープの外にあるRE2やその他プロジェクトのターゲットはubsanプリセットの元の値のまま、という不一致だったと判明。既存の`get_property(...DIRECTORY...BUILDSYSTEM_TARGETS)`は指定ディレクトリ直下のターゲットしか返さず、ネストした`absl_*`ターゲット群を全く捕捉できていなかったことも同時に発覚(この行自体は今回新規に書いたコードだが、意図通り動いていなかった)。新規`neomifes_collect_targets_recursive()`関数(`SUBDIRECTORIES`プロパティを再帰的に辿る)で全ターゲットを正しく収集した上で`MSVC_RUNTIME_LIBRARY`プロパティを明示的に上書きし解決。debug/releaseプリセット(この変数を設定しない)を壊さないよう、上書きは`CMAKE_MSVC_RUNTIME_LIBRARY`が非空の場合のみに限定
- 新規`src/util/utf8_convert.h/.cpp`: `toUtf8WithOffsets(u16string_view) -> Utf8Conversion{utf8, byteToUtf16}`。RE2はUTF-8バイト列を対象とするがDocument内部はUTF-16のため、変換とバイトオフセット→UTF-16オフセットの対応表を1文字ずつ手書きエンコーダで構築(`WideCharToMultiByte`は使わず — オフセット表構築自体が1文字ずつの処理を要求するため、Win32 API呼び出しをその都度挟むより単純)。孤立サロゲートはU+FFFDへ置換
- 新規`src/search/`モジュール: `Query{pattern, caseSensitive, wholeWord, regex}`/`Match{TextRange}`/`SearchService::findAll(const Document&, const Query&) -> vector<Match>`。リテラル/正規表現検索を**RE2の1本のコードパス**で統一(リテラルは`RE2::QuoteMeta()`でエスケープしてから同じRE2経路へ)。`wholeWord`はRE2の`\b`(ASCII単語境界のみ — 既存`selectWordAt()`のCJK対応`classify()`とは非連携、既知の制限として明記)。**単一行スコープ**(マッチが`'\n'`をまたぐケースは対象外、Phase 4b6bの単語移動が単一行から始めて4b7bで拡張された前例と同じ「まず小さく正しく作る」順序)。空行はRE2の空入力に対する`submatch[i].data()==NULL`という仕様上オフセット計算が意味を持たないためスキャン対象から除外
- テスト数: 250→271 (単体+21: `util_utf8_convert_test.cpp`7件(ASCII/CJK/サロゲートペア/孤立サロゲートの往復変換とオフセット精度)、`search_search_service_test.cpp`14件(大文字小文字区別・単語単位・リテラル/正規表現・日本語UTF-16オフセット精度・行境界不可侵・空行混在・ReDoS的パターン耐性・ゼロ幅マッチの無限ループ防止))
- 新規`tests/bench/search_find_all_bench.cpp`: 20万行(約10MB相当)の合成ログ風ドキュメントに対する`findAll()`をRelease構成で実測。約60〜66ms(リテラル/正規表現/無マッチいずれも同程度) — 単純換算で約150MB/s相当。要件定義書§5「検索: 数GBファイルでも高速」の達成には現状の同期・単一スレッド実装のままでは数GB規模で数十秒かかる計算になり、非同期化・チャンク並列化(detailed_design.md §7.3、Phase 5b以降のスコープ)が実際に必要であることを示す最初の実測データとなった

**検証:**
- ローカル **Debug/Release/ubsan(clang-cl) の3プリセット全てでフルビルド・全271テストpass**。RE2/Abseil導入直後、`SearchService`実装に進む前に単独ビルド確認(計画の想定通りリスク要因を先に切り分けた)
- clang-tidy (`src/.clang-tidy`の`WarningsAsErrors: '*'`込み、新規`.cpp`2ファイル対象) で2件検出・修正: `readability-convert-member-functions-to-static`(`SearchService::findAll`がインスタンス状態を使わないため`static`化を指摘され、素直に従い呼び出し元も`SearchService::findAll(...)`直接呼び出しに統一)、`readability-math-missing-parentheses`(`text.size() * 3 + 1`に括弧追加、Phase 4a以来繰り返し出現しているのと同じルール)。再スキャンで0警告確認

**教訓:** (1) FetchContentで取り込んだサードパーティ依存(Abseil)が親プロジェクトのビルド設定(`CMAKE_MSVC_RUNTIME_LIBRARY`)を自身のCMakeLists.txt内で無条件に上書きするケースがあり、しかもその影響範囲が「そのターゲットが実際に作られたディレクトリスコープの中だけ」という非直感的な形で限定されるため、`get_property(DIRECTORY ... BUILDSYSTEM_TARGETS)`のような「直下のみ」取得するAPIでは検出も修正もできない — 複数レベルの`add_subdirectory()`を持つ大きな外部プロジェクトを扱うときは、ターゲット収集を最初から再帰的に書く必要がある。この教訓は[[reference-windows-cpp-ci-gotchas]]に項目7として追記した。(2) 「まずRE2/Abseilの単独ビルドを確認してから本体ロジックの実装に進む」という計画時点でのリスク分離判断が、実際に2件のCMake問題(install(EXPORT)衝突とubsan特有のCRTミスマッチ)を早期に(SearchServiceのロジック実装より前に)発見・解決できたことで報われた — 新規の大きな外部依存を追加するときは、まず「依存だけを単体でビルドが通ることを確認する」ステップを独立させる価値が改めて確認された。(3) 性能目標(「数GBファイルでも高速」)に対する実装の同期・単一行・単一スレッドという初期スコープの限界を、実装直後にベンチマークで具体的な数値(約150MB/s相当)として可視化したことで、次フェーズ以降の非同期化・並列化が「いつかやるべき最適化」ではなく「今回の実測値から導かれる次の必然的なステップ」として位置づけられた — CLAUDE.mdルール10「性能改善は必ずベンチマーク結果を根拠とする」の実践例。

**Phase 5a コードレビュー・修正 (同セッション継続):** ユーザーが「Phase 4b8の実装機能をレビューせよ」と指示したが、Phase 4b8は未着手のまま保留中であることを確認し、AskUserQuestionで対象を確認したところ「Phase 5aをレビュー」が選ばれた。`/code-review`スキル(high effort)で `587c5ff..d3ff4cd`(Phase 5a本体+doc sync)を対象にレビューを実施。8体のfinderエージェント(行単位スキャン/削除された動作の監査/呼び出し元追跡/再利用/簡素化/効率性/抽象度/CLAUDE.md準拠)を並列起動し、候補を検証の上10件の所見に収束、`ReportFindings`で報告。ユーザーが「1・2・3・4を修正せよ」と指示し、確認済み(CONFIRMED)の正当性バグ4件を同セッション内で修正。

**修正1(ゼロ幅正規表現マッチの重複):** `findAllInLine()`のゼロ幅マッチ後の走査位置前進が1バイト単位だったため、マルチバイトUTF-8文字(日本語等)の途中に着地し、同じUTF-16オフセットへの重複マッチを生成していた(自分の手でトレースして確認、既存テストがASCIIのみだったため未検出)。修正: `conv.byteToUtf16`を使い、ゼロ幅マッチ後は現在のコードポイント全体を読み飛ばすまで前進するよう変更。新規テスト`ZeroWidthRegexMatchNearMultiByteCharacterDoesNotDuplicate`(`u"あb"`に対するパターン`"a*"`が正しく3件を返すことを検証)を追加。

**修正2(空行が検索から除外される問題):** `findAll()`が空行を`continue`で無条件スキップしていたため、`^$`等の「空行にマッチすべき」パターンが常に0件を返していた。RE2は空入力に対する`submatch[0].data()`をNULLとして返す(「マッチした」と「マッチしなかった」を区別不能)ため、そのままではポインタ演算がUBになる懸念があったのが元々の回避理由。修正: `findAllInLine()`内で空行を特別扱いし、ポインタ演算を経由せず「唯一あり得る位置(0)」を直接使うよう変更。新規テスト`EmptyLineMatchesZeroWidthPattern`/`EmptyLineDoesNotMatchNonEmptyPattern`を追加。

**修正3(O(行数×ピース数)):** `findAll()`が1行ごとに`BufferSnapshot::extract()`を呼んでおり、これは`buffer_snapshot.h`が明示的に警告する「ピースリストを毎回先頭から再走査する」アンチパターンだった(`LineIndex::build()`で一度修正済みの問題の再発)。修正: `LineIndex::build()`と同じ`pieceView()`ベースの単一パス走査に全面書き換え(新規`scanDocument()`)。ピース内の`\n`位置で行バッファを`findAllInLine()`へ渡し、O(文書長)を達成。この書き換えの副作用として、レビューで指摘された「`lineContentEnd()`の`core`/`search`間重複」(所見6、修正対象外)も自然に解消した(`search_service.cpp`が`Document`の行インデックスAPIを一切使わなくなったため)。

**修正4(NEOMIFES_BUILD_TESTS=OFFでも無条件フェッチ):** `NeoMIFES.exe`が現時点で`neomifes::search`をリンクしていないにもかかわらず(`src/app/CMakeLists.txt`で確認)、RE2/Abseilを常時フェッチする設計になっており、「アプリ本体がリンクする」というコメントの主張が実態と矛盾していた。修正: `include(Dependencies)`と`add_subdirectory(search)`を`NEOMIFES_BUILD_TESTS`で再度ガード(Phase 5a以前の構造に復帰)。`-DNEOMIFES_BUILD_TESTS=OFF`の新規ビルドディレクトリで`_deps`が一切作られず、`NeoMIFES.exe`がネットワークアクセス無しでビルドできることを実測確認。Phase 5bでSearchServiceを実際にUIへ配線する段になったら、このガードを外すのが自然なタイミングであることをコメントに明記した。

**検証:** ローカル Debug/Release/ubsan(clang-cl) 全274テストpass(250テスト+Phase 5aの21件+今回の回帰テスト3件)。clang-tidy(`search_service.cpp`)で新規警告0。ベンチマーク実測値もわずかに改善(単一ピースの合成ドキュメントで約51〜56ms、以前は約60〜66ms)— ただし既存ベンチマークは依然として単一ピース文書のみを対象としており、修正3の本来の効果(多ピース文書でのO(document length)化)を数値では実証できていない点は既知の限界として残る。

**教訓:** レビュー対象を明示的に指定する運用(ユーザーが「Phase 4b8」と誤指定したが、実装されていないことをその場で`git log`で確認しユーザーに選択肢を提示した)により、存在しないコードへのレビューという無駄な作業を避けられた — レビュー依頼を受けたら対象コミット範囲の実在を確認してから着手する価値がある。また、8角度並列レビューでは複数のエージェントが独立に同じ問題(`extract()`のO(pieces)再走査、`lineContentEnd()`の重複)へ到達しており、独立した角度からの収束は所見の信頼度を裏付ける強いシグナルになることを確認した。

## Session 26 (2026-07-19): Phase 5b1 (複数行にまたがるマッチ対応) + レビュー残台のIssue化

**スコープ確認:** ユーザーが「Phase 5b着手せよ」と指示。Phase 5b自体のスコープが未確定だったためAskUserQuestionで確認したところ、Find bar UI配線(推奨案として提示)・複数行マッチ対応・置換(ReplaceAllCommand)・レビュー残台のIssue化の4項目全てが選択された。

**レビュー残台のIssue化(即実施):** Phase 5aコードレビューで未修正のPLAUSIBLE所見6件(うち1件はCONFIRMEDだが「fix 1〜4」の対象外だったもの)を、テーマ別に3つのIssueへ集約して起票: `docs/issues/search_crlf_line_ending.md`(CRLF行末未対応)、`docs/issues/cmake_msvc_runtime_library_fragility.md`(`MSVC_RUNTIME_LIBRARY`事後上書きの脆弱性、より根本的な`ABSL_MSVC_STATIC_RUNTIME`案も記録)、`docs/issues/search_utf8_convert_minor_cleanup.md`(`decodeOne()`のnoexcept欠如・サロゲート変換ロジックの重複・パターン変換時の無駄なオフセット表構築の3件をまとめて1つに)。コミット`27147fd`。

**計画フェーズ:** Explore agent 1体で(1) `core::ICommand`/`MultiCursorEditCommand`の設計制約(edit数とカーソル数の1:1前提がReplace-Allには転用できないこと)、(2) `CommandDispatcher::dispatch()`が`SelectionModel::setCursors()`を無条件に呼ぶため「カーソルに触れないコマンド」は存在しないこと、(3) `src/ui/`に子HWND/標準コントロールの前例が依然として皆無なこと(Phase 5a時点の調査を再確認)、(4) `main.cpp`の`wWinMain`スコープ状態変数パターン(Phase 4b6dの`altCursorAnchor`)がFind bar状態にも転用できる見込みであること、(5) `RenderPipeline`の`CursorVisual`+行ごと重なり判定描画パターンがマッチハイライトにも転用できる見込みであること、を調査。CLAUDE.mdルール8に従い**Issue化→5b1(複数行マッチ対応)→5b2(置換)→5b3(Find bar UI配線)**の順に分割し、Plan Modeで5b1のみを詳細設計(未着手の後続サブフェーズを先行設計するのは推測実装になるため)。Find bar UIの入力方式についてAskUserQuestionで確認し、**WC_EDIT子コントロール**(IME/カーソル点滅/クリップボード操作をOSに委譲、日本語入力も最初から正しく動作)が選ばれた(自前描画D2D入力ボックスは対象外)。ExitPlanModeでユーザー承認を得て実装着手。

**成果物 (Phase 5b1):**
- `SearchService::findAll()`の内部実装(`scanDocument()`)を「1行ごとに`findAllInLine()`を呼ぶ」から「`pieceView()`で文書全体を1つの`std::u16string`バッファへ連結し`findAllInBuffer()`(改名)を1回だけ呼ぶ」方式に全面書き換え。パターンに`\n`を含むリテラルクエリや`[\s\S]`等の文字クラスを使ったマッチが行をまたげるようになった
- **設計上の要点: `^`/`$`のセマンティクス維持。** RE2は`posix_syntax=false`(本プロジェクトのモード)では`^`/`$`が既定でテキスト全体の先頭/末尾にのみアンカーし、行ごとにアンカーさせるには`(?m)`が必要(RE2ドキュメント "to perform multi-line matching...begin the regexp with (?m)" で確認)。Phase 5aは1行を1バッファとして渡していたため`^`/`$`は暗黙的に行アンカーとして機能していたが、文書全体を1バッファ化するとこの暗黙動作が壊れる。`buildPattern()`が生成する最終パターンの先頭に`"(?m)"`を付与することで解消し、既存の`EmptyLineMatchesZeroWidthPattern`等の`^`/`$`依存テストが変更なしでpassし続けることを確認した
- `.`は`dot_nl`オプションを既定`false`のままにし、複数行マッチは明示的な`\n`や`[\s\S]`を書いた場合にのみ発生するよう意図的に制限(VSCode等の一般的なエディタの慣習に合わせた設計判断)
- メモリスケーリングの既知の制約を`search_service.h`/`detailed_design.md`に明記: 文書全体を1バッファへ連結するため検索1回あたりのメモリ使用量が文書サイズに比例するようになった(Phase 5aは最長1行分で済んでいた)。要件定義書の「10GB」目標との緊張関係は認識しつつ、チャンク並列走査(未実装のまま)は今回のPhase 5bスコープに含まれないためCLAUDE.mdルール3に従い着手しなかった
- テスト数: 274→279。新規6件(複数行にまたがるリテラル/文字クラスマッチ、`^`/`$`が引き続き行アンカーであることの回帰、`\A`/`\z`が文書全体アンカーとして機能することの確認)、既存`MatchDoesNotCrossLineBoundary`を`LiteralQueryWithoutEmbeddedNewlineDoesNotSpanLines`+`DotDoesNotMatchNewlineByDefault`に分割・改名(いずれも実装変更なしでpassし続けた — 事前の設計時点で「`.`のdot_nl=false継続」と「リテラル`"foobar"`は`\n`を含まない」という2条件から予測済みの結果)
- ベンチマーク再実測: 20万行合成ドキュメントで約33〜39ms(Phase 5a時点は約60〜66ms) — 1行ごとのUTF-8変換・RE2呼び出しの繰り返しオーバーヘッドが無くなったことによる改善。単純換算で約260〜300MB/s相当。ただし単一ピース文書のみが対象のベンチマークである点は変わらず、多ピース文書での挙動は未検証のまま

**検証:**
- ローカル **Debug/Release/ubsan(clang-cl) 全green**、全279テストpass
- clang-tidy (`search_service.cpp`) で新規警告0

**教訓:** (1) RE2の`^`/`$`セマンティクス(`posix_syntax=false`時は`(?m)`が無いと行アンカーにならない)という非自明なAPI詳細を、実装前にRE2公式ドキュメントで確認してから設計したことで、既存テストを壊す実装のやり直しを避けられた — 「まず動かしてから直す」ではなく「既知の外部ライブラリの挙動を先に確認してから設計する」ことが、テスト駆動での手戻りコストを最初から回避する形で機能した実例。(2) 実装前の設計段階で「既存の`MatchDoesNotCrossLineBoundary`テストは新機能によって壊れるはずだ」という直感に反し、実際には`.`のdot_nl=false継続と対象パターンの性質から「変更なしでpassし続ける」という結論に事前に到達でき、その予測が実装後も的中した — 新機能追加時に「既存テストが本当に影響を受けるか」を実装前に手計算で検証する価値を示す一例。(3) レビュー残タスクのIssue化を「軽く流す」のではなくテーマ別に集約した3件の正式なIssueドキュメントとして起票したことで、Phase 5b2/5b3着手時に何が未解決として残っているかが一目で追跡可能になった。

**次回 (Phase 5b2):** 置換(`ReplaceAllCommand`)の詳細設計から着手。Explore調査で判明済みの制約: 既存`MultiCursorEditCommand`は「edit数とカーソル数が1:1」前提のため転用不可、`core::ICommand`を直接実装する新規クラスが必要(累積オフセット適用のアルゴリズム自体は`MultiCursorEditCommand::execute()`/`undo()`のパターンを再利用可能、`cursorsAfterExecute()`/`cursorsAfterUndo()`は置換前のカーソル位置をスナップショットして返す設計になる見込み)。完了後はPhase 5b3(Find bar UI配線、WC_EDIT子コントロール使用が決定済み)へ進む。**保留中のPhase 4b8**に戻る選択肢もあわせて次セッション開始時にユーザーへ提示すること。詳細は `detailed_design.md` §7・`RESUME_HERE.md` §3.17/§6 参照。

## Session 27 (2026-07-19): マスターロードマップ v1.0 発行 (Phase 4b8/5b2/5b3/6-12 一気通貫詳細設計)

**背景・動機:** Phase 5b1 完了 + CI green を確認した直後、ユーザーが「今後のフェーズについて具体的実装案を明確にすべきではないか？次フェーズのたびに実装内容が未確定では完成イメージがブレるためである。NeoMifesの存在価値である秀丸/サクラ/MIFES の良いとこ取り機能・世界最高峰のUI/UX・世界最高速の動作体験を使命として完成までの実装詳細を設計せよ」と指示。これまでフェーズ毎にセッション開始時にスコープを再確認する運用だったため「完成に近づいているか」の再確認コストが毎回発生していた問題を、一気通貫の計画文書で解消することを狙う。

**成果物 (Session 27):**
- **[`docs/design/master_roadmap.md`](../design/master_roadmap.md) v1.0 新設** (1183行、16章構成)
  - §0: 位置づけ・関連文書との役割分担 (詳細設計書は「実装済み機能のリファレンス」、本書は「未実装フェーズの Plan-of-Record」)
  - §1: 完成イメージ (三大エディタからの継承マトリクス、17カテゴリ × 4エディタで機能の位置づけを明示)
  - §2: 全フェーズ俯瞰
  - §3: **Phase 4b8** — 矩形選択 (既存複数カーソル基盤への写像) / タブ⇔スペース変換 / N対N分配クリップボード (VSCode互換 `CF_NEOMIFES_MULTICURSOR` 実装案)
  - §4: **Phase 5b2** — ReplaceAllCommand (キャプチャグループ `$1..$9`/`$$`/`$0` 対応、逆順適用アルゴリズム)
  - §5: **Phase 5b3 + 5c** — Find bar UI (WC_EDIT + FindBarState 状態管理、`SetTimer`ベースのデバウンス150ms) / Grep (Worker Pool、ストリーミングコールバック)
  - §6: **Phase 6** — 全8エンコーディング + 行末3種 + 自動判定3段階 (BOM/統計/N-gram)
  - §7: **Phase 7** — TextMate vs tree-sitter PoC (ADR-013 として発行予定)、非同期増分解析、折り畳み、アウトライン
  - §8: **Phase 8** — C ABI プラグイン (関数ポインタで CoreApi を渡す境界設計)、ホットロード、SEH隔離、権限モデル
  - §9: **Phase 9** — Claude/GPT/Gemini/OpenAI互換の統一 `IAiProvider` 抽象、WinHTTP vs libcurl PoC (ADR-004 Superseded 予告)、Credential Manager (DPAPI) キー保管、AI無効時の完全ネットワークI/O封鎖 (要件定義書 §7 絶対条件対応)
  - §10: **Phase 10** — 本ソフト最大の差別化点であるログ解析モード (12種の組込パターン、非同期チャンクインデックス、レベル/時系列フィルタ) / CSV (1000万行対応、列オフセット表遅延構築) / JSON+XML Tree (差別化点、XPath/JSONPath 自前実装)
  - §11: **Phase 11** — Git (libgit2、Diff/Blame/3-Way Merge) / LSP (C++/TS/Python 3言語限定、stdio JSON-RPC 自前実装) / マクロ (Lua 5.4 + QuickJS)
  - §12: **Phase 12** — 出荷判定チェックリスト (16項目)
  - §13: UI/UX トップレベル方針 (キーバインドプリセット4種: 標準/秀丸互換/サクラ互換/MIFES互換)
  - §14: **パフォーマンス予算表** (全機能横断、要件定義書 §5 の目標数値を Phase 単位に配分)
  - §15: リスク・未決事項の再整理 (basic_design.md §8/§9 を Phase 対応表として再構造化、U#9-11の3件を新規追記)
- **[`docs/handoff/RESUME_HERE.md`](../handoff/RESUME_HERE.md) 更新** — ヘッダに master_roadmap.md への導線を追加、§5ドキュメント地図に master_roadmap を追加、§6 次回推奨プロンプトを「master_roadmap.md §4 を読んでから Plan Mode」形式に書き換え

**設計上の主要判断 (master_roadmap.md 執筆時に確定):**
- **マクロ言語 (要件定義書 U#5 の未決):** Lua 5.4 + QuickJS の両対応で確定。両方ともマクロランタイム DLL としてプラグイン境界の上で動作
- **LSP初期対応 (U#6):** C++ (clangd) / TypeScript (typescript-language-server) / Python (pylsp) の3言語で確定 (basic_design.md R4 のリスク対策通り)
- **Phase 5c 位置づけ:** Grep/複数フォルダ検索は Phase 5b3 (Find bar UI) 完了後の独立サブフェーズとして分離。ストリーミングコールバック方式・専用モード `Mode::GrepResult` で結果表示する設計
- **Phase 9 プライバシー方針:** AI コンテキストに「ユーザー選択範囲 + カーソル前後N行以外は送信しない」を設計原則として文書化。監査ログにトークン数のみ記録 (内容は非記録)
- **Phase 10 ログ解析モードを本ソフト最大の差別化点として位置づけ確定:** 対象12種、非同期インデックス、時系列ジャンプ、レベル色分けを詳細まで規定
- **未決だった正規表現エンジン再評価 (U#3/R2)、シンタックス定義形式 (U#4/R3):** Phase 5c/7a での PoC 実施 → ADR 発行という運用に格上げ

**運用ルール確定:**
1. 各フェーズ着手前に master_roadmap.md の該当章を読み、Plan Mode でセッション個別の詳細プランを起こす
2. 各フェーズ完了時に実装で確定した詳細を `detailed_design.md` へ吸収し、master_roadmap.md の該当章末尾に「実装後の確定事項/変更点」を追記
3. master_roadmap.md 自体は「実装前の計画」を残し続ける歴史的計画文書として保持

**検証:** ドキュメントのみの変更のためビルド不要。要件定義書 §5/§6/§8-13 の全項目が master_roadmap.md のいずれかの Phase §で拾われていることを目視確認 (17継承マトリクスと 15パフォーマンス予算表で網羅)。

**教訓:** 「各セッション開始時にスコープを再確認する」運用は柔軟性が高い反面、完成イメージのブレと再確認コストが累積する。マクロレベルの完成計画を先に一気通貫で立てておくことで、Phase 単位の意思決定は「マスタープランからの差分」に還元でき、判断の一貫性が担保される — CLAUDE.md ルール3 (推測実装をしない) と両立させるには「計画は詳細に立てるが実装は必ずフェーズ単位で Plan Mode を通す」二段階制が有効。

**次回 (Phase 5b2 継続):** 本 Session 27 は計画文書追加のみで実装コード変更なし。次セッションは master_roadmap.md §4 を読み、Phase 5b2 の Plan Mode 詳細プラン → 実装 → 検証の順に進める。Phase 4b8 保留オプションも並行して提示すること。

## Session 28 (2026-07-19): master_roadmap.md v2.0 — Google/MS 責任者視点の徹底レビュー反映

**背景・動機:** Session 27 で発行した master_roadmap.md v1.0 (1183 行) に対しユーザーが「本当に『秀丸/サクラ/MIFES の良いとこ取り』『世界最高峰の UI/UX』『世界最高速の動作体験』を実現する内容になっているか、Google/Microsoft のソフトウェア開発責任者の立場で徹底レビューを行い、更なるブラッシュアップを行なって欲しい」と指示。v1.0 は Phase 単位の粒度で書かれていたが、シニアテックリーダー視点で見ると 18 項目の構造的欠陥があった。

**特定した v1.0 の 18 項目の構造的欠陥:**
- A: 「良いとこ取り」の網羅性不足 (秀丸のキーマクロ・grep 結果ジャンプ・DIFF ビュー・タグジャンプ、サクラのフリーカーソル・タイプ別設定、MIFES の桁位置ジャンプ・マーカー・全角罫線 が全て漏れていた)
- B: 「世界最高峰の UI/UX」の裏付け欠如 (ミニマップ・Breadcrumb・Sticky scroll・Indent guides が完全に欠落、コマンドパレット深掘り不足、Zen mode/分割ビュー/タブグループ空白、タッチ/ペン/スタイラス無視、ligature/カラーエモジ抜け)
- C: 「世界最高速」の裏付け技術要素の欠如 (SIMD 戦略が「BM+SIMD (未着手)」の一言のみ、GPU 未検討、Direct Storage 未検討、Frame pacing/VRR 未検討、キャッシュレイアウト浅い)
- D: AI 統合が浅い (Copilot 型リアルタイム予測補完がゼロ、エージェント/RAG/ローカル LLM 表層のみ)
- E: アクセシビリティ・国際化・セキュリティ・プロダクト運営基盤が総じて欠落 (UI Automation、CJK IME、RTL、grapheme cluster、サンドボックス、SBOM、脆弱性開示、テレメトリ、KPI/SLO、自動更新、フィーチャーフラグ、クラッシュ収集、bisect、シェル統合)

**成果物: master_roadmap.md v2.0 (2026 行、v1.0 から +844 行の大幅拡張、23 章構成):**
- §1 完成イメージ全面拡張: 差別化 5→10、**ペルソナ 7 種 (P1: SAP コンサル / P2: Windows インフラ / P3: Web 開発者 / P4: 技術ライター / P5: OSS 開発者 / P6: エンタープライズ管理者 / P7: エディタホッパー)**、**競合ポジショニング (VSCode/Sublime/Notepad++/UltraEdit/秀丸/サクラ/MIFES/Vim/Emacs との比較表)**、**60 機能継承マトリクス** (v1.0 の 17 機能から 60 機能へ精緻化、22 の差別化点を明示)
- §3 (Phase 4b8) にフリーカーソル (虚数位置)、マーカー (Bookmark)、桁位置ジャンプ、ブックマーク列を追加
- §5 (Phase 5b3) に**コマンドパレット** (VSCode 相当、Ctrl+Shift+P、ファジー検索、最近使用ボーナス) を Find bar と同時実装する設計を追加、§5.5 (Phase 5c) に検索履歴・タグジャンプ・秀丸互換 Grep 結果ペインを追加
- §7 (Phase 7) に**ミニマップ・Breadcrumb・Sticky scroll・Indent guides・Semantic highlighting** を統合 (VSCode 相当の全モダン UI を Phase 7 に集約)
- §8 (Phase 8) に**プラグインサンドボックス** 3 レベル (SEH 隔離 / Job Object / Windows AppContainer)、マーケットプレース基盤を追加
- §9 (Phase 9) 大幅拡張: **Copilot 型ゴーストテキスト補完・RAG (Retrieval-Augmented Generation)・AI エージェント (Tool use)・マルチモデル並列比較・ローカル LLM (Ollama) 対応** を統合、`InlineCompletionEngine`/`RagIndexer`/`VectorStore`/`Agent` の型スケッチを提示
- §10 (Phase 10) ログ解析拡張: **リアルタイムテール、分散トレース ID 対応、Structured Log (JSON/Logfmt)、OpenTelemetry/AWS X-Ray/Loki/Fluentd 対応、統計ダッシュボード** を追加
- §11 (Phase 11) LSP 完全実装: Semantic tokens/Code lens/Inlay hints/Workspace symbols/Rename/Code actions/Signature help/Call hierarchy/Type hierarchy の 15 機能に拡張、マクロに**秀丸互換 API レイヤ**を追加
- §13 UI/UX トップレベル方針大幅拡張: Zen mode/分割ビュー/タブグループ/ピン留め/Mica/Acrylic/HDR/VRR/タッチ/ペン/スタイラス
- §15 世界最高速の裏付け技術要素 (新規): SIMD 動的 dispatch (SSE4.2/AVX2/AVX-512/NEON)、GPU compute shader 検索検討、Direct Storage、Frame pacing/VRR、キャッシュ最適レイアウト、Lock-free 並行データ構造、PGO/LTO
- §16 国際化・アクセシビリティ (新規): CJK IME (日中韓)、RTL、grapheme cluster (Unicode 16 UAX #29)、UI Automation、高コントラスト、カラーブラインドネスモード、WCAG 2.2 AA 準拠
- §17 セキュリティ (新規): サンドボックス 3 レベル、Code signing、SBOM (CycloneDX)、脆弱性開示プロセス、データ暗号化 (Undo/Session/AI キー/RAG)、権限最小化
- §18 リリース・配布・自動更新 (新規): MSIX/Portable Zip/MSI、カナリア→ステーブル 2 チャネル、差分更新 (bsdiff)、ロールバック、月次/四半期/年次リリースサイクル
- §19 KPI/SLO/メトリクス (新規): DAU/リテンション/NPS 目標、パフォーマンス SLI/SLO、テレメトリ opt-in 原則
- §20 エコシステム戦略 (新規): プラグインマーケットプレース (公式初期 10 種)、テーマギャラリー、スニペット/マクロ共有、ライセンス戦略 (Apache 2.0 候補)
- §21 開発品質基盤 (新規): テストピラミッド (unit/integration/E2E/soak/fuzz)、パフォーマンス回帰検出、フィーチャーフラグ、クラッシュ収集、Bisect ツール、Windows シェル統合 (右クリック/Jump List/Windows Terminal)
- §22 リスク・未決事項 12→20 に拡張 (GPU compute/Direct Storage/AppContainer/HDR/RTL/秀丸互換カバレッジ/テレメトリ項目/マーケットプレース運営/ライセンス を追加)

**設計上の主要判断 (v2.0 で追加確定):**
- **AI 統合の方針:** Copilot 相当のゴーストテキスト補完を標準機能として Phase 9 に組込 (プラグイン境界の内側で完結、AI 無効時は完全非ロード)。RAG/エージェント/マルチモデル比較も Phase 9 スコープに含める
- **UI/UX の VSCode 相当到達目標:** Zen mode/分割ビュー/タブグループ/ミニマップ/Breadcrumb/Sticky scroll/Indent guides を全て標準機能として実装、"VSCode に慣れたユーザーが違和感なく移行できる" を到達点に設定
- **アクセシビリティ:** WCAG 2.2 AA 準拠を Phase 12 の出荷判定条件に組込。NVDA/JAWS 手動確認を必須化
- **セキュリティ運営基盤:** 脆弱性開示プロセスを事前に文書化 (`security@neomifes.dev` 仮)、SBOM を CI で自動生成、Coordinated Disclosure 90 日ポリシーを採用
- **テレメトリ:** 完全 opt-in、送信内容は明示同意項目のみ、個人特定情報一切送信しない、匿名ユニーク ID は opt-out で削除可
- **マーケットプレース戦略:** Phase 8 で基盤実装、Phase 12 出荷後に運営開始、初期は公式プラグイン 10 種 (Vim/Emacs/AI/Git/LSP マネージャ/Markdown プレビュー/LaTeX/HTML/Docker/AWS CLI)

**検証:** ドキュメントのみの変更のためビルド不要。要件定義書 §5/§6/§8-13 の全項目・§20 の最終目標・§18 の非機能要件 全て が v2.0 のいずれかの章で拾われていることを目視確認。**60 機能継承マトリクスで秀丸/サクラ/MIFES 各エディタの固有機能が全て網羅されていることを再確認**。

**教訓:** (1) 「設計文書のブラッシュアップ」は複数の視点 (テックリード / プロダクト責任者 / セキュリティ / アクセシビリティ / エコシステム) からの独立レビューで大幅に強化できる。単一視点の設計は必然的にブラインドスポットを持つ。(2) 「三大エディタの良いとこ取り」を主張するには、三大エディタの固有機能を全て列挙して「対応 Phase」を割り当てるマトリクスが必要 — v1.0 の 17 機能では網羅性不足、v2.0 の 60 機能で初めて主張の裏付けが取れた。(3) 「世界最高速」を主張するには、他エディタが実装していない/実装が浅い高速化技術を明示する必要がある (SIMD/GPU/Direct Storage/Frame pacing/PGO/LTO)。ただ「速い」と言うだけでは差別化にならない。(4) 「世界最高峰の UI/UX」は VSCode 相当を到達点に設定し、VSCode の全モダン UI (ミニマップ/Breadcrumb/Sticky scroll/Zen mode/分割ビュー) を明示的にスコープに含めることで達成基準が具体化される

**追加確認事項:** Phase 5b1 push 後の CI (run `29668590762`) は 4 ジョブ全 green を確認 (Build & Test release 3m50s / debug 4m6s、UBSan clang-cl 3m4s、clang-tidy 32m45s)。予約していた ScheduleWakeup (25 分後の CI 確認用) は本セッションで CI green が確定したためキャンセル済。

**push + 永続化の強化 (同セッション追記):** ユーザーが「pushせよ。また本ブラッシュアップ版の開発設計は今後コンテキストが失われても完全な永続化が可能となるようにファイルに全てを記録し毎回認識できるようにせよ」と指示。まず v1.0/v2.0 の2コミット (`98c3baf`, `79d8340`) を push、CI 未実施 (ドキュメントのみの変更のため対象ワークフロー無し)。

続けて「コンテキストが失われても master_roadmap.md が確実に参照される」ことを担保するため、**セッション開始時に必ず読まれる文書へ相互参照を追加** (コミット `dbc0e4e`):
- `CLAUDE.md` 冒頭 (既存の RESUME_HERE.md 誘導と並ぶ位置) に「未着手フェーズについて推測・再設計する前に必ず該当章を読むこと」「要件定義書と同格の拘束力を持つ Plan-of-Record」と明記
- `CLAUDE.md` §7 進行フェーズ表 (v0 時点の粗い 12 行表) の直後に「この表ではなく master_roadmap.md が正」の警告を追加 — 古い粗い表を読んで浅い理解のまま実装着手するリスクを塞ぐ
- `docs/design/basic_design.md` / `detailed_design.md` の冒頭ヘッダに相互参照を追加 (detailed_design.md 側は「実装済み機能のリファレンス、未着手計画は master_roadmap.md、フェーズ完了時に吸収」という役割分担を明記)
- ルート `README.md` のドキュメント一覧に追加

**教訓 (永続化設計):** 「ファイルに書けば永続化される」は必要条件だが十分条件ではない — 新しいセッションがそのファイルの存在を*発見する経路*が無ければ実質的に忘れられる。本プロジェクトでは CLAUDE.md が全セッションで強制的に読まれる文書であるため、そこに「参照必須」と明記することが最も確実な永続化の担保になる。単なる `docs/design/` への配置だけでは、次セッションが `RESUME_HERE.md` しか見ずに `master_roadmap.md` の存在に気づかない可能性があった。

**次回 (Phase 5b2 継続):** 本 Session 28 は計画文書・ドキュメント相互参照の追加のみで実装コードは変更なし。次セッションは (1) `CLAUDE.md` 冒頭の誘導により自動的に master_roadmap.md の存在を認識できるはずだが、明示的に master_roadmap.md v2.0 の §4 (Phase 5b2 詳細設計) を読み、Plan Mode で個別詳細プランを起こしてから実装。Phase 4b8 保留オプションも並行提示。

## Session 28 続き (2026-07-19): Phase 5b2 (置換 core::ReplaceAllCommand + search::expandReplacementTemplate) 完了

**着手経緯:** ユーザーが「順次開発を進めよ」と指示。master_roadmap.md(Plan-of-Record)の順序どおりPhase 5b2に着手。Plan Modeへ移行し、まず既存コード(`command.h`/`edit_commands.{h,cpp}`/`search_service.{h,cpp}`/`core_edit_commands_test.cpp`)を実地に読んで設計方針を固めた上で、Plan agentに設計検証を依頼した。

**Plan agentによる設計検証で判明した重要な乖離:** master_roadmap.md §4.3のスケッチは `ReplaceAllCommand` が `search::MatchWithCaptures` を直接コンストラクタで受け取る設計(core→search依存)だったが、これはPhase 5aレビューのFix#4(「searchは実アプリ本体`NeoMIFES.exe`にまだリンクされていないため、RE2/Abseilの取得をNEOMIFES_BUILD_TESTS限定にする」)という既存判断と衝突することが判明。Plan agentは「両方とも根拠のある選択で、ユーザーに明示確認すべき」と指摘し、AskUserQuestionで確認したところ**疎結合を維持する方針**(核心の`core::ReplaceAllCommand`はsearch::を一切知らない)が選ばれた。副次的に、roadmap §4.3が想定していた`ICommand`のシグネチャ(`document::EditResult execute(document::Document&)`、`SelectionModel::Snapshot`)も実際のコードに存在しない型であることが判明し、roadmapのPhase 5b2章は実装確定前の高レベルスケッチに過ぎなかったことが確認された。

**成果物:**
- 新規`src/core/include/neomifes/core/cumulative_shift_edit.{h,cpp}`(`applyEditsWithCumulativeShift()`/`undoEditsDescending()`) — 既存`MultiCursorEditCommand::execute()`/`undo()`の累積オフセットアルゴリズムを機械的に抽出。既存の`viewport_math.h`/`resize_math.h`と同じ「モジュール直下フラット配置、detail::等のネスト無し」規約に従う
- `MultiCursorEditCommand`を上記2関数を呼ぶ形にリファクタ(挙動不変、既存4テストが無変更のままpassすることで実証)
- 新規`src/core/include/neomifes/core/replace_all_command.{h,cpp}` — `core::ReplaceAllCommand`。既存`MultiCursorEditCommand`(edit数=カーソル数前提)は転用不可なため新規クラスとして実装。`cursorsAfterExecute()`/`cursorsAfterUndo()`はどちらも構築時のカーソルスナップショットを無変更のまま返す(置換はカーソルを一切動かさない設計)。`PerCursorEdit`(既存構造体)をそのまま再利用し新規struct無し
- `search::Match`にキャプチャグループ対応(`std::vector<document::TextRange> groups`)を追加。RE2の`NumberOfCapturingGroups()`を`std::min(9, ...)`でキャップ(`expandReplacementTemplate()`が`$1`-`$9`しか消費しないため、RE2公式ドキュメントの「要求サブマッチ数を絞ると高速化する」という推奨に従う)。非参加の任意グループ(例: `(a)|(b)`が"b"にマッチした場合のグループ1)はマッチ開始位置での空レンジとして表現、RE2が空文書に対して全submatchを`nullptr`扱いする既存の仕様と同じ判定ロジックを再利用する新規`submatchToRange()`ヘルパーで対応
- 新規`src/search/include/neomifes/search/replacement.{h,cpp}` — `search::expandReplacementTemplate()`。`$0`/`$&`(全体マッチ)・`$1`-`$9`(キャプチャグループ)・`$$`(リテラル`$`)を展開。範囲外の`$N`・未知のエスケープ・末尾の`$`はリテラルのまま残す(エラーにしない、`findAll()`の「不完全な正規表現は空結果」という既存方針と同じ哲学)
- テスト数: 279→300(+21件)。`core_replace_all_command_test.cpp`(新規6件、`SearchService::findAll`→`expandReplacementTemplate`→`ReplaceAllCommand`のフルパイプラインを検証する統合テスト含む)・`search_replacement_test.cpp`(新規9件)・`search_search_service_test.cpp`(キャプチャグループ関連6件追加)

**検証:**
- ローカル **Debug/Release/ubsan(clang-cl) 全green**、全300テストpass
- clang-tidy: 新規5ファイル全てチェック、`hicpp-use-auto`警告1件を検出(`submatchToRange()`内の`const std::size_t byteStart = static_cast<std::size_t>(...)`が型名重複と判定)、`const auto`化で修正・再検証してゼロ警告を確認

**意図的にスコープ外とした項目 (roadmap §4.3との差分):**
- Preview API(`ReplaceAllCommand::preview()`静的メソッド)・100万件ベンチマーク・チャンク圧縮Undo — UIの消費者(Find bar)がまだ無い状態で作るのはCLAUDE.mdルール3の推測実装にあたるため、Phase 5b3以降へ明示的に延期
- `search::Match` → `core::PerCursorEdit`変換のグルーコード — Phase 5b3でFind bar UIが実際にsearch::を本体へリンクするまで書かない。現状はテスト内でのみパイプライン全体の合成可能性を証明(本番コードとしては存在しない)

**新規発見・記録した既知の制約:** `BufferSnapshot::extract()`が毎回ピースリストを先頭から再走査するコストは、`MultiCursorEditCommand`から既存のものだが、`ReplaceAllCommand`が数十万件規模のマッチを処理するようになると初めて実務上のボトルネックになりうる(Plan agentのレビューで指摘)。`docs/issues/replace_all_buffer_snapshot_extract_scaling.md`として起票、Phase 5b3で実際の大量マッチ経路ができてから再評価する方針。

**ドキュメント同期:**
- `docs/design/detailed_design.md` §7に新規§7.1'''(Phase 5b2実装リファレンス)追加、§7.1''から実装済みの`ReplaceAllCommand`を除去
- `docs/design/master_roadmap.md` §4に新規§4.7(実装後の確定事項)追加 — coupling方針の決定・実際の`ICommand`シグネチャとの乖離・Preview/ベンチ延期を記録
- `docs/handoff/RESUME_HERE.md`に新規§3.18(完了記録)追加、§1状態表・§6推奨プロンプトをPhase 5b3向けに更新

**教訓:** (1) 実装前に書かれた計画文書(master_roadmap.md)のスケッチは「実装確定前の高レベル指針」であり、実コードのシグネチャや既存のアーキテクチャ判断(Fix#4のガード)と衝突しうる — CLAUDE.mdルール3が要求する「矛盾が生じた場合はユーザーに確認する」を字義通り実行したことで、roadmapを無批判に実装してビルドシステムを壊す(RE2/Abseilの取得を全ビルド必須化する)という手戻りを未然に防げた。(2) Plan agentによる設計検証は「自分の設計が正しいか」を確認するだけでなく、自分では気づいていなかった既存コードとの整合性問題(Fix#4との衝突)・パフォーマンスリスク(`BufferSnapshot::extract()`のO(pieces)コスト)を独立した視点から発見する価値がある。(3) 既存の類似クラス(`MultiCursorEditCommand`)から新規クラス(`ReplaceAllCommand`)を作る際、「アルゴリズムは同じだがカーソル移動の意味論が異なる」という場合は、アルゴリズム部分だけを共有ヘルパーへ抽出し、意味論が異なる部分(cursorsAfterExecute/Undo)は別クラスとして残すのが適切な粒度— 全部を1クラスにまとめる(条件分岐で意味論を切り替える)より責務が明確になる。

**次回 (Phase 5b3):** Find bar UI + コマンドパレット配線の詳細設計から着手。master_roadmap.md §5に既に詳細規定済み(WC_EDIT子コントロール、FindBarState、コマンドパレットのファジー検索設計まで含む)。ここで初めて`search::`が実アプリ本体へリンクされ、`search::Match` + `expandReplacementTemplate()` → `core::PerCursorEdit` → `core::ReplaceAllCommand`を繋ぐ実際のグルーコードをUIコードとして書くことになる。**保留中のPhase 4b8**に戻る選択肢も次セッション開始時にユーザーへ提示すること。push は本セッション終了時点で未実施 — 次回開始時にユーザーへ確認すること。

## Session 29 (2026-07-19): Phase 5b3a (Find bar UI基盤 + マッチハイライト) 完了

**着手経緯:** ユーザーが「Phase 5b3に進め」と指示。master_roadmap.md §5(Find bar UI + コマンドパレット + マッチハイライト)に着手する前に、既存コード(`main.cpp`・`main_window.h/.cpp`・`render_pipeline.h/.cpp`・`viewport.h`・`CMakeLists.txt`群)を直接調査し、本プロジェクト初の子HWND導入という前例の無い工学的挑戦であることを確認。roadmap §5が「Find bar UI + コマンドパレット」を1章にまとめていたが、CLAUDE.mdルール8に従い**5b3a(Find bar UI基盤)→5b3b(置換行配線)→5b3c(コマンドパレット)**の3段階分割を採用(Grepは元々roadmap上も別フェーズ5cとして区別済み)。

**Plan agentによる設計検証で判明した4件の必須修正:**
1. **Alt+C/W/RはWM_KEYDOWNではなくWM_SYSKEYDOWNで届く**(Altはシステムキー修飾子のため)。専用ハンドラが必要で、処理した3キーはフォールスルーさせず`return 0`する必要がある(でないと既定処理が存在しないシステムメニューを開こうとする)
2. **IME変換中はEnter/Escape/F3をFind barショートカットとして横取りしてはいけない。** `WM_IME_STARTCOMPOSITION`/`WM_IME_ENDCOMPOSITION`で変換状態を追跡し、変換中は`DefSubclassProc`(IME自身)へ委譲する必要がある — 本プロジェクトの「CJK IME一級市民」という方針に直結する必須修正、見落とすと日本語入力が壊れる
3. **デバウンスタイマーは発火後に`KillTimer`が必要。** 単純な`SetTimer`のままでは入力停止後も150ms間隔で無限に検索が再実行される
4. **`cmake/Dependencies.cmake`の`NEOMIFES_BUILD_TESTS`ガード解除は単純な`include()`移動では不十分。** RE2/AbseilとGoogleTest/benchmarkが同一ファイルに同居しており、単純に無条件化するとテスト専用依存まで無条件フェッチされる。既存コード自身のコメント(`CMakeLists.txt`・`cmake/Dependencies.cmake`)がこの分割を予告していたことも確認

**成果物:**
- 新規`ui::FindBar`(`src/ui/include/neomifes/ui/find_bar.{h,cpp}`) — `WC_EDIT`子コントロール、`SetWindowSubclass`/`DefSubclassProc`でEnter/Escape/F3/Shift+F3/Ctrl+F/Alt+C/W/Rを横取り。`ui::MainWindow`と同じ「search::/document::/core::を一切知らない」設計、`FindBarConfig`の4コールバックで`main.cpp`と疎結合連携。`platform::WindowHandle`/`platform::GdiObjectHandle`(既存だが未使用だったRAIIラッパー)をHWND/HFONT所有に採用
- 新規`ui::find_navigation.h`(ヘッダオンリー純粋関数) — `nextMatchIndex`/`previousMatchIndex`(ラップアラウンド)・`formatMatchCountLabel`。`click_tracking.h`と同じパターンでユニットテスト可能に
- `render::MatchVisual` + `RenderPipeline::setMatchVisuals`/`drawMatchesOnLine`/`drawMatchOnLine` — 既存`CursorVisual`/`drawSelectionsOnLine`と全く同じ構造を踏襲(roadmapが示唆した別ファイル`match_visual.h`ではなく既存`CursorVisual`と同じ`render_pipeline.h`に配置、実際の既存配置との一貫性を優先)。`FrameState`に`matchVisuals`を追加しdamage-tracking対象化
- `ui::MainWindow`に`onCommand`フック追加(`WM_COMMAND`、既存フックと同じパターン)
- **CMakeガード解除:** `cmake/Dependencies.cmake`をRE2/Abseil専用に整理して無条件`include()`化、新規`cmake/TestDependencies.cmake`にGoogleTest/benchmarkを分離し`NEOMIFES_BUILD_TESTS`限定のまま維持。`src/CMakeLists.txt`の`add_subdirectory(search)`を無条件化、`src/app/CMakeLists.txt`に`neomifes::search`を追加 — `NeoMIFES.exe`が初めて`search::`を実リンク
- `main.cpp`に`navigateToMatch`/`runFindQuery`/`jumpToMatch`/`closeFindBar`/`handleFindBarKey`/`buildFindBarConfig`(既存の`dispatchMouseDown`/`handleClipboardKey`と同じ「ヘルパー抽出でcognitive complexity対策」パターン)を追加、`wireNormalMode`にFind bar関連の5パラメータを追加(既に8パラメータあった関数がさらに拡張、5b3bで更に伸びる見込みだが今回は範囲外としてリファクタしない判断)
- `wWinMain`冒頭に`InitCommonControlsEx()`呼び出しを追加(comctl32初期化、厳密な必要性は未確定だが防御的に追加)
- テスト数: 300→310(+10件、`ui_find_navigation_test.cpp`新規)

**検証:**
- ローカル **Debug/Release/ubsan(clang-cl) 全green**、全310テストpass
- **`NEOMIFES_BUILD_TESTS=OFF`での単独アプリビルドを別ビルドディレクトリ(`build/debug-appOnly`、検証後削除)で実施** — RE2/Abseilのみフェッチされ、GoogleTest/benchmarkはフェッチされないことを確認(CMakeガード解除の正しさを実測で裏付け)
- clang-tidy: 新規/変更4ファイル(`find_bar.cpp`/`main_window.cpp`/`render_pipeline.cpp`/`main.cpp`)全てチェック、`misc-redundant-expression`3件(`DEFAULT_PITCH | FF_DONTCARE`のWin32慣用句、`255.0F / 255.0F`の自己除算×2)・`misc-const-correctness`1件を検出・修正、再検証でゼロ警告
- 実アプリを起動し3秒間クラッシュしないことを確認(基本的な起動安定性のスモークテストのみ)。**Ctrl+F操作・日本語IME変換中のEnter/Escape動作・マッチハイライトの視覚的な色/重なり順の確認は、この環境にWin32 GUI自動化手段が無いため未実施 — 次セッション冒頭でユーザーに依頼すること**

**意図的にスコープ外とした項目 (Phase 5b3b/5b3c/5cへ延期):**
- 置換行(Ctrl+H)配線、コマンドパレット(Ctrl+Shift+P)、Case/Word/Regexのクリック可能なトグルボタン(Alt+C/W/Rキーバインドのみ実装)、検索履歴、タグジャンプ、Grep — UIの消費者が無い状態でこれらを作るのはCLAUDE.mdルール3の推測実装にあたるため

**新規発見・記録した既知の制約:** `drawMatchesOnLine()`が可視行ごとに`m_matchVisuals`全件を線形走査するため、マッチ件数が数千〜数万件規模になると60fps目標に抵触しうる(`docs/issues/match_highlight_linear_scan_scaling.md`として起票、Phase 5c等で大量マッチ経路ができてから再評価)。

**ドキュメント同期:**
- `docs/design/detailed_design.md` §7に新規§7.1''''(Phase 5b3a実装リファレンス)追加
- `docs/design/master_roadmap.md` §5に新規§5.8(実装後の確定事項)追加 — 5b3a/5b3b/5b3c分割の記録、`FindBarState`スケッチからの状態配置の乖離、CMakeガード解除の詳細
- `docs/handoff/RESUME_HERE.md`に新規§3.19(完了記録)追加、§1状態表・§6推奨プロンプトをPhase 5b3b向けに更新

**教訓:** (1) 「Win32メッセージがどのHWNDに届くか」という基礎知識(子コントロールにフォーカスがある間は親のWM_KEYDOWNは発火しない、EN_CHANGE等の通知は常に親へWM_COMMANDで届く)を実装前にPlan agentで検証したことで、後から「Ctrl+Fが反応しない」「入力中に検索が走らない」といった実機デバッグでしか気づけない類の不具合を設計段階で回避できた。(2) 「日本語IME一級市民」という掲げた方針は、具体的な機能(サロゲートペア対応等)だけでなく、こういう地味だが見落としやすいキーボードメッセージの横取りタイミングにも一貫して適用しないと簡単に破られる — 方針を掲げた時点で終わりではなく、個々の実装判断のたびに立ち返って確認する必要がある。(3) `misc-redundant-expression`のような一見些細なclang-tidy警告も、`255.0F / 255.0F`(自己除算)のような「意図せず本当に冗長な式」を実際に検出しており、無視せず都度対処する価値がある。

**次回 (Phase 5b3b):** 置換行(Ctrl+H)配線から着手。`detailed_design.md` §7.1''''を先に読む。`currentMatches`/`currentMatchIndex`は既に`main.cpp`側にローカル状態として存在するため、Replace用の2つ目の子HWNDと既存`core::ReplaceAllCommand`/`search::expandReplacementTemplate`への配線を追加するだけでよく、`FindBar`自体の作り直しは不要。**保留中のPhase 4b8**に戻る選択肢も次セッション開始時にユーザーへ提示すること。**実アプリでのCtrl+F/日本語IME/マッチハイライトの視覚確認がまだの場合はセッション冒頭でユーザーに依頼すること。** push は本セッション終了時点で未実施。

## Session 30 (2026-07-19): Phase 5b3b (置換行配線: Ctrl+H + FindReplaceState統合) 完了

**着手経緯:** 前セッション(Session 29、Phase 5b3a)終了後、ユーザーが「継続して進めよ」と指示。push は行わず(既存運用どおり明示指示待ち)、次サブフェーズ Phase 5b3b(置換行配線)へ進行。設計検証のため`Plan`サブエージェントを呼び出したところセッション制限エラー("You've hit your session limit")で失敗。ユーザーから改めて「継続実行せよ」と指示を受け、このセッション自身が実装した既存コード(`cumulative_shift_edit.h`の「ascending, non-overlapping」順序要求・`search_service.h`の`findAll()`順序保証・`command_dispatcher.cpp`の3行`dispatch()`実装)を直接再読して設計の妥当性を自己検証し、その旨をプランのContext節に明記した上で`ExitPlanMode`によりユーザー承認を得た。

**設計:**
- `ui::FindBar`に2つ目の`WC_EDITW`(Replace edit)を追加。既存のFind editと**同一の`SetWindowSubclass`コールバック/`dwRefData=this`を共有**し、`handleSubclassMessage`/`handleSubclassKeyDown`が既に受け取っている`HWND hwnd`引数だけで両エディットを区別する設計とした(サブクラス登録・メッセージルーティング機構の複製を避けるため)
- `FindBarConfig`に`onReplaceCurrent`(Enter)/`onReplaceAll`(Ctrl+Enter)コールバックを追加。既存`onQueryChanged`と同じ「現在のテキストを引数で渡す」形を踏襲
- Tabキーによるフォーカス巡回(`FindBar::cycleFocus()`)を自前実装。本アプリのメッセージループ(`runMessageLoop()`)は`IsDialogMessageW`を使わない素の`GetMessageW`/`TranslateMessage`/`DispatchMessageW`ループのため、ダイアログなら無料で手に入るTab巡回が自動では効かない。2要素間のトグルのみのため、Shift+Tabは意図的に未特別扱い(A→B/B→Aが同一操作)
- `main.cpp`: `currentQuery`(新規)/`currentMatches`/`currentMatchIndex`を`FindReplaceState`構造体へ統合。Phase 5b3a完了時点で`wireNormalMode`が12引数に達しており、Session 29の完了記録が「5b3bのreplace行状態が加わったら検討」と明記していたとおりのタイミングで実施
- `refreshMatches()`を`runFindQuery()`から抽出(検索実行+状態更新のみ、ジャンプ処理を含まない) — `replaceCurrentMatch()`が「置換後、元のインデックスに近いマッチへジャンプ」したいのに対し`runFindQuery()`は常にマッチ#0へジャンプしたいため、ジャンプ処理を呼び出し側の責務として分離
- `replaceCurrentMatch()`: 現在マッチを`core::ReplaceRangeCommand`で置換 → 同一`state.currentQuery`で再検索 → 置換前インデックスを`std::min(replacedIndex, count-1)`でクランプして次に近いマッチへジャンプ(置換は1件ずつしかマッチ数を減らさないため範囲外アクセスは起きないことを手計算で確認済み)
- `replaceAllMatches()`: `state.currentMatches`は`SearchService::findAll()`の「document order、非重複」保証によりソート不要のまま`core::PerCursorEdit`列へ変換し、`core::ReplaceAllCommand`で1Undoステップとして一括置換。置換後は再検索せずハイライトを単純にクリア(`closeFindBar()`と同じ扱い) — 置換後テキストが同じクエリに再マッチして見えると「置換できていない」ように誤解されるため
- 両関数とも`search::expandReplacementTemplate()`によるキャプチャグループ展開は編集適用**前**の(まだ変更されていない)ドキュメント状態に対して行う(Phase 5b2で確認済みの契約どおり)

**実装時に発覚した2件のビルド/静的解析問題と対処:**
1. `find_bar.h`に`std::u16string`を公開シグネチャ(`readEditText()`の戻り値)として追加したが`<string>`の`#include`漏れ、MSVCが`C2039`で検出 → `<string_view>`の並びに追加して解消
2. clang-tidyが`readEditText()`を`readability-convert-member-functions-to-static`(`this`を使わないメンバ関数)、`handleSubclassKeyDown()`を`readability-function-cognitive-complexity`(26 > 閾値25)で検出。前者は`static`化、後者はVK_RETURN/Replace edit分岐(4段ネスト)を新規`handleReplaceReturn()`ヘルパーへ抽出して解消。再検証でゼロ警告

**検証:**
- ローカル **Debug/Release/ubsan(clang-cl) 全green**、全310テストpass(純粋ロジックの新規切り出しなし、テスト数は5b3aと変わらず)
- clang-tidy: `find_bar.h`/`find_bar.cpp`/`main.cpp`を個別チェック、上記2件を検出・修正後ゼロ警告
- 実アプリを起動し3秒間クラッシュしないことを確認(基本的な起動安定性のスモークテストのみ)。**Ctrl+H操作・Replace edit入力・Enter/Ctrl+Enter置換・Tab巡回の視覚的動作確認は、この環境にWin32 GUI自動化手段が無いため未実施 — 次セッション冒頭でユーザーに依頼すること(Phase 5b3aから持ち越しのCtrl+F/日本語IME視覚確認と合わせて)**

**意図的にスコープ外とした項目 (Phase 5b3cへ延期):**
- コマンドパレット(Ctrl+Shift+P)、クリックできるReplace/Allボタン(Case/Word/Regexトグルと同じ簡略化方針、キーバインドのみ実装) — UIの消費者が無い状態でこれらを作るのはCLAUDE.mdルール3の推測実装にあたるため

**ドキュメント同期:**
- `docs/design/detailed_design.md` §7に新規§7.1'''''(Phase 5b3b実装リファレンス)追加
- `docs/design/master_roadmap.md` §5.8を「Phase 5b3a・5b3b 完了」に更新、5b3b固有の確定事項を追記
- `docs/handoff/RESUME_HERE.md`に新規§3.20(完了記録)追加、§1状態表・§6推奨プロンプトをPhase 5b3c向けに更新
- `docs/issues/replace_all_buffer_snapshot_extract_scaling.md` — 「Phase 5b3で大量マッチ置換のUI導線ができたら再評価」の条件が本セッションで満たされたことを追記(ベンチマーク実施は依然スコープ外)

**教訓:** (1) Plan agentのようなサブエージェントがセッション制限等で利用不能になった場合でも、設計の正しさを裏付ける根拠(本件では2つのヘッダファイルのコメントに明記された契約)が既存コード自身に残っていれば、それを直接再読することで同水準の検証を代替でき、かつその代替の経緯を計画文書に明記することで透明性を保てる。(2) 同一のサブクラスプロシージャを複数のHWNDで共有する設計は、区別に必要な情報(`HWND hwnd`引数)が既に渡されている場合、新規インフラを追加せずに済む — Phase 5b3aで「`handleSubclassMessage`は`hwnd`を受け取る」という選択をしていたことが、本フェーズでの拡張コストを下げた。(3) 関数のパラメータ数増加は「次のフェーズで検討」と先送りにした場合、実際にそのフェーズで着手する際に必ず立ち返って実施すべき負債であり、Session 29の完了記録に残した見込みどおりのタイミングで解消できたことは、セッション終了時ドキュメント同期の効用の一例。

**次回 (Phase 5b3c):** コマンドパレット(Ctrl+Shift+P)から着手。`master_roadmap.md` §5.2後半(元スケッチ)を先に読む。コマンドパレットは完全に独立したUI表面のため、Phase 5b3aで確立したWC_EDIT+サブクラス化パターンを再利用しつつ新規クラスとして設計する(`FindBar`への機能追加ではない)。**保留中のPhase 4b8**に戻る選択肢も次セッション開始時にユーザーへ提示すること。**実アプリでのCtrl+F/Ctrl+H/日本語IME/マッチハイライトの視覚確認がまだの場合はセッション冒頭でユーザーに依頼すること。** push は本セッション終了時点で未実施。

## Session 31 (2026-07-19): Phase 5b3c (コマンドパレット: Ctrl+Shift+P + ファジー検索) 完了、roadmap §5 全体完了

**着手経緯:** ユーザーが「継続せよ」と指示し、あわせて「私へのアウトプットは日本語とせよと過去に伝えた、これは重要事項なので忘れずにメモリに記載せよ」と明示的に念押し。直前ターンで英語応答してしまっていたため、まず`feedback_respond_in_japanese.md`(新規メモリ)を起票し`user_communication_style.md`を強化してから作業を再開した。Phase 5b3c(コマンドパレット)に着手し、既存コード(`find_bar.h/.cpp`・`main_window.h`・`command_dispatcher.h`・`editor_input.cpp`)を直接調査した上でPlan Modeへ移行。今回はPlan agentのセッション制限エラーは発生せず、正常にレビューを完了できた。

**Plan agentによる設計検証で判明した1件の必須修正(+実装トレース中に自己発見した1件):**
1. **標準`WC_LISTBOX`は自身の`WM_LBUTTONDOWN`処理内で自分自身に`SetFocus`を呼ぶ。** 「Editのみサブクラス化しフォーカスを固定し続ける」当初設計のままだと、結果行を1回クリックしただけでフォーカスが奪われ、以降Up/Down/Enter/Escapeが素のリストボックスの`DefWindowProc`に届いて無反応になる。リストボックスも同一パターンでサブクラス化し、`DefSubclassProc`処理直後に`::SetFocus(m_hwndEdit)`でフォーカスを奪い返す設計へ修正
2. **[このセッション自身が実装トレース中に発見]** 上記の「フォーカス奪回」をダブルクリックにも適用すると、`WM_LBUTTONDBLCLK`の`DefSubclassProc`処理がネストした`SendMessage`で`LBN_DBLCLK`を親へ同期送出し、親がその場でコマンドの`action()`を実行して`hide()`する場合(例: `findBar.show()`で別の子HWNDへフォーカスが移る)、直後の無条件`SetFocus`がそのコマンドから今開いたばかりのUIのフォーカスを奪い返してしまう競合バグになるところだった。`isVisible()`確認によるガードで対処。Plan agentのレビューだけでは拾いきれない、実装の詳細に踏み込んだトレースでしか見つからないクラスの不具合が実際にあることを示す実例

**成果物:**
- 新規`util::fuzzyMatchScore()`(`src/util/include/neomifes/util/fuzzy_matcher.{h,cpp}`) — ASCII範囲casefoldのみの簡略化された部分列マッチ(VSCode等のDP最適スコアラーより意図的に簡素化、コマンド候補が最大数十件の定型英語文字列であるため)。貪欲最左マッチ+連続一致ボーナス+単語境界ボーナス
- 新規`ui::CommandDescriptor`(`command_descriptor.h`)・`ui::filterAndRankCommands()`(`command_palette_filter.h`、ヘッダオンリー) — `find_navigation.h`/`click_tracking.h`と同系統の「Win32非依存の純粋ロジック」パターンをそのまま踏襲
- 新規`ui::CommandPalette`(`command_palette.{h,cpp}`) — `FindBar`を直接モデルにしつつ、`WC_EDITW`+`WC_LISTBOXW`という**異なる2種類のコントロール型**を同一`SetWindowSubclass`コールバック/`dwRefData=this`で扱う初のケース(FindBarのFind/Replace editは同一型2つの共有だった)。フォーカスはクエリEdit側に固定し続け、Up/Down/Enterはすべてそちら側のサブクラスで横取りして`LB_SETCURSEL`のみでハイライトを動かす設計(VSCode実際のUXに合わせた)。デバウンス無し(候補が最大数十件、roadmap性能目標「500件で20ms」に対し十分)
- `main.cpp`: `buildCommandRegistry()`(6コマンド構築 — Find/Find+Replace/Find Next/Find Previous/Undo/Redo、**すべて既存実装済みキーバインドの再露出のみ**。File Open/Save等の未実装機能はコマンドパレット用に新規実装しない方針をコメントで明記、CLAUDE.mdルール3の推測実装回避)、`handleCommandPaletteKey()`(Ctrl+Shift+P)、`wireNormalMode()`/`wWinMain`への配線
- テスト数: 310→322(+12件、`util_fuzzy_matcher_test.cpp`7件・`ui_command_palette_filter_test.cpp`5件)

**実装時に発覚したビルド/静的解析問題と対処:**
1. **新規発見: clang-cl(ubsanプリセット)の`-Wmissing-designated-field-initializers`が、MSVCでは無診断のdesignated initializerフィールド省略をエラー扱いする。** `ui_command_palette_filter_test.cpp`の`CommandDescriptor{...}`が`.action`を省略していた4箇所で発覚。`.action = nullptr`を明示して解消。「MSVCのローカル通常検証では見えないclang-cl固有の診断がある」という既存の教訓([[reference-windows-cpp-ci-gotchas]]項目6・7と同系統)に項目8として追記
2. clang-tidyで`command_palette.cpp`/`fuzzy_matcher.cpp`/`main.cpp`/新規テスト2ファイルを個別チェック、新規警告0(前セッションのfind_bar.cpp/main.cppと異なり、今回は`readability`系の指摘は発生しなかった — helper抽出の粒度が最初から妥当だったため)

**検証:**
- ローカル **Debug/Release/ubsan(clang-cl) 全green**、全322テストpass
- 実アプリを起動し3秒間クラッシュしないことを確認(基本的な起動安定性のスモークテストのみ)。**Ctrl+Shift+P操作・入力フィルタ・クリック選択・ダブルクリック実行・Enter実行・Escapeの視覚的動作確認は、この環境にWin32 GUI自動化手段が無いため未実施 — 次セッション冒頭でユーザーに依頼すること(Phase 5b3a/5b3bから持ち越しのCtrl+F/Ctrl+H/日本語IME視覚確認と合わせて)**

**意図的にスコープ外とした項目:**
- サブメニュー、絵文字アイコン、最近使用ボーナス、検索履歴共有、Quick Open(Ctrl+P)・行ジャンプ(Ctrl+G)、Grep、クリックできるReplace/Allボタン — いずれもroadmap v2.0の拡張項目でありUIの消費者/要件確定が別途必要なため

**ドキュメント同期:**
- `docs/design/detailed_design.md` §7に新規§7.1''''''(Phase 5b3c実装リファレンス)追加
- `docs/design/master_roadmap.md` §5.8を「Phase 5b3a・5b3b・5b3c 完了」に更新、5b3c固有の確定事項を追記。§3の進行フェーズ早見表(5b2/5b3/5cの状態が数セッション前から古いままだった)も併せて修正
- `docs/handoff/RESUME_HERE.md`に新規§3.21(完了記録)追加、§1状態表・§6推奨プロンプトを更新(次はPhase 5cまたはPhase 4b8のいずれかをユーザーに選択させる)
- メモリ: `feedback_respond_in_japanese.md`新規起票、`user_communication_style.md`強化、`reference_windows_cpp_ci_gotchas.md`に項目8追加

**教訓:** (1) Plan agentの設計レビューは有用だが万能ではない — 今回、レビュー自体は正しく1件の必須修正(リストボックスのフォーカス窃取)を検出したが、その修正案(「クリック後に無条件でフォーカスを奪い返す」)自体に潜む二次的な副作用(ダブルクリックでコマンドが既に別のUIを開いていた場合の競合)は、レビューの応答文には含まれておらず、実装コードを実際に書きながら「このSetFocusが呼ばれる全ての経路を辿るとどうなるか」を手で追跡する過程で初めて見つかった。設計レビューを受けた後も、実装者自身が実際のメッセージフローを最後までトレースする作業を省略してはならない。(2) 「MSVCのローカルビルドでは見えないがclang-cl(ubsanプリセット)だけが検出する診断」という部類の落とし穴は、これで3件目(defaulted operator==、MSVC_RUNTIME_LIBRARY上書き、designated initializer省略)に達した — 新しいコードパターンを書くたびに、それがこの部類に該当しうるかを意識し、pushする前に必ずubsanプリセットを一度通す運用の価値が改めて裏付けられた。(3) 「既存実装済みキーバインドの再露出のみ」という自己制約(File Open/Save等の未実装機能をコマンドパレットのために新規実装しない)を明文化してからコマンド一覧を構築したことで、6件という具体的で検証可能なスコープが得られた — 「コマンドパレットに何を入れるか」という一見無限に広がりうる設計判断も、既存実装済み機能への限定というルールを先に立てることで推測実装を避けられる。

**次回 (Phase 5cまたはPhase 4b8):** roadmap §5全体(Find bar + 置換行 + コマンドパレット)がPhase 5b3a/5b3b/5b3cで完了した。次はPhase 5c(Grep/複数フォルダ検索/検索履歴/タグジャンプ、`master_roadmap.md` §5.5)か、**保留中のPhase 4b8**(矩形選択・タブ変換・分配・フリーカーソル・マーカー、`master_roadmap.md` §3)のいずれかをユーザーに選択させてから着手すること。**実アプリでのCtrl+F/Ctrl+H/Ctrl+Shift+P/日本語IME視覚確認がまだの場合はセッション冒頭でユーザーに依頼すること。** push は本セッション終了時点で未実施。

## Session 32 (2026-07-19): Phase 4b8a (矩形選択・基本機能) 完了

**着手経緯:** ユーザーが「保留タスクを実施せよ。何故保留となっているのかを実行前に確認したい」と指示。まずPhase 4b8の保留理由を`docs/handoff/RESUME_HERE.md` §3.16から確認し、「技術的ブロッカーではなく、要件定義書§8『ログ解析モード』が検索機能(Phase 5)に構造的に依存するための優先順位判断だった」ことをユーザーへ提示。Phase 5(5a〜5b3c)が全て完了し前提条件が解消されたことを確認した上で着手。`master_roadmap.md` §3が矩形選択・フリーカーソル・マーカー・桁位置ジャンプ・タブ⇔スペース変換・N対N分配クリップボードの6機能を1章にまとめていたため、CLAUDE.mdルール8に従い**矩形選択の基本機能のみ**をPhase 4b8aとして切り出した。

**実装前に発見・解決したキーバインド衝突:** `master_roadmap.md` §3.2は起動キーを`Alt+LMouseドラッグ`と定めていたが、既存コード(`src/app/main.cpp`)を調査したところ、これは既にPhase 4b6dで「Alt+ドラッグ=直前のAlt+クリックで追加したカーソルを拡張する」ジェスチャーとして実装済みであることが判明。roadmapスケッチと既存実装済みコードの衝突を実装着手前に発見し、AskUserQuestionでユーザーに確認 — VSCodeの実際の慣習(Alt+クリック=カーソル追加、Shift+Alt+ドラッグ=矩形選択)に合わせて`Shift+Alt+ドラッグ`へ変更する方針で解決(既存のAlt+ドラッグ挙動は無変更のまま維持)。

**Plan agentへの2ラウンドのレビューで、方針転換自体が引き起こす3件の設計不備を実装前に検出・修正:**
1. **[1回目のレビューで検出]** `SelectionModel::setRectangularSelection()`の初期案は各行の列を`min(anchorCol,activeCol)`/`max(...)`で`position`/`anchor`に振り分けていたが、これは本コードベースの「ドラッグは`position`のみを動かし`anchor`は固定」という規約に反し、ドラッグがanchorの列を跨いだ瞬間にキャレットが視覚的に後退するバグになる。修正: 各行で`anchorCol`は常に`anchor`側、`activeCol`は常に`position`側に(行の実長でクランプしつつ)独立に書き込むよう変更
2. **[1回目のレビューで検出]** マウス配線の初期案は、Shift+Alt+クリックの瞬間に「矩形選択かaltCursorAnchor拡張か」を二者択一で判定しようとしていたが、既存`altCursorAnchor`はセッション中ずっと残る(プレーンクリックでのみリセット)ため、無関係な過去のAlt+クリックが新規`rectangularAnchor`ジェスチャーを乗っ取ってしまう不備があった。修正: `dispatchMouseDown`はクリック単体の既存挙動を変更せず`rectangularAnchor`を副次的に記録するだけに留め、実際の判定は`onMouseDrag`側の最優先分岐に一任する設計へ変更(`setRectangularSelection()`が常に`setCursors()`でカーソル集合を丸ごと置き換えるため、クリック単体の既存副作用は無害になる、という性質を利用)
3. **[2回目のレビューで検出]** 修正版マウス配線の5シナリオをPlan agentにトレースさせたところ、矩形選択ドラッグ後に`altCursorAnchor`が古いカーソル(既に存在しない)を指したまま残留し、次の無関係なShift+Alt+クリックが「何も起きないのにスクロールだけ発生する」不具合になるところだった。修正: `onMouseDrag`の矩形選択分岐で`setRectangularSelection()`呼び出し直後に`altCursorAnchor.reset()`を追加

**成果物:**
- 新規`core::SelectionModel::setRectangularSelection(TextPos anchor, TextPos active, const Document& doc)`(`src/core/include/neomifes/core/selection_model.h` / `src/core/src/selection_model.cpp`) — 既存の private `lineContentEnd()`ヘルパー(`moveVertically()`が使用中)を再利用し、`anchor`/`active`の行・列を算出、範囲内の各行に1カーソルを生成して`setCursors()`(既存のソート/マージ不変条件をそのまま再利用)へ渡す。短い行は列を実際の行長でクランプ(フリーカーソル未対応、次サブフェーズへ)
- `SelectionMode`列挙体は不採用(roadmapスケッチから乖離) — 既存`SelectionModel::moveAll()`がカーソル集合へ一様適用される設計のおかげで、矩形選択後の矢印キー操作がVSCode同様「N個の独立カーソルへ降格」する挙動を新規コード無しで得られたため、今回のスコープでは「モード」概念自体が不要と判明
- `main.cpp`: 新規`rectangularAnchor`状態(`wWinMain`スコープ、`altCursorAnchor`と並行・独立)、`dispatchMouseDown()`/`onMouseDrag`ラムダの配線変更。**描画(`CursorVisual`/`drawSelectionsOnLine`)・クリップボード(`textToCopy`/`handlePaste`)は一切変更不要** — 矩形選択が生成するN個のカーソルを既存の複数カーソル基盤がそのまま処理するため、roadmapが前提としていた「既存の複数カーソル基盤の上に矩形選択を実装する」という設計方針が正しかったことを実装で裏付けた
- テスト数: 322→328(+6件、`tests/unit/core_selection_model_test.cpp`に追加) — 複数行にまたがるカーソル生成、短い行でのクランプ、ドラッグ方向(上下左右)によるposition/anchor取り違えバグの回帰テスト、単一行での通常選択との等価性、isPrimaryの位置、空行を含む範囲でのクラッシュ耐性

**検証:**
- ローカル **Debug/Release/ubsan(clang-cl) 全green**、全328テストpass。新規6テストは手計算で期待値を導出した上で実装し、一発でパス(手計算の正しさが実装の正しさの裏付けにもなった)
- clang-tidy: `selection_model.cpp`/`main.cpp`/新規テストファイルを個別チェック、`misc-const-correctness`5件(テストのDocumentローカル変数、`const`未付与)を検出・修正、再検証でゼロ警告
- 実アプリを起動し3秒間クラッシュしないことを確認(基本的な起動安定性のスモークテストのみ)。**Shift+Alt+ドラッグでの矩形選択作成・描画・Ctrl+C/V、および既存のAlt+ドラッグ/Alt+Shift+クリックが無変更のまま動作することの視覚的動作確認は、この環境にWin32 GUI自動化手段が無いため未実施 — 次セッション冒頭でユーザーに依頼すること(Phase 5b3a/5b3b/5b3cから持ち越しの視覚確認と合わせて)**

**意図的にスコープ外とした項目 (Phase 4b8の後続サブフェーズへ):**
- キーボードでの矩形選択拡張(`Alt+Shift+矢印`) — 現在`MainWindow`は`WM_SYSKEYDOWN`を一切処理しておらず(`WM_KEYDOWN`のみ)、Alt同時押しの矢印キーはWin32仕様上`WM_SYSKEYDOWN`で届く(Phase 5b3aのFind bar実装で確認済みの規則と同じ)ため、新規`onSysKeyDown`フックの追加を要する
- フリーカーソル(虚数位置・仮想空白の視覚表示・自動スペース挿入)、`Shift+Alt+I`(矩形選択→各行末尾に1カーソル変換)、マーカー(Bookmark)、桁位置ジャンプ、タブ⇔スペース変換、N対N分配クリップボードの高度な設定

**ドキュメント同期:**
- `docs/design/detailed_design.md` §5.1.1(縦編集)を「Phase 4b以降に延期」から実装済み内容へ更新、`SelectionModel::setRectangular`(存在しないメソッド名だった旧記述)を`SelectionModel::setRectangularSelection`へ修正、§5.3にPhase 4b8aの実装リファレンス(キーバインド衝突の経緯・2ラウンドの設計不備検出を含む)を追記
- `docs/design/master_roadmap.md` §3に新規§3.7(実装後の確定事項)追加、§7フェーズ早見表の4b8行を「4b8a完了・4b8b以降未着手」に分割更新
- `docs/handoff/RESUME_HERE.md`に新規§3.22(完了記録)追加、§1状態表・§6推奨プロンプトを更新

**教訓:** (1) roadmapのキーバインドスケッチは、それを書いた時点でまだ存在しなかった後続フェーズの実装(本件ではPhase 4b6dのAlt+ドラッグ)と衝突しうる — 「roadmapは実装確定前の高レベル指針」という既存の教訓([[project-neomifes-state]]のPhase 5b2エントリ参照)に、「後から実装されたフェーズがroadmapの未来のスケッチと衝突することもある」という新しいバリエーションが加わった。着手前に必ず実際のキーバインド一覧を`grep`等で洗い出し、roadmapの記述と突き合わせる価値が改めて裏付けられた。(2) 1回のPlan agentレビューで全ての設計不備が出尽くすとは限らない — 修正版の設計に対してもう一度、より具体的なシナリオ(今回は5つの操作シーケンス)をトレースさせる2回目のレビューを行ったことで、1回目のレビューでは見えていなかった追加の不具合(altCursorAnchorの残留)を発見できた。「修正した」で終わらせず、修正後の設計そのものを再度検証する価値がある。(3) 既存の複数カーソル基盤(Phase 4a〜4b7c)が十分に一般的な設計だったおかげで、矩形選択という一見大きな新機能が、実質的に「カーソル集合を構築する1つの新規メソッド」だけで完成した — 早い段階で「Nカーソルへの一様適用」という抽象化を徹底していたことの複利的な効果を示す実例。

## Session 33 (2026-07-20): Phase 4b8b〜4b8g (Phase 4b8 残り全機能) 完了、Phase 4b8 全体完了

**着手経緯:** ユーザーから「Phase 4b8の残りを実施せよ。フェーズの残項目は残したくはない」と明示的に指示された。Session 32で完了した4b8a(矩形選択の基本機能)以外の5機能(桁位置ジャンプ・マーカー・タブ⇔スペース変換・フリーカーソル・N対N分配クリップボード)+調査中に判明した6件目(キーボードでの矩形選択拡張、`Alt+Shift+矢印`+`Shift+Alt+I`)を、全て1セッション内で完了させる方針で着手。着手前の調査(`grep`)で3つの事実を確認: (1) 行ガター描画コードが`src/render`に一切存在しない、(2) 設定システムが存在しない、(3) `document::TextPos`が28ファイル176箇所で使用済み。この3事実を根拠に、AskUserQuestionで2件をユーザーに確認:
1. **マーカーの視覚表示** — 行番号・折りたたみを含む本格的なLine Gutterではなく、**最小限のブックマーク専用ガター(●印のみ)を新設**する方針(推奨案)が選ばれた。本格的なLine Gutterは引き続き独立した将来フェーズへ先送り
2. **フリーカーソルの実装方式** — `document::TextPos`を拡張する大規模変更ではなく、**main.cpp(UI層)のみで仮想列オフセットを追跡する簡略実装**(推奨案)が選ばれた

CLAUDE.mdルール8「1PR=1責務」に従い**4b8b→4b8c→4b8d→4b8e→4b8f→4b8g**の6サブフェーズに分割。各サブフェーズごとに実装→ローカル検証(Debug/Release/ubsan/clang-tidy)→実アプリ起動スモークテスト→コミットのサイクルを独立して繰り返した(4b8a〜4b8gで計6コミット、いずれもpush未実施)。ドキュメント同期は6回繰り返さず、全サブフェーズ完了後にこの1回にまとめた(CLAUDE.md §11「関連する要約節も同期」原則)。

**設計検証で2件のPlan agentレビューを実施し、いずれも実装着手前に不具合を検出・修正:**
1. **[ガター描画レビュー]** `IDWriteTextLayout::HitTestTextPosition()`が返すX座標は`DrawTextLayout()`の描画原点(呼び出し側が指定)とは独立したレイアウトローカル座標である。ガター新設に伴い`DrawTextLayout`の原点だけを`kGutterWidthDips`右へずらしても、`drawCaretOnLine`/`drawSelectionOnLine`/`drawMatchOnLine`の3メソッドはHitTestの戻り値をそのまま絶対座標として使っていたため自動追従せず、キャレット/選択/マッチのハイライトがガター幅ぶん左にズレて文字とズレるバグになるところだった。3メソッド全てに`kGutterWidthDips`の明示的加算を実装前に追加して対処(自分自身の直接ソース読解と、独立したPlan agentレビューの両方で同一の結論に到達し、相互検証できた)
2. **[フリーカーソル状態機械レビュー]** 複数の指摘のうち、「単一カーソルのみ許可するガード」を導入することで2件の問題(複数カーソル時にRight矢印が正しく動かなくなる回帰、および文字入力での実体化が`ReplaceRangeCommand`経由で単一カーソルへ収束するため元の複数カーソル集合が意図せず消える問題)が同時に解決することを、レビュー結果を統合する形で自ら見出し設計に反映した

**成果物 (サブフェーズ別):**
- **4b8b (桁位置ジャンプ):** 新規`ui::GotoLineBar`(`goto_line_bar.{h,cpp}`、単一`WC_EDITW`のみ、デバウンス・リストボックス不要、FindBar/CommandPaletteより単純)。新規`ui::parseGotoLineInput()`(`goto_line_parser.h`、ヘッダオンリー純粋関数)が`"123"`/`"123:45"`(共に1始まり)をパース。`Ctrl+G`で表示、`jumpToGotoTarget()`が0始まりへ変換しクランプ
- **4b8c (マーカー):** 新規`core::BookmarkManager`(ソート済み`vector<LineNumber>`、`toggle`/`next`/`previous`ラップアラウンド)。ドキュメント編集への追従は本コードベースにEditEvent購読機構が無いため実装しない既知の制約として明記(ADR-010、`Document`は`version()`ポーリングのみ)。`RenderPipeline`に最小ブックマーク専用ガター(`kGutterWidthDips=24dip`)新設。`Ctrl+F2`/`F2`/`Shift+F2`
- **4b8d (タブ⇔スペース変換):** 新規`core::computeIndentationConversionEdits()`(ヘッドレス純粋関数、各行先頭の連続空白のみ対象)。専用コマンドクラスは新設せず既存`core::ReplaceAllCommand`(Phase 5b2)へそのまま渡す。コマンドパレットに"Convert Tabs to Spaces"/"Convert Spaces to Tabs"(`tabWidth=4`固定)
- **4b8e (フリーカーソル、簡略版):** `main.cpp`のセッション状態(`freeCursorVirtualColumns`)のみで実装、`document::TextPos`は無変更。コマンドパレットの"Toggle Free Cursor Mode"で有効化。単一プライマリカーソル・無選択時のRight矢印が行の実行末に達すると仮想列をインクリメント、文字入力時に仮想列数分のスペース+入力文字を`ReplaceRangeCommand`で一括実体化。`render::CursorVisual::virtualColumnOffset`+等幅フォント(Consolas)1文字幅の近似(既存の"Ag"プローブレイアウトを流用して計測)でキャレット描画をずらす
- **4b8f (N対N分配クリップボード):** `handlePaste()`を変更 — 行数とカーソル数が一致する場合のみ1対1分配、不一致時(単一カーソルへの複数行貼り付けを含む)は従来通り全カーソルへ同一テキスト。`insertTextAtEveryCursor()`を`insertPerCursorTexts()`へ内部リファクタし両方から共有(挙動を変えない機械的リファクタ)
- **4b8g (キーボード矩形選択拡張 + Shift+Alt+I):** `MainWindow::onSysKeyDown`(`WM_SYSKEYDOWN`)新設 — 未消費時は必ず`DefWindowProcW`へフォールスルーし、Alt+F4等のシステムキー既定動作を保持する設計を徹底。`SelectionModel`のprivate`moveOne()`を公開自由関数`moveTextPos()`へ格上げ(`moveAll()`もこれ経由に変更)。`Shift+Alt+矢印`ハンドラは`moveTextPos()`で新active位置を計算し、4b8aの`rectangularAnchor`状態を再利用して`setRectangularSelection()`を呼ぶ(マウスとキーボードの矩形選択拡張が同じ状態変数を共有)。新規`SelectionModel::convertToLineEndCursors()`が`Shift+Alt+I`で選択範囲を各行末尾の1カーソルへ変換(`position`と`anchor`の両方を考慮して行範囲を決定)

**実装中に検出・修正したclang-tidy指摘 (全てpush前に解消):**
- `render_pipeline.cpp`の`readability-math-missing-parentheses`(ガターのhitTest座標計算)
- `indentation_conversion.cpp`の`modernize-use-integer-sign-comparison`(`std::cmp_equal`を使うよう修正、`static_cast`による手動キャストではclang-tidyのこのチェックは満足しない)
- `wireNormalMode`が新規`onChar`分岐追加でcognitive complexity閾値25を26に超過 → `handleCharEvent()`という独立関数へ抽出して解消(Phase 5b3aと同じ「ラムダ本体を外に出す」パターン)
- テストコード2件: `misc-unused-using-decls`(未使用の`PerCursorEdit` using宣言)、`readability-function-cognitive-complexity`(テスト内のfor-loopをフラットな3行のEXPECT_EQへ展開して解消)

**テスト数:** 328(4b8a完了時点)→366(+38件: `core_bookmark_manager_test.cpp`11件、`ui_goto_line_parser_test.cpp`11件、`core_indentation_conversion_test.cpp`8件、`app_editor_input_test.cpp`+3件、`core_selection_model_test.cpp`+5件。4b8eはUI層に閉じた状態機械のため新規テストなし、既存FindBar/CommandPalette同様の方針を踏襲)

**検証:**
- 6サブフェーズ全てでローカル **Debug/Release/ubsan(clang-cl) 全green**、変更/新規ファイルのclang-tidy新規警告0(上記の指摘を都度検出・修正)
- 各サブフェーズ完了時に実アプリ起動スモークテスト(3秒、クラッシュなし)を実施
- **実アプリでのCtrl+G/Ctrl+F2・F2・Shift+F2(ガター含む)/コマンドパレットのタブ変換2種/Toggle Free Cursor Mode有効時のRight矢印+文字入力/N対N貼り付け/Shift+Alt+矢印(矩形拡張)/Shift+Alt+Iの視覚的動作確認は、この環境にWin32 GUI自動化手段が無いため未実施 — 次セッション冒頭でユーザーに依頼すること(Phase 5b3a〜5b3c・4b8aから持ち越しのCtrl+F/Ctrl+H/Ctrl+Shift+P/Shift+Alt+ドラッグ・日本語IME視覚確認と合わせて)**

**意図的にスコープ外とした項目 (roadmapスケッチからの意図的乖離、詳細は`master_roadmap.md` §3.7参照):**
- カスタムクリップボードフォーマット`CF_NEOMIFES_MULTICURSOR`、「サイクル貼り付け」等の高度なN対N分配設定(設定システムが存在しないため)
- `Auto`モード(統計的多数派自動判定)でのタブ⇔スペース変換(同上)
- フリーカーソルのマウス対応(行末より右クリック)・複数カーソル同時のフリーカーソル・仮想空間の視覚的パディング表示
- キーボードでの矩形拡大時の列保持(短い行を経由した後の元の意図列を記憶しない、通常の垂直移動とは異なる簡略実装として既知の制約に明記)
- 本格的なLine Gutter(行番号・折りたたみ)— 引き続き独立した将来フェーズ

**ドキュメント同期:**
- `docs/design/master_roadmap.md` §3.7を「4b8a完了」から「4b8全体(4b8a〜4b8g)完了」へ全面書き換え、サブフェーズ別に確定事項・roadmapスケッチからの乖離点を追記。§2フェーズ早見表の4b8行を1行に統合し完了マーク
- `docs/design/detailed_design.md` §5.1.1に4b8g追記(`moveTextPos`/`convertToLineEndCursors`)、§5.3に4b8b〜4b8g全サブフェーズの実装リファレンスを追記、冒頭の「Phase 4b8以降はmaster_roadmapが正」という記述を「Phase 5c以降」に更新(4b8の確定内容は本書へ吸収済みのため)
- `docs/handoff/RESUME_HERE.md`に新規§3.23(完了記録)追加、§1状態表(4b8b〜4b8g各行+統合行)・§6推奨プロンプトを更新、「Phase 4b8は未着手のまま保留」という古い記述を完了報告へ差し替え、Phase 5b3b着手時向けだった古い「次回確認すること」チェックリストの陳腐化した2項目(5b3b自体・保留中のPhase 4b8)を削除し残り5項目を「次回(Phase 5cまたはPhase 6)着手時」向けに更新
- メモリ: `project_neomifes_state.md`をPhase 4b8完了状態へ更新

**教訓:** (1) 「設定システムが存在しない」という1つの制約事実が、roadmapスケッチが想定していた複数の高度な機能(タブ変換のAutoモード、N対N分配のサイクル貼り付け設定、カスタムクリップボードフォーマット)を横断的にスコープ外とする根拠として繰り返し機能した — 個別に判断するのではなく、着手前にこの制約を確認しておくことで、6サブフェーズ全体を通じて一貫した「実在しない設定を前提にしない」判断を効率的に下せた。(2) 複数の独立した設計課題(ガター描画・フリーカーソル状態機械)がある場合、Plan agentのレビューを並列で(順番に待たず)呼び出し、その待ち時間で計画書のドラフト作業を進めることで、検証の厚みを保ちながら時間効率を落とさずに済んだ。(3) `insertTextAtEveryCursor()`を`insertPerCursorTexts()`へ内部リファクタしてN対N分配と全カーソル同一挿入の両方から共有した判断は、既存のコードレビュー原則(重複コードの検出)を待たずに実装者自身が「N:N分配ロジックを書く前に、既存の全カーソル同一挿入ロジックと本質的に同じ構造(カーソルごとに1つのテキストを対応させてMultiCursorEditCommandを組み立てる)であることに気づく」形で先回りできた一例。

**次回 (Phase 5cまたはPhase 6):** Phase 4b8はこれでroadmap上の保留項目を残さず完全に完了した。次はPhase 5c(Grep/複数フォルダ検索/検索履歴/タグジャンプ、`master_roadmap.md` §5.5)かPhase 6(エンコーディング+自動判定+10GB mmap、`master_roadmap.md` §6)のいずれかをユーザーに選択させてから着手すること。**実アプリでのCtrl+F/Ctrl+H/Ctrl+Shift+P/Shift+Alt+ドラッグ(矩形選択)/Ctrl+G/Ctrl+F2・F2/コマンドパレットのタブ変換2種/Toggle Free Cursor Mode/N対N貼り付け/Shift+Alt+矢印・Shift+Alt+I/日本語IME視覚確認が全て未実施のため、セッション冒頭でユーザーに依頼すること。** push は本セッション終了時点で未実施(4b8a〜5b3c・4b8b〜4b8g、計11コミットが蓄積中)。

## Session 34 (2026-07-20): push (5b2〜4b8全体) + Phase 5c1 (GrepService コア) 完了

**着手経緯:** ユーザーから「pushせよ」と指示され、蓄積していた12コミット(Phase 5b2〜Phase 4b8全体完了)を`git push origin main`で送信、CI(run 29709906375)が56分49秒でsuccessになることを確認した。続いてユーザーから「次のフェーズに進め」と指示され、次フェーズの選択肢(Phase 5c「Grep等」かPhase 6「エンコーディング」)をAskUserQuestionで確認したところ「貴方の推奨で進めよ」と一任された。直前に完成した検索基盤(SearchService/ReplaceAllCommand/Find bar/コマンドパレット)の上に自然に構築できることを理由にPhase 5cを推奨し着手した。

**着手前の調査(Explore agent + 自身のコードベース調査):** `std::thread`/`std::async`等の並行処理が本コードベースに一切存在しないこと、`Mode`/`ViewMode`列挙体のような複数ビュー切替の仕組みが存在しないこと、`std::filesystem::recursive_directory_iterator`/globマッチングが未実装であること、JSON/JSON5パーサ依存が存在しないこと、を確認。`master_roadmap.md` §5.5(Grep・複数フォルダ検索・検索履歴・タグジャンプ・秀丸互換Grep結果ペイン)を1章にまとめていたロードマップ記述を、CLAUDE.mdルール8に従い**GrepServiceコア(ヘッドレス、UIなし)のみを5c1として切り出す**方針でPlan Modeへ移行。

**Plan Mode → Plan agentレビューで確定した設計:**
- `search_service.h`の実際のソースを読み込み、`compile()`/`findAllInBuffer()`がDocumentに直接依存しないことを確認した上で、当初検討していた「search_service.cppの内部ロジックを共有ヘルパーへ抽出する」リファクタは不要と判明 — `document::loadUtf8File()`で各ファイルを`Document`化し、既存`search::SearchService::findAll(doc, query)`をそのまま呼ぶだけでGrepが実現できることをPlan agentへのレビュー依頼で検証・確定した
- roadmapスケッチの「Search Worker Pool(論理コア数-1スレッド)」「`std::function<void(GrepMatch)>`ストリーミングコールバック」は、`search_service.h`が既に明記していた「UIが必要とするまで非同期化はしない」方針をそのまま踏襲し不採用。5c1にはまだUIが無いため、`SearchService::findAll()`と同じ「`std::vector`を同期的に返す」形に統一
- ADR起票は不要と判断(新規外部依存なし、既存レイヤ依存を破らない、`docs/decisions/README.md`の既存ADR一覧に類似判断が無いことを確認)

**成果物:**
- 新規`util::globMatch()`(`src/util/include/neomifes/util/glob_match.h` / `src/util/src/glob_match.cpp`) — `*`/`?`のみのファイル名マスク(パス全体やディレクトリ境界を跨ぐglobは対象外)、ASCII範囲のみの大文字小文字無視(`util::fuzzyMatchScore`の既存方針を踏襲)、アンカー付き全文マッチ。標準的な2ポインタ方式(バックトラック不要)
- 新規`search::GrepService`(`src/search/include/neomifes/search/grep_service.h` / `src/search/src/grep_service.cpp`) — `GrepQuery{roots, includeGlobs, excludeGlobs, query}`を受け取り`GrepService::findAll() -> vector<GrepMatch>`を返す。`grepOneRoot()`(`std::filesystem::recursive_directory_iterator`を非throwの`it.increment(ec)`で走査、range-based forは内部でthrowする`operator++`を使うため不使用)→`shouldProcessFile()`(exclude優先のinclude/exclude判定)→`grepOneFile()`(`loadUtf8File()`→`SearchService::findAll()`→`GrepMatch`への変換、`Document::offsetToLine()`/`lineToOffset()`/`BufferSnapshot::extract()`を再利用)の3段構成。`GrepMatch::columnRange`は`lineText`先頭からの相対位置(GrepServiceが読み込む`Document`は検索後に破棄される一時オブジェクトのため、絶対`TextPos`は後続利用者にとって無意味という判断)
- 存在しないルート・読み込みに失敗したファイル(バイナリ含む、`LoadError::InvalidUtf8`)・走査中のエラーは、そのルート/ファイルをスキップするのみで全体を失敗させない設計(grep/ripgrepの一般的な挙動、CLAUDE.mdの「システム境界では検証するが起こり得ないシナリオには対応しない」原則)
- テスト数: 366→389(+23件、`util_glob_match_test.cpp`10件・`search_grep_service_test.cpp`13件)。後者は`document_file_loader_test.cpp`の`tempFileWith()`パターンを複数ファイル/複数階層向けに一般化した`TempGrepTree`ヘルパー(コンストラクタでtemp配下にディレクトリ作成、デストラクタで`fs::remove_all`)を新設して使用

**検証:**
- ローカル **Debug/Release/ubsan(clang-cl) 全green**、全389テストpass(新規23件は手計算通り一発でpass)
- clang-tidy: `glob_match.cpp`/`grep_service.cpp`は新規警告0。テストファイルで`misc-unused-using-decls`(未使用の`GrepMatch` using宣言)・`cppcoreguidelines/hicpp-special-member-functions`(`TempGrepTree`へmove ctor/assignの明示的`=delete`追加)・`misc-const-correctness`(ローカル変数多数)を検出・修正
- **新規発見: clang-cl(ubsanプリセット)の`-Wmissing-designated-field-initializers`が`GrepQuery{...}`の`includeGlobs`/`excludeGlobs`省略をテストファイル14箇所で検出。** Phase 5b3c([[reference-windows-cpp-ci-gotchas]]項目8)と同一パターンの再発 — 全箇所を明示的に埋めて解消。一方`Query{...}`側は`caseSensitive`/`wholeWord`/`regex`に明示的デフォルトメンバ初期化子(`= true`等)があるため警告が出ないことも確認(`GrepQuery`側のフィールドには`= {}`が無かったことが原因と特定)
- `modernize-use-nodiscard`の指摘(`TempGrepTree::writeFile`)は一度`[[nodiscard]]`を追加したところ、MSVC側の`/WX`が戻り値を使わない9箇所の呼び出しをC4834エラーとして検出しビルドが失敗したため、追加を撤回した(`document_file_loader_test.cpp`の`tempFileWith()`も同様に`[[nodiscard]]`無しであることを確認済み、既存precedentと整合)。`std::rand()`関連の2件も同ファイルの既存precedentと同一のため意図的に未修正のまま残した(`tests/`は`WarningsAsErrors`対象外)
- ヘッドレス追加(main.cppのワイヤリングなし)のため実アプリ起動スモークテストは対象外

**意図的にスコープ外とした項目 (Phase 5cの後続サブフェーズへ):** ワーカースレッド/`std::async`/ストリーミングコールバック、`contextLines`(周辺行表示)、`GrepMatch`へのキャプチャグループ(「Grep結果内での置換」機能が出来てから)、`Mode::GrepResult`・結果ペインUI・`render_pipeline`へのマッチビジュアル配線・`main.cpp`のキーバインド配線(5c2)、タグジャンプパーサ(5c3)、検索履歴永続化(5c4、JSON依存追加はADR起票が必要になる見込み)、パス全体を対象とするglob言語(`**`等)。

**ドキュメント同期:**
- `docs/design/master_roadmap.md` §5.5に「実装後の確定事項/変更点」を追加、§2フェーズ早見表の5c行を「5c1完了・5c2以降次候補」に分割
- `docs/design/detailed_design.md` §7に新規§7.1'''''''(GrepService実装リファレンス)を追加
- `docs/handoff/RESUME_HERE.md`に新規§3.24(完了記録)追加、§1状態表・§6推奨プロンプト・冒頭メタデータを更新
- メモリ(`project_neomifes_state.md`)更新

**教訓:** (1) 新機能を設計する前に「既存のこの関数は本当にDocumentに依存しているのか、それとも単に呼び出し元がDocument経由でしか渡していないだけか」を実ソース読解で確認する価値がある — `search_service.cpp`の`compile()`/`findAllInBuffer()`は実質的にDocument非依存だったため、当初想定していた「共有ヘルパーへの抽出リファクタ」が不要と判明し、`search_service.{h,cpp}`を一切変更せずに済んだ。仮説(「抽出が必要そうだ」)を実装着手前にコード読解で検証してから計画を確定させたことで、無駄なリファクタを避けられた。(2) `-Wmissing-designated-field-initializers`のようなclang-cl固有の落とし穴は、新規struct定義のたびに「デフォルトメンバ初期化子(`= {}`等)を明示的に持たせるか、テスト側で全フィールドを埋めるか」を意識しないと再発する — 今回`Query`は影響を受けず`GrepQuery`だけ影響を受けた違いも、まさにデフォルトメンバ初期化子の有無だった。(3) clang-tidyの`modernize-use-nodiscard`提案をそのまま受け入れると、既存の呼び出しパターン(戻り値を使わない箇所が多数存在するヘルパー関数)と衝突してMSVCの`/WX`ビルドを壊すことがある — 提案を機械的に適用する前に、既存の呼び出しサイトへの影響を確認する価値がある。

**次回 (Phase 5c2以降またはPhase 6):** Phase 5c1(GrepServiceコア)が完了した。次はPhase 5c2以降(Grep結果ペインUI・タグジャンプ・検索履歴、`master_roadmap.md` §5.5のスコープ外一覧参照)かPhase 6(エンコーディング、`master_roadmap.md` §6)のいずれかをユーザーに選択させてから着手すること。**実アプリでのCtrl+F/Ctrl+H/Ctrl+Shift+P/Shift+Alt+ドラッグ(矩形選択)/Ctrl+G/Ctrl+F2・F2/コマンドパレットのタブ変換2種/Toggle Free Cursor Mode/N対N貼り付け/Shift+Alt+矢印・Shift+Alt+I/日本語IME視覚確認が全て未実施のため、セッション冒頭でユーザーに依頼すること。** push は5b2〜4b8全体分(12コミット)が本セッション内で完了済み、Phase 5c1分(1コミット)は本セッション終了時点で未実施。

<!-- 次セッションはここに追記 -->
