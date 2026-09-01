# Issue: 大規模JSONファイルを開くだけでJSON構文ハイライトが長時間UIをハングさせる (P1 — 🟢 解決済み)

- **起票日:** 2026-09-01 ([`json_tree_ui_population_hang.md`](json_tree_ui_population_hang.md)の実機検証中に副次的に発見)
- **解決日:** 2026-09-01 (次フェーズ候補①として着手、当日中に解決)
- **対象:** `src/syntax/src/outline.cpp`(`extractOutline()`)。`src/render/src/syntax_worker.cpp`/`src/syntax/src/incremental_parser.cpp`は原因調査で無実と判明(下記参照)
- **優先度:** P1 (実害は約47秒のUIハング。JSON/XML Treeモードを一切使わなくても、大規模JSONファイルを開くだけで発生する)
- **対応 Phase:** 未定 (原因調査は未着手)

## 事実

[`json_tree_ui_population_hang.md`](json_tree_ui_population_hang.md)の修正を実機検証していた際、約78MB・145万行(1行1要素、改行区切り)のJSON配列ファイルを`--open`で開いたところ、**JSON構造ツリー機能に一切触れていないにもかかわらず**、プロセスが起動から約47秒間`Responding=False`(UIハング)であることを確認した。

同一内容のファイルを拡張子だけ`.txt`に変更して(構文ハイライト対象外にして)開いたところ、**約1秒で応答可能になった。** これにより、ハングの原因はJSON構文ハイライト(tree-sitter JSON文法によるトークン化)であり、`ui::JsonTreePane`とは完全に無関係な、独立した問題であると判断した。

## 影響

- JSON/XML Treeモード(Phase 10.3)を一度も使わないユーザーでも、大規模JSONファイルを開くだけでこの問題に遭遇する。
- v1出荷判定チェックリストの「10GBファイル対応」項目の一部として、`decode_cache_unbounded_growth.md`修正後にJSON/XML Treeモードの10GB再検証が未実施のまま残っていた経緯があり、本issueはその文脈で発見された。

## 実際の原因 (2026-09-01、診断ログで実測・確認済み)

標準の`DOGFOOD-TEMP`診断ログ手法(env var `NEOMIFES_DOGFOOD_PARSE_LOG`でゲート、`incremental_parser.cpp`/`syntax_worker.cpp`/`render_pipeline.cpp`の各段階へ一時的に埋め込み、実装完了後に全除去)で、47秒の内訳を実測した:

| 段階 | 実行スレッド | 実測コスト |
|---|---|---|
| `extractOutline()`(Breadcrumb/アウトライン抽出) | **UIスレッド、同期** | **約22.8秒** |
| `SyntaxWorker`のトークン着色パース(`IncrementalParser::reparseRange()`) | バックグラウンドスレッド、非同期 | 約15.4秒(parse 10.6秒+walk 4.8秒) |
| その他(デコード・minimap反映等) | 両方 | 数百ms |

**真の原因は`extractOutline()`だった。** `RenderPipeline::refreshDocumentCacheIfStale()`が言語設定時に`syntax::extractOutline()`をSYNCHRONOUSに(UIスレッド上で)呼び出す設計(Phase 7h、既存コメントで意図的な設計と明記)自体は把握されていたが、**`extractOutline()`は言語のシンボルテーブルが空(`emptySymbolTable()`)であっても無条件に文書全体をtree-sitterでフルパースしていた。** `src/syntax/src/outline.cpp`の`symbolTableFor(Language)`を確認したところ、JSON含む19言語(Json/Html/Css/Shell/Yaml/Toml/Xml/TypeScript/Tsx/Php/Markdown/PowerShell/Ini/Batch/Sql)が`emptySymbolTable()`を返す設計であり、これらの言語では`extractOutline()`が何を返しても空の`std::vector<OutlineNode>`にしかなり得ない(`walkForOutline()`が空のテーブルに対して何かを認識できることは原理的にない)。つまり145万行のJSONを22.8秒かけてフルパースした結果は、**最初から空だと分かっている結果**だった — 純粋に無駄な計算だった。

`SyntaxWorker`側のトークン着色パース(約15.4秒)は既に非同期設計(WorkerスレッドでUIをブロックしない)であり、UIの応答不能には無関係と確認できた。

## 既存issueとの関係 (2026-09-01、調査完了)

既存の[`tree_sitter_incremental_parse_cost.md`](tree_sitter_incremental_parse_cost.md)(P2、凍結)は「50万行で155.95ms」という**インクリメンタル再パース**(小さい編集1回あたり)の性能を扱っており、今回の「初回全文パースで145万行」とは異なる呼び出し経路(初回パース vs インクリメンタル編集)である。**両者は「tree-sitterのパースコストが文書サイズに比例する」という同じ根本的性質に起因するが、別々の消費者(`extractOutline()`と`SyntaxWorker`)が別々の理由でこれを踏んでいた。** `extractOutline()`側は「そもそも不要な計算だった」ことが判明し根絶できたが、`SyntaxWorker`のトークン着色パース(約15.4秒、非同期のためUIはブロックしないが完全にゼロにはできない)は本質的な制約であり、`tree_sitter_incremental_parse_cost.md`が扱う範囲として凍結状態のまま残る。

## 実装した修正 (2026-09-01)

`src/syntax/src/outline.cpp`の`extractOutline()`冒頭に、`symbolTableFor(language)`が返すテーブルが空である場合の早期リターンを追加した:

```cpp
const SymbolTable& table = symbolTableFor(language);
if (table.empty()) {
    return {};
}
```

これにより、アウトライン抽出をサポートしない19言語(JSON含む)では、tree-sitterへの`ts_parser_parse_string_encoding()`呼び出し自体が完全にスキップされる。C++/Python等、アウトラインをサポートする言語の挙動は一切変更されない(実機ドッグフーディングで確認済み、下記参照)。

## 実機検証結果 (2026-09-01、Release)

issueと同条件(78MB・145万行のJSON配列ファイル)で再検証した。

| 指標 | 修正前(実測) | 修正後(実測) |
|---|---|---|
| ファイルを開いてから応答可能になるまで | **約47秒** | **約1秒** |
| `extractOutline()`自体のコスト | 約22.8秒(UIスレッドをブロック) | **0.0ms**(早期リターン) |

**約47倍の改善。** 残る約15秒(`SyntaxWorker`の非同期トークン着色パース)はバックグラウンドで進行し、UIは終始応答可能なまま数秒後には構文ハイライトが正しく反映される(実機ドッグフーディングでスクリーンショット確認、キー・文字列・数値が正しく色分けされることを確認済み)。

実機ドッグフーディングでは、大規模JSONファイル(上記)に加え、C++ファイル(`src/csvmode/src/csv_model.cpp`)を開きBreadcrumb/アウトライン機能(`Ctrl+Shift+O`相当のトグル)を実際に操作し、`neomifes::csvmode`名前空間配下の全関数(`build`/`row`/`headerRow`/`dataRowCount`/`dataRow`/`csvCellValue`×2/`escapeCsvCellText`)が従来通り正しく一覧表示されることを確認し、回帰が無いことを実証した。

## 完了条件

- [x] ハングの発生箇所・原因を特定する — `extractOutline()`が、言語のシンボルテーブルが空でも無条件にフルパースしていたことが真因(標準プローブ/診断ログで実測)
- [x] `tree_sitter_incremental_parse_cost.md`との関係を明らかにする — 同じ根本的性質(tree-sitterの文書サイズ比例コスト)に起因するが、別々の消費者・別々の理由。`extractOutline()`側は不要な計算だったため根絶、`SyntaxWorker`側の本質的なコストは同issueの範囲として引き続き凍結
- [x] 修正方針を決定する(ユーザー確認) — 空のシンボルテーブルなら即座に空を返す早期リターン。設計上のトレードオフが無い(既存の挙動をどの言語でも変えない)ため、AskUserQuestionでの選択肢提示は不要と判断し直接実装した
- [x] 修正を実装し、大規模JSONファイルを開いても実用的な時間で応答性を維持することを確認する — 47秒→約1秒(約47倍改善)を実機で確認

## 再検証コマンド

```powershell
# 改行区切りの大規模JSON配列ファイル(145万要素)を生成し、開いた直後の
# Responding状態をポーリングする。同一内容を.txt化して比較すると原因の
# 切り分けができる。
$sw = [System.IO.StreamWriter]::new("large.json", $false, [System.Text.Encoding]::UTF8)
$sw.Write("[`n")
for ($i = 0; $i -lt 1450000; $i++) {
    $sep = if ($i -lt 1449999) { "," } else { "" }
    $sw.Write('  {"id":' + $i + ',"name":"item' + $i + '","value":' + ($i * 1.5) + "}$sep`n")
}
$sw.Write("]`n")
$sw.Close()

$proc = Start-Process -FilePath "build\release\src\app\NeoMIFES.exe" -ArgumentList "--open","large.json" -PassThru
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Seconds 1
    $proc.Refresh()
    Write-Output "t=${i}s responding=$($proc.Responding)"
}
```
