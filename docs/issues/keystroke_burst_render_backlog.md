# Issue: バーストタイピング時、vsync同期Presentのメッセージキュー滞留により再描画が「ガタつく」(P1 — 実測診断完了、対応方針は未定)

- **起票日:** 2026-09-05(WI-31、ユーザー報告「まだキー入力すると全再描写されてウィンドウの描写がガタつく」の実測診断)
- **対象:** `src/render/src/render_device.cpp`の`RenderDevice::endFrame()`(`Present1(1, 0, ...)`)、`src/app/normal_mode_wiring.cpp`の`handleCharEvent()`/`syncRenderStateAndInvalidate()`、`MainWindow`のWM_PAINT/メッセージループ全体の設計
- **優先度:** P1(WI-28で対応したステータスバーのチラつきとは別の、ユーザーが現在も報告しているコア体験〔タイピング〕の品質問題)

## 事実(WI-31の実測、一時計装+ドッグフーディングにより取得・DOGFOOD-TEMPコードは診断後に完全除去済み)

`RenderPipeline::render()`にQueryPerformanceCounterベースの一時計装(`platform::PerfClock`)を追加し、`renderOnce()`が実際に走った各フレームの「開始時刻(プロセス起動からのns)」と「所要時間(ns)」をCSVへ記録。確立済みのハイブリッド技法(`keybd_event`+`PostMessage`)でDebugビルドへ2種類のタイピング速度を模擬注入した。

**① 現実的な間隔(1文字ごとに130msスリープ、26文字、ASCII+日本語混在):**
フレーム開始間隔は136〜152ms(スクリプトの130ms間隔とほぼ一致)、各`renderOnce()`自体の所要時間は0.54〜2.85ms——16.6msのフレーム予算に対し十分な余裕があり、問題は一切観測されなかった。

**② バースト間隔(1文字ごとに20msスリープ、"the quick brown fox jumps"の26文字):**
`renderOnce()`自体の所要時間は0.65〜2.05ms(①と同水準、CPU側コストは変わらず軽い)。**しかし実際のフレーム開始間隔は、注入した20msではなく一貫して30〜32ms(一部46ms)——60Hzの1〜2vblank分(16.67ms/33.3ms)に張り付いた。** 26文字の注入自体はスクリプト上約520ms(20ms×26)で完了するのに対し、実際に25フレーム分の処理が完了するまでに約780ms要しており、**入力の到着速度がレンダリングパイプラインの処理速度(1フレームあたり最低1vblank)を上回った結果、WM_CHARがメッセージキューに滞留し、画面反映が入力に追いつかず後追いで「バーストして」表示される**ことが実測で確認された。

## 原因

`RenderDevice::endFrame()`(`render_device.cpp:259-283`)は`m_swapChain->Present1(1, 0, &presentParams)`——SyncInterval=1で、次のvblankまで**必ず**ブロックする(ティアリング防止の意図的な設計、ADR-011参照)。この呼び出しは`RenderPipeline::render()`→`handlePaintEvent()`(`normal_mode_wiring.cpp`)→`MainWindow::handlePaint()`という、WM_PAINTのディスパッチと**完全に同期した**経路で行われ、UIスレッドを1回のPresentごとに最低1vblank(≈16.6ms、実測では稀に2vblank≈33ms超)ブロックする。

`syncRenderStateAndInvalidate()`は1キー入力(`WM_CHAR`)ごとに`InvalidateRect(hwnd, nullptr, FALSE)`を1回呼び、次のWM_PAINTで上記の同期Present経路が丸ごと1回走る。人間の実際のタイピングは終始一定間隔ではなく、単語内の連続打鍵等で数十ms未満の間隔になるバーストを含むのが通常であり、そのバースト区間でこの「1キー入力=最低1vblankの同期処理」という制約に到達すると、WM_CHARの処理(`handleChar()`によるドキュメント編集自体は軽い)は素早く進む一方、その都度のPresentがボトルネックとなって**メッセージキューにWM_CHARが滞留し、画面表示が実際の入力より遅れて「追いつく」**——これが「全再描写されてガタつく」の実体であると、実測データにより裏付けられた。

## 却下した仮説(WI-31着手前のExploreエージェント調査+今回の実測で除外)

- **TextLayoutCacheのwholesale invalidationによるCPUコスト:** 既存ベンチ(`BM_TextLayoutCache_Miss`=542ns/行)から無視できる規模と推定していたが、今回の実測(`renderOnce()`所要時間が①②とも常に3ms未満)で直接裏付けられた。CPU側のレンダリングコストは問題ではない。
- **ティアリング:** `Present1`はSyncInterval=1で常にvsync同期、ティアリングは発生しない(該当なし)。
- **WM_ERASEBKGND由来の背景フラッシュ、二重invalidate、WM_PAINT再発行ループ:** いずれもExploreエージェントの調査で明示的に排除済み(`main_window.cpp`のWM_ERASEBKGND抑制・単一InvalidateRect呼び出し・毎回の`ValidateRect`を確認)。

## 対応案(未実施、大規模変更のためユーザー承認が必要 — CLAUDE.mdルール9)

根本原因はCPUコストではなく「1キー入力ごとに同期的にvsync待ちPresentを行う」というメッセージループ/描画パイプラインのアーキテクチャそのものにあるため、WI-28型の「無駄な再構築を減らす」修正では解決しない。候補として以下を検討すべきだが、いずれも設計の再検討とユーザー承認を要するため本issueでは実施しない:

1. **入力キューのドレイン優先**: WM_CHAR/WM_KEYDOWNを処理する際、`PeekMessage(..., PM_NOREMOVE)`等でメッセージキューに追加の入力が既に溜まっているか確認し、溜まっている場合は`InvalidateRect`(≒再描画)を都度発行せず、キューが実際に捌けた(バーストが収まった)タイミングでまとめて1回だけ再描画する——複数キー入力を1フレームに合流させる設計。バースト終了後に必ず最終状態が1回は描画されることを保証する設計が必要。
2. **描画をUIスレッドから分離**(より大規模): 別スレッドでPresentを行い、入力処理スレッドをブロックしない設計。ADR-011の想定を超える規模の変更になる。
3. **SyncInterval=0への変更**: ティアリングとのトレードオフになるため不採用が濃厚(意図的にSyncInterval=1を選んだADR-011の判断を覆す必要があり、却下される可能性が高い)。

## 完了条件

- [ ] 対応方針をユーザーと合意する(候補1が最有力だが未承認)
- [ ] 選定した方針を実装し、①②と同様の手法で実測し、バースト時のフレーム開始間隔が注入間隔に近い値まで改善することを確認する
- [ ] 実機ドッグフーディングで「ガタつく」体感が解消したことを確認する

## 再現手順

`RenderPipeline::render()`に一時計装(`platform::PerfClock::nanosSinceProcessStart()`を`renderOnce()`前後で取得しCSVへ追記)を追加し、Debugビルドを`--open`で起動、`keybd_event`+`PostMessage(WM_CHAR)`で20ms間隔の連続文字入力を注入してCSVのフレーム開始間隔を確認する(本issue記載の実測①②と同一手順)。計装コード自体はWI-31完了時に完全除去済みのため、再現時は同等のコードを再度一時的に追加する必要がある。
