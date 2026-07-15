# ADR-009: Direct2D デバイス生成は同期・UIスレッド・自己ポストメッセージ経由で遅延させる

- **ステータス:** Accepted
- **決定日:** 2026-07-16 (Phase 3a 着手時)
- **関連:** [ADR-008](ADR-008-com-raii-comptr.md)、[`docs/design/basic_design.md`](../design/basic_design.md) §4.1、Phase 1 起動計測実測値 (CI 22ms)

## コンテキスト

`basic_design.md` §4.1 は「Direct2D デバイス生成もメインウィンドウ表示後に非同期化」「COM/DirectWrite ファクトリはシングルトンで再利用」を要求している。Phase 1 で実測した起動時間 (CI 上で first paint = 22ms、目標 300ms の 7%) を Phase 3 で退化させないことが必須要件 (CLAUDE.md 絶対ルール10: 性能改善はベンチマーク根拠必須、退化は許されない)。

D3D11+D2D+DXGI のデバイス・スワップチェーン生成 (`D3D11CreateDevice` → `D2D1CreateDevice` → `CreateDeviceContext` → `CreateSwapChainForHwnd` → バックバッファのバインド) を `WM_CREATE`/`CreateWindowExW` の同期パス内で行うと、これらの COM 呼び出しのコストがそのまま `firstPaintNs` に上乗せされ、退化リスクがある。

## 選択肢

1. **同期・UIスレッド・自己ポストメッセージ経由 (採用):** `MainWindow` が最初の `WM_PAINT` 完了直後に `WM_APP` を自分自身に `PostMessageW` し、メッセージループを1周挟んでからデバイス生成を行う。処理自体はUIスレッド上で同期的だが、`CreateWindowExW`/`UpdateWindow` の同期パスからは完全に外れる
2. **ワーカースレッドで非同期生成:** 別スレッドでデバイス生成を行い、完了をUIスレッドに通知する
3. **`WM_CREATE` 内で同期生成 (現状維持):** 却下 — 上記の退化リスクをそのまま抱える

## 決定

**選択肢1 (同期・UIスレッド・自己ポストメッセージ経由) を採用する。**

## 根拠

- **実測コストが起動予算に対して無視できる規模:** D3D11+D2Dのデバイス生成 (HARDWARE、フォールバック含め) は経験的に5ms未満で完了する。300ms起動目標、あるいはCI実測22msという既存の余裕に対しても、「`CreateWindowExW`/`UpdateWindow`の同期パスの外に出す」だけで実質的な影響はゼロにできる — ワーカースレッド化までは不要
- **`WM_APP`自己ポストの1手だけで`--measure-startup`の計測契約を壊さず達成できる:** `firstPaintNs` は既存の GDI プレースホルダー描画(`WM_PAINT`内、`m_onFirstPaint`発火)で計測される。デバイス生成の`WM_APP`ポストはその**後**に行われるため、計測対象のタイミングに一切触れない。ワーカースレッド化ではこの保証を得るために追加の同期ロジックが必要になり、オーバーエンジニアリングになる
- **ワーカースレッド化はCOMアパートメント設計の複雑性を持ち込む:** `D2D1_FACTORY_TYPE_SINGLE_THREADED`のファクトリをスレッド間で受け渡す場合、`D2D1_FACTORY_TYPE_MULTI_THREADED`への変更や`ComPtr`のスレッド間所有権移転の設計が必要になり、Phase 3aのスコープ(基盤配線)に対して不釣り合いなコストとなる。将来的にレイアウト計算をSyntax Worker等の別スレッドに逃がす段階で改めて評価する余地は残す

## 影響

### 実装

- `MainWindowConfig`に`std::function<void(HWND)> onDeferredInit`を追加 — 内部専用メッセージ`WM_APP+1`経由で、初回`WM_PAINT`完了後に1回だけ発火
- `main.cpp`は`LaunchMode::Normal`時のみ`onDeferredInit`で`RenderPipeline::attach()`を呼び、成功したら`MainWindow::setPaintHandler()`でGDIプレースホルダーからD2D描画へWM_PAINTハンドラを差し替える
- `--measure-startup`/`--measure-memory`モードは`onDeferredInit`を設定しない (計測PoCハーネスの契約を変えない)

### 却下した選択肢の理由 (補足)

ワーカースレッド案は「デバイス生成失敗時のリトライ」や「生成完了前にリサイズ/ペイントイベントが来た場合の同期」など、この基盤配線フェーズでは本質的でない複雑性を追加する。同期・自己ポストメッセージ方式なら、`onDeferredInit`発火時点でUIスレッドは他に何もしていないため、これらの競合状態が原理的に発生しない。

## 将来の再評価タイミング

- デバイス生成コストが実測で有意に増大した場合 (例: 複数モニタ対応やD3D11デバッグレイヤ有効化時の初期化コスト増)
- レイアウト計算やグリフラスタライズを別スレッドに逃がす設計 (Phase 3c以降) に伴い、D2Dデバイスコンテキストのスレッドアフィニティ自体を見直す必要が生じた場合

## 参考

- Phase 1 起動計測実測値: CI上でfirst paint = 22ms ([`docs/phase_reports/phase_1_report.md`](../phase_reports/phase_1_report.md))
- `docs/design/basic_design.md` §4.1「起動時間」
