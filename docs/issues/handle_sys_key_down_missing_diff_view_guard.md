# Issue: `handleSysKeyDownEvent()` に `isDiffViewActive()` ガードが無く、Diffビュー表示中でも不可視の実文書へ矩形選択が適用されてしまう (P2〜P3 — 🟢 WI-38で解決済み)

- **起票日:** 2026-09-04 (WI-27のPlan agentによる設計レビュー中に発見)
- **解決日:** 2026-09-09(WI-38)
- **対象:** `src/app/normal_mode_wiring.cpp`(`handleSysKeyDownEvent()`)
- **優先度:** P2〜P3(実害はDiffビュー表示中にShift+Alt+矢印/Iを押すという狭い操作に限定される)

## 事実

`renderPipeline.isDiffViewActive()`(WI-17f、Diffビューは合成された読み取り専用文書を表示し、裏の実文書は非表示のまま生存し続ける)は、以下の3箇所で一貫してガードされている:

- `handleKeyDownEvent()`(`normal_mode_wiring.cpp:1853`)
- `handleCharEvent()`(WM_CHAR、`normal_mode_wiring.cpp:1970`付近)
- もう1箇所(`normal_mode_wiring.cpp:1648`付近)

しかし**`handleSysKeyDownEvent()`(WM_SYSKEYDOWN、Shift+Alt+矢印/Iのハンドラ)にはこのガードが無い**。そのため、Diffビュー表示中にShift+Alt+矢印を押すと、画面に見えていない(裏で生存している)実文書に対して`SelectionModel::setRectangularSelection()`/`convertToLineEndCursors()`が適用されてしまう——選択状態が画面に反映されないまま実文書側で変化し、Diffビューを閉じて実文書へ戻った瞬間に、ユーザーの意図しない矩形選択/カーソル配置が突然現れる。

## 影響

WI-26で新設した`rectangular_anchor_stale_across_keyboard_only_reuse.md`(P2)の調査・修正過程で、Plan agentによる設計レビューがこの既存の別種のバグ(anchorの陳腐化とは無関係)を偶然発見した。矩形選択・Shift+Alt+I自体の既存バグであり、WI-27のスコープ(anchorリセット)には含めなかった。

## 対応案(実施済み)

`handleSysKeyDownEvent()`の冒頭へ`if (renderPipeline.isDiffViewActive()) { return false; }`を追加した。当初懸念していた「Escapeの特別扱いが必要か」という論点は、調査の結果不要と判明した: Escapeキーは修飾キー無しで押される限りWM_SYSKEYDOWNではなくWM_KEYDOWNとして届くため、`handleKeyDownEvent()`側の既存ガード(Escapeで`syncViewForActiveSession()`を呼びDiffビューを閉じる)がそのまま処理しており、`handleSysKeyDownEvent()`が同じキーを二重に処理することは無い。また、この関数の戻り値は`DefWindowProcW`へのフォールスルー可否を決める特殊な契約(Alt+F4等のシステムキー維持のため)を持つため、他3箇所の`void`関数のように黙って`return`するのではなく、明示的に`return false`することで「ハンドラが一切設定されていないのと同じ状態」(`main_window.h`が文書化している既定動作)を再現し、この関数が認識する全キー(Shift+Alt+矢印/Shift+Alt+I/プレーンAlt+↑↓)について安全にフォールスルーさせる設計とした。

## 完了条件

- [x] `handleSysKeyDownEvent()`にDiffビューガードを追加する — 実施済み(WI-38、2026-09-09)。他3箇所(`handleKeyDownEvent()`/`handleCharEvent()`/`dispatchCommand()`)の既存ガードパターンを踏まえた設計
- [~] Diffビュー表示中はShift+Alt+矢印/Iが実文書へ影響しないことを実機ドッグフーディングで確認する — **未完走、正直に記録。** Diffビューはコマンドパレット限定(`Ctrl+Shift+P`)でのみ起動可能な設計のため、対話的検証にはこの複数修飾キーの合成入力が必須だったが、本セッションの自動化環境ではこの組み合わせを複数の手法(`keybd_event`によるモディファイア単体合成+`PostMessage`、完全`keybd_event`合成、`AttachThreadInput`+`SetFocus`+`SetForegroundWindow`による明示的フォーカス確保、`GetAsyncKeyState`でのモディファイア実在確認済み)で4回試みても一貫して機能させることができなかった(`reference_no_win32_gui_automation.md`に記録済みの「修飾キー合成入力はこの環境では機能しない」という既知の制約と一致、今回は複数モディファイアの組み合わせで顕在化)。**修正自体の信頼性は、①`isDiffViewActive()`によるガードという同一パターンが同一ファイル内の他3箇所で既に実証済み・本番稼働中であること、②`grep -n isDiffViewActive src/app/normal_mode_wiring.cpp`で全4箇所の一貫性を確認したこと、③Debug/Release/ASan/UBSan全構成でのビルド・テスト・clang-tidyが問題無く通ったこと、の3点を根拠に判断した。**

## 再検証コマンド

```bash
grep -n "isDiffViewActive" src/app/normal_mode_wiring.cpp
```
