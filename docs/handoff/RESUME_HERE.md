# NeoMIFES — 次回セッション再開ガイド

> **最終更新:** 2026-07-15 (Phase 2b3 Step 1 完了時)
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
| **Phase 2b3 Step 1 (OriginalBuffer mmap + Lazy Decode コア)** | ✅ **完了** (CI 確認待ち) |
| **Phase 2b3 Step 2 (1GB/100MB bench + SEH + Phase 2b 完了報告)** | ⏭️ **次回着手** |

---

## 2. ビルド検証について

**この環境 (Claude Code エージェント) には MSVC が無く、ローカルビルドは恒常的に不可能。** 検証は常に GitHub Actions CI (`itbizmonky/NeoMIFES`, `.github/workflows/ci.yml`) に委ねる。

- **リポジトリは初期化・push 済み。** `git init` は不要 (Session 7 で完了、以降 main ブランチへの push を継続)
- 変更を加えたら `git add` → `git commit` → **ユーザーに `git push` を依頼** (エージェントは push しない方針、これまでの全セッションで一貫)
- push 後 `gh run list --limit 3` で CI 結果を確認。Debug/Release/clang-tidy の 3 ジョブが green であること
- CI が失敗した場合は `gh run view <id> --log-failed` でログを取得し、原因を特定してから修正する (前セッション群で 5+ 回この手順を踏んでおり、Windows/MSVC/clang-tidy 特有の落とし穴は [`reference_windows_cpp_ci_gotchas.md`](../../../../.claude memory を参照、または Claude のメモリ機能内 `reference_windows_cpp_ci_gotchas.md`) に集約済み

ローカルで動作確認したい場合の参考手順 (このエージェント環境では実行不可、ユーザー環境向け):
```powershell
# 前提: Visual Studio 2022 17.13+, CMake 3.28+, Ninja
cmake --preset debug && cmake --build --preset debug && ctest --preset debug --output-on-failure
cmake --preset release && cmake --build --preset release && ctest --preset release --output-on-failure
./build/release/tests/bench/neomifes_document_bench.exe --benchmark_min_time=0.1s
```

---

## 3. Phase 2b3 Step 2 の着手手順 (次回)

**目標 (Phase 2 全体 DoD):** 1GB ファイル読込ベンチ通過。

### 3.1 参照する意思決定
1. [**ADR-007**](../decisions/ADR-007-piece-tree-mutable-rb.md) — Mutable RB-Tree + Piece-Vector Snapshot (Phase 2b2 で採用済み)
2. [`docs/issues/lazy_decode_mmap.md`](../issues/lazy_decode_mmap.md) — Step 1 で完了条件の一部を達成、残りが Step 2 の主対象 (1GB open ≤2s、Working Set 増分 ≤30MB、SEH)
3. [`docs/issues/line_index_o_log_n.md`](../issues/line_index_o_log_n.md) — LineIndex O(log n) 化の撤回理由 (参考、対応不要)

### 3.2 Phase 2b1〜2b3 Step 1 完了済 (前セッション群)
- ✅ `BufferSnapshot::pieceView` + AddBuffer チャンク化 (Phase 2b1)
- ✅ `PieceTree` (insert/split/erase, CLRS 13.3+13.4) + `PieceTable` 内部差し替え (Phase 2b2)
- ✅ プロパティテスト 20,000 反復化
- ✅ **`OriginalBuffer` を mmap + Lazy Decode に全面再設計** (Phase 2b3 Step 1):
  - `platform::FileMapping` (mmap RAII) 新設
  - `OriginalBuffer` が InMemory (テスト用) / MemoryMapped (実ファイル) の二本立てに
  - 64KiB ごとのチェックポイント索引 (常にコードポイント境界で記録、マルチバイト文字分割を構造的に防止)
  - `view()` は on-demand decode + キャッシュ (evict なし、理由は lazy_decode_mmap.md 参照)
  - `PieceTable` コンストラクタが `OriginalBuffer::newlineCount()` を使うよう変更 (open 時の全文デコードを排除、これが実質的な laziness の核)
  - `OriginalBuffer::view()` / `BufferSnapshot::pieceView()` は非 noexcept に変更 (mmap decode が allocate しうるため)
  - テスト +12 (80→92): file_mapping RAII、チェックポイント境界をまたぐマルチバイト文字、複数チェックポイントにまたがる 200KB コンテンツ、newlineCount 事前計算の検証

### 3.3 Phase 2b3 Step 2 で作る予定 (次回)
```
tests/bench/
  └── document_load_bench.cpp   # 新規: CI は 100MB ケース、ローカル手動検証は 1GB ケース

src/document/src/original_buffer.cpp   # SEH (EXCEPTION_IN_PAGE_ERROR) 対策を追加
                                        # (ネットワークドライブでの mmap アクセス例外)

tests/integration/
  └── (working set 計測の統合テスト。Phase 1 の startup_measure_test.cpp と
      同じ --measure-memory ハーネスパターンを流用できないか検討)
```

### 3.3.1 実装ガードレール (継続)

| # | チェック項目 |
|---|---|
| G13 | `docs/issues/lazy_decode_mmap.md` §リスク の「ネットワークドライブで EXCEPTION_IN_PAGE_ERROR」対策 (SEH フィルタ) — **Step 2 の主要作業** |
| G14 (NEW) | 既存 92 単体テストは **1 行も変更なしで green** を維持してから bench 追加に進む |
| G15 (NEW) | 1GB ベンチを CI に直接載せない (共有ランナーへの負荷)。100MB で代替し 1GB はローカル手動検証と明記する |

### 3.4 Phase 2b 全体の完了条件
- [x] `PieceTable::insert` / `erase` が O(log n) (tree 経由で達成)
- [x] `PieceTable::insert` (small edit) < 500ns 中央値 — **CI 実測 243〜276ns で達成**
- [x] ~~`PieceTable::snapshot` 100K piece で ≤1ms~~ — 実測 1.196ms、約20%超過。低優先度の残タスクとして受容 ([`piece_table_rb_tree.md`](../issues/piece_table_rb_tree.md))
- [ ] 1GB UTF-8 load ≤ 2s、Working Set 増分 ≤ 30MB (**Phase 2b3 Step 2 の主目標、未検証**)
- [x] 既存単体テスト + プロパティテスト (20,000 反復) 全 green を維持
- [x] RB invariant テスト (root black / no red-red / uniform black height / aggregate 整合) 追加済
- [x] OriginalBuffer の mmap + Lazy Decode コア実装・テスト (Step 1)
- [ ] SEH によるネットワークドライブ例外対策 (Step 2)

### 3.5 Phase 2b 完了時に片付ける Phase 1 宿題
1. **`.clang-tidy` の `WarningsAsErrors: '*'`** に切替 (Phase 0.5 P05-4)
2. **Named Mutex 単一インスタンス化** (basic §2.3)
3. **CI に clang-cl UBSan ジョブ追加** (self-review R4)

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
- Issue (Phase 2b 引継ぎ): [`docs/issues/`](../issues/)
- フェーズ報告:
  - [Phase 0.5](../phase_reports/phase_0.5_report.md)
  - [Phase 1](../phase_reports/phase_1_report.md)
  - [Phase 2a](../phase_reports/phase_2a_report.md)

---

## 6. 次回の推奨最初のプロンプト例

```
RESUME_HERE.md を読んで現在の状態を把握し、
Phase 2b3 Step 2 (1GB/100MB load bench + SEH 例外対策 + Phase 2b 完了報告) に着手せよ。
既存 92 単体テストの変更なし green を厳守すること。
```

Phase 2b3 Step 2 の具体的作業:
1. `tests/bench/document_load_bench.cpp` — 新規。CI 実行は 100MB モックファイル生成 → load → 時間・Working Set 計測。1GB フルサイズはコメントで「ローカル手動検証用」と明記し、CI では実行しない
2. `src/document/src/original_buffer.cpp` の `FileMapping::open` 呼び出し周辺に **SEH (`EXCEPTION_IN_PAGE_ERROR`) 対策**を追加 — ネットワークドライブ上のファイルで mmap アクセス中に発生しうる例外を検出し `OriginalBufferError::IoFailure` に変換する ([`lazy_decode_mmap.md`](../issues/lazy_decode_mmap.md) §リスク参照)。`__try/__except` は C++ の例外処理と混在させる際に関数分離が必要な点に注意 (MSVC の制約)
3. Working Set 計測は Phase 1 の `--measure-startup`/`--measure-memory` パターン (`src/app/startup_profile.h`) を参考に、大きいファイルを開いた直後の Working Set 増分を計測する仕組みを検討 (統合テスト or ベンチのどちらに寄せるか要判断)
4. Phase 2b 全体の完了条件 (RESUME_HERE §3.4) を満たしたら **`docs/phase_reports/phase_2b_report.md` を1本発行** (2b1/2b2/2b3 をまとめて総括。個別レポートは作らない — CLAUDE.md §11 参照)

## 7. 履歴を辿りたいとき
[`docs/history/TIMELINE.md`](../history/TIMELINE.md) にセッション単位で全ての意思決定と成果物を時系列に記録。「なぜこう決めたか」を後追いする際の一次資料。

## 8. セッション終了時に必ず確認すること
[`CLAUDE.md`](../../CLAUDE.md) §11 の「セッション終了時チェックリスト」を実行してから作業を締めること。2026-07-15 の包括レビューでドキュメント鮮度の不整合 (本ファイルの `git init` 指示残留、Issue チェックボックス未更新、ベンチ実測値の未確認等) が多数見つかった反省に基づく恒久ルール。
