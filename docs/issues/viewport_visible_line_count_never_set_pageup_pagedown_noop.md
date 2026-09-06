# Issue: `Viewport::setVisibleLineCount()`が本番コードから一度も呼ばれず、PageUp/PageDownキーが実質0行しか移動しない(P1 — 実機確認済み、未修正)

- **起票日:** 2026-09-06(WI-34、縦スクロールバー新規実装のためのpageStep調査中に発見)
- **対象:** `src/core/include/neomifes/core/viewport.h`(`Viewport::m_visibleLineCount`/`setVisibleLineCount()`/`visibleLines()`)、`src/app/editor_input.cpp`の`handleKeyDown()`(`viewport.visibleLines()`をpageSizeとして使用)
- **優先度:** P1(実機で再現確認済みのキー操作バグ、水平方向のみ対応していたWI-03の縦方向版が長らく欠落していた)

## 事実

`Viewport::setVisibleLineCount(std::uint32_t count)`(`viewport.h:53`)は宣言・定義されているが、`grep -rn setVisibleLineCount src/`の結果、**本番コードのどこからも一度も呼ばれていない**(この関数自体の宣言以外に出現しない)。そのため`m_visibleLineCount`は常にデフォルト値`0`のまま。

`applyMovementKey()`の呼び出し元`handleKeyDown()`(`src/app/editor_input.cpp:226-228`)は、PageUp/PageDownの移動量(`pageSize`)を次のように取得している:
```cpp
const auto visible = viewport.visibleLines();
changed = applyMovementKey(vkCode, shiftDown, ctrlDown, selection, document,
                           visible.end - visible.start, folding);
```
`visibleLines()`(`viewport.h:81-83`)は`LineRange{start=m_topLine, end=m_topLine+m_visibleLineCount}`を返すため、`m_visibleLineCount=0`のとき`visible.end - visible.start`は常に`0`。すなわち`MovementKind::PageUp`/`PageDown`へ渡される移動量は常に`0`行——**PageUp/PageDownキーは実質何もしない(カーソルが1行も動かない)**。

## 原因

`Viewport`の水平方向カウンターパート`setVisibleColumnCount()`は`RenderPipeline::visibleColumnCount()`から`handleCharEvent()`等で毎フレーム同期されている(WI-03)が、縦方向の同期処理が一度も実装されなかった。`RenderPipeline`側には`visibleLineRange()`(fold/wrap考慮済み、private)という同等の情報源が既に存在するにも関わらず、`Viewport::setVisibleLineCount()`へ橋渡しする配線が欠落したまま放置されていたと見られる。

## 対応案(未実施)

WI-03の水平方向の配線(`RenderPipeline::visibleColumnCount()`→`Viewport::setVisibleColumnCount()`、`handleCharEvent()`等での毎フレーム同期)と同型で、`RenderPipeline::visibleLineCount()`(WI-34で新設済み、`visibleLineRange()`の公開ラッパー)→`Viewport::setVisibleLineCount()`の毎フレーム同期を追加する。

## 完了条件

- [ ] `Viewport::setVisibleLineCount()`を毎フレーム(または関連イベント発生時)呼ぶ配線を追加する
- [ ] PageUp/PageDownキーが実際にカーソルを移動させることを実機で確認する
- [ ] 既存のPageUp/PageDown関連の単体テスト(もしあれば)がこの配線漏れを検出できていなかった理由を確認し、必要ならテストを追加する
