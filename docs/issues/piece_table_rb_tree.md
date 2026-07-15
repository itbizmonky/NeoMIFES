# Issue: PieceTable を std::vector から RB-tree (順序統計木) に置換

- **起票日:** 2026-07-14 (Phase 2a 完了時)
- **状態:** 🟢 **大部分解消** (2026-07-15、Phase 2b2 Step 1+2 完了)。残るは §完了条件 の bench 直接検証のみ
- **対象:** [`src/document/src/piece_table.cpp`](../../src/document/src/piece_table.cpp)
- **優先度:** 高 (Phase 2b の中核) → 解消につき低

## 背景

Phase 2a MVP では `std::vector<Piece>` を採用している。API 上は同じだが計算量が問題:

| 操作 | MVP (vector) | 目標 (RB-tree + 順序統計) |
|---|---|---|
| `findPiece(pos)` | O(n) 線形走査 | O(log n) |
| `insert(pos, text)` | O(n) 要素移動 + O(n) 探索 | O(log n) |
| `erase(range)` | O(n) | O(log n + k) k = 削除ピース数 |
| `snapshot()` | O(n) コピー | O(1) 参照コピー (persistent tree) |
| `LineIndex.lineToOffset` | O(n) | O(log n) |

100 万編集の Undo / 10GB ファイル / 60fps スクロールの各要件を満たすには O(log n) が必要。

## 設計指針 (詳細設計 §3.1 + [ADR-007](../decisions/ADR-007-piece-tree-mutable-rb.md))

**実装形態は [ADR-007](../decisions/ADR-007-piece-tree-mutable-rb.md) で "Mutable RB-Tree + Piece-Vector Snapshot" に確定** (旧 ADR-006 の path-copying 案は Superseded)。

- 各ノードは **mutable** で `std::unique_ptr<Node>` で親から所有 (PieceTable が排他)
- 挿入/削除は **CLRS 標準アルゴリズム** — in-place mutation + 回転 + 再着色
- ノードは subtree aggregate `subtreeLength` / `subtreeNewlineCount` / `subtreeCount` を保持し、rotate/insert/delete で更新
- `snapshot()` は **tree を in-order 走査して `std::vector<Piece>` を作り、BufferSnapshot にラップ** (Phase 2a 以降の実装方式据え置き)
- Multi-thread 安全性は **piece vec が snapshot 毎に独立コピー** であることと、Phase 2b1 の AddBuffer チャンク化による pointer stability で担保

## 実装ステップ提案

1. `piece_tree.h/cpp` を新設し RB-tree + 順序統計を実装 (LineIndex の集約も同時に)
2. 単体テストで挿入/削除/回転バランスを網羅
3. **既存の `PieceTable` 単体テスト・プロパティテストがそのまま通ること** を必須要件とする
4. `PieceTable` 実装だけを差し替え、ヘッダ (公開 API) は据え置き
5. マイクロベンチで O(log n) 特性を確認
6. `LineIndex` を tree の集約から派生させる (今の外部再スキャンを削除)

## 参考

- VS Code の実装解説: `microsoft/vscode` `src/vs/editor/common/model/pieceTreeTextBuffer/`
- Boehm, Atkinson, and Plass "Ropes: an Alternative to Strings" (1995)

## 完了条件 (2026-07-15 実態反映)

- [x] `PieceTable::insert` (small edit) < 500ns 中央値 — **CI 実測 276ns (Release) で達成** (2026-07-15 確認、`neomifes_document_bench` の `BM_PieceTable_InsertAtEnd`)
- [ ] `PieceTable::snapshot` 100K piece で ≤ 1ms — **未検証。既存 bench は 1000 piece 規模までしか測っていない** (1000 piece で 3549ns、線形なら 100K で ≈0.355ms と推定されるが実測ではない)。100K piece ケースの追加が必要
- [x] ~~`LineIndex::lineToOffset`/`offsetToLine` が O(log n)~~ — **撤回**。tree 集約は piece 内の改行位置を持たないため原理的に不可能と判明。詳細は [`line_index_o_log_n.md`](line_index_o_log_n.md) 参照。LineIndex は O(N) 再構築 + O(log n) 二分探索のまま存続する設計として確定
- [x] 既存単体テスト + プロパティテストが 1 行も変更なしで green を維持 (テスト総数は 37 → 80 に増加、全て公開 API 経由でテストしているため既存分は無改変)
- [x] プロパティテストの反復数を 20,000 に拡張して 0 fail (Phase 2b2 Step 2 で達成)
- [x] `document_piece_tree_test.cpp` で RB invariant を検証:
  - [x] root is black
  - [x] no two consecutive red nodes
  - [x] black height uniform across root-to-leaf paths
  - [x] `subtreeLength` / `subtreeNewlineCount` / `subtreeCount` が実データと一致

## 残タスク

- [ ] `document_piece_table_bench.cpp` (または新設する `document_load_bench.cpp`) に 100K piece 規模の snapshot ケースを追加し、目標を外挿でなく実測で確認する (Phase 2b3 着手前を推奨)
