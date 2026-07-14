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

## 設計指針 (詳細設計 §3.1 準拠)

- 各ノードが `Piece` を保持
- サブツリーの `length` と `newlineCount` を集約 (順序統計)
- 挿入/削除は Red-Black 回転で平衡維持
- スナップショットは **path-copying** (persistent tree) にして O(log n) で immutable ビューを作る
  - もしくは RCU: mutable ツリー + `atomic<shared_ptr<const RbTree>>` で読み手だけ古い版を掴む

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

## 完了条件

- [ ] `PieceTable::insert` (small edit) < 500ns 中央値
- [ ] `PieceTable::snapshot` < 100ns
- [ ] 既存プロパティテストが 20,000 反復で 0 fail
- [ ] `LineIndex::lineToOffset` が O(log n) (少なくとも 1M 行で 10µs 未満)
