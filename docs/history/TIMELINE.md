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

<!-- 次セッションはここに追記 -->
