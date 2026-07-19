# Issue: `ReplaceAllCommand`/`MultiCursorEditCommand` は `BufferSnapshot::extract()` の O(pieces) 再走査コストを N 回払う

- **起票日:** 2026-07-19 (Phase 5b2 実装時に設計検証で判明、`/code-review` 未実施だが Plan agent によるレビューで指摘)
- **対象:** [`src/core/src/cumulative_shift_edit.cpp`](../../src/core/src/cumulative_shift_edit.cpp) の `applyEditsWithCumulativeShift()`(`MultiCursorEditCommand`/`ReplaceAllCommand` 共通の適用ループ)
- **優先度:** 低 (Phase 5b3b (2026-07-19) で Find bar UI から実際にマッチの置換 (Enter/Ctrl+Enter → `replaceCurrentMatch()`/`replaceAllMatches()`, `src/app/main.cpp`) が可能になり、再評価条件は満たされた。ただしベンチマーク実施は本 Issue の完了条件のとおり依然未着手 — 下記「再評価条件成立の記録」参照)

## 背景

`document::BufferSnapshot::extract()` は自身のドキュメントコメントで明記されている通り、呼び出しごとにピースリストを先頭 (`cursor = 0`) から再走査する(`buffer_snapshot.cpp` 参照)。`search_service.cpp` の `scanDocument()` はこのコストを避けるため意図的に `extract()` を使わず `pieceView()` を使う設計にした(Phase 5b1)。

しかし `cumulative_shift_edit.cpp` の `applyEditsWithCumulativeShift()` は各編集ごとに `doc.snapshot()->extract(currentRange)` を1回呼ぶ。これは `MultiCursorEditCommand`(Phase 4b5a)からすでに存在する既存コストであり、本 Issue が新規に生む問題ではないが、`ReplaceAllCommand`(Phase 5b2)によって「N」が数十万〜100万件規模になりうるユースケースが新たに生まれたことで、初めて O(matches × pieces) が実務上のボトルネックになりうる。

`master_roadmap.md` §4.4 が置換の性能目標として「100万マッチ置換: ≤ 5秒」を掲げているが、この目標はピース数が多い(＝編集を繰り返した)ドキュメントに対しては未検証のまま。

## 対応方針 (未着手)

1. **計測してから判断する** (CLAUDE.md 絶対ルール10)。Phase 5b3 で Find bar UI から実際に大量マッチの置換が可能になった時点で、`tests/bench/` に `replace_all_bench.cpp` を追加し、ピース数(編集履歴の長さ)を変えた合成ドキュメントで実測する。
2. 実測でボトルネックが確認された場合の候補:
   - `BufferSnapshot` に「連続した range 群を1パスで抽出する」バッチ API を追加(`extract()` を N 回呼ぶ代わりに 1 回のピース走査で全編集分のテキストを取得)
   - Piece Table 側にランダムアクセス用のインデックス(`docs/issues/piece_table_rb_tree.md` / `line_index_o_log_n.md` に近い発想)を持たせる

## 再評価条件成立の記録 (Phase 5b3b, 2026-07-19)

「Phase 5b3 で Find bar UI から実際に大量マッチの置換が可能になった時点」という上記の再評価トリガーは、Phase 5b3b (Ctrl+H 置換行配線) の実装により成立した。`replaceCurrentMatch()`/`replaceAllMatches()` (`src/app/main.cpp`) が `core::ReplaceRangeCommand`/`core::ReplaceAllCommand` 経由で実際に UI から到達可能になっている。

**ただし本セッションではベンチマーク自体は実施していない** (計画のスコープ外、下記完了条件は未消化のまま)。次に大量マッチのテストデータ (数十万件規模) が必要になった際、あるいは性能計測のための別セッションで着手すること。

## 完了条件

- [ ] `replace_all_bench.cpp` で 10万〜100万マッチ規模の実測値を記録 (Find bar UI からの到達経路は Phase 5b3b で確立済み、着手可能)
- [ ] 実測が `master_roadmap.md` §4.4 の目標 (≤5秒) を満たさない場合、上記候補のいずれかで対応し、ベンチマークで改善を実証
- [ ] 目標を満たす場合は「実測により許容範囲内と確認」の記録を残してこの Issue をクローズ
