# Issue: 表示メニューが手薄・折り返し(word wrap)機能が存在しない (P2 — 🟢 解決済み、WI-21a〜f完了)

- **起票日:** 2026-09-02 (ユーザーからの品質フィードバック「秀丸/MIFESに到底及ばない」を受けた監査中に発見)
- **解決日:** 2026-09-03 (WI-21f完了、折り返し機能〔WI-21a〜e〕+表示メニュー拡充〔WI-21e〕が完結)
- **対象:** `src/app/include/neomifes/app/menu_bar.h`(`kViewMenuItems`)、`src/render/src/render_pipeline.cpp`
- **優先度:** P2 (要件定義書§6には明記が無いが、秀丸/MIFES/一般的なテキストエディタの table-stakes 機能)
- **対応 Phase:** WI-21a〜f(2026-09-03完了)、`docs/design/build_plan.md`のWI-21a〜fセクション参照

## 事実

WI-18a(ファイルを閉じる操作の追加)の作業中、`表示(&V)`メニューの実装を確認したところ、以下のみだった([menu_bar.h](../../src/app/include/neomifes/app/menu_bar.h)の`kViewMenuItems`):

- アウトライン(&O)
- 構造ツリー(&J)
- CSVグリッド(&G)

行番号表示切替・折り返し切替・テーマ切替・フォントサイズ変更など、秀丸/MIFES/VSCode等のエディタで表示メニューの定番項目が一切無い。

さらに、折り返し(word wrap)機能自体がコードベースに一切存在しないことを確認した。`RenderPipeline`のテキストレイアウト生成は`DWRITE_WORD_WRAPPING_NO_WRAP`にハードコードされている([render_pipeline.cpp:365](../../src/render/src/render_pipeline.cpp)):

```cpp
hr = format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
```

トグル・設定項目・関連するビューポート/スクロール計算のいずれも存在しない。

## 影響

- 折り返しはプレーンテキスト・READMEファイル・長い1行のログ等を読む際の基本機能であり、これが一切無いと横スクロールでしか長い行を読めない。
- テーマ・行番号・フォントサイズは`core::Settings`(settings.json)経由では変更可能だが、メニューから発見できない(コマンドパレット/直接JSON編集を知らないと辿り着けない)。

## 対応方針 (完了)

1. **折り返し機能自体の実装** - `DWRITE_WORD_WRAPPING_WRAP`への切替に加え、折り返しはビューポート/スクロール/キャレット位置計算(現在「1行=1論理行」を前提にしている箇所全て)に影響するため、規模の大きい変更になる可能性が高い。着手前に影響範囲の調査が必要。
2. **表示メニューの拡充** - 行番号表示・テーマ切替(既存`core::Settings`の値を読み書きするだけ)は比較的小さい追加。折り返しトグルは(1)の実装が前提。

いずれもユーザーへ提示済みで「別Work Itemとすべき」との認識で合意し、今回のWI-18スコープには含めていない。

**2026-09-02〜03追記(WI-21着手):** ユーザーが「折り返しも含めて設計から着手」を選択。着手前調査で折り返しは既存の折り畳み機能(`FoldingModel`)を流用できない大規模な変更(`RenderPipeline`の11箇所以上が「1論理行=1描画行」を前提)と判明し、Plan Modeで詳細設計、WI-21a〜fの6段階(a: ヘッドレス計算モジュール、b: Settings/RenderPipeline配線、c: 単一の真実の源の確立、d: ヒットテスト書き換え+多行描画バグ2件修正、e: 実配線+表示メニュー拡充、f: カーソル移動/ミニマップ方針確定+本issue解決)に分割して実装した。

## 完了条件

- [x] 折り返し機能を実装するかどうかを決定する(ユーザー確認、規模次第で別途設計レビューが必要な可能性) — 2026-09-02、「折り返しも含めて設計から着手」に決定
- [x] 表示メニューに行番号表示・テーマ切替など、既存`core::Settings`項目への到達手段を追加する — WI-21e(2026-09-03)、`kViewMenuItems`を3→6項目へ拡張(折り返し/行番号/テーマ切替)
- [x] 実機ドッグフーディングで確認する — WI-21e(2026-09-03)、折り返しON/OFF・行番号トグル・テーマ切替・View menu目視確認・再起動後の永続化を実機確認。**実機ドッグフーディング中に`RenderPipeline::FrameState`の重大バグ(`wordWrapEnabled`が粗粒度フレームスキップの比較対象に含まれておらず、メニュー操作が画面に反映されない)を発見・修正——WI-15i/WI-21bに続く3度目の同型再発、自動テストでは検出不可能で実機ドッグフーディングでのみ発見できた実例。**

## 関連

- カーソル移動(Up/Down/Home/End/PageUp/PageDown)は`core::moveVertically()`が純粋に論理行ベース(`Document::offsetToLine()`/`lineToOffset()`/`lineCount()`のみに依存、`RenderPipeline`/`Viewport`/折り返し状態への依存が一切無い)であることをコードレビューで確認済みのため、WI-21fで無変更のまま完了。既存の自動テスト(`tests/unit/core_selection_model_test.cpp`の`MovementKind::Up`/`Down`関連ケース)がそのまま回帰カバレッジとして機能する。この点の実機での対話的な検証(実際にキー入力で折り返し境界をまたいで移動する操作)は、この環境の既知のキーストローク合成制約(`SendKeys`/`SendInput`が確実に届かない)により実施を試みたが、合成した`Shift+Down`がIME経由と見られる予期しない文字入力を引き起こすという新しい形の不具合に遭遇し断念、正直に「コードレビュー+既存自動テストによる代替検証」に留めた。
- ミニマップのビューポートハイライトは折り返し有効時に論理行ベースの近似のまま維持する設計判断を確定し、[`minimap_highlight_ignores_word_wrap_row_density.md`](minimap_highlight_ignores_word_wrap_row_density.md)(P3)として別途記録した。
