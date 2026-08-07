# Issue: ネイティブ Win32 オーバーレイウィジェットが画面上に一切描画されない (P0 — 原因未特定)

- **起票日:** 2026-08-08 (WI-05 ステップ2、`ui::TabBar` のドッグフーディング中に発見)
- **対象:** `src/ui/find_bar.cpp` / `grep_bar.cpp` / `command_palette.cpp` / `goto_line_bar.cpp` / `outline_pane.cpp` / `tab_bar.cpp` (WI-05) — メインウィンドウ (`MainWindow`) の子 `HWND` として実装されている全ウィジェット
- **優先度:** P0 (実アプリでの視覚的な操作性を広範囲に損なう、原因未特定)
- **対応 Phase:** 未定 (デバッガによる本格調査が必要、次のセッションで引き継ぐ)
- **親文書:** WI-05 (`build_plan.md`) ステップ2 実装中に発見

## 事実

WI-05 ステップ2 で `ui::TabBar` (`WC_TABCONTROL` ベース) を新設したところ、実アプリで作成・配置は成功する (`TCM_GETITEMCOUNT` で1件、`IsWindowVisible()=true`、`GetWindowRect()` も設計通りの座標) にもかかわらず、**画面上には一切描画されない** (デスクトップ合成後のスクリーンショットで、背景色と完全に同一のピクセルが並ぶのみ) というドッグフーディングが発覚した。

ユーザー自身が実機で `Ctrl+F` (FindBar) を押して確認したところ、**FindBar の入力欄も同様に見えない**ことが判明した。これにより、TabBar 固有の新規バグではなく、`MainWindow` の子 `HWND` として実装されている**ネイティブオーバーレイウィジェット全般に共通する、既存の(未診断だった)問題**であると確定した。

### 調査で否定できた仮説

1. **`ICC_TAB_CLASSES` の欠落** — これ自体は実在するバグで `launch_setup.cpp::initCommonControls()` に追加済み (`ICC_STANDARD_CLASSES | ICC_TREEVIEW_CLASSES | ICC_TAB_CLASSES`)。修正は正しく必要だったが、**この修正だけでは可視化しない**。
2. **DXGI flip-model スワップチェーンが子ウィンドウ合成を妨げている** — `render_device.cpp` の `desc.SwapEffect` を `DXGI_SWAP_EFFECT_FLIP_DISCARD` → `DXGI_SWAP_EFFECT_DISCARD` (bitblt方式) へ一時的に変更し再ビルド・再起動して A/B テストしたが、**結果は完全に同一**(TabBar は依然として不可視)。この仮説は明確に否定された。コードは `FLIP_DISCARD` に復元済み。
3. **リモートデスクトップ / DWM 合成無効化** — `GetSystemMetrics(SM_REMOTESESSION) == 0`(リモートセッションではない)、`DwmIsCompositionEnabled() == TRUE`(DWM 合成は有効)。
4. **低コントラストで実際には描画されているが見えにくいだけ** — タブ帯領域のピクセルをコントラスト強調処理 (min/max 輝度でリニアストレッチ) したが、完全に均一な単色のままだった (ミニマップが右端からはみ出す部分を除く)。つまり「文字が薄くて読めない」のではなく「本当に何も描画されていない」。
5. **メッセージループがビジーループで `WM_PAINT` を飢餓状態にしている** — `runMessageLoop()` (`main.cpp`) は標準的な `::GetMessageW()` + `::DispatchMessageW()` のブロッキングループであり、この種の問題が起きる構造ではない。

### 未検証の残り仮説

- `MainWindow::create()` が `ShowWindow(SW_SHOWNORMAL)` + 同期的な `UpdateWindow()` を、`RenderPipeline`/オーバーレイウィジェットがまだ一つも存在しない時点 (`onDeferredInit` が `WM_APP+1` としてキューされ、後続のメッセージループ反復で初めて処理される**前**) で呼んでいる (`main_window.cpp:109-112`)。この最初の同期的 `UpdateWindow()` が確立するウィンドウの「検証済み領域」状態と、後から作成される子ウィンドウの再描画要求が何らかの形で干渉している可能性があるが、デバッガによるステップ実行か `WM_PAINT` 受信有無の直接ログ計装なしには確定できない。

## 影響

Phase 5b3a (`FindBar` 新設) 以降に作られた**ほぼ全てのインタラクティブ UI 機能**が対象になりうる:

- 検索 (`FindBar`, Ctrl+F) / 置換
- Grep (`GrepBar`)
- コマンドパレット (`CommandPalette`)
- 行ジャンプ (`GotoLineBar`)
- アウトラインペイン (`OutlinePane`)
- タブ UI (`TabBar`, WI-05 進行中)

いずれのウィジェットも「作成・配置・入力状態は正しい (キーボード入力そのものは届いている可能性が高い)」が「画面に描画されない」ため、**実際のユーザーにとってはこれら機能の大半が事実上使用不能**になっている懸念がある。

過去の各フェーズ完了報告に記載された「実アプリ視覚確認」は、多くの場合スクリーンショットの詳細なピクセル解析までは行っておらず、プロセス生存確認や大まかな目視に留まっていた可能性が高い。このため、**この不可視化バグが Phase 5b3a 以降ずっと見過ごされてきた**と考えられる。

## 対応方針 (未着手)

以下のいずれか (あるいは組み合わせ) による本格調査が必要:

1. **デバッガ (Visual Studio / WinDbg) を実際にアタッチし、`TabBar::create()` の `CreateWindowExW` 戻り値、`WM_PAINT` が実際に `SysTabControl32` の内部ウィンドウプロシージャへ届いているかをブレークポイント/ウォッチで直接確認する。**
2. **一時的なデバッグ計装** — `SetWindowSubclass` で対象ウィジェットに一時的なサブクラスプロシージャを追加し、`WM_PAINT`/`WM_ERASEBKGND`/`WM_NCPAINT` 等の受信有無を `OutputDebugStringW` でログ出力する。
3. **別の検証済み通常デスクトップ環境での再現確認** — 現在の開発機/セッション固有の環境要因 (仮想ディスプレイドライバ、特殊なテーマ設定等) を切り分けるため、別のWindows環境で同じビルドを実行し同じ症状が出るか確認する。

## 完了条件

- [ ] `Ctrl+F` で `FindBar` の入力欄が実際に画面上で視認できる
- [ ] WI-05 完了後、`TabBar` のタブ帯が実際に画面上で視認できる
- [ ] 根本原因が特定され本ドキュメントに追記される
- [ ] 修正が適用され、**ピクセルレベルのスクリーンショット解析**(構造的な `Win32 API` 照会だけでなく)による視覚確認で解決が実証される

## 追加確認 (2026-08-08、WI-05完了後の再検証)

WI-05完了直後、ユーザーの指示で本issueを再確認した。フルビルド(Debug)を起動し、`EnumChildWindows`で`SysTabControl32`(TabBar)のハンドル・`GetWindowRect()`座標(スクリーン座標)を取得した上で、その座標へ**ピクセル単位で正確に一致するクロップ**(ウィンドウ全体のスクリーンショットから該当矩形のみを切り出し2倍ズーム)で再検証した。従来のセッションでは大まかな領域のスクリーンショットに留まっていたため、この構造座標と完全一致したクロップによる検証は今回が初めて。

**結果: 再現を確認した。** `TabControl`の構造座標(`IsWindowVisible()=true`)に一致する領域は、完全に均一な背景色(タブラベル・区切り線・選択インジケータ等のコントロール描画が一切見えない)であり、issueの記述と完全に一致する。根本原因は依然として未特定のまま。

**副産物として、別の独立したバグを発見した(このissueとは無関係、別途起票済み)。** `RenderPipeline::drawMinimap()`が`reservedTopHeightDips()`(TabBar/Breadcrumb/Sticky scroll用に確保された上部の高さ)を一切考慮せず、常に`y=0.0F`から描画を開始している(`Phase 7v`、コミット`64467c7`から存在する既存不具合、WI-05由来ではない)。実際にミニマップの色付きバーがタブ帯の構造領域の最上部から描画されていることをスクリーンショットで確認した。この不具合は別issueとして切り出す価値があると判断し、バックグラウンドタスクとして記録した(このissue自体の完了条件には含めない)。

## 再検証コマンド

```powershell
# NeoMIFESを起動し、ウィンドウ左上~タブ帯付近をスクリーンショット
build\debug\src\app\NeoMIFES.exe --open src\app\main.cpp
# → Ctrl+Fを押し、タイトルバー直下の領域を目視/スクリーンショットで確認
```

## WI-05 への影響 (暫定回避策)

TabBar 自体の実装 (`ui::TabBar`, `RenderPipeline::setTabBarHeightDips()` 等) は Win32 API レベルで正しく動作している (`TCM_GETITEMCOUNT`/`TCM_GETCURSEL`/`TCM_SETCURSEL` 等が期待通り機能し、`TCN_SELCHANGE` 通知も正しく発火する見込み) ため、本 issue の解決を待たずに WI-05 の実装自体は続行する。ステップ2以降の「実アプリ視覚確認」は、本 issue が解決するまでの間、`EnumChildWindows`/`SendMessage` 等の Win32 API 照会による構造的検証で代替する。
