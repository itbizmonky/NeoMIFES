# Issue: 大規模JSONファイルを開くだけでJSON構文ハイライトが長時間UIをハングさせる (P1 — 未対応)

- **起票日:** 2026-09-01 ([`json_tree_ui_population_hang.md`](json_tree_ui_population_hang.md)の実機検証中に副次的に発見)
- **対象:** 未特定 (`src/render/src/syntax_worker.cpp`、または`src/syntax/`のtree-sitter JSON文法統合部分と推定、原因調査は未着手)
- **優先度:** P1 (実害は約47秒のUIハング。JSON/XML Treeモードを一切使わなくても、大規模JSONファイルを開くだけで発生する)
- **対応 Phase:** 未定 (原因調査は未着手)

## 事実

[`json_tree_ui_population_hang.md`](json_tree_ui_population_hang.md)の修正を実機検証していた際、約78MB・145万行(1行1要素、改行区切り)のJSON配列ファイルを`--open`で開いたところ、**JSON構造ツリー機能に一切触れていないにもかかわらず**、プロセスが起動から約47秒間`Responding=False`(UIハング)であることを確認した。

同一内容のファイルを拡張子だけ`.txt`に変更して(構文ハイライト対象外にして)開いたところ、**約1秒で応答可能になった。** これにより、ハングの原因はJSON構文ハイライト(tree-sitter JSON文法によるトークン化)であり、`ui::JsonTreePane`とは完全に無関係な、独立した問題であると判断した。

## 影響

- JSON/XML Treeモード(Phase 10.3)を一度も使わないユーザーでも、大規模JSONファイルを開くだけでこの問題に遭遇する。
- v1出荷判定チェックリストの「10GBファイル対応」項目の一部として、`decode_cache_unbounded_growth.md`修正後にJSON/XML Treeモードの10GB再検証が未実施のまま残っていた経緯があり、本issueはその文脈で発見された。

## 既存issueとの関係 (要調査)

既存の[`tree_sitter_incremental_parse_cost.md`](tree_sitter_incremental_parse_cost.md)(P2、凍結)は「50万行で155.95ms、DoD ≤50ms未達」という**インクリメンタル再パース**(小さい編集1回あたり)の性能を扱っており、今回発見した「**初回全文パースで145万行・47秒**」とは規模感が大きく異なる(155ms vs 47,000ms、桁が3桁以上違う)。同一の根本原因(tree-sitterの文書サイズ比例コスト)が「初回フルパース」という別の経路で顕在化した可能性もあるが、**両者が本当に同じ原因かは未検証。** 原因調査時に、この関係を明らかにすること。

## 対応方針 (未着手)

1. ハングの発生箇所を特定する(`src/render/src/syntax_worker.cpp`の初回パース処理が有力候補、非同期化されているか・同期的にUIスレッドをブロックしているかを確認)。
2. `tree_sitter_incremental_parse_cost.md`との関係を明らかにする(同一原因か、別の経路の別問題か)。
3. 修正方針を検討する(候補: 初回パースの非同期化、可視範囲のみの遅延ハイライト、行数上限を超えたファイルでの構文ハイライト無効化フォールバック等)。

## 完了条件

- [ ] ハングの発生箇所・原因を特定する
- [ ] `tree_sitter_incremental_parse_cost.md`との関係を明らかにする
- [ ] 修正方針を決定する(ユーザー確認)
- [ ] 修正を実装し、大規模JSONファイルを開いても実用的な時間で応答性を維持することを確認する

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
