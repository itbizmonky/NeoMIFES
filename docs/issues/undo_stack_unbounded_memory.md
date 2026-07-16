# Issue: UndoStack のメモリ使用量無制限成長(圧縮/ディスクスワップ未実装)

- **起票日:** 2026-07-16 (Phase 4a 実装中)
- **対象:** [`src/core/include/neomifes/core/undo_stack.h`](../../src/core/include/neomifes/core/undo_stack.h)
- **優先度:** 低(現時点では顕在化しうる駆動源が存在しない)

## 背景

Phase 4a で実装した `UndoStack` は `std::vector<std::unique_ptr<ICommand>>` を undo/redo それぞれ1本ずつ持つだけの単純な実装で、`detailed_design.md` §6.2 が構想していた1000件バケット化・zstd圧縮・メモリ予算超過時のディスクスワップ(`%LOCALAPPDATA%\NeoMIFES\undo\`)を実装していない。

## なぜ無制限のままにしたか

- 要件定義書 §5 は「Undo: 100万回以上」とのみ規定し、メモリ予算や圧縮方式は規定していない
- `tests/bench/core_undo_stack_bench.cpp` の実測(Release、1,000,000コマンド)では `BM_UndoStack_PushOneMillion` 338ms・`BM_UndoStack_UndoOneMillion` 172ms と、**時間面では** DoD を明確に満たしている(詳細は [ADR-012](../decisions/ADR-012-phase4a-editor-core-scope.md) 参照)
- **メモリ面は未計測。** ベンチはタイミングのみを測定しており、ピークメモリを実測していない。`InsertTextCommand` 1件あたりのサイズ概算(`std::u16string` のインライン/ヒープ表現 + vtable ポインタ + `unique_ptr`/ヒープアロケータのオーバーヘッド)から、100万件で **数十〜100MB オーダーになりうる**と見積もられるが、これは sizeof からの概算であり実測ではない
- Phase 4a には対話的キーボード入力(Phase 4b)が無いため、「実際のユーザー操作でどれだけの Undo エントリが蓄積するか」というリアルなシナリオ自体が今のコードベースでは再現できない

CLAUDE.md 絶対ルール3(推測実装をしない)・ルール10(性能改善はベンチマーク根拠必須)に照らし、実メモリ計測ができない段階で圧縮/ディスクスワップを先回りして実装するのは時期尚早と判断した([ADR-012](../decisions/ADR-012-phase4a-editor-core-scope.md) 参照)。

## リスクシナリオ

Phase 4b で対話的編集が実装された後、ユーザーが長時間のセッションで100万件規模の編集操作を行った場合、`UndoStack` のメモリ使用量はコマンド数に比例して増え続け、セッション終了まで解放されない。design doc が想定していた256MB予算を超過する可能性がある。

## 対応方針

**今回は対応しない。** Phase 4b で対話的編集が実装された後、実際の編集セッションでのメモリ増分を実測してから、必要であれば1000件バケット化+zstd圧縮、またはディスクスワップを検討する。

## 完了条件(将来この Issue に着手する場合)

- [ ] Phase 4b の対話的編集で、実際に100万件規模の編集操作を行った際のピークメモリ増分を実測(`neomifes::platform` の既存プロセスメモリ計測ユーティリティ、または `--measure-*` 系ハーネスと同じパターンの新規計測を追加)
- [ ] 実測結果が256MB予算(またはユーザーと合意した予算)を超過する場合、1000件バケット化+zstd圧縮の設計を別 ADR として起票(zstd は外部依存追加になるため CLAUDE.md ルール6の ADR 記録が必須)
- [ ] ディスクスワップが必要と判断された場合、`%LOCALAPPDATA%\NeoMIFES\undo\` への書き出し設計・失敗時のフォールバック(ディスク容量不足等)を別途設計
- [ ] `UndoStack` の単体テスト(`tests/unit/core_undo_stack_test.cpp`)に、バケット化・圧縮境界のテストを追加
