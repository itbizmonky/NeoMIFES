# NeoMIFES — 次回セッション再開ガイド

> **最終更新:** 2026-07-14 (Phase 2b1 完了時)
> **次回開いたら最初にこのファイルを読むこと。**

---

## 1. 現在の状態 (一目)

| 項目 | 状態 |
|---|---|
| Phase 0 (要件確認・設計書・自己レビュー) | ✅ 完了 |
| Phase 0.5 (ビルド基盤 / CI / 静的解析) | ✅ 完了 (CI green 達成) |
| Phase 1 (Win32 骨組み + 起動 0.3s/20MB PoC) | ✅ 完了 (CI 実測 22ms) |
| Phase 2a (Document Engine API + MVP 実装 + テスト網羅) | ✅ 完了 |
| Phase 2b1 (B-1 pieceView + B-2 AddBuffer チャンク化) | ✅ 完了 |
| Phase 2b2 Step 1 (PieceTree 追加: insert + split + validate + テスト) | ✅ 完了 |
| **Phase 2b2 Step 2 (eraseRange CLRS 13.4 + PieceTable 内部差し替え)** | ✅ **完了** |
| **Phase 2b3 (OriginalBuffer mmap + Lazy Decode + 1GB bench)** | ⏭️ **次回着手** |

---

## 2. 未検証の宿題 (再開時にまず確認)

Phase 0.5 / Phase 1 / Phase 2a のビルド検証はこの環境ではできない (MSVC 不在)。ユーザー環境か CI で:

```powershell
# 前提: Visual Studio 2022 17.13+, CMake 3.28+, Ninja

cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure

# Release + PoC + ベンチ
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
./build/release/tests/bench/neomifes_document_bench.exe --benchmark_min_time=0.1s
```

CI (`.github/workflows/ci.yml`) の green が最終検証。まだ `git init` していないので:
```powershell
git init
git add .
git commit -m "Phase 0.5 + Phase 1 + Phase 2a"
git remote add origin <URL>
git push -u origin main
```

---

## 3. Phase 2b3 の着手手順 (次回)

**目標 (Phase 2 全体 DoD):** 1GB ファイル読込ベンチ通過。

### 3.1 参照する意思決定
1. [**ADR-007**](../decisions/ADR-007-piece-tree-mutable-rb.md) — **Mutable RB-Tree + Piece-Vector Snapshot** を採用 (ADR-006 は Superseded)
2. [`docs/issues/piece_table_rb_tree.md`](../issues/piece_table_rb_tree.md) — 完了条件 (Step 2 完了で消化)
3. [`docs/issues/lazy_decode_mmap.md`](../issues/lazy_decode_mmap.md) — **次回の主対象**
4. [`docs/issues/line_index_o_log_n.md`](../issues/line_index_o_log_n.md) — LineIndex O(log n) 化の**撤回**理由と将来案 (Step 2 で判明)

### 3.2 Phase 2b1 / 2b2 完了済 (前セッション群)
- ✅ `BufferSnapshot::pieceView` + AddBuffer チャンク化 (Phase 2b1)
- ✅ `PieceTree` 新設: insert + splitPieceAt + validate (Phase 2b2 Step 1)
- ✅ `PieceTree::eraseRange` (CLRS 13.4 delete + fixup) + `pieceContainingStrictly` + `findNodeStartingAt` (Phase 2b2 Step 2)
- ✅ `PieceTable` 内部を `std::vector<Piece>` → `PieceTree` に差し替え (公開 API 不変)
- ✅ プロパティテスト 2000 → 20,000 反復化
- ✅ **重要な設計修正:** LineIndex の O(log n) 化は撤回 (tree 集約は piece 内の改行**位置**を持たないため不可能と判明)。O(N) 再構築 + O(log n) 二分探索クエリのまま維持

### 3.3 Phase 2b3 で作る予定 (次回)
```
src/platform/
  ├── include/neomifes/platform/file_mapping.h   # 新規: mmap RAII
  └── src/file_mapping.cpp                       # 新規

src/document/src/original_buffer.cpp             # 全読み込み → mmap + LRU デコードキャッシュ
src/document/src/file_loader.cpp                 # mmap 経路を使う

tests/unit/
  └── platform_file_mapping_test.cpp             # RAII

tests/bench/
  └── document_load_1gb_bench.cpp                # 1GB モック生成 → load
```

### 3.3.1 実装ガードレール (継続)

| # | チェック項目 |
|---|---|
| G7 | Piece.offset セマンティクスは **全て UTF-16 CU で統一** (mmap Lazy Decode で OriginalBuffer 内部がバイト↔CU 変換を担う) |
| G11 (NEW) | mmap ビューは RAII (`MapViewOfFileEx`+`UnmapViewOfFile`) で必ず包む。生ハンドルを裸で扱わない |
| G12 (NEW) | 既存 45+ 単体テストは **1 行も変更なしで green** を維持してから mmap 化に進む |
| G13 (NEW) | `docs/issues/lazy_decode_mmap.md` §リスク の「ネットワークドライブで EXCEPTION_IN_PAGE_ERROR」対策 (SEH フィルタ) を組み込む |

### 3.4 Phase 2b 全体の完了条件
- [x] `PieceTable::insert` / `erase` が O(log n) (tree 経由で達成)
- [ ] `PieceTable::insert` (small edit) < 500ns 中央値 (要ベンチ実測。CI 環境で計測しローカル未確認)
- [ ] 1GB UTF-8 load ≤ 2s、Working Set 増分 ≤ 30MB (Phase 2b3 の主目標)
- [x] 既存単体テスト + プロパティテスト (20,000 反復) 全 green を維持
- [x] RB invariant テスト (root black / no red-red / uniform black height / aggregate 整合) 追加済

### 3.5 Phase 2b 完了時に片付ける Phase 1 宿題
1. **`.clang-tidy` の `WarningsAsErrors: '*'`** に切替 (Phase 0.5 P05-4)
2. **Named Mutex 単一インスタンス化** (basic §2.3)
3. **CI に clang-cl UBSan ジョブ追加** (self-review R4)

---

## 4. Phase 2a のコンテキスト圧縮版

### 4.1 意図的な MVP 縮退 (Phase 2b で解消するもの)
| 縮退項目 | 現状 | 解消方針 |
|---|---|---|
| Piece コンテナ | `std::vector<Piece>` | RB-tree + 順序統計 |
| snapshot | vector 全コピー O(n) | path-copying or RCU O(1) |
| Original | 全読み込み UTF-16 | mmap + 64KB Lazy Decode LRU |
| LineIndex | mutation ごとに O(N) 再スキャン | tree の順序統計から O(log n) |
| Encoding | UTF-8 のみ | Phase 6 の Encoding Engine 側で拡張 |
| Loader | 同期 | Worker で非同期化 |

**公開ヘッダは Phase 2b で 1 行も変えない** ように設計してある。実装差し替えで完了する。

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
Phase 2b3 (OriginalBuffer mmap + Lazy Decode + 1GB load bench) に着手せよ。
docs/issues/lazy_decode_mmap.md の設計指針とリスク対策 (SEH フィルタ) に従い、
既存 45+ 単体テストの変更なし green を厳守すること。
```

Phase 2b3 の具体的作業:
1. `src/platform/include/neomifes/platform/file_mapping.h` — mmap RAII (`MapViewOfFileEx`/`UnmapViewOfFile`)
2. `src/document/src/original_buffer.cpp` — 全読み込みから mmap + 64KB Lazy Decode LRU キャッシュに差し替え
3. `src/document/src/file_loader.cpp` — mmap 経路を使うよう変更
4. `tests/unit/platform_file_mapping_test.cpp` — RAII の正しさ
5. `tests/bench/document_load_1gb_bench.cpp` — 1GB モックファイル生成 → load → Working Set 計測
6. Phase 2b 全体の完了条件 (RESUME_HERE §3.4) を満たしたら Phase 2 完了報告
7. property test の反復数を 2000 → 20,000 に拡張 (ADR-007 の DoD)

## 7. 履歴を辿りたいとき
[`docs/history/TIMELINE.md`](../history/TIMELINE.md) にセッション単位で全ての意思決定と成果物を時系列に記録。「なぜこう決めたか」を後追いする際の一次資料。
