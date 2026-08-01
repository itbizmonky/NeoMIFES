# Issue: VB / VBScript構文ハイライト — ライセンス明記された文法候補が存在しない

- **起票日:** 2026-08-01 (Phase 7x 追加言語対応バッチ4 着手前調査で判明)
- **対象:** roadmap §7.2(必須23言語)
- **優先度:** 低 (代替候補が現れるまで着手不可能)
- **関連:** `docs/design/master_roadmap.md` §7.2

## 背景

roadmap §7.2の必須23言語にはVB・VBScriptが含まれる。Phase 7n1/7r時点で「tree-sitter公式org・準公式org配下に存在せずコミュニティ文法のみ」として対象外にしていたが、Phase 7x(追加言語対応バッチ4)着手前の`gh api`/GitHub検索での再調査で、恒久的な対象外理由(ライセンス不明)が判明した。

## 調査結果 (GitHub API直接確認、CLAUDE.mdルール3)

VB6/VBA/VBScript/VB.NET系の文法候補を幅広く検索したが、実用に足る候補は以下の通り全てライセンス情報が欠落していた:

| リポジトリ | スター数 | ライセンス |
|---|---|---|
| `CodeAnt-AI/tree-sitter-vb-dotnet` (VB.NET) | 26★(最有力候補) | **null(不明)** |
| `JJK96/tree-sitter-vbscript` | 4★ | null |
| `andersonm3ai/tree-sitter-vb6`/`tree-sitter-vb` | 0〜1★ | 未確認(スター数から実用性低) |
| その他VBA系候補(`tmepple`/`arrmee-wt`/`gabriel-gubert`/`harumiWeb`等) | 全て0★ | 未確認(実用性低) |

GitHubがリポジトリの`license`フィールドを`null`と報告する場合、`LICENSE`ファイルが存在しないか、GitHubの自動検出アルゴリズムが認識できない形式であることを意味する。**ライセンスが明記されていないコードは、著作権法上デフォルトで「全著作権留保」扱いとなり、本プロジェクト(MITライセンスの既存18言語文法+今後の配布を前提とする)へのビルド時依存として組み込むことができない。**

## 対応方針

**恒久的に対象外とする。** 新しい文法が公開され、かつ明確なライセンス(MIT/Apache-2.0/BSD等の寛容なライセンス)が付与されるまで、この言語ペアはroadmap §7.2の未達成項目として残る。

## 将来の再評価タイミング

- 以下のいずれかの条件を満たす新規リポジトリが確認された場合に再調査する:
  1. tree-sitter公式org(`tree-sitter/`)または準公式org(`tree-sitter-grammars/`)配下にVB/VBScript文法が追加される
  2. 既存候補(`CodeAnt-AI/tree-sitter-vb-dotnet`等)がライセンスファイルを追加する
  3. 新規に一定の実績(★数・アクティブなメンテナンス)を持つ候補が現れる

## 完了条件 (将来この Issue に着手する場合)

- [ ] ライセンス明記された文法候補を発見し、GitHub APIで`license`フィールドを直接確認する
- [ ] `parser.c`がコミット済みであることを確認する(SQLと同じ落とし穴の再確認、`docs/issues/sql_grammar_needs_tree_sitter_cli.md`参照)
- [ ] `namedLeafKindsForVb()`/`namedLeafKindsForVbScript()`を実機probeで検証の上テーブル化する
- [ ] `Language::Vb`/`Language::VbScript`追加+`parseVb()`/`parseVbScript()`実装+`detectLanguage()`拡張(`.vb`/`.vbs`)
- [ ] 単体テスト・実アプリ視覚確認
