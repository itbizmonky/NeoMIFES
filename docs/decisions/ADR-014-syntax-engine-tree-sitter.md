# ADR-014: 構文解析エンジンに tree-sitter を採用する (ADR-003 を置き換え)

- **ステータス:** Accepted
- **決定日:** 2026-07-21

## コンテキスト

Phase 7 (シンタックスハイライト・アウトライン・折り畳み・ミニマップ等) 着手にあたり、構文解析エンジンの実装に着手する前に既存 [ADR-003](ADR-003-syntax-definition.md) (Phase 0、2026-07-14 決定、「シンタックス定義に TextMate 互換文法を採用する」) を再検証した。

ADR-003 は「`.tmLanguage.json` 形式は VS Code / Sublime Text / Atom が使う共通フォーマットで 100+ 言語分が MIT/BSD で既に整備されており、コピペで導入可能」を主要な根拠としていた。しかし着手前調査で、これは文法**定義ファイル** (`.tmLanguage.json`) が再利用可能という話であり、それを解釈する**インタプリタ実装**が C++ 向けに存在するかどうかとは別問題であることが判明した。Web 調査の結果、TextMate 文法インタプリタの成熟した実装は TypeScript (`microsoft/vscode-textmate`)・C# (`TextMateSharp`、`vscode-textmate` の .NET 移植)・Java (`eclipse/tm4e`) にしか存在せず、**C++ 向けの既製ライブラリは見つからなかった**。採用する場合、スコープスタック管理・oniguruma 正規表現・`begin`/`end`/`while` パターンマッチング・ネストキャプチャ・grammar injection を含むインタプリタ本体 (数千行規模) を C++ で新規に手書きする必要があり、CLAUDE.md 絶対ルール 3「推測実装をしない」の精神に照らしてリスクが高いと判断した。

## 選択肢

1. **tree-sitter** (C ライブラリ、MIT)
2. TextMate 互換文法インタプリタの自作 (ADR-003 の既定路線)
3. LSP の `semanticTokens` API への一任
4. 自作 lexer 群

## 決定

**tree-sitter を採用する。ADR-003 は本 ADR によって Supersede される。**

## 根拠

- **成熟した C ライブラリが既に存在する:** `tree-sitter/tree-sitter` (MIT、最新リリース `v0.26.11`) はルートに `CMakeLists.txt` を持ち、RE2 (ADR-002) / nlohmann-json (ADR-013) と同じ `FetchContent_Declare` + `EXCLUDE_FROM_ALL` パターンでそのまま導入できることを実機確認済み (下記「実装上の注意点」参照)
- **真の増分パース:** roadmap §7.9 が要求する「非同期増分解析 (変更範囲を含む解析単位だけ再解析)」という設計目標に、tree-sitter の増分パース機構 (`ts_tree_edit` + 旧木を渡しての再パース) が正面から適合する。TextMate 文法は行単位の正規表現再走査が基本であり、真の増分解析ではない
- **言語グラマー資産:** 各言語ごとに独立したリポジトリ (例: `tree-sitter/tree-sitter-cpp`、MIT、`v0.23.4`) が存在し、roadmap §7.2 の必須言語の大半をカバーできる見込み
- **内部標準 UTF-16 との親和性:** `ts_parser_parse_string_encoding(..., TSInputEncodingUTF16LE)` で `std::u16string` を直接パースでき、UTF-8 への往復変換が不要。スタンドアロン probe で実機確認済み (下記参照)
- **不正な入力に対する堅牢性:** 構文的に不正なソースでもパース自体は失敗せず、エラーノードを含む部分木を返す (`ts_node_has_error()`)。エディタが常に何らかの構文木を必要とする用途に適合

## 却下理由

- **TextMate 文法インタプリタの自作:** 上記コンテキストの通り、C++ 向け既製ライブラリが存在せずインタプリタ本体を新規に手書きする必要がある。文法定義ファイル自体は流用できても実装コストは「ほぼゼロから書く」に等しく、CLAUDE.md ルール 3 の観点でリスクが高い
- **LSP の `semanticTokens` 依存:** LSP サーバが存在しない言語 (INI/BAT/VBS 等、roadmap §7.2 の必須言語に含まれる) がハイライトされなくなる。ADR-003 の却下理由をそのまま継承
- **自作 lexer 群:** 保守が破綻する。ADR-003 の却下理由をそのまま継承

## 影響

- `cmake/Dependencies.cmake` へ tree-sitter core (`v0.26.11`) + `tree-sitter-cpp` (`v0.23.4`) を FetchContent 追加。`BUILD_SHARED_LIBS OFF` (静的リンク)、`TREE_SITTER_FEATURE_WASM OFF` (roadmap §7.3 の「WASM 除外版」要求)
- **`tree-sitter-cpp` はじめ各言語グラマーリポジトリの CMakeLists.txt を直接 `add_subdirectory()` しない。** 各グラマーリポジトリの CMakeLists.txt には `find_program(TREE_SITTER_CLI tree-sitter)` を使い `src/parser.c` を `src/grammar.json` から再生成しようとする `add_custom_command` があるが、既にコミット済みの `parser.c` を使えば十分であるにもかかわらず、`tree-sitter` CLI が環境に無い (CI 含む) と `TREE_SITTER_CLI-NOTFOUND generate ...` というコマンドが実行され失敗することをスタンドアロン probe で実機確認した。`FetchContent_Declare(... SOURCE_SUBDIR "does-not-exist")` (ソースは populate するが `add_subdirectory()` はしない、公式ドキュメント記載のイディオム) で各グラマーの CMakeLists.txt を経由せず、フェッチ済みソースの `src/parser.c` (+ 文法によっては `src/scanner.c`) を直接参照する自前の `add_library` ターゲットを立てる。今後言語グラマーを追加するたびに同じパターンを踏襲する
- 新規 `src/syntax/` モジュール (`neomifes::syntax`) が `tree-sitter`/`tree-sitter-cpp-grammar` (自前ターゲット名) へ PRIVATE リンクする。公開ヘッダには tree-sitter の型 (`TSNode`/`TSTree` 等) を一切露出しない (`nlohmann::json` を隠蔽した ADR-013 の設計判断を踏襲)
- 言語ごとの追加は今後のサブフェーズ (Phase 7b 以降) で、必要な `tree-sitter-<lang>` リポジトリを同じパターンで追加していく

## 実装上の注意点 (Phase 7a で確認済み)

1. スタンドアロン probe (`ts_parser_new()` → `ts_parser_set_language(tree_sitter_cpp())` → `ts_parser_parse_string_encoding(..., TSInputEncodingUTF16LE)`) でビルド・パース・`TSTreeCursor` によるリーフノード走査まで一通り動作することを確認済み。バイトオフセット (`ts_node_start_byte`/`ts_node_end_byte`) は UTF-16LE 入力時、常に偶数でありバイトオフセット ÷ 2 が UTF-16 コードユニットオフセットと一致する
2. 不正な構文の入力でもクラッシュせず `ts_node_has_error()` が真になるだけで安全にパースが完了することを確認済み

## 将来の再評価

- Phase 7b 以降で言語グラマーを追加するたびに、上記「`SOURCE_SUBDIR` + 自前 `add_library`」パターンが引き続き妥当か (文法によっては `scanner.c` が無い、複数の外部スキャナファイルを持つ等の差異がある) を個別に確認する
- roadmap §7.3 が言及する Semantic highlighting (LSP `semanticTokens` を tree-sitter の色付けに重ねる) は Phase 11 の LSP 統合以降に再検討する
