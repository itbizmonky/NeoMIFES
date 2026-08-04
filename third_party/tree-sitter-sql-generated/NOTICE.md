# tree-sitter-sql (vendored, generated)

このディレクトリは `neomifes::syntax` の SQL 構文ハイライト (Phase 7y) が使う
tree-sitter 文法一式を含む。他の全 tree-sitter 文法 (`cmake/Dependencies.cmake`
の `FetchContent_Declare` 群) とは異なり、**このディレクトリはビルド時に
ダウンロードされない** — `src/parser.c` を含め、全てこのリポジトリに直接
コミットされている。理由は [`docs/decisions/ADR-021-sql-grammar-vendored-generation.md`](../../docs/decisions/ADR-021-sql-grammar-vendored-generation.md) を参照。

## 由来

- 上流リポジトリ: <https://github.com/DerekStride/tree-sitter-sql>
- タグ: `v0.3.11`
- コミット: `7b51ecda191d36b92f5a90a8d1bc3faef1c7b8b8`
- ライセンス: MIT ([`LICENSE`](LICENSE) に全文を上流からそのままコピー)

## ファイルの由来内訳

- `src/scanner.c` — 上流 `v0.3.11` の `src/scanner.c` をバイト単位でそのまま
  コピー(人手による改変なし)。
- `src/tree_sitter/{parser.h,alloc.h,array.h}` — `tree-sitter generate` が
  `parser.c`と同時に生成したtree-sitterランタイムヘッダ一式。他の全grammarが
  それぞれ自分の`src/tree_sitter/`配下に同種のコピーを持つのと同じ規約(本体の
  `tree-sitter`コア(`build/*/​_deps/tree-sitter-src/lib/include`)が公開する
  `tree_sitter/api.h`とは別物 — grammarの`parser.c`/`scanner.c`はこちらの
  `tree_sitter/parser.h`を`#include`する)。
- `src/parser.c` — **機械生成物**。上流は `src/parser.c` をコミットしておらず、
  `grammar.js` から `tree-sitter generate` を実行して都度生成する設計になって
  いる(上流の `CMakeLists.txt` 自身がこれを行う)。本プロジェクトでは
  2026-08-04、tree-sitter CLI **v0.26.11**(本プロジェクトが依存する
  tree-sitter コア本体 `cmake/Dependencies.cmake` の `GIT_TAG` と同一バージョン、
  ABI不一致リスクなし)を使い、開発機上で一度だけ生成した。

## 将来、SQL 文法のバージョンを上げる場合の再生成手順

1. 上流リポジトリを目的のタグで `git clone --branch <new-tag> --depth 1 https://github.com/DerekStride/tree-sitter-sql.git`
2. tree-sitter CLI のスタンドアロン Windows バイナリ (`tree-sitter-cli-windows-x64.zip`、
   <https://github.com/tree-sitter/tree-sitter/releases> から入手、Node.js不要) を
   ダウンロード・展開。本プロジェクトが依存する tree-sitter コア本体
   (`cmake/Dependencies.cmake` の `GIT_TAG`) と同じバージョンを使うこと(ABI不一致回避)。
3. クローンしたディレクトリ内で `tree-sitter generate grammar.js` を実行
   (`grammar.js` の import は全て相対パスのローカルファイルのみのため、
   `npm install`/`node_modules` は不要)。
4. 生成された `src/parser.c` と、上流の `src/scanner.c` をこのディレクトリの
   `src/` へコピーし、`LICENSE` も上流から再コピーする。
5. `src/syntax/src/syntax_internal.h` の `namedLeafKindsForSql()` テーブルが
   新バージョンでも妥当か、実機probe(使い捨て、コミットしない)で再確認する
   (CLAUDE.mdルール3 — 記憶からの推測をしない。特に `keyword_*` 命名規則や
   `literal`/`comment`/`marginalia` ノード種別に変更が無いかを確認)。
6. このファイルの「由来」節(タグ・コミット・生成日・使用した tree-sitter CLI
   バージョン)を更新する。

## トレードオフ (ADR-021 参照)

この方式は「`tree-sitter` CLI をビルド依存として導入し毎回 `generate` する」
方式と比べ、CI・将来の全ビルドが完全に静的ファイルのコンパイルのみで完結し
(新規ツールプロビジョニング不要、ビルド時に第三者バイナリを実行しない)、
決定論的なビルドを維持できる利点がある。代わりに、SQL 文法のバージョンを
上げる際は上記手順を **手動で** 実行し再コミットする必要がある(自動追従しない)。
