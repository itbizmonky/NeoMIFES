# Issue: `tree-sitter-xml`が約505階層を超えるネストで整形式入力を誤検知する (クラッシュではなくパース精度の限界)

- **起票日:** 2026-08-25 (WI-15f 実装・単体テスト作成時に判明)
- **対象:** [`src/xmltree/src/xml_tree.cpp`](../../src/xmltree/src/xml_tree.cpp) の `parseXmlTree()`(内部で呼ぶ `tree_sitter_xml()` パーサ本体)
- **優先度:** P2 (低頻度・安全なフォールバックあり)
- **関連:** `docs/design/build_plan.md` WI-15f 計画、[`tests/unit/xmltree_xml_tree_test.cpp`](../../tests/unit/xmltree_xml_tree_test.cpp) の `WellFormedDeeplyNestedDocumentParsesWithoutCrashing`

## 背景

WI-15f計画の「実装前の技術検証」ステップで、標準入力プローブ (`ts_probe_xmltree`) により深いネスト (5000階層、整形式・バランス済み) を投入したところ `PARSE SURVIVED`(クラッシュなし)としつつも `hasError=1` という不可解な結果が出ており、計画時点では「フラットなERRORノードへの縮退が起きているようだが原因未特定」として保留していた。

実装完了後、単体テスト `WellFormedDeeplyNestedDocumentParsesWithoutCrashing`(当初 depth=3000 で作成)が実行時に失敗し、`hasErrors=true` かつ `root.tagName` が空という結果になった。これを受けて二分探索プローブ (`ts_probe_xmldepth`、スクラッチのみ・コミットなし) で閾値を実測した。

## 実測結果

`<a><a>...<a>x</a>...</a></a>` 形式の整形式・バランス済み入力(XMLタグのネスト深さを可変)を `tree_sitter_xml()` で直接パースし、`ts_node_has_error(root)` を確認:

| XMLタグのネスト深さ | `ts_node_has_error()` |
|---|---|
| 500以下 | `false`(正しく1段のdocumentノードとして解析) |
| 505 | `false` |
| 510以上 | **`true`**(ルートが`ERROR`型ノードへ縮退、`root`フィールド解決不能) |

**閾値は XMLタグのネスト深さ 505〜510 の間。** これより深いと、`ts_node_child_by_field_name(root, "root", 4)` が null を返し、本プロジェクトの `parseXmlTree()` は既存の「ルート要素が解決できない場合は `XmlNodeKind::Error` センチネル」という設計により、文書全体を1つの不透明な `Error` ノードとして返す(クラッシュはしない)。

## 原因の推定(未確定)

tree-sitter-xmlの文法は `element → content → element → ...` という2段階の入れ子構造を持つため(`element`ノードの子に`content`ノードがあり、`content`ノードの子に次の`element`ノードがある)、XMLタグの深さ1段につき文法規則レベルでは約3段(STag/EmptyElemTag、content、element自身の再帰)を消費する。実測の閾値(XMLタグ深さ約505〜509 = 内部的には約1500段前後)が2の冪に近い(512)ことから、tree-sitterランタイム内部の固定サイズスタック/配列(パーサの縮約スタックまたはエラー回復時の探索深度制限)に起因する可能性が高いが、`tree-sitter`本体のソース(`build/debug/_deps/tree-sitter-src/lib/src/`)を読んで確定させるところまでは未実施。

## 対応方針

**ワークアラウンドは実装しない。** 理由:

1. **クラッシュ・スタックオーバーフローではない。** 本プロジェクト自身の `buildXmlTree()` は明示スタックによる反復実装であり、この問題とは無関係に安全(WI-15f計画のDoD「深いネスト入力でのスタックオーバーフロー有無を実測」は別途5000階層まで安全と確認済み)。
2. **既存の`XmlNodeKind::Error`センチネル設計が、この場合も安全に縮退する。** 該当文書は「パースできない文書」として扱われる(不正確ではあるが、クラッシュや不正なメモリアクセスは発生しない)。
3. **実用上の発生頻度が極めて低いと考えられる。** 505階層を超える深さのXMLは、人手で書かれた設定/データファイルは元より、機械生成のXMLでも通常発生しない極端なケース(典型的な深いXMLスキーマでも数十階層程度)。
4. 回避策(事前にブラケットカウントで深さを検知し別処理に切り替える、パーサを差し替える等)は、この極端な入力1件のためだけに実装・保守コストに見合わない複雑さを持ち込む。

## 完了条件 (将来この Issue に着手する場合)

- [ ] tree-sitter本体のソースを読み、505〜510という閾値の正確な原因(内部スタック/配列のサイズ制限か)を特定する
- [ ] 実際のユーザーがこの制約に遭遇した実例が確認された場合、対応要否を再評価する
