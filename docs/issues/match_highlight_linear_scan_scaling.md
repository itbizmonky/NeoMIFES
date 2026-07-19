# Issue: `RenderPipeline::drawMatchesOnLine()` は可視行ごとに `m_matchVisuals` を線形走査する

- **起票日:** 2026-07-19 (Phase 5b3a 実装時に設計検証で判明、Plan agent によるレビューで指摘)
- **対象:** [`src/render/src/render_pipeline.cpp`](../../src/render/src/render_pipeline.cpp) の `drawMatchesOnLine()`
- **優先度:** 低 (現時点の性能目標 (master_roadmap.md §5.4「インクリメンタル検索 (10MB ファイル、100 マッチ): ≤ 100ms」) の範囲では問題にならない。Phase 5c (Grep) 等で単一ドキュメントに数万件規模のマッチが発生する経路ができてから再評価)

## 背景

`drawMatchesOnLine()` は既存の `drawSelectionsOnLine()`(カーソル選択範囲のハイライト、Phase 4b7a)と全く同じ構造を踏襲し、可視行 1 行を描画するたびに `m_matchVisuals` 全体を線形走査して overlap するマッチを探す。カーソル数(高々数十)を想定した `drawSelectionsOnLine()` ではこの O(cursors) 走査は問題にならないが、`m_matchVisuals` は `search::SearchService::findAll()` の結果全件(理論上ドキュメント中の全マッチ)を保持するため、可視行数 × 総マッチ数の計算量になる。

`SearchService::findAll()` はマッチをドキュメント順(昇順)でソート済みの状態で返すことが保証されている(`search_service.h` のドキュメントコメント参照)。この性質を活かせば、`std::lower_bound` で現在の可視行範囲に該当するマッチの開始インデックスを二分探索し、そこから線形に「行範囲を超えたら打ち切り」とすることで、可視行あたりの走査コストを O(可視領域内のマッチ数) 程度まで削減できる可能性がある。

## 対応方針 (未着手)

1. **計測してから判断する** (CLAUDE.md 絶対ルール10)。現時点でこの経路(単一ドキュメントに数万件規模のマッチが実際に発生する UI 導線)が存在しないため、測定対象が無い
2. Phase 5c (Grep) や大規模ファイルでのワイルドカード検索など、マッチ件数が数千〜数万件に達する経路が実装された時点で、`tests/bench/` にマッチハイライト描画のベンチマークを追加し実測する
3. 実測でボトルネックが確認された場合、`std::lower_bound` を使った二分探索起点の走査へ書き換える(`m_matchVisuals` がソート済みであることを前提にできる — `search::SearchService::findAll()` の保証を利用)

## 完了条件

- [ ] マッチ件数が数千件を超える実利用経路(Phase 5c Grep 結果のハイライト等)が実装された時点で、スクロール中のフレームタイムを実測
- [ ] 実測が 60fps 目標を満たさない場合、`std::lower_bound` ベースの最適化を実施しベンチマークで改善を実証
- [ ] 目標を満たす場合は「実測により許容範囲内と確認」の記録を残してこの Issue をクローズ
