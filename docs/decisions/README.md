# Architecture Decision Records (ADR)

本ディレクトリは NeoMIFES プロジェクトの重要な技術的意思決定を記録する。

## 一覧

| # | タイトル | ステータス |
|---|---|---|
| [ADR-001](ADR-001-build-system.md) | ビルドシステムに CMake を採用する | Accepted |
| [ADR-002](ADR-002-regex-engine.md) | 正規表現エンジンに RE2 を採用する | Accepted |
| [ADR-003](ADR-003-syntax-definition.md) | シンタックス定義に TextMate 互換文法を採用する | Accepted |
| [ADR-004](ADR-004-http-client.md) | HTTP クライアントに WinHTTP を採用する | Accepted |
| [ADR-005](ADR-005-min-msvc-version.md) | 最低 MSVC バージョンを VS 2022 17.13 以上とする | Accepted |

## 運用ルール

- 破壊的変更・大規模変更・外部依存追加・言語仕様への強い依存を伴う決定は必ず ADR を残す
- 命名: `ADR-<3桁連番>-<kebab-case-title>.md`
- ステータス: `Proposed / Accepted / Deprecated / Superseded by ADR-XXX`
- 決定変更時は既存 ADR を Deprecate または Superseded にマークし、新 ADR を作成する (削除は不可)
- テンプレート項目: **コンテキスト / 選択肢 / 決定 / 根拠 / 影響 / 却下理由 / (必要なら) 将来の再評価**
