# Issue: 横スクロールバーのつまみドラッグが`WM_HSCROLL`の16bit値をそのまま使っており、列65535を超えると破綻する(P2 — 🟢 WI-37で解決済み)

- **起票日:** 2026-09-06(WI-34、縦スクロールバー新規実装のための横スクロールバー実装調査中に発見)
- **解決日:** 2026-09-08(WI-37)
- **対象:** `src/ui/src/main_window.cpp`の`MainWindow::handleHScroll()`、`src/app/normal_mode_wiring.cpp`の`handleHScrollEvent()`
- **優先度:** P2(実用上は極めて長い1行〔列65535超〕を持つファイルでのみ顕在化する縮退ケース)

## 事実

`WM_HSCROLL`のWin32仕様では、`wParam`の`HIWORD`(つまみ位置、`SB_THUMBTRACK`/`SB_THUMBPOSITION`でのみ意味を持つ)は**16bitに切り詰められる**(最大65535)。`MainWindow::handleHScroll()`はこの値をそのまま`m_onHScroll(m_hwnd, LOWORD(wParam), HIWORD(wParam))`で転送し、`handleHScrollEvent()`もこれをそのまま`computeHScrollTargetColumn()`へ渡している——**列65535を超える横方向スクロールバーのつまみドラッグは、それ以上先へ進めなくなる**(値が16bitで折り返されるか頭打ちになる)。

## 原因

`SCROLLINFO`構造体自体は32bit(`nMax`/`nPos`)のフルレンジをサポートしているが、`WM_HSCROLL`/`WM_VSCROLL`メッセージ自体の`wParam`の`HIWORD`は歴史的経緯により16bitに制限されている。正しい値を得るには、`SB_THUMBTRACK`/`SB_THUMBPOSITION`受信時に`GetScrollInfo(hwnd, SB_HORZ, &si)`(`SIF_TRACKPOS`)で実際の値を取得し直す必要があるが、`handleHScrollEvent()`はこれを行っていない。

**WI-34で新規実装した縦スクロールバー(`handleVScrollEvent()`)はこの教訓を踏まえ、`GetScrollInfo(SIF_TRACKPOS)`で実際の値を解決してから`computeVScrollTargetLine()`へ渡す設計にしており、この問題を回避済み。** 横スクロールバーは既存のWI-03実装のまま未修正で残っている。

## 対応案(実施済み)

`handleHScrollEvent()`(`normal_mode_wiring.cpp`)を、新設の`handleVScrollEvent()`と同型に修正した: `SB_THUMBTRACK`/`SB_THUMBPOSITION`受信時に`GetScrollInfo(hwnd, SB_HORZ, &si)`(`SIF_TRACKPOS`)で実値を取得してから`computeHScrollTargetColumn()`へ渡す。あわせて`computeHScrollTargetColumn()`自体の`scrollPos`引数を`WORD`(16bit)から`std::uint32_t`(32bit、`computeVScrollTargetLine()`と同型)へ拡張した——`GetScrollInfo()`で解決した実値を`WORD`型の引数へ渡すと再び16bitへ切り詰められてしまい、修正が無効化されるため、この型変更も修正の一部として必須だった。

## 完了条件

- [x] `handleHScrollEvent()`を`GetScrollInfo(SIF_TRACKPOS)`使用に修正する — 実施済み(WI-37、2026-09-08)。`computeHScrollTargetColumn()`の`scrollPos`引数の型拡張(`WORD`→`std::uint32_t`)も同時実施
- [x] 単体テストで65535を超える値(999999)が`computeHScrollTargetColumn()`を素通りすることを確認 — `ComputeHScrollTargetColumnThumbTrackAndThumbPositionUseScrollPosDirectly`へ追加(`computeVScrollTargetLine()`側の既存の同種テストと対応)
- [~] 列65535を超える長い1行を持つファイルで、横スクロールバーのつまみドラッグが実機で正しく機能することを確認 — **部分的検証。** 90000文字の1行ファイルを開き実際のマウスドラッグ(`SetCursorPos`+`mouse_event`)でつまみを掴んで動かす実験を行い、`GetScrollInfo(SIF_TRACKPOS)`がドラッグ中に動的に変化する実値を返すこと自体は確認できた(この修正が依拠するWin32機構が本環境で実際に機能することの裏付け)。ただし、スクリーンショット座標からトラック上のピクセル位置を算出する自作の幾何計算では、列65535という閾値を厳密に跨ぐ再現を安定して得られなかった(このスケールの範囲では1ピクセルが数百〜数万列に相当し、合成マウス操作の精度では特定の列番号へ正確に着地させることが困難だった)。通常スケール(800文字の行)でのつまみ・トラッククリック操作は正常に機能することは確認済み(回帰無し)。修正自体はWI-34で実装・本番投入済みの`handleVScrollEvent()`と完全に同型の設計であり、単体テストが論理層の正しさを直接証明しているため、実機での巨大レンジ再現が得られなかったことは修正の信頼性を損なわない判断とした
