# Issue: CSVグリッドが「末尾改行由来の暗黙の空行」を1行として表示してしまう (P2 — 既知の設計・UX再検討)

- **起票日:** 2026-08-19 (WI-16e 実機ドッグフーディングで発見)
- **対象:** `src/ui/src/csv_grid_pane.cpp`(`ui::CsvGridPane`) / `src/csvmode/src/csv_model.cpp`(`CsvModel::build()`)
- **優先度:** P2 (実害は視覚的な違和感のみ、データの欠落・誤りは無い)
- **対応 Phase:** 未定 (要望が出るか、Phase 10.2 の次のUI改善サブWI着手時に再評価)
- **親文書:** WI-16e (`build_plan.md` §5) 実機ドッグフーディングで発見・調査

## 事実

**これは新規のバグではなく、WI-16a で意図的に確定した既存仕様がグリッドUIで初めて視覚的に露呈したもの。** `csv_model.h`の`CsvModel::build()`のドキュメントコメントに明記済み:

> Every row (including a trailing implicit empty row when the document ends in '\n', and the document's own single empty row when it is entirely empty - the same convention document::Document::lineCount() already establishes) always has at least one cell

つまり末尾が`\n`で終わる(ごく普通の)CSVファイルは、`document::Document::lineCount()`の「末尾改行は暗黙の空行を1行増やす」という全モジュール共通の規約をそのまま継承し、`CsvModel::dataRowCount()`が実際のデータ行数+1になる。

WI-16e の実機ドッグフーディングで6行の通常のCSV(末尾`\n`あり)を`Ctrl+Shift+G`で開いたところ、グリッドの「#」列が1〜7まで表示され(実データは6行)、7行目が完全に空のセルとして描画されることを確認した。ソートすると、この空行は数値ソートで「値なし=最小」扱いとなり先頭または末尾に移動する(実際に確認済み)。フィルタは正しくこの空行を除外する(空文字列はどんな非空クエリにもマッチしないため)。

## 影響

- **データの欠落・誤りは無い。** `CsvModel`/`computeCsvRowOrder()`/`CsvGridPane`いずれも仕様通り正しく動作しており、ジャンプ機能やセル内容も破損していない。
- **ユーザー視点では「ファイルに存在しない空行が1行多く見える」というノイズになる。** テキストエディタとしては「末尾改行は空行1つ」という規約はDocument全体で一貫しているため筋が通っているが、**表形式(グリッド)での可視化は文章と異なり「余分な1行」が非常に目立つ** — テキストビューでの末尾空行はほぼ気づかれないのに対し、グリッドでは「#」列の数字が実データ件数と噛み合わない形で常に見えてしまう。
- ソート時にこの空行が先頭(昇順で最小値扱い)または末尾に移動するため、「なぜ空白の行が並び替えのたびに位置を変えるのか」がさらに分かりにくい。

## 対応方針 (未着手・複数の選択肢を検討)

1. **`CsvGridPane`側で「末尾の暗黙空行」を表示から除外する。** `CsvModel`自体の規約(Document全体と一貫性を保つ)は変えず、グリッド表示専用のフィルタとして「最後の1行が完全に空セルのみで、かつ元ドキュメントが`\n`で終わっている」場合にのみ`dataRowCount()`から1引いて表示する。ヘッドレスAPI(`CsvModel`/`computeCsvRowOrder()`)自体は無変更。
2. **何もしない(現状維持).** 「テキストエディタとしての一貫性」を優先し、「1行多く見える」を許容範囲のUXコストとして受け入れる。要件定義書・roadmapいずれもこの点への明示的な要求は無い。
3. **ユーザー設定で切替可能にする.** 過剰実装(v1のスコープを超える)と判断される可能性が高い。

現時点では対応方針を確定せず、要望が出るかPhase 10.2の次のUI改善サブWI(WI-16d完了記録が示す「列固定・セル編集」等)着手時に再評価する。

## 完了条件

- [ ] 上記3方針のいずれかを採用するかを決定する(ユーザー確認)
- [ ] (方針1採用の場合) `CsvGridPane`/`buildCsvGridColumnLabels()`/行順序計算のいずれかで、末尾の暗黙空行を表示対象から正しく除外する
- [ ] 除外してもソート/フィルタ双方の動作が壊れないことを確認する

## 再検証コマンド

```bash
grep -n "trailing implicit empty row" src/csvmode/include/neomifes/csvmode/csv_model.h
```
