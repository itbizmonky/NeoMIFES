# Issue: 検索(正規表現の `$`/`^`)が CRLF 行末を考慮しない

- **起票日:** 2026-07-19 (Phase 5a コードレビューで指摘、`/code-review` high effort)
- **対象:** [`src/search/src/search_service.cpp`](../../src/search/src/search_service.cpp) の `scanDocument()`(行分割ロジック)
- **優先度:** 中 (Windows ネイティブエディタとして CRLF は既定の改行コードであり、要件定義書§2「日本語編集に強い」と同水準で重要なユーザー体験に影響しうる)

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

## 完了条件 (将来この Issue に着手する場合)

- [ ] CRLF 文書に対する `Query{.pattern=u"...$", .regex=true}` が視覚上の行末に正しくマッチする
- [ ] LF のみの文書での既存の挙動(全271+回帰テスト)に影響が無い
- [ ] `core::selection_model.cpp` の同種の制約(word movement 等)を同時に直すかどうかを明示的に判断し、直さない場合はその理由をこの Issue に追記する
