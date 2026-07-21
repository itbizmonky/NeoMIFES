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
| [ADR-006](ADR-006-piece-tree-implementation.md) | Piece Tree を Path-Copying Persistent RB-Tree で実装する | ~~Superseded by ADR-007~~ |
| [ADR-007](ADR-007-piece-tree-mutable-rb.md) | Piece Tree を Mutable Red-Black Tree + Piece-Vector Snapshot で実装する | Accepted |
| [ADR-008](ADR-008-com-raii-comptr.md) | Direct2D/DXGI/DirectWrite の COM オブジェクトは Microsoft::WRL::ComPtr で所有する | Accepted |
| [ADR-009](ADR-009-deferred-device-init.md) | Direct2D デバイス生成は同期・UIスレッド・自己ポストメッセージ経由で遅延させる | Accepted |
| [ADR-010](ADR-010-render-depends-on-document.md) | Rendering Engine は Document Engine に直接依存する | Accepted |
| [ADR-011](ADR-011-phase3c-render-cache-scope.md) | Phase 3c は TextLayoutCache のみを実装し、GlyphCache と細粒度 DamageTracker を延期する | Accepted |
| [ADR-012](ADR-012-phase4a-editor-core-scope.md) | Phase 4a は Command/Undo/Selection のヘッドレス基盤のみを実装し、UI配線・圧縮/ディスクスワップ・矩形選択を延期する | Accepted |
| [ADR-013](ADR-013-json-library.md) | JSON 入出力ライブラリに nlohmann/json を採用する | Accepted |

## 運用ルール

- 破壊的変更・大規模変更・外部依存追加・言語仕様への強い依存を伴う決定は必ず ADR を残す
- 命名: `ADR-<3桁連番>-<kebab-case-title>.md`
- ステータス: `Proposed / Accepted / Deprecated / Superseded by ADR-XXX`
- 決定変更時は既存 ADR を Deprecate または Superseded にマークし、新 ADR を作成する (削除は不可)
- テンプレート項目: **コンテキスト / 選択肢 / 決定 / 根拠 / 影響 / 却下理由 / (必要なら) 将来の再評価**
