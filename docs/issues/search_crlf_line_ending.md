# Issue: 検索(正規表現の `$`/`^`)が CRLF 行末を考慮しない (P1 — 🟢 解決済み)

- **起票日:** 2026-07-19 (Phase 5a コードレビューで指摘、`/code-review` high effort)
- **解決日:** 2026-09-03
- **対象:** [`src/search/src/search_service.cpp`](../../src/search/src/search_service.cpp) の `scanDocument()`(行分割ロジック)
- **優先度:** P1 (Windows ネイティブエディタとして CRLF は既定の改行コードであり、要件定義書§2「日本語編集に強い」と同水準で重要なユーザー体験に影響しうる)

## 背景

`scanDocument()` は `'\n'` でのみ行分割を行い、直前の `'\r'` は行内容として保持したまま各行を `findAllInLine()` に渡す。これは `src/core/src/selection_model.cpp` の既存 `lineContentEnd()`(word movement 等が使用)と同じ「CR は行末の一部として扱い、Phase 6 Encoding Engine まで対応を持ち越す」という既存の合意済み制約を踏襲したものであり、Phase 5a 単独の新規後退ではない。

しかし正規表現検索では、この制約が単語移動などより体感しやすい形で表面化する:

```cpp
Query{.pattern = u"bar$", .regex = true}
```

を `u"foo bar\r\nbaz"` に対して実行すると、RE2 の `$` は渡された行バッファ(`"foo bar\r"`)の**実際の末尾**にアンカーするため、`\r` を含めた末尾を要求してしまい、視覚的には行末の "bar" にもかかわらずマッチしない。

## 対応方針 (未着手)

以下のいずれかで解消可能:

1. **`scanDocument()` 側で `\r\n` を認識し、行バッファから末尾の `\r` を除いて `findAllInLine()` に渡す。** ただし、除いた `\r` を含めたオフセット計算(マッチ位置を `TextRange` に変換する際の `lineStart` の扱い)への影響を確認する必要がある。単純に `\r` を捨てるだけなら現状のオフセット計算はそのまま使えるはず(`\r` は「行の外」にあるものとして扱えばよい)
2. **Phase 6 Encoding Engine 側で改行コード正規化を行い、内部表現を常に `\n` のみにする**(BOM/エンコード判定と同じレイヤーで解決)方針を取るなら、この Issue は Phase 6 側へ吸収される

案1は `search::` モジュール単独で完結する局所的な修正、案2はより根本的だが Phase 6 着手を待つ必要がある。**まず案1で `search::` 単独の対症療法を入れるか、`core::`側の既存の同種制約(word movement 等)と足並みを揃えて Phase 6 まで待つかは、ユーザーとの方針確認が必要**(CLAUDE.mdルール3: 推測実装をしない)。

**2026-09-03追記(着手時点で判明した設計上の前提の変化):** 本Issue起票時(Phase 5a、2026-07-19)の`scanDocument()`は行ごとにバッファを分けて`findAllInLine()`へ渡す設計だったが、Phase 5b1(マッチが行境界をまたげるようにする変更)で文書全体を1つのバッファとして`(?m)`フラグ付きでRE2へ渡す設計へ変わっており、`findAllInLine()`という関数自体がもう存在しない。根本原因(RE2の`(?m)`モードの`$`/`^`は`\n`の直前/直後にしかアンカーせず、`\r\n`を1単位として認識しない)は変わらず有効だが、対応方針は本Issue起票時の想定(案1の「行バッファから`\r`を除く」)をPhase 5b1後の単一バッファアーキテクチャ向けに再設計する必要があった。

**採用した実装(案1の考え方をPhase 5b1後の設計へ適用):** `scanDocument()`が文書全体を連結したバッファをRE2へ渡す直前に、新規`stripCrBeforeLf()`で`\n`の直前の`\r`だけを取り除く(単独の`\r`、いわゆる旧Mac形式の改行はそのまま残す——`core::selection_model.cpp`の`lineContentEnd()`等、このコードベースの他の箇所が既に踏襲している「`\n`だけが行区切り」という規約と一貫させるため)。取り除いた分の位置ズレは、新規`boundaryToOriginal`(境界位置→元の文書上の位置、`\r`の直前を指す「左寄せ」規約)で復元し、`findAllInBuffer()`が返すマッチ位置(範囲全体+キャプチャグループ全て)を1パスで元の文書座標へ変換し直す。`\r`が1文字も無い文書(LFのみ、大多数のケース)は`stripCrBeforeLf()`自体を呼ばず既存コードパスをそのまま通る最適化を入れた。

**`core::selection_model.cpp`の同種の制約(word movement等)は今回は直さない、明示的な判断として記録する。** 理由: (1) `lineContentEnd()`は`moveVertically()`/`skipWhitespaceForward/Backward()`/`moveByWordForward/Backward()`/複数カーソル矩形選択/`selectWordAt()`/`selectLineAt()`など9箇所以上から使われており、本Issue(検索の`$`/`^`)単独の修正よりはるかに影響範囲が大きい。(2) 実害はほぼ視覚的に現れない——`\r`は何のグリフも描画しないため、End押下でカーソルが`\r`の直後(`\n`の直前)に来ても、視覚上は「行の真の末尾」にいるのと区別がつかない(検索の`$`アンカーのように「マッチが見つからない」という明確な失敗症状が無い)。(3) 起票当初の「Phase 6 Encoding Engineまで持ち越す」という前提自体、本セッション時点でPhase 6という括り自体が(WI番号体系への移行に伴い)既に形骸化しており、根本的な改行コード正規化を今から新設計するのは本Issueのスコープを大きく超える。将来もし実際にユーザーから体感される実害(例: 矩形選択でCRLF文書だけ挙動がずれる、等)が報告されたら、その時点で独立した作業項目として再検討する。

**既知のトレードオフとして記録:** `stripCrBeforeLf()`はCRLFの`\r`をRE2に一切見せなくする設計のため、CRLFの`\r`そのものを明示的に検索対象にする正規表現(例: パターン`"\\r"`)は、CRLFペアの`\r`をヒットしなくなった(単独の`\r`は引き続きヒットする)。`\r`を「行の外にある、行内容としては透過的な文字」として扱うという設計そのものの自然な帰結であり、意図的なトレードオフとしてテスト(`LiteralCarriageReturnSearchFindsLoneCrButNotACrlfPairsCr`)で明示的に固定した。

## 完了条件

- [x] CRLF 文書に対する `Query{.pattern=u"...$", .regex=true}` が視覚上の行末に正しくマッチする — `DollarAnchorMatchesVisualEndOfLineOnCrlfDocument`/`CaretAnchorMatchesVisualStartOfLineOnCrlfDocument`/`MultipleCrlfLinesAllAnchorCorrectlyNotJustTheFirst`/`CapturingGroupRangesAreRemappedOnCrlfDocumentToo`で確認
- [x] LF のみの文書での既存の挙動(全1572件の回帰テスト)に影響が無い — `\r`が1文字も無い文書は`stripCrBeforeLf()`を呼ばない専用の早期リターンで既存コードパスをそのまま通るため、`CrlfHandlingLeavesLfOnlyDocumentsByteForByteUnaffected`含む既存の全テストが無変更のまま green
- [x] `core::selection_model.cpp` の同種の制約(word movement 等)を同時に直すかどうかを明示的に判断し、直さない場合はその理由をこの Issue に追記する — 上記「2026-09-03追記」参照、明示的に「今回は直さない」と判断・理由を記録済み
