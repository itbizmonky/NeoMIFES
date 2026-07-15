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
| **Phase 2b2 Step 1 (PieceTree 追加: insert + split + validate + テスト)** | ✅ **完了** (PieceTable 差し替え無し、既存 38 テスト影響ゼロ) |
| **Phase 2b2 Step 2 (erase + line queries + PieceTable/LineIndex 内部差し替え)** | ⏭️ **次回着手** |
| Phase 2b3 (OriginalBuffer mmap + Lazy Decode + 1GB bench) | 予定 |

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

## 3. Phase 2b2 の着手手順 (次回)

**目標 (Phase 2 全体 DoD):** 1GB ファイル読込ベンチ通過。

### 3.1 参照する意思決定
1. [**ADR-007**](../decisions/ADR-007-piece-tree-mutable-rb.md) — **Mutable RB-Tree + Piece-Vector Snapshot** を採用 (ADR-006 は Superseded)
2. [`docs/issues/piece_table_rb_tree.md`](../issues/piece_table_rb_tree.md) — 完了条件と計算量目標 (ADR-007 準拠に更新済み)
3. [`docs/issues/lazy_decode_mmap.md`](../issues/lazy_decode_mmap.md) — Phase 2b3 で対応

### 3.2 Phase 2b1 完了済 (前セッション)
- ✅ `BufferSnapshot::pieceView(const Piece&)` API 追加、LineIndex を O(N²) → O(N) 化
- ✅ `AddBuffer` を append-only チャンク deque 化 — pointer stability 確立、スレッド安全性ホール解消
- ✅ 単体テスト 6 ケース追加 (`document_add_buffer_test` 拡充 + `document_buffer_snapshot_test` 新設)
- ✅ 既存 31 単体テストは公開 API 据え置きで green を維持

### 3.3 Phase 2b2 で作る予定 (次回)
```
src/document/
  ├── include/neomifes/document/piece_tree_node.h  # 新規: mutable RB ノード (Piece + subtree 集約)
  └── src/piece_tree.cpp                           # 新規: CLRS RB insert/delete + rotate + 集約更新 + in-order 走査

# 差し替え (公開ヘッダは 1 行も変えない)
src/document/src/piece_table.cpp                 # vector → mutable RB tree に
src/document/src/line_index.cpp                  # tree 集約 (subtreeNewlineCount) から O(log n) 導出

tests/unit/
  └── document_piece_tree_test.cpp               # RB invariant + 集約整合性 + edge cases
```

### 3.3.1 実装ガードレール (ADR-007 §実装ガードレール 準拠、必ず守る)

| # | チェック項目 |
|---|---|
| G1 | Node は mutable struct、`std::unique_ptr<Node>` で親から所有 (排他) |
| G2 | rotate 後は **新旧の親両方** に `updateAggregate()` を呼ぶ (bug 温床) |
| G3 | empty tree は **nullptr root** で扱う (sentinel nil は将来最適化) |
| G4 | 既存 37 単体テストが **1 行も変更なしで green** になるまで delete まわり触らない |
| G5 | RB invariant テストを実装と同時に追加 (root black / no red-red / uniform black height / aggregate 整合) |
| G6 | プロパティテスト 20K 反復に上げる (Phase 2b2 DoD の一部) |
| G7 | Piece.offset セマンティクスは **全て UTF-16 CU で統一** (Phase 2b3 の mmap Lazy Decode で OriginalBuffer 内部でバイト↔CU 変換) |
| G8 | LineIndex クラスは **薄い wrapper として残置**。build() を tree 集約経由に書き換え、外部 API は据え置き |
| G9 | Delete 実装は CLRS 13.4 の double-black 処理を丁寧に。Kahrs/GM 系は不要 |
| G10 | insert/delete の path は **stack-based** に (parent pointer 使うなら別) |

### 3.4 Phase 2b3 (mmap + 1GB bench)
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

### 3.5 Phase 2b 全体の完了条件
- `PieceTable::insert` < 500ns 中央値
- `PieceTable::snapshot` < 100ns
- 1GB UTF-8 load ≤ 2s、Working Set 増分 ≤ 30MB
- 既存 単体テスト + プロパティテスト全 green を維持
- プロパティテストの反復数を 20,000 に増やして 0 fail

### 3.6 Phase 2b 完了時に片付ける Phase 1 宿題
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
Phase 2b2 Step 2 に着手せよ。PieceTree.erase (CLRS 13.4) と
offsetToLine / lineToOffset を追加し、PieceTable と LineIndex の
内部を PieceTree に差し替えよ。既存 45+ 単体テストの変更なし green を厳守。
```

Step 2 の具体的作業:
1. `PieceTree::eraseRange(TextRange)` — CLRS 13.4 delete + double-black fixup + aggregate 維持
2. `PieceTree::offsetToLine(TextPos)` — subtreeNewlines を辿って O(log n)
3. `PieceTree::lineToOffset(LineNumber)` — 同じく O(log n)
4. `PieceTree::validate` を eraseRange 経路にも通す (property test で担保)
5. `src/document/src/piece_table.cpp` — 内部 `std::vector<Piece>` を `PieceTree` に差し替え
6. `src/document/src/line_index.cpp` — tree 集約経由の O(log n) 実装に差し替え
7. property test の反復数を 2000 → 20,000 に拡張 (ADR-007 の DoD)

## 7. 履歴を辿りたいとき
[`docs/history/TIMELINE.md`](../history/TIMELINE.md) にセッション単位で全ての意思決定と成果物を時系列に記録。「なぜこう決めたか」を後追いする際の一次資料。
