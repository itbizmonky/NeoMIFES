# ADR-007: Piece Tree を Mutable Red-Black Tree + Piece-Vector Snapshot で実装する

- **ステータス:** Accepted
- **決定日:** 2026-07-14 (Phase 2b2 着手直前レビューで判断)
- **Supersedes:** [ADR-006](ADR-006-piece-tree-implementation.md)
- **関連 Issue:** [`docs/issues/piece_table_rb_tree.md`](../issues/piece_table_rb_tree.md)
- **関連レビュー:** Phase 2b2 事前レビュー (会話履歴 / TIMELINE.md)

## コンテキスト

[ADR-006](ADR-006-piece-tree-implementation.md) は **Path-Copying Persistent RB-Tree** を採用した。Phase 2b2 着手直前の再レビューで以下 4 点が判明:

1. **snapshot() O(1) は要件ではなく aspirational な目標**
   詳細設計 §18.3 の「snapshot ≤ 100ns」は目標。要件 (CLAUDE.md §5) には snapshot 計算量の規定なし。
   実運用で snapshot を要求する頻度は秒単位 (LineIndex rebuild / search / autosave / plugin) であり、10K piece 級で ~100μs、100K piece で ~1ms は 60fps 予算 (16.6ms) の 1〜6% にすぎない。
   さらに、Phase 2a / Phase 2b1 の現行 `BufferSnapshot` は既に O(n pieces) — path-copying を採用しても、既存の piece-vec ベース snapshot 実装を捨てなければ O(1) にはならない。

2. **Persistent RB Delete の実装難度が高すぎる**
   - Insert は Okasaki の関数型 RB で ~50 行と簡潔。
   - Delete は Kahrs / Germane-Might アルゴリズムで 200+ 行、教科書レベルの難度。
   - このリポジトリはこの環境で MSVC ビルドできないため、複雑な delete のバグ検出は CI ラウンドに依存 — コスト大。

3. **shared_ptr オーバーヘッドで 500ns insert 目標が達成困難**
   - 挿入経路 (~20 nodes) × shared_ptr 構築 (~100ns) = ~2000ns
   - 詳細設計 §18.3「insert ≤ 500ns」達成には intrusive refcount への移行が必要 = さらなる書き直しコスト

4. **snapshot O(1) の恩恵範囲が狭い**
   - LineIndex rebuild は Phase 2b2 で tree 集約から O(log n) にできる → snapshot 不要
   - search / autosave / plugin は snapshot 頻度が低い

## 決定

**Mutable Red-Black Tree** を採用する。実装は CLRS 教科書アルゴリズム。Snapshot は既存の piece-vec コピー方式を維持。

- **Node は mutable**: `std::unique_ptr<Node>` (PieceTable が排他所有)
- **書き込み**: 標準の RB insert / delete (in-place mutation + 回転 + 再着色)
- **snapshot()**: tree を in-order 走査して `std::vector<Piece>` を作り、`BufferSnapshot` にラップ (現行 API 据え置き)
- **BufferSnapshot** は piece vec + `shared_ptr<AddBuffer>` + `shared_ptr<OriginalBuffer>` を持つ (現行構造そのまま)
- **スレッド安全性**: 単一 writer (UI Thread) が tree を mutate、readers は独立した piece vec の copy を持つ (Phase 2b1 の AddBuffer チャンク化と組み合わせて完全な read/write 分離)

## 根拠

### 要件充足
| 要件 | 達成 |
|---|---|
| insert / erase O(log n) | ✅ (RB 標準アルゴリズム) |
| snapshot: 任意スレッドから安全に参照可能 | ✅ (piece vec は snapshot 毎に独立 copy) |
| lineToOffset O(log n) | ✅ (tree の subtreeNewlineCount 集約) |
| offsetToLine O(log n) | ✅ (tree の subtreeLength 集約) |
| PieceTable 公開 API 互換性 | ✅ (Phase 2a 以降 1 行も変えない) |

### 計算量比較
| 操作 | ADR-006 (path-copying) | ADR-007 (mutable) |
|---|---|---|
| insert | O(log n) alloc (~2μs 見積) | O(log n) mutate (~500ns 見積) |
| erase | Kahrs/GM 実装必須 (困難) | CLRS textbook (well-known) |
| snapshot | O(1) root shared_ptr copy | O(n pieces) piece vec 走査 |
| lineToOffset | O(log n) tree walk | O(log n) tree walk (同じ) |
| メモリ | 古い snapshot が古い node を保持 | 古い snapshot は独立 piece vec のみ |

### 実装コスト
- Insert: CLRS 13.3, ~80 行
- Delete: CLRS 13.4, ~150 行 (double-black 処理含む)
- 集約更新 (subtreeLength, subtreeNewlineCount) は rotate/insert/delete の各ヘルパで再計算
- **合計 ~500 行程度** — 週末プロジェクト規模

### スレッド安全性 (詳細)
Basic design §5.2 の要求 (「Document 書き込み: UI Thread のみ」「BufferSnapshot: 任意スレッド」) に完全準拠:

```
Writer (UI Thread)                    Readers (any thread)
─────────────────                     ────────────────────
PieceTable
  ├─ std::unique_ptr<Node> m_root  ←  BufferSnapshot A (piece vec copy)
  ├─ shared_ptr<AddBuffer>       ─┐   BufferSnapshot B (piece vec copy)
  └─ shared_ptr<OriginalBuffer>  ─┼→  BufferSnapshot C (piece vec copy)
                                  │
     mutable tree ops             │   独立した piece vec (heap)
     ↑ Writer only                │   ↑ Reader-owned
                                  │
             shared_ptr で共有 ───┘
             (buffers は immutable / append-only-with-stability)
```

**Race conditions: 無し**
- Writer は tree のみ mutate、reader は piece vec のみ read
- Buffers (`AddBuffer`, `OriginalBuffer`) は Phase 2b1 で pointer-stable 化済
- Snapshot 生成のみ writer thread から (basic §5.2 遵守)

## 影響

### 実装上の変更 (Phase 2b2 で実施)

**新規ファイル:**
- `src/document/include/neomifes/document/piece_tree_node.h` — Node 定義 (mutable, subtree aggregate 込み)
- `src/document/src/piece_tree.cpp` — RB insert / delete / rotate + aggregate 更新 + in-order 走査 + tree-based line queries
- `tests/unit/document_piece_tree_test.cpp` — RB invariant + aggregate 整合性 + edge cases

**差し替え (公開ヘッダ据え置き):**
- `src/document/src/piece_table.cpp` — 内部を `std::vector<Piece>` から mutable RB tree に差し替え、snapshot() は tree in-order 走査に書き換え
- `src/document/src/line_index.cpp` — tree の subtreeNewlineCount 集約経由で O(log n)

**Public API 影響:** なし
- `PieceTable` の public メソッド (insert/erase/replace/snapshot/length/newlineCount/lineCount/pieceCount) は全て据え置き
- `BufferSnapshot` の public メソッド (extract/pieceView/pieces/length/lineCount/newlineCount) は全て据え置き
- **既存の 37 単体テスト + 2000 反復プロパティテストは 1 行も書き換えずに green を維持しなければならない** ことを DoD とする

### 却下した選択肢
| 選択肢 | 却下理由 |
|---|---|
| Path-copying persistent RB (ADR-006) | 実装難度・shared_ptr コスト・O(1) 恩恵の限定性 |
| RCU (root swap) | in-place 変更が古い snapshot を壊す → snapshot 内部で piece vec コピーする以上、そもそも tree の永続化は不要 |
| Node-level refcount + COW | mutable + 独立 snapshot で足りるので不要な複雑度 |

## 将来の再評価タイミング

- Phase 3+ で snapshot() の実測値が LineIndex rebuild / search / autosave のいずれかでボトルネック化した場合
- 100K+ piece 級の巨大文書で snapshot が 10ms 超え、かつ頻度が高くなった場合
- Undo/Redo (Phase 4) で「編集 N ステップ前の状態」を大量に保持したくなった場合

→ その時点で改めて persistent 化 (ADR-006 相当) or 差分 snapshot / hazard pointer 等を評価する。**Public API は変わらないので実装のみの swap で済む** — この投資回収可能性を確保するのが本 ADR の最大の狙い。

## 実装ガードレール (Phase 2b2 で守ること)

1. **Node は `struct` に近い形で、public フィールド (`piece`, `left`, `right`, `parent?`, `color`, `subtreeLength`, `subtreeNewlineCount`, `subtreeCount`)。std::unique_ptr で所有権を親から子に張る**
2. **rotate 後は必ず `updateAggregate()` を新旧親両方に呼ぶ** — bug 温床、専用テストで担保
3. **empty tree は nullptr root で扱う** — sentinel nil は optimization、MVP では null 分岐
4. **既存 37 テスト green を CI で確認するまでは delete まわり触らない**
5. **RB invariant テストを実装と同時に追加** — root black / no red-red / uniform black height / aggregate 整合
6. **プロパティテスト 20K 反復に上げる** — Phase 2b2 完了 DoD

## 参考文献
- CLRS 3rd Ed. Chapter 13 (Red-Black Trees) — 標準アルゴリズム出典
- Okasaki "Red-Black Trees in a Functional Setting" (JFP 1999) — Persistent 版 (今回は不採用だが参照)
- VS Code の piece tree 実装 (`microsoft/vscode` `src/vs/editor/common/model/pieceTreeTextBuffer/`) — 実質同方針 (mutable + snapshot-time copy)
