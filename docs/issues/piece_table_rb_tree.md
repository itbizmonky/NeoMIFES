# Issue: PieceTable を std::vector から RB-tree (順序統計木) に置換

- **起票日:** 2026-07-14 (Phase 2a 完了時)
- **対象:** [`src/document/src/piece_table.cpp`](../../src/document/src/piece_table.cpp)
- **優先度:** 高 (Phase 2b の中核)

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

## 完了条件 (ADR-007 準拠に更新)

- [ ] `PieceTable::insert` (small edit) < 500ns 中央値 (mutable なら現実的目標)
- [ ] `PieceTable::snapshot` 100K piece で ≤ 1ms — **旧目標 100ns は ADR-007 で撤回** (piece vec 走査コスト受容)
- [ ] `LineIndex::lineToOffset` が O(log n) (1M 行で ≤ 10µs)
- [ ] `LineIndex::offsetToLine` が O(log n) (1M 行で ≤ 10µs)
- [ ] 既存 37 単体テスト + 2000 反復プロパティテストが 1 行も変更なしで green を維持
- [ ] プロパティテストの反復数を 20,000 に拡張して 0 fail
- [ ] 新規 `document_piece_tree_test.cpp` で RB invariant を検証:
  - root is black
  - no two consecutive red nodes
  - black height uniform across root-to-leaf paths
  - `subtreeLength` / `subtreeNewlineCount` が実データと一致
