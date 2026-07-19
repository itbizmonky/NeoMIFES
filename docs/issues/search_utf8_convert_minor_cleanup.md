# Issue: `search`/`utf8_convert` の小規模改善候補 3件

- **起票日:** 2026-07-19 (Phase 5a コードレビューで指摘、`/code-review` high effort)
- **対象:** [`src/util/src/utf8_convert.cpp`](../../src/util/src/utf8_convert.cpp)、[`src/search/src/search_service.cpp`](../../src/search/src/search_service.cpp)
- **優先度:** 低 (いずれも現状の正しさ・性能に実害は無い軽微な指摘。まとめて着手する方が効率的なため1つの Issue に集約)

## 1. `decodeOne()` に `noexcept` が付いていない

`src/util/src/utf8_convert.cpp` の無名 namespace 内 `decodeOne()` はアロケーションを行わず例外も投げないが、`noexcept` が付与されておらず、なぜ付けていないかのコメントも無い。CLAUDE.md §4「`const`/`noexcept`/`constexpr` を積極付与。noexcept 保証できない箇所は例外仕様を明記」に反する。

対応: `decodeOne()`(および同様の性質を持つ他のヘルパー)に `noexcept` を付与するだけの一行修正。

## 2. UTF-16 サロゲート変換ロジックの重複

`utf8_convert.cpp` は UTF-16 サロゲートペアの符号化・復号ロジック(`0xD800`/`0xDBFF`/`0xDC00`/`0xDFFF`/`0x10000` の境界定数とシフト/マスク演算)を独自に実装しているが、`src/document/src/original_buffer.cpp` に既に(逆方向の)UTF-8→UTF-16 デコード・UTF-16 サロゲート符号化ロジックが存在する。両者は完全に独立しており、孤立サロゲートの扱いも異なる(`original_buffer.cpp` はロード時にエラーとして拒否、`utf8_convert.cpp` は実行時に U+FFFD へ置換)。

対応方針の選択肢:
- サロゲート境界定数・変換ロジックを共有ヘルパー(例: `src/util/` 配下の新規ヘッダ)に抽出し、両ファイルから参照する
- あるいは、「ロード時に拒否 vs 実行時に置換」という挙動の違いが意図的な設計判断であることをコメントで明記し、重複自体は許容する(理由: `original_buffer.cpp` は信頼できるファイル入力のみを扱うのに対し、`utf8_convert.cpp` は将来プラグイン等が渡す可能性のある任意の `u16string` も扱うため、防御的な挙動の違いは正当化できる)

いずれを取るかはユーザー確認の上で決定する(CLAUDE.mdルール3)。

## 3. `compile()` がパターン変換で不要なオフセット表を構築している

`search_service.cpp` の `compile()` は検索パターンを `util::toUtf8WithOffsets()` で変換しているが、`.utf8` フィールドしか使わず `.byteToUtf16` は毎回構築されて即座に破棄される。パターン文字列は通常短いため実害はほぼ無いが、`SearchService::findAll()` の呼び出しごとに無駄な `std::vector<std::uint32_t>` 構築が発生する。

対応: オフセット表を必要としない `toUtf8(std::u16string_view) -> std::string` を `neomifes::util` に追加し、`compile()` はそちらを使うよう変更する。

## 完了条件 (将来この Issue に着手する場合)

- [ ] `decodeOne()` に `noexcept` を付与、`ubsan`/Debug/Release 全green確認
- [ ] サロゲート変換ロジックの重複について、共有ヘルパー化 or 意図的重複としてコメント明記のいずれかを実施
- [ ] `toUtf8()`(オフセット表無し版)を追加し `compile()` から利用、既存テスト全green確認
