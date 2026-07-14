# ADR-003: シンタックス定義に TextMate 互換文法を採用する

- **ステータス:** Accepted
- **決定日:** 2026-07-14

## コンテキスト

要件定義書 §6 で多数のプログラミング言語 (C/C++/Go/Rust/Python/Java/PHP/TS/JS/HTML/CSS/SQL/Markdown 等) のシンタックスハイライトが必須。文法定義を自作するのは非現実的。

## 選択肢

1. **TextMate 互換文法定義 (`.tmLanguage.json`)**
2. tree-sitter
3. LSP のセマンティックトークン API に丸投げ
4. 自作 lexer 群

## 決定

**TextMate 互換文法を第一採用。tree-sitter は Phase 7 完了後に主要言語のみ併用導入を評価する (置換ではなく上乗せ)。**

## 根拠

- **エコシステム:** VS Code / Sublime Text / Atom が使う `.tmLanguage.json` 形式は 100+ 言語が既に整備されており、コピペで導入可能 (MIT/BSD ライセンスのものが多い)
- **軽量:** 正規表現ベースなのでコンパイル不要、ホットリロード容易
- **要件の "対応ファイル" (§6) の全言語をカバー可能**
- **tree-sitter の課題:** C ランタイム必須、各言語の grammar を C ソースから個別ビルドする必要、バイナリが数十 MB に肥大、要件 20MB を圧迫

## 影響

- 正規表現マッチングは既に採用する **RE2** をそのまま流用 (パフォーマンス上有利)
- `SyntaxHighlighter` は TextMate 文法用と (将来の) tree-sitter 用の両インターフェースを持てるように抽象化する
- 標準搭載する `.tmLanguage.json` は `data/grammars/` に配布し、ユーザー追加も可能

## 却下理由

- **tree-sitter (今):** 高機能だが重量級。**リファクタリング/セマンティック解析が必要な段階になったら選択的に導入する**
- **LSP のセマンティックトークン依存:** LSP サーバがない言語 (INI/BAT/PS1 等) がハイライトされなくなる
- **自作 lexer:** 保守が破綻する

## 将来の再評価

- Phase 8 (プラグイン) 以降で LSP 統合後、以下 3 言語について tree-sitter パーサ組込を検討:
  - C++ (SAP 業務スクリプト絡み)
  - TypeScript
  - Python
- リファクタリング機能を実装する時点で必須になる可能性が高い
