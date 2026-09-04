# Issue: `handleSysKeyDownEvent()` に `isDiffViewActive()` ガードが無く、Diffビュー表示中でも不可視の実文書へ矩形選択が適用されてしまう (P2〜P3 — 未修正)

- **起票日:** 2026-09-04 (WI-27のPlan agentによる設計レビュー中に発見)
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

## 対応案(未実施)

`handleSysKeyDownEvent()`の冒頭に、他3箇所と同じ`if (renderPipeline.isDiffViewActive()) { return false; }`(またはEscapeのみ処理する同種のガード)を追加する。ただし`handleKeyDownEvent()`のガードはEscapeで`syncViewForActiveSession()`を呼びDiffビューを閉じる副作用を持つため、`handleSysKeyDownEvent()`側にも同種のEscape処理が必要か(現状Diffビューを閉じるキーが他経路で処理されているか)の確認が必要——単純に`return false`するだけで良いのか、既存の`handleKeyDownEvent()`のEscape処理と重複・競合しないかは要検討。

## 完了条件

- [ ] `handleSysKeyDownEvent()`にDiffビューガードを追加し、Diffビュー表示中はShift+Alt+矢印/Iが実文書へ影響しないことを実機ドッグフーディングで確認する

## 再検証コマンド

```bash
grep -n "isDiffViewActive" src/app/normal_mode_wiring.cpp
```
