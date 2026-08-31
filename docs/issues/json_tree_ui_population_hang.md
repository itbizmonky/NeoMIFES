# Issue: JSON/XMLツリーUIが大規模ファイルでUIスレッドを長時間ハングさせる (P1 — 未対応)

- **起票日:** 2026-08-30 (v1出荷判定、JSON/XML Treeモードの大規模ファイル実機検証中に発見)
- **対象:** `src/ui/src/json_tree_pane.cpp`(`ui::JsonTreePane`、WI-15c実装、WI-15hでXML側と共通化)
- **優先度:** P1 (実害は3分以上のUIハング、メモリ枯渇は伴わないため[decode_cache_unbounded_growth.md](decode_cache_unbounded_growth.md)より緊急度は低い)
- **対応 Phase:** 未定 (原因調査は未着手、実装のみで解決するか設計変更が必要かも未確定)

## 事実

v1出荷判定の「10GBファイル対応(JSON/XML Treeモード)」検証で、約100MB・145万要素のJSON配列ファイルを`--open`で開いた直後に「Toggle Structure Tree」コマンド(`view.jsonTree.toggle`)を実行してJSONツリーモードを有効化したところ、コマンド実行直後からプロセスが`Responding=False`(UIハング)になり、**168秒(約3分)経過してもハングが解消しなかった**ため、これ以上待たずに`taskkill`で強制終了した。

メモリは3.7GB前後で安定しており(こちらは[decode_cache_unbounded_growth.md](decode_cache_unbounded_growth.md)のようなOOMではない)、緊急停止が必要な危険な状態ではなかったが、実用に耐えないレベルの応答不能である。

## 推定原因 (未検証)

`ui::JsonTreePane`(WI-15c実装)によるツリーアイテム構築、および`ui::JsonTreePane`自体への流し込み処理が、145万ノードに対して同期的・非仮想化(non-virtualized)な実装になっている可能性が高い。CSVモードの`CsvGridPane`が`WC_LISTVIEW`の仮想モード(`LVS_OWNERDATA`)を採用しているのに対し、`JsonTreePane`は「実際の`WC_LISTVIEW`アイテム」を使う設計だとメモリ内に記録されている(仮想モードではない) — これが真の原因である可能性が高いが、本issue起票時点では未検証。

**重要な設計上の関連性:** WI-15h(2026-08-25)で「`Ctrl+Shift+J`をJSON/XML両対応の単一トグルへ統一」しており、`ui::JsonTreePane`はJSON・XML共通のUI実装である。つまりこの問題はJSONツリーだけでなくXMLツリーモードにも同一のボトルネックとして存在する可能性が高い(未検証)。

## 対応方針 (未着手)

1. `ui::JsonTreePane`の実装と、ツリーアイテムをそこへ流し込むapp層のブリッジ関数(`buildJsonTreeItems()`等)を読み、145万ノード規模で何が同期的・O(N)以上のコストになっているか特定する(候補: 非仮想化`WC_LISTVIEW`への逐次`ListView_InsertItem`呼び出し、折り畳み状態計算、文字列コピー等)。
2. 1万・10万・100万ノードなど複数スケールで実測し、要素数に対するコストの増加傾向(線形かそれ以上か)を確認する。
3. XMLツリーモードでも同じ問題が再現するか(同じ`ui::JsonTreePane`を使うため理論上は再現するはず)を小規模ファイルで確認する。
4. 修正方針の選択肢を検討する(例: `CsvGridPane`と同様の仮想モード化、遅延ロード、折り畳みデフォルトでの初期表示ノード数削減等)。

## 完了条件

- [ ] 原因を特定する(上記調査ステップ)
- [ ] 修正方針を決定する(ユーザー確認)
- [ ] 修正を実装し、100万ノード規模でも実用的な時間(具体的な目標値は要検討)で応答性を維持することを確認する

## 再検証コマンド

```bash
grep -n "ListView_InsertItem\|LVS_OWNERDATA" src/ui/src/json_tree_pane.cpp
```
