# Issue: `--measure-frame`(および`RenderPipeline::render()`のvsync同期`Present1`全般)がウィンドウが一度も表示されない(非合成)状態だと無期限にハングする (P2 — 未修正、実機で100%再現確認済み)

- **起票日:** 2026-09-09(WI-39、`frame_measure_hangs_under_ubsan_clang_cl.md`の調査中に副次的に発見)
- **対象:** `src/render/src/render_device.cpp`の`Present1(1, 0, &presentParams)`呼び出し、`src/app/main.cpp`の`wireMeasureFrameMode()`/`runFrameMeasurement()`
- **優先度:** P2(`--measure-frame`は開発者向けコマンドラインフラグでエンドユーザー機能への直接影響は無いが、CI/自動化スクリプトから非表示ウィンドウで起動されるシナリオがあれば無期限ハングしうる)

## 事実

`--measure-frame <out.json>`をウィンドウが非表示(`SW_HIDE`)状態で起動すると、**Release/UBSanいずれのプリセットでも100%再現してハングする**(通常の表示状態での起動では約5.3秒で正常終了)。

実測(`Start-Process -WindowStyle Hidden`で起動、20秒タイムアウト):
- `build\ubsan\src\app\NeoMIFES.exe`: 8/8回ハング
- `build\release\src\app\NeoMIFES.exe`: 3/3回ハング

一方、通常の表示状態(`Start-Process`のデフォルト、ウィンドウ表示あり)では:
- `build\ubsan\src\app\NeoMIFES.exe`: 3/3回、約5.3秒で正常終了

## 原因(推定、未確証)

`render_device.cpp`の`Present1(1, 0, &presentParams)`は`SyncInterval=1`(次のvblankまで同期待ち)の標準的な呼び出し。DXGIのフリップモデルスワップチェーンは、ウィンドウが実際にDWMによって合成されて初めてvsync同期先(モニタの垂直帰線タイミング)を持つ——**ウィンドウが一度も表示されない(`SW_HIDE`)場合、この同期先が存在せず、`Present1`が無期限にブロックする可能性が高い。** コード自体は`DXGI_STATUS_OCCLUDED`を許容エラーとして扱っているが、これはレガシー(BitBlt)モデルのスワップチェーンや`DXGI_PRESENT_TEST`使用時のみ返る値で、フリップモデルでは通常返らないため、非表示ウィンドウのケースを実質的に想定していないと考えられる。

`runFrameMeasurement()`(`main.cpp`)は`onDeferredInit`コールバック内で300回の`pipeline.render()`を完全に同期的に呼び出す設計であり、この中の1回でも`Present1`がハングすればプロセス全体が応答不能になる。

## 影響

- `--measure-frame`を非表示ウィンドウ(`STARTF_USESHOWWINDOW`+`SW_HIDE`)から起動する自動化シナリオが存在すれば無期限にハングする。
- 現在の`tests/integration/frame_measure_test.cpp`は`CREATE_NO_WINDOW`(コンソール抑制フラグ、GUIアプリのウィンドウ表示状態には影響しない)を使っており、`SW_HIDE`とは異なる機構のため、**この既存テスト自体はこの問題の影響を受けない**(実測で確認済み、後述の`frame_measure_hangs_under_ubsan_clang_cl.md`参照)。

## `frame_measure_hangs_under_ubsan_clang_cl.md`との関係(未確証)

本issueの発見は、上記issueが報告する「CIの`ubsan`ジョブでのみ`FrameMeasureTest.ProducesValidProfile`が非決定的にハングする」という現象の調査中に得られた。**メカニズム(vsync同期`Present1`が窓の合成状態に依存してブロックしうる)には類似性があるが、両者を同一の根本原因と断定する証拠は無い**——実際、上記issueが使う`CREATE_NO_WINDOW`はウィンドウを非表示にする機構ではないため、少なくとも本issueで実証した「明示的に非表示にする」ケースとは直接同一ではない。CI環境固有の要因(共有ランナーでのGPU/ディスプレイドライバの一時的な状態、リモートデスクトップセッションでの合成の遅延等)がある種の「ウィンドウが一時的に合成されない」状況を作り出し、同じ`Present1`の脆弱性を別経路から突いた可能性はあるが、推測の域を出ない。

## 対応案(未実施)

1. `--measure-frame`モードで、ウィンドウが実際に表示され最初のペイントが完了するまで`Present1`を呼ばないようにする防御的なガードを追加する(例: `onFirstPaint`相当のシグナルを待ってから計測ループを開始する)。
2. あるいは、`Present1`呼び出しにタイムアウト機構(別スレッド+`WaitForSingleObject`によるウォッチドッグ等)を追加し、無期限ブロックを防ぐ。
3. 最小対応として、`--measure-frame`が非表示ウィンドウでは動作しないことをドキュメント化し、既存のCI/自動化が`SW_HIDE`を使っていないことを確認するだけに留める(実害が現時点で無いため)。

## 完了条件

- [ ] 対応方針(上記1〜3のいずれか、またはユーザー判断による見送り)を決定する
- [ ] 決定した対応を実施し、非表示ウィンドウでの`--measure-frame`起動が無期限にハングしないことを確認する(または意図的に対象外とする場合はその判断を記録する)
