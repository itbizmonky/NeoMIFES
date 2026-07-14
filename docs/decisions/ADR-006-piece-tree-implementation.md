# ADR-006: Piece Tree を Path-Copying Persistent RB-Tree で実装する

- **ステータス:** Accepted
- **決定日:** 2026-07-14 (Phase 2b 着手前)
- **関連 Issue:** [`docs/issues/piece_table_rb_tree.md`](../issues/piece_table_rb_tree.md)
- **関連 Phase 報告:** [`phase_2a_report.md`](../phase_reports/phase_2a_report.md)

## コンテキスト

Phase 2a MVP の `PieceTable` は `std::vector<Piece>` を採用し、以下 3 特性を持つ (詳細: `phase_2a_report.md`):

| 操作 | 計算量 |
|---|---|
| `insert / erase` | O(n) (piece 探索 + vector 要素移動) |
| `snapshot()` | O(n) (vector 全コピー) |
| `LineIndex::lineToOffset` | O(n) |

Phase 2b は以下を全て O(log n) にすることが要件:
- 挿入/削除の探索と適用
- スナップショット取得
- 行番号 ⇔ オフセット変換

同時に **basic_design.md §5.2 の「BufferSnapshot は任意スレッドから参照可能」** を守る必要がある — 書き込み中に読み手が古いスナップショットを触ることは頻繁に起きる。

## 選択肢

### A. Path-Copying Persistent Red-Black Tree (推奨)

各ノードは immutable (`std::shared_ptr<const Node>`)。挿入/削除時は変更経路上の O(log n) 個のノードだけを **新規** に確保し、その他は既存ノードを共有する。ルートは `std::atomic<std::shared_ptr<const Node>>` で保持。

- **snapshot() の実装:** 現在のルート `shared_ptr` を atomic ロードするだけ (原理的に O(1))
- **書き込み:** 新しい経路を組み、新しいルートを atomic store
- **リーダ:** 古いルートを掴んだままで安全 (共有部分は immutable、変更部分は別ノード)
- **メモリ:** 挿入ごとに log n 個のノード追加。古いスナップショットが解放されたら自動的に共有ノードが 0 になり回収される

### B. Mutable RB-Tree + 全スナップショットコピー

書き込みは in-place、`snapshot()` で **ツリー全体を deep copy**。

- snapshot() が O(n) に戻る → 要件違反
- **却下**

### C. Mutable RB-Tree + RCU (atomic root swap)

書き込みは in-place、ルートだけ atomic swap。

- in-place 変更 = 古いスナップショットが変更を目撃してしまう (UB)
- ツリー全体を毎回再構築するなら A と同じだがはるかに遅い
- **却下**

### D. Mutable RB-Tree + node-level 参照カウント + COW

各ノードが参照カウント付き。書き込み時に refcount > 1 のノードだけコピー。

- 実質 A と同じセマンティクス。実装コストが余計にかかる (COW チェック分岐)
- shared_ptr で十分に達成できる
- **却下**

## 決定

**A. Path-Copying Persistent Red-Black Tree** を採用する。

## 根拠

### 要件充足
| 目標 | A で達成できるか |
|---|---|
| insert / erase O(log n) | ✅ (RB-tree の性質) |
| snapshot() O(1) | ✅ (atomic ptr ロード) |
| lineToOffset O(log n) | ✅ (順序統計木の集約) |
| 任意スレッドからの snapshot 参照安全性 | ✅ (immutable ノード + shared_ptr) |

### 実装コスト
- **shared_ptr のオーバーヘッド** はある (atomic refcount)。だが 100 万編集で総額 O(log 2^20) = 20 ノード × 平均 24 バイト程度なので、1 編集あたり **~500 バイト + 20 回の atomic インクリメント** に収まる
- ノードプール (`boost::pool` 相当を自作、Phase 2b 後半で最適化可能) を後で導入すれば allocator コストは下げられる
- 対する B/C/D の実装コスト or 性能劣化のほうがはるかに大きい

### 実績
- **VS Code の Piece Tree** も本質的にこの方式 (immutable node + snapshot via root reference)。実装参考にできる (MIT ライセンス)
- Rust の im-rs、Clojure の PersistentVector、React の Fiber tree などで確立されたパターン
- 教科書パターンなので保守性が高い

### メモリ回収の安全性
- shared_ptr の refcount が **異なる shared_ptr インスタンス間で正しく直列化される** ことは C++11 で保証済み
- Snapshot 側が古いルートを掴んでいる間、変更されていない部分木のノードは refcount > 0 で保持され続ける
- 最後の Snapshot が破棄された時点で古いノードは cascade 破棄

## 影響

### 実装上の変更 (Phase 2b で実施)

**新規ファイル:**
- `src/document/include/neomifes/document/piece_tree_node.h` — ノード定義 (immutable)
- `src/document/include/neomifes/document/piece_tree.h` — Piece Tree の公開 API (snapshot 生成等)
- `src/document/src/piece_tree.cpp` — 挿入/削除 + RB 回転 + 順序統計更新
- `tests/unit/document_piece_tree_test.cpp` — RB 平衡 / 順序統計 / 永続性

**差し替え (公開ヘッダ据え置き):**
- `src/document/src/piece_table.cpp` — 内部を `std::vector<Piece>` から `PieceTree` に差し替え
- `src/document/src/line_index.cpp` — 順序統計木の `newlineCount` 集約から O(log n) で導出
- `src/document/src/buffer_snapshot.cpp` — pieces() の代わりに tree root へのアクセス提供 (内部 API)

**Public API 影響: なし** (Phase 2a の [phase_2a_report §5](../phase_reports/phase_2a_report.md) に「ヘッダは 2b で 1 行も変えない」と宣言済)

### スレッド安全性
- 書き込みは **単一スレッド** 前提 (basic_design.md §5.2 の UI Thread のみ)
- 読み取りは任意スレッド。ルート atomic load 後は immutable なので lock free に traverse できる
- `std::atomic<std::shared_ptr<T>>` は C++20 で標準化 (MSVC v143 で lock-free 実装、確認要)

### メモリ挙動
- 100 万編集後、古い Snapshot を全て解放すれば古い path のノードは開放される
- Long-lived Snapshot (例: プラグイン持ち) が古いバージョンを大量に保持するとメモリ使用が増える → **ドキュメント化して SDK 側にガイドを書く** (Phase 8)

### 副次効果
- Undo/Redo は **各 Command が古い tree root を持つだけ** で自然に永続化される (現在の bespoke undo stack より効率的な可能性)。Phase 4 で再検討

## 却下理由

- **B (mutable + copy-on-snapshot):** snapshot() O(n) が致命的
- **C (mutable + RCU root swap):** in-place mutation が古い snapshot を破壊する
- **D (mutable + node refcount + COW):** shared_ptr で同じことができるので車輪の再発明

## 実装上の注意点 (Phase 2b で守ること)

1. **ノードは必ず `shared_ptr<const Node>`** で持つ。const 除去して mutate したら永続性が壊れる
2. **ルート更新は `std::atomic<std::shared_ptr<Node>>::store(new_root, std::memory_order_release)`** で書き手 → 読み手の happens-before を確立
3. **順序統計** (`subtreeLength`, `subtreeNewlineCount`) はノード作成時に一度だけ計算する。lazy 更新はしない (immutable なので不整合が絶対起きない)
4. **RB 回転** は必ず新ノードを作る (mutating rotate 禁止)
5. **shared_ptr の allocation コスト** は Phase 2b の bench で計測。500ns 目標を超えたら `std::allocate_shared` + プール allocator を検討 (追加 ADR で判断)

## 将来の再評価

- 一度に 10 万ノード級の同時 snapshot を扱う需要が出たら、`shared_ptr` の atomic op がボトルネックになる可能性がある。その場合 **hazard pointer** や **RCU + epoch GC** に切り替えを検討 (Phase 12 以降)
- モジュール (`import`) を採用する際 (CLAUDE.md §4) にヘッダ配置を見直す可能性あり
