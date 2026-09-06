# Issue: 横スクロールバーのつまみドラッグが`WM_HSCROLL`の16bit値をそのまま使っており、列65535を超えると破綻する(P2 — 未修正、縦スクロールバーは新規実装時に回避済み)

- **起票日:** 2026-09-06(WI-34、縦スクロールバー新規実装のための横スクロールバー実装調査中に発見)
- **対象:** `src/ui/src/main_window.cpp`の`MainWindow::handleHScroll()`、`src/app/normal_mode_wiring.cpp`の`handleHScrollEvent()`
- **優先度:** P2(実用上は極めて長い1行〔列65535超〕を持つファイルでのみ顕在化する縮退ケース)

## 事実

`WM_HSCROLL`のWin32仕様では、`wParam`の`HIWORD`(つまみ位置、`SB_THUMBTRACK`/`SB_THUMBPOSITION`でのみ意味を持つ)は**16bitに切り詰められる**(最大65535)。`MainWindow::handleHScroll()`はこの値をそのまま`m_onHScroll(m_hwnd, LOWORD(wParam), HIWORD(wParam))`で転送し、`handleHScrollEvent()`もこれをそのまま`computeHScrollTargetColumn()`へ渡している——**列65535を超える横方向スクロールバーのつまみドラッグは、それ以上先へ進めなくなる**(値が16bitで折り返されるか頭打ちになる)。

## 原因

`SCROLLINFO`構造体自体は32bit(`nMax`/`nPos`)のフルレンジをサポートしているが、`WM_HSCROLL`/`WM_VSCROLL`メッセージ自体の`wParam`の`HIWORD`は歴史的経緯により16bitに制限されている。正しい値を得るには、`SB_THUMBTRACK`/`SB_THUMBPOSITION`受信時に`GetScrollInfo(hwnd, SB_HORZ, &si)`(`SIF_TRACKPOS`)で実際の値を取得し直す必要があるが、`handleHScrollEvent()`はこれを行っていない。

**WI-34で新規実装した縦スクロールバー(`handleVScrollEvent()`)はこの教訓を踏まえ、`GetScrollInfo(SIF_TRACKPOS)`で実際の値を解決してから`computeVScrollTargetLine()`へ渡す設計にしており、この問題を回避済み。** 横スクロールバーは既存のWI-03実装のまま未修正で残っている。

## 対応案(未実施)

`handleHScrollEvent()`(`normal_mode_wiring.cpp`)を、新設の`handleVScrollEvent()`と同型に修正する: `SB_THUMBTRACK`/`SB_THUMBPOSITION`受信時に`GetScrollInfo(hwnd, SB_HORZ, &si)`(`SIF_TRACKPOS`)で実値を取得してから`computeHScrollTargetColumn()`へ渡す。

## 完了条件

- [ ] `handleHScrollEvent()`を`GetScrollInfo(SIF_TRACKPOS)`使用に修正する
- [ ] 列65535を超える長い1行を持つファイルで、横スクロールバーのつまみドラッグが正しく機能することを実機で確認する
