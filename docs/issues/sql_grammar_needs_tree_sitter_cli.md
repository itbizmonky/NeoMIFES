# Issue: SQL構文ハイライト — 唯一の有力な文法候補が`parser.c`未コミット

- **起票日:** 2026-08-01 (Phase 7x 追加言語対応バッチ4 着手前調査で判明)
- **対象:** `cmake/Dependencies.cmake`、`src/syntax/`(将来SQL対応する場合)
- **優先度:** 低 (roadmap §7.2の必須言語だが、代替手段の検討が先決)
- **関連:** [ADR-014](../decisions/ADR-014-syntax-engine-tree-sitter.md)、`docs/design/master_roadmap.md` §7.2

## 背景

roadmap §7.2の必須23言語のうちSQLは、Phase 7n1/7r時点で「tree-sitter公式org(`tree-sitter/`)・準公式org(`tree-sitter-grammars/`)配下に存在せずコミュニティ文法のみ」として対象外にしていた。Phase 7x(追加言語対応バッチ4)着手前の`gh api`再調査で、コミュニティ文法の中に比較的有力な候補が存在することが判明した。

## 調査結果

**`DerekStride/tree-sitter-sql`**(GitHub API直接確認、CLAUDE.mdルール3):
- 243★、MIT ライセンス、2026-07-01時点でアクティブにメンテナンスされている(直近更新あり)
- 他の候補(SQLに関する検索結果に出た他リポジトリ)より圧倒的にスター数・アクティビティが高く、事実上唯一の有力候補

**決定的な問題:** `src/`ディレクトリの内容を直接確認した結果、`scanner.c`のみが存在し**`parser.c`がコミットされていない**。リポジトリには`grammar.js`(文法定義)は存在するが、実際にパーサとして使うには`tree-sitter generate`コマンド(tree-sitter CLI、Node.js依存)でその場生成する必要がある。

これは本プロジェクトがADR-014で確立した「`SOURCE_SUBDIR "does-not-exist"`ワークアラウンドで生成済み`parser.c`を直接コンパイルする」パターンの前提(全ての採用言語が`parser.c`をリポジトリにコミット済み)を満たさない、初めてのケースである。

## 対応方針 (今回は見送り)

Phase 7xでは以下の理由からSQL対応をスコープ外とした:
1. Node.js + tree-sitter CLIという、本プロジェクトに存在しない新規ビルド時依存の追加になる(CI環境へのセットアップステップ追加も必要)
2. `tree-sitter generate`の出力(生成された`parser.c`)を都度CI上で生成する設計にするか、生成済み`parser.c`を本リポジトリに一度だけ生成してベンダリング(vendor)するかの設計判断が必要で、後者は「アップストリームの`grammar.js`更新にどう追従するか」という新たな運用課題を生む
3. 上記いずれも、既存の18言語(すべて生成済み`parser.c`を直接参照するだけ)より実装・運用コストが大きく、Phase 7xの「PowerShell/Ini/Batchの3言語追加」というスコープに対して不釣り合いに大きい

## 対応案 (将来この Issue に着手する場合)

### 案A: `tree-sitter generate`をCMakeのビルドステップに組み込む
- `find_program(TREE_SITTER_CLI tree-sitter)`でCLIの存在を確認し、`add_custom_command()`で`grammar.js`→`parser.c`の生成をビルド時に行う
- 利点: アップストリームの`grammar.js`更新に自動追従
- 欠点: 開発機・CI環境の両方にNode.js + tree-sitter CLIのインストールが必要になる(ADR-014が「`tree-sitter`CLIが未インストールでもビルドできる」ことを重視して`SOURCE_SUBDIR "does-not-exist"`ワークアラウンドを選んだ経緯と矛盾する)

### 案B: 生成済み`parser.c`を一度だけローカルで生成し、`third_party/`等へベンダリングする
- 利点: 既存の18言語と同じ「生成済みソースを直接コンパイル」パターンを維持できる、新規ビルド依存が増えない
- 欠点: アップストリームの`grammar.js`が更新されても自動追従しない(手動で再生成・再ベンダリングする運用が必要)。ライセンス上「生成物を再配布してよいか」の確認も必要(MITなので問題ない可能性が高いが未確認)

### 案C: 他のSQL文法候補を再探索する
- 調査時点(2026-08-01)では`DerekStride/tree-sitter-sql`以外に実用的な候補が見当たらなかったが、tree-sitterエコシステムは活発なため、将来再調査する価値はある

## 推奨

案Bが既存パターンとの一貫性が最も高く、着手する場合はまず案Bで実装し、`docs/decisions/`にADRを起票して「生成済みソースのベンダリング」という新パターンを明文化するのが妥当と考えられる。ただし現時点では投機的検討にとどめ、実際の着手はユーザーとの合意の上で行う(CLAUDE.mdルール3)。

## 完了条件 (将来この Issue に着手する場合)

- [ ] 案A/B/Cのいずれかを選定し、ADRを起票する
- [ ] SQL文法の`namedLeafKindsForSql()`を実機probeで検証の上テーブル化する
- [ ] `Language::Sql`追加+`parseSql()`実装+`detectLanguage()`拡張(`.sql`)
- [ ] 単体テスト・実アプリ視覚確認
