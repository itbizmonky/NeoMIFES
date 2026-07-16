# ADR-012: Phase 4a は Command/Undo/Selection のヘッドレス基盤のみを実装し、UI配線・圧縮/ディスクスワップ・矩形選択を明示的に延期する

- **ステータス:** Accepted
- **決定日:** 2026-07-16 (Phase 4a 着手時)
- **関連:** [ADR-010](ADR-010-render-depends-on-document.md)、[ADR-011](ADR-011-phase3c-render-cache-scope.md)、`docs/design/detailed_design.md` §5〜§6、CLAUDE.md 絶対ルール3(推測実装をしない)・ルール4(巨大クラス/関数の回避)・ルール8(1PR=1責務)・ルール10(性能改善はベンチマーク根拠必須)

## コンテキスト

`detailed_design.md` §5/§6 は Phase 0 時点で Editor Core のスケッチ(`Cursor`/`SelectionModel`/縦編集コマンド群/`Viewport`/`ICommand`/約20種の標準コマンド一覧/`UndoStack` の1000件バケット化+zstd圧縮+ディスクスワップ)を構想していた。これは Document/Rendering 両エンジンの実装が確定する**前**に書かれたもので、`RESUME_HERE.md` 自身が「再検証が必要」と明記していた。

CLAUDE.md §7 の Phase 4 DoD は「100万Undo達成」の一点のみであり、要件定義書 §5 も「Undo: 100万回以上」とだけ規定し、メモリ予算や圧縮方式には触れていない。この全量を一度に実装することは、ADR-011 が Phase 3c で GlyphCache/DamageTracker を延期した判断と同じ理由(推測実装の回避、ベンチマーク未実施での最適化着手の回避)で不適切と判断した。

## 選択肢

1. **`detailed_design.md` §5/§6 の全コンポーネントを一括実装する**
2. **Command/Undo/Selection のヘッドレスな基盤のみを Phase 4a として実装し、UI配線・圧縮/ディスクスワップ・矩形選択・単語単位移動・他エンジン依存コマンドを Phase 4b 以降に明示的に延期する(採用)**
3. **キーボード配線を含む最小限の対話的エディタを最初から作る**

## 決定

**選択肢2を採用する。**

## 根拠

### `UndoStack` の1000件バケット化・zstd圧縮・ディスクスワップを延期する理由

要件は「100万回以上のUndo」であり、具体的なメモリ予算(design doc は256MBという例を示すが要件定義書に規定はない)は決まっていない。圧縮/ディスクスワップは実測でメモリ予算超過が判明してから講じるべき対策であり、先に実装するのは CLAUDE.md ルール10 違反(憶測最適化)になる。

**実測による裏付け(本ADR採択の根拠として記録):** `tests/bench/core_undo_stack_bench.cpp` の実測値(1,000,000コマンド、`--benchmark_min_time=0.01s`):

| ベンチマーク | Release | Debug |
|---|---|---|
| `BM_UndoStack_PushOneMillion`(100万件push) | 338ms | 5.01s |
| `BM_UndoStack_UndoOneMillion`(100万件undo) | 172ms | 2.05s |

Release 実測でプッシュ・アンドゥとも1秒未満であり、単純な `std::vector<std::unique_ptr<ICommand>>` 2本構成のままで「100万Undo達成」の DoD を満たすことを実測で確認した。メモリ使用量は `docs/issues/undo_stack_unbounded_memory.md` に tripwire として記録し、圧縮/ディスクスワップの要否は実測メモリ値が出てから再検討する。

### `tryMerge`(連続入力パッキング)を延期する理由

design doc は `InsertTextCommand::tryMerge()` をキーストロークの連続入力をまとめる機能として構想しているが、実際のキーボード入力(Phase 4b)が存在しない Phase 4a の時点でマージ閾値(文字数上限、タイムアウト等)を決めるのは推測実装になる。Phase 4b で実際の入力イベント頻度を観測してから設計する。

### `MovementUnit`(単語/段落単位移動)を延期する理由

単語境界判定(Unicode 単語分割規則、CJK 文字種別の扱い等)は未設計であり、今実装すると仕様不明のまま推測実装することになる。Phase 4a の `SelectionModel::moveAll` は文字/行単位の8種類の `MovementKind` のみをサポートする。

### 矩形選択・縦編集コマンド群(`ColumnInsert/Delete/Overwrite/Append`)を延期する理由

矩形選択のハイライト描画(Rendering Engine側)がまだ存在せず、機能を実装しても視覚的に確認・テストできない。`RenderPipeline` は現状テキスト描画のみで選択範囲/キャレットの描画パスを持たない。

### キーボード/マウス入力の `MainWindow` 配線・キャレット描画を延期する理由

`MainWindow::wndProc` は現状 `WM_PAINT`/`WM_SIZE`/`WM_DPICHANGED`/内部初期化メッセージ/`WM_ERASEBKGND`/`WM_CLOSE`/`WM_DESTROY` のみを処理し、`WM_KEYDOWN`/`WM_CHAR`/`WM_LBUTTONDOWN`/`WM_MOUSEWHEEL` は一切未実装。DoD「100万Undo達成」はベンチマークで実測できるため、この配線を待たずに証明可能。Phase 4a はヘッドレスなベンチマークで DoD を満たし、Phase 4b で実際に使えるインタラクティブエディタに仕上げる。

### 標準コマンド一覧(design doc §6.1.1)の大半を延期する理由

`edit.autoIndent`/`file.changeEncoding`/`search.*`/`ai.invoke` 等は Search Engine(Phase 5)/Encoding Engine(Phase 6)/Plugin Engine(Phase 8)/AI Plugin(Phase 9)に依存しており、依存先が未着手のため実装不能。Phase 4a は `edit.insert`/`edit.delete`/`edit.replace` の3種のみを実装する。

### `Viewport` の `FoldingMap`/`setFoldingRanges` を延期する理由

折り畳みエンジンは Phase 7 スコープで未着手。空の骨組みだけを今作るのは推測実装になる。

## 影響

### 実装 (`src/core/`, 新規)
- `Cursor`(design doc §5.1のまま) / `ICommand`・`ExecutionContext`(design doc §2.2 準拠 + 新規グルー) / `SelectionModel`(8種の `MovementKind`) / `InsertTextCommand`・`DeleteRangeCommand`・`ReplaceRangeCommand` / `UndoStack`(圧縮/ディスクスワップなし) / `CommandDispatcher`(新規グルー) / `Viewport`(folding無し)
- `neomifes::core` は `neomifes::render` に依存しない(Viewport はヘッドレス。RenderPipeline::setTopLine() へのブリッジは Phase 4b で app/ui 層が担う)

### 実装しない(Phase 4b 以降へ)
- MainWindow のキーボード/マウス入力配線、キャレット/選択範囲のレンダリング
- 矩形選択・縦編集コマンド群
- `UndoStack` のバケット化・zstd圧縮・ディスクスワップ
- `tryMerge` 連続入力パッキング
- `MovementUnit`(単語/段落単位移動)
- Search/Encoding/Plugin/AI 依存の標準コマンド群
- `Viewport` の `FoldingMap`

## 将来の再評価タイミング

- **UndoStack 圧縮/ディスクスワップ:** `docs/issues/undo_stack_unbounded_memory.md` の完了条件(Phase 4b の対話的編集セッションでの実メモリ計測)を満たした時点
- **`tryMerge`:** Phase 4b でキーボード入力配線が実現し、実際の入力頻度が観測できるようになった時点
- **矩形選択・縦編集:** Rendering Engine に選択範囲ハイライト描画が実装された時点
- **`MovementUnit`:** 単語境界判定の仕様(Unicode UAX #29 準拠か、簡易ASCII判定か等)がユーザーと合意された時点

## 参考
- `docs/design/detailed_design.md` §5〜§6
- `docs/issues/undo_stack_unbounded_memory.md`
- `docs/phase_reports/phase_3_report.md` §6(Phase 4 への引き継ぎ事項)
