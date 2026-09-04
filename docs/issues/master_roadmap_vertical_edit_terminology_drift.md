# Issue: master_roadmap.md が「縦編集」の意味を確定済みの定義から無断で再定義していた (P2 — 解決済み、ドキュメント修正のみ)

- **起票日:** 2026-09-04 (WI-25着手前の完成度監査で発見)
- **解決日:** 2026-09-04 (同日、`master_roadmap.md`の記述をユーザー確認済みの原義へ修正)
- **対象:** `docs/design/master_roadmap.md`(§1.5 60機能マトリクス行112、§22 U#21、行3314)
- **優先度:** P2(実装への影響は無いが、次フェーズ候補選定を誤らせるドキュメント不整合)

## 事実

要件定義書§6が「縦編集」を必須機能として明記している。プロジェクト初期(Phase 0、self_review.md/ADR-012)の段階で、この用語の曖昧さ(「縦書き」か「列(カラム)編集」か)が既に指摘され、**ユーザー確認を経て「MIFES由来の縦編集 = 列単位の追記/削除」に確定していた**:

- `self_review.md` §該当行: 「用語が『縦書き』か『列(カラム)編集』か曖昧。**MIFES 由来の "縦編集" = 列単位の追記/削除** と解釈すべき。設計にその明記なし → 補強必要」→ 続く行で「Issue: 縦編集 = 列編集 (MIFES 由来) に確定」
- `docs/history/TIMELINE.md`(Phase 0該当セッション): 「ユーザー確認4項目(縦編集/独自マクロ/マクロ言語/ビルド)→全て推奨案採用」「『縦編集』= 列編集 (MIFES 由来) / 『独自マクロ』= キー操作記録」
- `docs/decisions/ADR-012-phase4a-editor-core-scope.md`: 「矩形選択・縦編集コマンド群(`ColumnInsert/Delete/Overwrite/Append`)を延期する理由」というセクションを設け、Phase 4aでは矩形選択・縦編集コマンド群を明示的にPhase 4b以降へ延期(削除ではない)
- `docs/design/detailed_design.md` §5.1.1「縦編集 (列編集 / MIFES 由来)」: この確定を反映し、`ColumnInsertCommand`/`ColumnDeleteCommand`/`ColumnOverwriteCommand`/`ColumnAppendCommand`という4つの専用コマンドの構想コードまで記載

ところが**後発の`master_roadmap.md`(v2.0/v2.1)がこの確定を参照せず、「縦編集」を独自に「縦編集(縦書き入力)」= DirectWriteの縦書きレイアウト対応として再定義していた**:

- `master_roadmap.md`行112(60機能マトリクス): `| 縦編集 (縦書き入力) | ○ | ○ | ◎ | **未計画** — v2.0 は「4b8 (研究)」としたが研究も実装も行われていない。Phase 12 前に要否を判断 (U#21) |`
- `master_roadmap.md`行3314(§22 U#21): `**縦編集 (縦書き入力) の要否** — v2.0 は「4b8 (研究)」としたが研究も実装も行われなかった。DirectWrite の縦書きレイアウト対応が必要で実装コストが大きい`

この結果、`gap_analysis.md`(2026-08-04中間レビュー)は「縦編集は未実装」という事実自体は正しく指摘したが、`master_roadmap.md`側の再定義をそのまま引用したため、規模感(「DirectWrite縦書きレイアウト、実装コスト大」)が実際の必要作業(「列編集コマンド4種、矩形選択自体は既にPhase 4b8a/4b8gで実装済み」)から大きく乖離したまま次フェーズ候補リストに残り続けていた。

## 影響

WI-25着手前の完成度監査で、この用語矛盾により「縦編集」候補の優先順位判断を一時保留した(規模が「小〜中」〔列編集〕なのか「大」〔縦書き入力、Phase 12前まで凍結中〕なのかが文書間で食い違っていたため)。実コード確認(`detailed_design.md` §5.1.1)により、矩形選択自体(`SelectionModel::setRectangularSelection()`/Shift+Alt+ドラッグ/Shift+Alt+矢印/`convertToLineEndCursors()`)は**既にPhase 4b8a/4b8gで実装済み**で、既存の`MultiCursorEditCommand`経由でタイプ入力・貼り付けもある程度動作することが判明した。残る未実装部分は`ColumnInsertCommand`/`ColumnDeleteCommand`/`ColumnOverwriteCommand`/`ColumnAppendCommand`という4つの専用コマンド(特に`ColumnAppendCommand`が「MIFESの縦編集の主用途」と明記されている)のみであり、「DirectWrite縦書きレイアウト対応」とは全く異なる、遥かに小規模な残作業である。

## 対応

`self_review.md`/ADR-012/`detailed_design.md`/TIMELINE.mdが一致して記録する**ユーザー確認済みの原義(列編集/MIFES由来)を正**とし、`master_roadmap.md`側の「縦編集(縦書き入力)」という記述を、原義への参照+実際の残作業(列編集専用コマンド4種)の記述へ修正した(本コミットで実施)。「縦書き入力」(DirectWrite縦組みレイアウト)という機能自体が不要というわけではなく、要件定義書に明記が無い以上、ユーザーが将来別途要望しない限り着手しない(現状スコープ外のまま)。

## 完了条件

- [x] `master_roadmap.md`の該当2箇所を原義(列編集)に修正し、混同されていた「縦書き入力」は別概念として明示的に切り離す
- [ ] 列編集コマンド4種(`ColumnInsertCommand`等)の実装は本issueのスコープ外(ドキュメント修正のみ)。実装するかどうか・どの範囲を実装するかは別途ユーザーへ確認、または次フェーズ候補として提示する

## 再検証コマンド

```bash
grep -rn "縦編集" docs/design/master_roadmap.md docs/design/detailed_design.md docs/design/self_review.md docs/decisions/ADR-012-phase4a-editor-core-scope.md
```
