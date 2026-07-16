# ADR-011: Phase 3c は TextLayoutCache のみを実装し、GlyphCache と細粒度 DamageTracker は明示的に延期する

- **ステータス:** Accepted
- **決定日:** 2026-07-16 (Phase 3c 着手時)
- **関連:** [ADR-010](ADR-010-render-depends-on-document.md)、`docs/design/detailed_design.md` §4.1〜§4.3、CLAUDE.md 絶対ルール3(推測実装をしない)・ルール10(性能改善はベンチマーク根拠必須)

## コンテキスト

`detailed_design.md` §4.1 は Phase 0 時点で `TextLayoutCache`/`GlyphCache`/`DamageTracker` の3コンポーネントをレンダリングパイプラインの構成要素として構想していた。これは Phase 3a/3b で実際の DirectWrite 描画コード (`RenderPipeline::drawVisibleLines()`) が存在する**前**に書かれたスケッチである。

Phase 3b 完了時点で `drawVisibleLines()` は毎フレーム全可視行に対して `dc.DrawText()` を直接呼んでおり、`IDWriteTextLayout` のキャッシュを一切行っていなかった。CLAUDE.md の Phase 3 DoD は「60fps スクロール確認」であり、`detailed_design.md` §4.3 は「1行のレイアウト生成 <50µs」「キャッシュヒット時の1行描画 <5µs」という具体的な性能目標を既に明記している。

Phase 3c 着手にあたり、3コンポーネント全てを今実装すべきかを再検討した。

## 選択肢

1. **元のスケッチ通り3コンポーネント全てを実装する**
2. **TextLayoutCache のみを実装し、粗粒度のフレームスキップ(状態不変なら再描画自体をスキップ)を「DamageTracker」の代替とし、GlyphCache と細粒度 DamageTracker は明示的に延期する(採用)**
3. **何も実装せず、計測ハーネスのみを Phase 3c のスコープとする**

## 決定

**選択肢2を採用する。**

## 根拠

### GlyphCache(独自グリフラン/アトラスラスタライズ)を延期する理由

D2D の `ID2D1DeviceContext::DrawTextLayout()` は、渡された `IDWriteTextLayout` が既に内部で保持しているシェーピング済みグリフラン情報(スクリプト解析・シェーピング・グリフ置換の結果)を再利用する。つまり `TextLayoutCache` 単体で「毎フレーム同じ計算をやり直す」コストの大部分を排除できる可能性が高く、それを超える独自グリフアトラス基盤(`IDWriteFontFace::GetGlyphRunOutline`/`GetGlyphRunAnalysis` によるラスタライズ、アトラステクスチャ管理、D2D 描画パスの手動書き換え)が本当に必要かどうかは、**測定してみるまで分からない**。

CLAUDE.md 絶対ルール10「性能改善は必ずベンチマーク結果を根拠とする。憶測で最適化しない」に照らし、実測データが無い段階でこの規模の独自レンダリング基盤に着手するのは時期尚早と判断した。

**実測による裏付け(本ADR採択の根拠として記録):** `tests/bench/render_text_layout_cache_bench.cpp` の CI 実測値(Release、`--benchmark_min_time=0.3s`):
- `BM_TextLayoutCache_Miss`: **542ns**(目標 <50µs に対し約92倍のマージン)
- `BM_TextLayoutCache_Hit`: **4.37ns**(目標 <5µs に対し約1145倍のマージン)

TextLayoutCache 単体で両目標を大幅に(2桁〜3桁のマージンで)クリアしており、現時点で GlyphCache が必要という測定上の根拠は無い。

### 細粒度 DamageTracker(部分矩形 dirty-rect 追跡)を延期する理由

現状 `Document::version()` は変更範囲情報を一切持たないグローバルカウンタのみであり、対話的編集(Phase 4 Editor Core)も対話的スクロール入力も未実装のため、「画面の一部だけが変化した」という部分矩形再描画を正当化する具体的なユースケースが今存在しない。今日の「damage」パターンは実質的に2つしかない:

- (a) 何も変化していない → 再描画自体を丸ごとスキップするのが正しい(部分再描画ではなく)
- (b) スクロール/リサイズ/ロード → 可視行全体が縦方向に移動するため、どのみちビューポート全体が実質的に damage 対象になる(サブ矩形ではない)

`Document` に変更範囲追跡を持ち込むこと自体、Document Engine の内部実装への越権であり、かつ実際の駆動源(Phase 4 の対話的編集)が存在しない段階では推測実装(CLAUDE.md ルール3違反)になる。

代わりに、`RenderPipeline::render()` に「前回成功フレームと `(Document::version(), topLine, width, height, dpiScale)` が完全一致なら `beginFrame()`/`Clear()`/`drawVisibleLines()`/`endFrame()` を丸ごとスキップする」という粗粒度の判定のみを実装した(`FrameState`/`captureFrameState()`)。

**安全性の根拠:** `RenderDevice` は `DXGI_SWAP_EFFECT_FLIP_DISCARD` を使用しており、DWM 合成下では前回 Present した内容をコンポジタ側が保持する(blit モデルと異なり、毎 `WM_PAINT` での再描画は必須ではない)。`MainWindow::handlePaint()`(`src/ui/src/main_window.cpp`)は既にペイントハンドラの内容に関わらず無条件に `::ValidateRect()` を呼んでいるため、描画をスキップしても `WM_PAINT` の再発行ループにはならない。

## 影響

### 実装
- `src/render/include/neomifes/render/text_layout_cache.h` / `.cpp`(新規): 行番号キーの `IDWriteTextLayout` キャッシュ。無効化は `Document::version()` 変化時の wholesale `clear()` のみ
- `RenderPipeline::drawVisibleLines()`: `dc.DrawText()` 直呼びから `TextLayoutCache::getOrCreate()` + `dc.DrawTextLayout()` に変更
- `RenderPipeline::render()`: `FrameState` 比較による粗粒度フレームスキップ
- GlyphCache・細粒度 DamageTracker は実装しない

### 却下した選択肢の理由(補足)
選択肢3(計測ハーネスのみ)は、`detailed_design.md` §4.3 が既に明記している具体的な性能目標(<50µs/<5µs)を検証する手段を Phase 3c 内で用意しないことになり、CLAUDE.md ルール10の「ベンチマーク根拠」を得る機会を先送りするだけで、選択肢2に対する優位性が無いと判断した。

## 将来の再評価タイミング

- **GlyphCache:** `render_text_layout_cache_bench.cpp` の実測値、または `--measure-frame` ハーネスの p95/max フレーム時間が、キャッシュヒット時にもフレーム予算(16.6ms)を割り込む/迫ることが実測で判明した場合
- **細粒度 DamageTracker:** Phase 4 (Editor Core) が対話的な1行単位の編集を実現し、「1行だけ変化した」という実際のユースケースと HWND 更新領域の必要性が生じた場合

## 参考
- `docs/design/detailed_design.md` §4.1〜§4.3
- `docs/issues/text_layout_cache_unbounded_growth.md`(TextLayoutCache のサイズ無制限成長に関する tripwire)
