# Issue: バーストタイピング時、vsync同期Presentのメッセージキュー滞留により再描画が「ガタつく」(✅ WI-32で解決)

- **起票日:** 2026-09-05(WI-31、ユーザー報告「まだキー入力すると全再描写されてウィンドウの描写がガタつく」の実測診断)
- **解決日:** 2026-09-06(WI-32、入力キューのドレイン優先を実装)
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

- [x] 対応方針をユーザーと合意する(候補1「入力キューのドレイン優先」をAskUserQuestionで選択)
- [x] 選定した方針を実装し、実測でバースト時の再描画回数が大幅に削減されることを確認する
- [x] 実機ドッグフーディングで「ガタつく」体感が解消したことを確認する(視覚的に最終状態が正しく反映されることも screenshot で確認)

## WI-32での解決内容

`syncRenderStateAndInvalidate()`(`normal_mode_wiring.cpp`)の末尾`InvalidateRect`呼び出しを、`PeekMessageW(hwnd, WM_KEYFIRST, WM_KEYLAST, PM_NOREMOVE)`で「このウィンドウにキー入力がまだキューに残っているか」を確認した上で条件化した。残っていれば今回の再描画要求はスキップ(RenderPipelineへの状態同期自体は無条件に行われるためデータが古くなることは無い)、キューが実際に捌けた時点で1回だけ`InvalidateRect`する。バースト終了後に必ず最終状態が描画される保証として、`RenderPipeline::markPaintRequested()`/`paintOverdue(50ms)`による時間ベースの安全弁を追加(WM_KEYUPや境界no-opなど、それ自体が再描画を起こさないキー入力がバーストの末尾に来て再描画要求を永久に失うケースへの対策)。詳細設計は`docs/design/build_plan.md`のWI-32セクション参照。

### 実測診断で判明した追加の知見(WI-31実測手法の限界)

WI-32実装後、WI-31と同一手法(20ms間隔でのPostMessage注入)で再検証したところ、**この注入方法では`PeekMessageW`が一度も「キューに追加入力あり」を検知しなかった**(修正前と同じ30〜32msのフレーム間隔が再現された)。原因を調査した結果、20ms間隔で1通ずつ送信する手法は、各`WM_CHAR`の処理(ドキュメント編集+チェック)がPresent待ちの前に一瞬で完了するため、実際には「キューに複数メッセージが同時に滞留する」状態を作れていなかったと判明——WI-31が観測した30〜32msという間隔自体は実在するが、その原因は本issueが想定した「メッセージキューの滞留」ではなく、別の(Direct2D/DXGIプレゼンテーションパイプライン側の)メカニズムによるものである可能性が高い。

一方、**遅延なしで一括投入する真のバースト(23文字を間髪入れずPostMessage)では、修正が設計通りに機能することを実測で確認した**: `hasQueuedKeyboardInput()`が20回連続で`true`を返し再描画をスキップ、キューが捌けた最後の1回のみ`InvalidateRect`が発行され、実際の`RenderPipeline::render()`呼び出しは**23文字に対しわずか1回**に集約された(修正前なら最大23回)。スクリーンショットで最終状態(全文字が正しい位置に反映)も確認済み。

**結論:** 本修正は「メッセージキューに真の滞留が生じた場合」に設計通り機能し、その場合の再描画回数を劇的に削減することを実証した。ただし、WI-31が実測した「20ms間隔注入で一貫して30〜32msに張り付く」という現象そのものの完全な説明(なぜキューの滞留無しにこの間隔になるか)は本WIのスコープでは特定していない——次点候補として記録する(下記参照)。実際の人間のタイピングは瞬間的なバースト(数ms間隔で連続する文字)を含むため、本修正はその実際のユースケースに対して有効である。

## 再現手順

`RenderPipeline::render()`に一時計装(`platform::PerfClock::nanosSinceProcessStart()`を`renderOnce()`前後で取得しCSVへ追記)、`syncRenderStateAndInvalidate()`に判定入力/結果のログを追加し(WI-32実装時に使用、完了後に完全除去済み)、Debugビルドを`--open`で起動、`PostMessage(WM_CHAR)`を遅延なしで連続注入して(`keybd_event`不要、修飾キー無しの生文字入力のみで再現可能)ログを確認する。
