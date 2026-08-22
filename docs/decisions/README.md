# Architecture Decision Records (ADR)

本ディレクトリは NeoMIFES プロジェクトの重要な技術的意思決定を記録する。

## 一覧

| # | タイトル | ステータス |
|---|---|---|
| [ADR-001](ADR-001-build-system.md) | ビルドシステムに CMake を採用する | Accepted |
| [ADR-002](ADR-002-regex-engine.md) | 正規表現エンジンに RE2 を採用する | Accepted |
| [ADR-003](ADR-003-syntax-definition.md) | シンタックス定義に TextMate 互換文法を採用する | ~~Superseded by ADR-014~~ |
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
| [ADR-014](ADR-014-syntax-engine-tree-sitter.md) | 構文解析エンジンに tree-sitter を採用する (ADR-003 を置き換え) | Accepted |
| [ADR-015](ADR-015-plugin-host-c-abi-seh.md) | プラグインホストは C ABI + LoadLibraryW + 無条件 SEH トランポリンで実装し、CoreApi・サンドボックス・署名検証を Phase 8b 以降へ延期する | Accepted |
| [ADR-016](ADR-016-plugin-core-api-bridge.md) | NeoMifesCoreApi はドキュメント操作4関数のみを src/app/ のブリッジ層で実装し、neomifes::plugin 自体は Document Engine に依存させない | Accepted |
| [ADR-017](ADR-017-plugin-job-object-sandbox.md) | プラグインの資源制限は Windows Job Object の JOB_OBJECT_LIMIT_ACTIVE_PROCESS=1 のみを有効化し、メモリ/CPU時間制限と AppContainer 化は見送る | Accepted |
| [ADR-018](ADR-018-plugin-permission-model.md) | プラグインの permissions は自己申告ビットフィールド + NULL 関数ポインタ・ゲートで実装し、マニフェスト検証・署名検証・確認ダイアログは見送る | Accepted |
| [ADR-019](ADR-019-plugin-show-toast-headless.md) | NeoMifesCoreApi::showToast はヘッドレスな ui::ToastState 状態層のみ実装し、registerCommand と実UIウィジェットは延期する | Accepted |
| [ADR-020](ADR-020-plugin-register-command.md) | NeoMifesCoreApi::registerCommand は既存SEHトランポリンを再利用したヘッドレスな ui::PluginCommandRegistry 状態層のみ実装し、CommandPalette への実配線とunload時自動クリーンアップは延期する | Accepted |
| [ADR-021](ADR-021-sql-grammar-vendored-generation.md) | tree-sitter-sql は上流に parser.c が無いため、ビルド時 tree-sitter CLI 導入ではなく開発機上で一度だけ生成した parser.c を third_party/ へベンダリングする | Accepted |
| [ADR-022](ADR-022-git-integration-library.md) | Git 統合ライブラリに libgit2 を採用する | Accepted |

## 運用ルール

- 破壊的変更・大規模変更・外部依存追加・言語仕様への強い依存を伴う決定は必ず ADR を残す
- 命名: `ADR-<3桁連番>-<kebab-case-title>.md`
- ステータス: `Proposed / Accepted / Deprecated / Superseded by ADR-XXX`
- 決定変更時は既存 ADR を Deprecate または Superseded にマークし、新 ADR を作成する (削除は不可)
- テンプレート項目: **コンテキスト / 選択肢 / 決定 / 根拠 / 影響 / 却下理由 / (必要なら) 将来の再評価**
