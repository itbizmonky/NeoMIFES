# Issue: LineIndex の O(log n) 化 (offsetToLine / lineToOffset)

- **起票日:** 2026-07-15 (Phase 2b2 Step 2 実装中に判明)
- **対象:** [`src/document/include/neomifes/document/line_index.h`](../../src/document/include/neomifes/document/line_index.h)
- **優先度:** 低 (現状の O(N) 再構築 + O(log n) クエリで実用上問題なし。巨大ファイルで顕在化する可能性)
- **2026-07-29 追記 (Phase 7p):** 「案C」(下記) を実装済み。経緯・実測値は本ファイル末尾の追記セクション参照。

## 背景

Phase 2b2 の当初計画 (RESUME_HERE.md G8) では「LineIndex を PieceTree の集約経由で O(log n) 化する」ことを想定していた。しかし実装検討の結果、**これは PieceTree の現在の設計では不可能**と判明した。

### なぜ不可能か

`PieceTreeNode` の `subtreeNewlines` 集約は「サブツリー内の改行**総数**」しか保持しない。`offsetToLine(pos)` を答えるには「`pos` より前に改行が何個あるか」を知る必要があるが、`pos` が **piece の途中** に位置する場合、その piece 内で `pos` より前に何個改行があるかは **piece のテキスト内容を実際に見ないと分からない**。

`PieceTree` は設計上テキスト内容 (`AddBuffer` / `OriginalBuffer`) への参照を持たない (`Piece` は `offset` / `length` / `newlineCount` のメタデータのみ)。そのため tree 単体では piece 内部の改行位置を答えられない。

## 対応方針 (今回採用)

**LineIndex は Phase 2b1 の設計のまま据え置く:**
- `build(snapshot)`: 全 piece を `pieceView()` で走査し、改行位置を列挙 — O(N) (N = ドキュメント長)
- `offsetToLine` / `lineToOffset`: 構築済みの `std::vector<TextPos>` に対する二分探索 — O(log n)
- `Document` が mutation 毎に `m_lineIndexDirty = true` を立て、次のクエリで遅延再構築 — Phase 2a から変更なし

これは要件 (CLAUDE.md §5 検索速度・スクロール 60fps) を **現時点では** 満たす: LineIndex の再構築は「編集操作の都度」ではなく「行番号クエリが実際に呼ばれた時」にのみ発生し、典型的な編集セッション (キー入力の連続) では毎回再構築されない (Document 側で dirty フラグにより re-build を 1 回にまとめている)。

## 将来 O(log n) 化する場合の設計案

もし 10GB 級ファイルで「編集の都度、行番号クエリも都度発生する」ワークロードが要件化された場合、以下のいずれかで解決可能:

### 案 A: Piece に newline-offset 配列を持たせる
```cpp
struct Piece {
    // ...既存フィールド...
    std::vector<std::uint32_t> newlineOffsets;  // piece 内の相対オフセット、昇順
};
```
- `splitPieceAt` 時に `newlineOffsets` も左右に分割する必要がある (現在は `leftNewlines` という **カウントのみ** を渡しているが、**実際の各オフセット**を渡す必要が生じる)
- Piece が「小さい配列を持つ」形になり、`sizeof(Piece) <= 32` という現行の制約 ([piece.h](../../src/document/include/neomifes/document/piece.h) の `static_assert`) が崩れる → 別途 side-table 化を検討
- tree 集約は `subtreeNewlines` に加え、二分探索用の補助構造 (各 piece の newlineOffsets を tree 走査時に合成) が必要

### 案 B: Piece 単位でなく「行」を第一級のツリーノードにする
- VS Code の実装 (`piece tree with line starts cached per buffer`) に近い
- Piece 自体に「この piece が含む改行の相対オフセット配列」をキャッシュし、tree 集約で `subtreeLineFeedCnt` を維持 (これは概ね案 A と同等)

### 案 C: 現状維持 + 増分再構築の最適化
- `build()` を全 rebuild ではなく、変更範囲 (最後の mutation の TextRange) のみ再走査する差分更新に変える
- tree を全く変えずに LineIndex 単体を改善できる、最も低リスク

## 推奨

**Phase 2b3 (mmap + Lazy Decode) 完了後、実際のプロファイリング結果を見てから判断する。** 現時点では投機的最適化を避ける (CLAUDE.md §9 「性能を主張する場合は必ず計測値を示す」に整合)。

## 完了条件 (将来この Issue に着手する場合)

- [ ] 1M 行のドキュメントで `offsetToLine` / `lineToOffset` が実測 O(log n) (定数倍を含め ≤ 10µs)
- [x] 既存 LineIndex 単体テストが全て pass (公開 API 不変) — 案C適用後も変更なし、下記追記参照
- [ ] `splitPieceAt` の呼び出し側 (`PieceTable::ensureBoundary`) が newline-offset 配列の分割にも対応 — 案A/B(ツリー集約化)は依然未着手、案Cの範囲外

## 追記 (2026-07-29, Phase 7p): 「案C」実装 — 経緯と実測値

**発端:** Phase 7k (2026-07-27) で `document::EditDelta` を導入した際、`Document::insertText()`/`eraseRange()`/`replaceRange()` が編集の**都度** `offsetToLine()` を呼ぶようになった。当時の設計判断は「RenderPipeline が毎フレーム1回呼んでいたコストを、Document 内部に前倒ししているだけで新規コストではない」という前提だったが、これは誤りだった。実際には Document 自身の変更メソッドが `m_lineIndexDirty = true` をセットした**直後**に `offsetToLine(newEnd)` を呼んでいたため、**1回の編集ごとに必ず1回、`LineIndex::build()` の O(文書長) フルスキャンが発生**していた。

Phase 7j までの12コミットをまとめて push した直後の CI 実行 (run [30367272798](https://github.com/itbizmonky/NeoMIFES/actions/runs/30367272798)) で、`Build & Test (debug)`/`(release)` の両ジョブが `neomifes_core_bench.exe` (`BM_UndoStack_PushOneMillion`、100万回の逐次 `insertText()`) の実行中に停止したまま進まなくなり、6時間のジョブ上限でキャンセルされた。100万回の逐次挿入それぞれで文書全体 (最大時点で約100万文字) の O(N) 再構築が起きるため、合計コストは Σi (i=1..1,000,000) ≈ 5×10¹¹ 相当の O(N²) となり、実質的にハングしていた。

**対応 (案C を採用):** [`line_index.h`](../../src/document/include/neomifes/document/line_index.h) に `LineIndex::applyInsert(pos, text)` / `LineIndex::applyErase(range)` を新設。`build()` によるフル再構築を行わず、影響を受ける `m_lineStarts` の要素だけをシフト/挿入/削除して既存のインデックスを直接更新する — O(pos 以降の行数 + 編集サイズ) で完結し、文書全体は再走査しない。文書末尾への挿入 (タイピング・逐次追記の典型パターン) では「pos 以降の行数」が常にゼロなので実質 O(1) に近い。

`Document::insertText()`/`eraseRange()`/`replaceRange()` は `m_lineIndexDirty = true` をセットする代わりに、これらのメソッドを直接呼ぶよう書き換えた (`replaceRange()` は `applyErase()` → `applyInsert()` の2段適用、`PieceTable::replace()` 自身の「erase してから insert」という意味論に合わせた)。結果として `m_lineIndexDirty` は構築直後の初回クエリでのみ意味を持ち、以降は一度も `true` にならない — `offsetToLine()`/`lineToOffset()` は常に O(log n) の二分探索で完結する。

**実測値 (Release ビルド、ローカル):**

| ベンチマーク | 修正前 | 修正後 |
|---|---|---|
| `BM_UndoStack_PushOneMillion` (100万回逐次 insert) | CI で6時間タイムアウト (未完走) | **412.5 ms** |
| `BM_UndoStack_UndoOneMillion` (上記の undo 100万回) | 計測不能 (上記に依存) | **267.1 ms** |

これは roadmap の性能目標 (CLAUDE.md §7 / 要件定義書 §5「Undo: 100万回以上」) に対する定量的な裏付けにもなる。

**この Issue 自体の残課題:** 上記は「編集の都度フル再構築されない」ことの修正 (案C) であり、Issue 本文が指す「`offsetToLine`/`lineToOffset` 自体を O(log n) 化する」という元々のスコープ (案A/B、PieceTree のツリー集約化) は依然未着手のまま。`offsetToLine`/`lineToOffset` は今回の修正後も二分探索 (O(log n) in 行数) であり、これは Phase 2b2 時点から変わっていない — 変わったのは「インデックスを最新に保つコスト」だけ。
