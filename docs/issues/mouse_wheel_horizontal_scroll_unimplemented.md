# Issue: マウスホイールによる水平スクロールが実装されていない (P2 — 🟢 WI-46で解消)

- **起票日:** 2026-09-11(ユーザーの実機ドッグフーディング報告)
- **解決日:** 2026-09-11(WI-46)
- **対象:** `src/ui/include/neomifes/ui/main_window.h`/`.cpp`(`onMouseHWheel`/`handleMouseHWheel()`新設)、`src/app/include/neomifes/app/editor_input.h`/`.cpp`(`applyMouseWheelScrollColumn()`新設)、`src/app/normal_mode_wiring.cpp`
- **優先度:** P2(横スクロールバー・キーボードでの代替手段は存在するため、マウスホイール操作という利便性の欠如)

## 事実

ユーザー報告: 「横スクロールをマウスのホイールで操作しようとしても左右端までスクロールしない。」

## 原因

調査の結果、`WM_MOUSEHWHEEL`(チルトホイール/トラックパッドの水平スクロールジェスチャ)のハンドリングがコードベースに一切存在しなかった——「左右端まで届かない」という症状ではなく、**水平方向のホイール操作自体がそもそも一切実装されていなかった。** `MainWindow`のウィンドウプロシージャには`WM_MOUSEWHEEL`(垂直)のcaseはあるが`WM_MOUSEHWHEEL`のcaseが無く、`MainWindowConfig`にも対応するコールバックフィールドが存在しなかった。

## 解決内容

`WM_MOUSEWHEEL`/`handleMouseWheel()`/`MainWindowConfig::onMouseWheel`の水平版として、`WM_MOUSEHWHEEL`のcase、`MainWindow::handleMouseHWheel()`、`MainWindowConfig::onMouseHWheel`をそれぞれ新設。垂直方向の`applyMouseWheelScroll()`の水平版として`applyMouseWheelScrollColumn()`を新設し、`normal_mode_wiring.cpp`で配線した。

`WM_MOUSEHWHEEL`の符号規約は`WM_MOUSEWHEEL`と逆(正の値=右へチルト=後方のカラムを表示=`leftColumn`増加)である点に注意し、垂直版のような符号反転は行っていない。`computeHScrollTargetColumn()`と同じ「上限クランプ無し、レンダー時のクランプが唯一の真実の源」という既存設計方針を踏襲し、`applyMouseWheelScrollColumn()`にも上限クランプを設けていない(文書全体の水平方向の最大幅をO(1)で権威的に知る手段が無く、10GBファイル対応の制約上O(文書サイズ)の走査もできないため)。

新規単体テスト3件(`ApplyMouseWheelScrollColumnRightIncreasesColumn`/`...LeftDecreasesColumnClampedToZero`/`...RightHasNoUpperClamp`)で論理層を検証。実機ドッグフーディングで、300文字の長い1行に対し`WM_MOUSEHWHEEL`を実際に`PostMessage`し、右方向で文書の右端を超えてブランク表示になる(=右端に到達したことの証明)こと、左方向で正確に列0まで戻ることの両方をスクリーンショットで確認済み。

## 完了条件

- [x] マウスホイールでの水平スクロールが機能しない原因を特定した(該当ハンドリングが一切存在しなかった)
- [x] `WM_MOUSEHWHEEL`ハンドリングを新設し、単体テスト+実機ドッグフーディング(左右端到達を含む)で動作を確認した
