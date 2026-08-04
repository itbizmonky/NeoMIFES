# ADR-021: SQL文法(`tree-sitter-sql`)は上流に`parser.c`が無いため、ビルド時に`tree-sitter generate`を実行するのではなく、開発機上で一度だけ生成した`parser.c`を`third_party/tree-sitter-sql-generated/`へベンダリングする

- **ステータス:** Accepted
- **決定日:** 2026-08-04 (Phase 7y 実装完了時)
- **関連:** [ADR-014](ADR-014-syntax-engine-tree-sitter.md)(`neomifes::syntax`のtree-sitter統合、FetchContent + 既存`parser.c`直接参照パターンの確立)、CLAUDE.md 絶対ルール3(推測実装をしない)・ルール6(外部ライブラリ追加は最小限、ADRを残す)・ルール9(大規模変更は事前承認)

## コンテキスト

Phase 8f(`registerCommand`ヘッドレス実装、ADR-020)完了後、ユーザーから次のPhaseとしてroadmap上の次候補(AppContainerサンドボックス/大規模文書の性能DoD再挑戦/SQL文法対応)を提示し、**SQL文法対応**が選ばれた。roadmap §7.2の必須23言語のうち、SQLはPhase 7x(バッチ4)時点で唯一「候補文法はあるが上流に`parser.c`が無いため対象外」として意図的に据え置かれていた最後の1言語(VB/VBScriptはライセンス不明で恒久除外、SAP ABAPは元よりP1優先度低)。

**着手前調査(GitHub API直接確認、CLAUDE.mdルール3)で確定した事実:**

- `DerekStride/tree-sitter-sql`(v0.3.11、2025-10-01リリース、MIT、243★、アクティブ)の`src/`ディレクトリには`scanner.c`のみ存在し、`parser.c`は無い。上流の`CMakeLists.txt`自身が`find_program(TREE_SITTER_CLI tree-sitter)` + `add_custom_command(... COMMAND tree-sitter generate ...)`で`grammar.js`から`parser.c`を都度生成する設計であり、ADR-014が全21言語について前提としていた「既にコミット済みの`parser.c`を使う」パターンが成立しない。
- `scanner.c`は`tree_sitter/parser.h` + 標準Cヘッダのみに依存する自己完結ファイル(相対include複雑化は無い)。`grammar.js`の依存は全てリポジトリ内ローカルファイルへの相対import(`./grammar/keywords.js`等)で、npm packageの解決(`node_modules`)は不要。
- tree-sitter CLI公式最新リリース(v0.26.11、本プロジェクトが依存するtree-sitterコア本体`cmake/Dependencies.cmake`の`GIT_TAG`と同一バージョン)は、Node.js不要のスタンドアロンRustバイナリとしてGitHub Releasesで配布されている(`tree-sitter-cli-windows-x64.zip`)。
- CI(`.github/workflows/ci.yml`)には現状Node.js/npm/cargoいずれのツールチェインも存在しない(`choco install`はLLVMのみ)。

## 選択肢

1. **tree-sitter CLIをビルド依存として導入し、CMakeが毎回`tree-sitter generate`を実行して`parser.c`を生成する**
2. **開発機上で一度だけ`tree-sitter generate`を実行し、結果の`parser.c`(+上流の`scanner.c`+生成された`tree_sitter/{parser.h,alloc.h,array.h}`)を`third_party/tree-sitter-sql-generated/`へコミットする(採用)**
3. **SQL対応を見送る**

## 決定

**選択肢2を採用する。** SQL文法対応自体はroadmap上の実質的な価値があるため選択肢3は採らない。選択肢1(tree-sitter CLIを実際のビルド依存として導入)は、CI 3ジョブ全て(`build-and-test`×2マトリクス、`ubsan`、`static-analysis`)への新規ツールプロビジョニング追加と、「ビルド時に第三者バイナリを実行する」という本プロジェクト初のリスクカテゴリ(これまでの全FetchContent依存は「ソースを自前コンパイラでコンパイルする」のみで、外部実行ファイルを走らせたことは一度も無い)を伴う。この状況をAskUserQuestionで提示し、選択肢2(事前生成してベンダリング)が選ばれた。

## 根拠

### ベンダリング方式を採用した理由

CI/将来の全ビルドが、他の21言語と全く同じ「静的ファイルをコンパイルするだけ」に留まり、新規ツール依存もビルド時コード生成も一切発生しない。これはCLAUDE.mdルール6(外部ライブラリ追加は最小限)の精神に最も忠実であり、本プロジェクトが一貫して踏襲してきた「決定論的・オフライン完結可能なビルド」という設計思想(FetchContentも含め、全依存は「ソースを取得してコンパイルする」の枠を出ない)を維持できる。

### 生成物のサイズについて、ユーザーに事前確認した理由

実際に生成した`parser.c`は17.3MBに達した(現在の`.git`全体が約30MBであるのに対して大きな割合)。これはtree-sitter-cppの`parser.c`(同じく17MB超、ただしFetchContent経由でOUR git履歴には残らない)と同等のサイズで、SQL文法の構造上自然な規模だが、「ベンダリング」という言葉が示唆する規模感を計画時点で明示していなかったため、コミット前にAskUserQuestionでサイズを開示し、承認を得た上で進めた(「このまま17MBをコミット」が選ばれた)。

### `third_party/tree-sitter-sql-generated/`に`tree_sitter/{parser.h,alloc.h,array.h}`も含めた理由

`tree-sitter generate`は`parser.c`と同時に、この文法専用のtree-sitterランタイムヘッダ一式も`src/tree_sitter/`配下に生成する。これは他の全FetchContent'd文法(例: `tree-sitter-batch-src/src/tree_sitter/parser.h`)が上流リポジトリ自身のコミットとして持っているのと同じファイル群であり、`scanner.c`/`parser.c`はどちらも`#include "tree_sitter/parser.h"`(文法自身のディレクトリ相対)でこれを参照する。tree-sitterコア本体(`build/*/_deps/tree-sitter-src/lib/include`)が公開する`tree_sitter/api.h`とは別物であり、当初`parser.c`/`scanner.c`のみをコピーしてビルドし`fatal error C1083: 'tree_sitter/parser.h': No such file or directory`で失敗したことから、この3ファイルも必須と判明した(実機ビルド検証で発見、CLAUDE.mdルール3)。

### `namedLeafKindsForSql()`テーブルを最小限(3エントリ)に留め、`classifyLeaf()`へ`keyword_`プレフィックス規則を追加した理由

実機probeで、`tree-sitter-sql`が定義する名前付きノード型のうち`keyword_*`が356種類(`node-types.json`から機械的に抽出、記憶からの推測ではない)存在すると判明した。他の全20言語はキーワードを匿名の文字列リテラルトークンとして扱い、既存の`classifyAnonymousLeaf()`の「無名リーフかつ全アルファベット文字なら`Keyword`」ヒューリスティックがそのまま機能してきたが、SQLは全てのキーワードをそれぞれ独立した名前付きリーフノードとして表現する。356個の明示的テーブルエントリを書き出すことは技術的には可能(`node-types.json`から機械的に導出できるため推測ではない)だが、`syntax_internal.h`の他の全`namedLeafKindsForX()`テーブル(3〜15エントリ程度)を大きく超える規模になり、将来の文法バージョンアップ時に無言で古びるリスクも増す。代わりに`classifyLeaf()`に「テーブルに無い名前付きリーフの型名が`keyword_`で始まるなら`Keyword`」という1行の汎用規則を追加した — これはSQL専用の特殊対応ではなく、同じ命名規則を採用する将来のどの文法にも自動的に効く一般化であり(code-reviewの「特殊ケースを重ねるより下位機構を一般化する」原則に沿う)、既存の20言語のいずれの名前付きノード型も`keyword_`で始まらないため副作用は無い。

### `namedLeafKindsForSql()`が`literal`を意図的に含まない理由(スコープの割り切りではなく正しさ上の理由)

実機probeで、`tree-sitter-sql`の`literal`ノードは(a)実際の文字列/数値リテラル(真のリーフ、子ノード無し)と(b)`TRUE`/`FALSE`/`NULL`を表す`keyword_true`/`keyword_false`/`keyword_null`を**子として包む**ラッパー(子ノード1個、真のリーフではない)の両方に使われる同一型名だと判明した。`isAtomicNode()`は「型名がテーブルにあれば無条件にリーフとして扱う(子へ降りない)」仕組みのため、`literal`をテーブルに追加すると、(b)のケースで`TRUE`/`FALSE`/`NULL`が正しい`keyword_true`等への分類ではなく`literal`のテーブル値へ強制的に上書きされてしまう(誤分類)。`literal`をテーブルから外すことで、(b)は`isAtomicNode()`の子ノード数チェックを通過せず正しく子(`keyword_true`等)まで降りて`Keyword`に分類される一方、(a)は真のリーフのままテーブル外(`TokenKind::Text`)に分類される。この結果、真の文字列/数値リテラル自体には専用の色分けが付かない(既存の`TokenKind`にはSQLの`literal`が指す2つの概念(文字列/数値)を型名だけで判別する手段が無い、XML/YAMLの「1つのノード型が複数概念を指す」既存の割り切りと同じ性質の限界)が、これは受容可能なトレードオフと判断した — TRUE/FALSE/NULLの正しい分類の方が、生SQLサンプルにおいて視覚的重要度が高いと判断したため。

## 影響

### 実装(`third_party/`, `cmake/`, `src/syntax/`, `src/app/`, `tests/`)
- 新規`third_party/tree-sitter-sql-generated/`: `src/parser.c`(機械生成、17.3MB)、`src/scanner.c`(上流v0.3.11からそのままコピー)、`src/tree_sitter/{parser.h,alloc.h,array.h}`(生成物)、`LICENSE`(上流MITライセンス全文コピー)、`NOTICE.md`(由来・再生成手順)
- `cmake/Dependencies.cmake`: FetchContentを使わない新パターンの`tree-sitter-sql-grammar`ターゲット(`third_party/`配下の静的ファイルを直接参照)、末尾`foreach`ループへ追加
- `src/syntax/src/syntax_internal.h`: `tree_sitter_sql()`宣言、`tsLanguageFor()`に`Sql`ケース追加、`namedLeafKindsForSql()`新設(3エントリ)、`classifyLeaf()`へ`keyword_`プレフィックス規則追加
- `src/syntax/include/neomifes/syntax/syntax.h`: `Language`列挙子末尾へ`Sql`追加、`parseSql()`宣言
- `src/syntax/src/syntax.cpp`: `parseSql()`実装、ディスパッチャへ`Sql`ケース追加
- `src/syntax/src/outline.cpp`: `Sql`を`emptySymbolTable()`へのfallthrough一覧へ追加(安全な劣化、既存の全非Cpp/Python言語と同じ扱い)
- `src/syntax/src/incremental_parser.cpp`: `namedKindsFor()`へ`Sql`ケース追加
- `src/app/include/neomifes/app/syntax_language.h`: `.sql`拡張子→`Language::Sql`
- `src/syntax/CMakeLists.txt`: `tree-sitter-sql-grammar`をPRIVATEリンクへ追加
- `tests/unit/syntax_syntax_test.cpp`/`app_syntax_language_test.cpp`/`syntax_outline_test.cpp`/`syntax_incremental_parser_test.cpp`: SQL向けテスト追加(既存の全21言語と同じパターン)

### 実装しない(意図的、スコープ外)
- `extractOutline()`のSQL向けシンボル抽出ロジック本体(既存の全非Cpp/Python言語と同じ)
- `RenderPipeline`/`SyntaxWorker`/`main.cpp`への変更(Phase 7dで確立済みの汎用ディスパッチがそのまま機能するため不要)
- tree-sitter CLIを将来のビルド依存として導入する案の再検討(需要が変わらない限り見送り)
- 文字列/数値リテラル自体への専用色分け(上記「根拠」節参照、`TokenKind`の型名ベース判別の限界)

## 将来の再評価タイミング

- **SQL文法のバージョンアップ:** `third_party/tree-sitter-sql-generated/NOTICE.md`に記載の手順で手動再生成・再コミットが必要(自動追従しない)。
- **tree-sitter CLIをビルド依存として導入する判断:** 他の文法でも同種の「上流にparser.c無し」問題が頻発するようになった場合、ビルド時生成方式への切り替えを再検討する。

## 参考
- `docs/decisions/ADR-014-syntax-engine-tree-sitter.md`(「将来の再評価」節が既に「文法によっては差異がある個別確認」を予告しており、本ADRはその想定内の例外として位置づく)
- `third_party/tree-sitter-sql-generated/NOTICE.md`(由来・再生成手順)
- `src/syntax/src/syntax_internal.h`の`namedLeafKindsForSql()`/`classifyLeaf()`コメント(probe結果の詳細)
