# Issue: `ReplaceAllCommand`/`MultiCursorEditCommand` は `BufferSnapshot::extract()` の O(pieces) 再走査コストを N 回払う

- **起票日:** 2026-07-19 (Phase 5b2 実装時に設計検証で判明、`/code-review` 未実施だが Plan agent によるレビューで指摘)
- **対象:** [`src/core/src/cumulative_shift_edit.cpp`](../../src/core/src/cumulative_shift_edit.cpp) の `applyEditsWithCumulativeShift()`(`MultiCursorEditCommand`/`ReplaceAllCommand` 共通の適用ループ)
- **優先度:** 低 (現時点で顕在化する実利用経路が存在しない。Phase 5b3 で Find bar UI から実際に大量マッチの置換が可能になった時点で再評価)

## 背景

`document::BufferSnapshot::extract()` は自身のドキュメントコメントで明記されている通り、呼び出しごとにピースリストを先頭 (`cursor = 0`) から再走査する(`buffer_snapshot.cpp` 参照)。`search_service.cpp` の `scanDocument()` はこのコストを避けるため意図的に `extract()` を使わず `pieceView()` を使う設計にした(Phase 5b1)。

しかし `cumulative_shift_edit.cpp` の `applyEditsWithCumulativeShift()` は各編集ごとに `doc.snapshot()->extract(currentRange)` を1回呼ぶ。これは `MultiCursorEditCommand`(Phase 4b5a)からすでに存在する既存コストであり、本 Issue が新規に生む問題ではないが、`ReplaceAllCommand`(Phase 5b2)によって「N」が数十万〜100万件規模になりうるユースケースが新たに生まれたことで、初めて O(matches × pieces) が実務上のボトルネックになりうる。

`master_roadmap.md` §4.4 が置換の性能目標として「100万マッチ置換: ≤ 5秒」を掲げているが、この目標はピース数が多い(＝編集を繰り返した)ドキュメントに対しては未検証のまま。

## 対応方針 (未着手)

1. **計測してから判断する** (CLAUDE.md 絶対ルール10)。Phase 5b3 で Find bar UI から実際に大量マッチの置換が可能になった時点で、`tests/bench/` に `replace_all_bench.cpp` を追加し、ピース数(編集履歴の長さ)を変えた合成ドキュメントで実測する。
2. 実測でボトルネックが確認された場合の候補:
   - `BufferSnapshot` に「連続した range 群を1パスで抽出する」バッチ API を追加(`extract()` を N 回呼ぶ代わりに 1 回のピース走査で全編集分のテキストを取得)
   - Piece Table 側にランダムアクセス用のインデックス(`docs/issues/piece_table_rb_tree.md` / `line_index_o_log_n.md` に近い発想)を持たせる

## 完了条件

- [ ] Phase 5b3 (Find bar UI) 実装後、`replace_all_bench.cpp` で 10万〜100万マッチ規模の実測値を記録
- [ ] 実測が `master_roadmap.md` §4.4 の目標 (≤5秒) を満たさない場合、上記候補のいずれかで対応し、ベンチマークで改善を実証
- [ ] 目標を満たす場合は「実測により許容範囲内と確認」の記録を残してこの Issue をクローズ
