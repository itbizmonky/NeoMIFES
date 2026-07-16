# Phase 3 完了レポート — Rendering Engine (3a / 3b / 3c)

- **期間:** 2026-07-16 (Session 18〜20、詳細は [`docs/history/TIMELINE.md`](../history/TIMELINE.md) 参照)
- **担当:** Claude Code (テックリード)
- **対象:** CLAUDE.md §7 の Phase 3 DoD「60fps スクロール確認」を満たす Rendering Engine を構築する。GDI プレースホルダー描画から、D2D/DXGI デバイスグラフ・DirectWrite テキスト実描画・行キャッシュ・粗粒度フレームスキップまでを、CLAUDE.md §11 の規定に従いここに統合する (3a/3b/3c 個別レポートは作らない)。

3a/3b/3c への段階分割は Phase 2 の 2a/2b1/2b2/2b3 分割の前例に倣った、本セッション群で導入した実装計画上の内部区分であり、CLAUDE.md の公式フェーズ表には存在しない。Line Gutter・ダーク/ライトテーマ・日本語フォントフォールバック・IME (当初「Phase 3d」として検討事項に挙げていたもの) は、Phase 3 の DoD である「60fpsスクロール確認」の必須要件ではないため、本レポートの対象外とし、独立した将来フェーズとして扱う。

---

## 1. サブフェーズ構成と成果物

### 1.1 Phase 3a — D2D/DXGI/COM 基盤配線
| ファイル | 変更内容 |
|---|---|
| [render_error.{h,cpp}](../../src/render/src/render_error.cpp) | 新規。`RenderExpected<T> = std::expected<T, RenderError>` — プロジェクト初の `std::expected` 実用箇所 |
| [d2d_factories.{h,cpp}](../../src/render/src/d2d_factories.cpp) | 新規。`ID2D1Factory7`/`IDWriteFactory7` のプロセス単位シングルトン (magic-static) |
| [render_device.{h,cpp}](../../src/render/src/render_device.cpp) | 新規。D3D11+D2D+DXGI デバイスグラフの RAII 所有。`D3D_DRIVER_TYPE_HARDWARE`→`WARP` フォールバック |
| [render_pipeline.{h,cpp}](../../src/render/src/render_pipeline.cpp) | 新規。MainWindow/app が触るファサード。デバイスロスト時の全体再生成 |
| [resize_math.h](../../src/render/include/neomifes/render/resize_math.h) | 新規。純粋関数 (`sanitizeSwapChainSize`/`dpiToScale`) |

**設計判断:** [ADR-008](../decisions/ADR-008-com-raii-comptr.md) COM RAII に `Microsoft::WRL::ComPtr` を採用 / [ADR-009](../decisions/ADR-009-deferred-device-init.md) デバイス生成は同期・UIスレッド・自己ポストメッセージ (`WM_APP`) 方式、ワーカースレッド化は不採用。

### 1.2 Phase 3b — DirectWrite テキストレイアウト + Document 実描画
| ファイル | 変更内容 |
|---|---|
| [viewport_math.h](../../src/render/include/neomifes/render/viewport_math.h) | 新規。`computeVisibleLineCount()` 純粋関数 |
| [render_device.{h,cpp}](../../src/render/src/render_device.cpp) | `clearAndPresent()` を `beginFrame()`/`endFrame()` に分解、`setDpi()` 追加 |
| [document.{h,cpp}](../../src/document/src/document.cpp) | `version()` カウンタ追加。`offsetToLine`/`lineToOffset` を `mutable` キャッシュ経由の `const` 化 |
| [render_pipeline.{h,cpp}](../../src/render/src/render_pipeline.cpp) | `setDocument()`/`setTopLine()`、DirectWrite 基盤 (`ensureTextFormat`)、`drawVisibleLines()` (`dc.DrawText()` 直呼び)、`refreshDocumentCacheIfStale()` (`snapshot()` の変更検知キャッシュ) |
| [main.cpp](../../src/app/main.cpp) | `--open <path>` 追加、`Document` 配線 |

**設計判断:** [ADR-010](../decisions/ADR-010-render-depends-on-document.md) Rendering Engine は Document Engine に直接依存する (`neomifes_render` → `neomifes_document` PUBLIC 依存)。CLAUDE.md §3 のレイヤ図は上位→下位の依存を示しており、Rendering Engine は Document Engine より上位に描かれているため規約上正しい方向と確認。

### 1.3 Phase 3c — TextLayoutCache + 粗粒度フレームスキップ + `--measure-frame` 計測ハーネス
| ファイル | 変更内容 |
|---|---|
| [text_layout_cache.{h,cpp}](../../src/render/src/text_layout_cache.cpp) | 新規。行番号キーの `IDWriteTextLayout` キャッシュ。無効化は `Document::version()` 変化時の wholesale `clear()` のみ |
| [render_pipeline.{h,cpp}](../../src/render/src/render_pipeline.cpp) | `drawVisibleLines()` を `TextLayoutCache::getOrCreate()` + `DrawTextLayout` に変更。`FrameState`/`captureFrameState()` による粗粒度フレームスキップ (状態不変なら描画を丸ごとスキップ) |
| [frame_profile.{h,cpp}](../../src/app/frame_profile.cpp) | 新規。`--measure-frame` の JSON レポート (min/max/avg/p50/p95 + キャッシュ統計) |
| [main.cpp](../../src/app/main.cpp) | `--measure-frame <out.json>` 追加。合成ドキュメント生成 (`synthesizeMeasurementDocument()`)、300フレーム連続スクロール計測 (`runFrameMeasurement()`) |
| [render_text_layout_cache_bench.cpp](../../tests/bench/render_text_layout_cache_bench.cpp) | 新規。デバイス/vsync を介さない TextLayoutCache 単体のCPUコスト計測 |
| `.github/workflows/ci.yml` | 「Frame PoC (report only, no hard fail)」ステップ追加 (Startup PoC と同じ soft-fail パターン) |

**設計判断:** [ADR-011](../decisions/ADR-011-phase3c-render-cache-scope.md) Phase 3c は TextLayoutCache のみを実装し、GlyphCache (独自グリフアトラス) と細粒度 DamageTracker (部分矩形再描画) を明示的に延期する。

- **GlyphCache 延期の理由:** D2D の `DrawTextLayout()` は既にキャッシュ済み `IDWriteTextLayout` 内部のシェーピング済みグリフラン情報を再利用する。TextLayoutCache 単体の実測 (§3 参照) が目標を2〜3桁のマージンでクリアしており、独自ラスタライズ基盤が必要という測定上の根拠が無い (CLAUDE.md ルール10)
- **細粒度 DamageTracker 延期の理由:** 対話的編集 (Phase 4 Editor Core) も対話的スクロールも未実装のため、部分矩形再描画を正当化する具体的なユースケースが存在しない。代わりに `Document::version()`/`topLine`/`width`/`height`/`dpiScale` が前回成功フレームと完全一致なら描画自体をスキップする粗粒度判定のみを実装 (`FLIP_DISCARD` + DWM 合成下での安全性を確認済み)
- 無制限キャッシュ成長のトリペワイヤは [`docs/issues/text_layout_cache_unbounded_growth.md`](../issues/text_layout_cache_unbounded_growth.md) に記録 (Phase 4 で対話的スクロールが実装された後に実測して再評価)

---

## 2. テスト・ベンチマークインフラの成長

| 指標 | Phase 2 完了時 | Phase 3 完了時 |
|---|---|---|
| 単体テスト数 | 93 + 20,000反復プロパティ | 125 + 20,000反復プロパティ (+ render 関連32件: resize_math/error/viewport_math/text_layout_cache/document version) |
| 統合テスト数 | 1 (`startup_measure`) | 4 (`startup_measure`/`render_device_smoke`/`render_text_smoke`/`frame_measure`) |
| ベンチマーク本数 | 6 | 8 (+ `render_text_layout_cache_bench.cpp` 2本) |
| 総テスト数 (ctest) | 93 | 129 |

---

## 3. Phase 3 全体 DoD 判定 (最終)

| 項目 | 目標 | 実測 | 判定 |
|---|---|---|---|
| 60fps スクロール確認 | フルフレーム予算 16.6ms | `--measure-frame` (50,000行合成ドキュメント、300フレーム連続スクロール、Release): avg 5.52ms / p50 5.56ms / p95 5.66ms / max 8.11ms / min 0.25ms — 全フレームが予算内 | ✅ |
| 1行のレイアウト生成 (TextLayoutCache miss) | < 50µs | 532ns (`BM_TextLayoutCache_Miss`, Release, 約94倍のマージン) | ✅ |
| キャッシュヒット時の1行描画準備 (TextLayoutCache hit) | < 5µs | 4.34ns (`BM_TextLayoutCache_Hit`, Release, 約1152倍のマージン) | ✅ |
| 実アプリでの D2D/DXGI/DirectWrite 実描画確認 | 目視 | `--open <file>` で実ファイルを開き、複数行・タブインデントを含むテキストの正しい描画をスクリーンショットで確認 | ✅ |
| リサイズ耐性 | クラッシュ・表示崩れ無し | 600x400→1400x900→300x200→1000x650 の4段階リサイズで確認 | ✅ |
| デバイスロスト時の再生成ロジック | 実装 | `RenderPipeline::recreateDevice()`、実機での強制デバイスロスト誘発テストは未実施 (通常操作では発生しないため) | ✅ (既知の限界あり) |
| 起動時間退化なし | 目標300ms | firstPaintNs 実測 33ms (Phase 3a 配線後、目標の11%) | ✅ |
| 既存単体・統合テスト全 green | - | 129/129 (ローカル Debug/Release 両方、CI green) | ✅ |
| `snapshot()` がフレームごとに呼ばれない | - | `refreshDocumentCacheIfStale()` の version 比較で保証、統合テストで再取得/再利用の両経路をカバー | ✅ |

**ピクセル単位の描画正しさ検証は対象外。** キャプチャ機構が存在しないため、統合テストは「クラッシュ・エラーなく描画される」「キャッシュ統計が期待通り推移する」のみを保証する。将来ピクセル差分検証が必要になった場合は別途計測ハーネスの拡張として検討する。

---

## 4. 検証状況

Phase 3a/3b/3c いずれもローカル Debug/Release フルビルド + 全テスト pass + clang-tidy (`src/.clang-tidy` の `WarningsAsErrors: '*'` 込み) 新規警告 0 を確認してから push。CI (`build-and-test` debug/release、`static-analysis`、`ubsan`) も各セッションで green を確認済み。

```powershell
$vsPath = "C:\Program Files\Microsoft Visual Studio\18\Community"
Import-Module "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll" -ErrorAction Stop
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
Set-Location "D:\IDE\Claude\NeoMIFES"
cmake --preset debug && cmake --build --preset debug && ctest --preset debug --output-on-failure
cmake --preset release && cmake --build --preset release && ctest --preset release --output-on-failure
.\build\release\tests\bench\neomifes_render_bench.exe --benchmark_min_time=0.3s
.\build\release\src\app\NeoMIFES.exe --measure-frame frame.json; Get-Content frame.json
```

---

## 5. 既知の懸念事項

| # | 懸念 | 対応 |
|---|---|---|
| P3-1 | GlyphCache 未実装 | 意図的延期 (ADR-011)。TextLayoutCache 実測が目標を大幅にクリアしており現時点で不要と判断。再評価トリガー: ベンチ/`--measure-frame` 実測での予算割れ |
| P3-2 | 細粒度 DamageTracker 未実装 (部分矩形再描画) | 意図的延期 (ADR-011)。対話的編集が存在しないため現時点でユースケースが無い。再評価トリガー: Phase 4 での対話的1行単位編集の実現 |
| P3-3 | `TextLayoutCache` のサイズ無制限成長 | 意図的 (LRU 未実装)。[`docs/issues/text_layout_cache_unbounded_growth.md`](../issues/text_layout_cache_unbounded_growth.md) に tripwire を記録。Phase 4 の対話的スクロール実装後に実測して再評価 |
| P3-4 | デバイスロスト再生成の実機テスト未実施 | 通常操作では発生しないため統合テストでは検証していない。既知の限界として受容 |
| P3-5 | 対話的スクロール入力が存在しない | Phase 4 (Editor Core) スコープ。`RenderPipeline::setTopLine()` は Phase 4 が接続するための既存フック |

---

## 6. Phase 4 への引き継ぎ事項

順序の提案:
1. Editor Core (Cursor/SelectionModel/Command/Undo) の実装に伴い、`RenderPipeline::setTopLine()` を実際にキーボード/マウスから駆動する `Viewport` クラスへの置換を検討 (§4.4 記載の暫定フックからの移行)
2. Phase 4 で対話的編集 (1行単位の localized edit) が実現した段階で、ADR-011 の再評価トリガーに従い細粒度 DamageTracker の要否を判断
3. Phase 4 で対話的スクロールが実装された後、`TextLayoutCache` の実際のメモリ増分を長時間スクロールセッションで実測し、LRU 化の要否を [`docs/issues/text_layout_cache_unbounded_growth.md`](../issues/text_layout_cache_unbounded_growth.md) の完了条件に従って判断
4. Line Gutter・ダーク/ライトテーマ・日本語フォントフォールバック・IME (旧「Phase 3d」検討事項) は、Phase 4 (Editor Core) と独立した将来フェーズとしてスコープを再確認する

---

## 7. Definition of Done

**Phase 3 (3a + 3b + 3c) 全体完了。** CLAUDE.md §7 の DoD「60fps スクロール確認」は `--measure-frame` の実測 (全フレームが16.6ms予算内、max 8.11ms) で達成。GlyphCache・細粒度 DamageTracker は測定に基づき意図的に延期 (ADR-011、再評価トリガー明記済み)。Phase 4 (Editor Core) 着手可能と判断する。
