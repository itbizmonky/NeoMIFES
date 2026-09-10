# Issue: `--measure-frame`がウィンドウが一度も表示されない(非表示)状態だと無期限にハングする (P2 — 🟢 WI-42で解消)

- **起票日:** 2026-09-09(WI-39、`frame_measure_hangs_under_ubsan_clang_cl.md`の調査中に副次的に発見)
- **調査日:** 2026-09-10(WI-41)、真因を確定(Win32の`STARTF_USESHOWWINDOW`仕様) — **ただし後にWI-42で診断が誤りと判明、後述**
- **解決日:** 2026-09-11(WI-42)。真の原因を再確定した上で`main.cpp`に防御的ガードを追加し解消
- **対象:** `src/app/main.cpp`の`wWinMain()`/`wireMeasureFrameMode()`、`src/ui/src/main_window.cpp`の`handlePaint()`
- **優先度:** P2(`--measure-frame`は開発者向けコマンドラインフラグでエンドユーザー機能への直接影響は無いが、CI/自動化スクリプトから非表示ウィンドウで起動されるシナリオがあれば無期限ハングしうる)

## 事実

`--measure-frame <out.json>`をウィンドウが非表示(`SW_HIDE`)状態で起動すると、**Release/UBSanいずれのプリセットでも100%再現してハングする**(通常の表示状態での起動では約5.3秒で正常終了)。

実測(`Start-Process -WindowStyle Hidden`で起動、20秒タイムアウト):
- `build\ubsan\src\app\NeoMIFES.exe`: 8/8回ハング
- `build\release\src\app\NeoMIFES.exe`: 3/3回ハング

一方、通常の表示状態(`Start-Process`のデフォルト、ウィンドウ表示あり)では:
- `build\ubsan\src\app\NeoMIFES.exe`: 3/3回、約5.3秒で正常終了

## WI-41時点の診断(誤り、記録として保持)

WI-41では、`Present1(1, 0, &presentParams)`(`render_device.cpp`、`SyncInterval=1`のvsync同期Present)がウィンドウ非表示だとDWMに一度も合成されず同期先を持てないためブロックする、と診断した。`STARTF_USESHOWWINDOW`仕様(起動元プロセスのSTARTUPINFOがアプリ自身の最初の`ShowWindow()`呼び出しを暗黙に上書きする)により`Start-Process -WindowStyle Hidden`が`MainWindow::create()`の`ShowWindow(SW_SHOWNORMAL)`を上書きする、という部分の理解自体は正しかった。

**しかし「`onDeferredInit`(計測ループが実際に走る場所)で`IsWindowVisible()`をチェックして待てばよい」という対応案(選択肢1として採用)は、WI-42の実装検証で誤りと判明した。**

## WI-42で確定した真因

`main_window.cpp`の`handlePaint()`を確認すると、`onDeferredInit`は**WM_PAINTが実際に発火した後にしか**`PostMessageW(kMsgDeferredInit)`されない設計になっている(`m_firstPaintFired`ゲート)。つまり「ペイント完了を待ってから計測を始める」という発想はこの時点で既に実装済みだった。

WI-42でマーカーファイル方式のプローブ(`onDeferredInit`の呼び出し直後に一時ファイルを書き出す診断ビルド)を実施した結果、**非表示ウィンドウでは`onDeferredInit`自体が一切発火しない**ことを実証した(5秒待機してもマーカーファイルが作成されない)。

**真因: 一度も表示されない(`WS_VISIBLE`が立たない)ウィンドウはそもそも`WM_PAINT`を一切受け取らない。** `handlePaint()`は`WM_PAINT`メッセージのハンドラであり、`WM_PAINT`が配送されなければ`m_onFirstPaint`も`m_onDeferredInit`も永久に発火しない。結果、`wireMeasureFrameMode()`の計測ロジック(`RenderPipeline::attach()`/`Present1()`を含む)は**一度も実行されないまま**、`runMessageLoop()`の`GetMessageW()`が処理すべきメッセージを一切受け取れず無期限にブロックする——**Present1のvsync待ちではなく、単純な「メッセージキューが空のまま誰も`window.requestClose()`を呼ばない」という、より単純な種類のハングだった。**

## 対応(WI-42で実装)

`wWinMain()`内、`window.create(hInstance, cfg)`が返った直後(`MeasureFrame`モード限定)に`IsWindowVisible(window.hwnd())`を直接チェックするガードを追加した。`create()`内の`ShowWindow()`/`UpdateWindow()`は同期呼び出しのため、`create()`が返った時点で`WS_VISIBLE`状態は確定している——これが「まだペイントされていない」と「今後も一切ペイントされない」を区別できる最も早いタイミングだった。

非表示と判定した場合、メッセージループに一切入らず**終了コード3**で即座に終了する(プロファイルJSONは書き出さない、書き出すと誤って成功したように見えるため)。

**影響範囲はユーザー承認のもと`--measure-frame`のみに限定した。** `MeasureStartup`/`MeasureMemory`モードの`onFirstPaint`も同じくWM_PAINT起点で発火するため、理論上は同じ脆弱性を抱えている可能性が高いが、これは未検証のまま将来の再評価に委ねる(推測実装を避けるため)。

## 検証(WI-42)

- 新規回帰テスト`FrameMeasureTest.HiddenWindowFailsFastInsteadOfHanging`(`tests/integration/frame_measure_test.cpp`)を追加。`STARTF_USESHOWWINDOW`+`SW_HIDE`で`CreateProcessW`起動し、終了コード3・出力ファイルが空のまま・15秒以内に完了することを確認。修正前はこのテストが15秒タイムアウトで強制終了(`TerminateProcess`)された状態を実際に再現・確認した上で修正、修正後は約40msで完了することを実測した。
- 既存`FrameMeasureTest.ProducesValidProfile`(通常表示、ハッピーパス)への回帰無し(約5.3秒で成功、変化なし)。

## `frame_measure_hangs_under_ubsan_clang_cl.md`との関係(未確証のまま、WI-41の記述を維持)

本issueの発見は、上記issueが報告する「CIの`ubsan`ジョブでのみ`FrameMeasureTest.ProducesValidProfile`が非決定的にハングする」という現象の調査中に得られた。**メカニズム(非表示ウィンドウでの無期限ハング)には類似性があるが、両者を同一の根本原因と断定する証拠は無い**——本issueの再現条件(`STARTF_USESHOWWINDOW`+`SW_HIDE`)は上記issueの再現条件(`CREATE_NO_WINDOW`、ウィンドウ非表示とは異なる機構)とは異なる。関係は依然として未確証のまま。

## 完了条件

- [x] 対応方針を決定する(ユーザー選択: 非表示検知時はエラーで即座に終了)
- [x] 決定した対応を実施し、非表示ウィンドウでの`--measure-frame`起動が無期限にハングしないことを確認する(実機・回帰テスト両方で確認済み)
