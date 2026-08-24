# Issue: CSVグリッドのフィルタ行付近に表示異常が出る (🟢 解決済み)

- **起票日:** 2026-08-24 (WI-16f 実機ドッグフーディングで発見)
- **解決日:** 2026-08-25 (WI-16f、pushする前にユーザーから修正を要請され追加対応)
- **対象:** `src/ui/src/csv_grid_pane.cpp`(`ui::CsvGridPane`)、`src/app/normal_mode_wiring.cpp`(`handlePaintEvent()`)
- **優先度:** P2 (視覚的な違和感のみ、データ破損は無し)
- **親文書:** WI-16f (`build_plan.md` §5)

## 事実 (発見時)

WI-16f(CSVセル単位クリック編集)の実機ドッグフーディング中、ユーザーからCSVグリッド(`Ctrl+Shift+G`)を開いた直後(セルをクリックする前)の時点で、フィルタ行(「フィルタ:」ラベル+編集欄)のすぐ下、実際のヘッダー行が表示される位置との間に、欠けた/重なったように見えるテキストが表示されるとの報告があった。

`git worktree`でWI-16f着手前のコミット(`27a212c`)をチェックアウトしビルドして同じ操作で再現するかを確認したところ、`27a212c`時点のビルドでも同じ症状が再現し、WI-16fが原因ではなく少なくともWI-16c(CSVグリッドUI実装、2026-08-19)以降のいずれかの時点から存在していた既存の表示バグと判断した。

## 原因

`GetWindowRect`によるCsvGridPaneの子ウィンドウ全ての実測矩形を調査したところ、フィルタ行・リストビューいずれの配置も数学的に正しく、重なりも無いことを確認した。しかし`normal_mode_wiring.cpp`の`WM_PAINT`ハンドラを再確認したところ、**通常のテキストビュー(裏の`RenderPipeline`によるDirect2D描画)が、CSVグリッド表示中かどうかに関わらず毎回無条件に描画されている**ことが判明した。

`CsvGridPane`のフィルタ行(`onParentResized()`)は、32dip高のバンド内に24dip高のラベル/編集欄コントロールを中央寄せで配置する設計になっており、上下に意図的な4dip程度の余白を残していた。この余白部分はどのネイティブ子ウィンドウにも覆われておらず、`WM_PAINT`がクライアント領域全体を毎回塗り直す裏のDirect2D描画(CSVファイルの生テキスト)がその隙間から透けて見えていた、というのが実際の原因だった。

## 対応

1. `handlePaintEvent()`(`normal_mode_wiring.cpp`、`wireNormalMode()`のcognitive-complexity閾値超過を避けるため独立関数へ抽出)で、`csvGridPane.isVisible()`の間は`RenderPipeline::render()`自体を呼ばないよう変更(無駄な描画コストの削減、ただしこれだけでは既に描画済みの最後のフレームが画面に残るため症状は解消しなかった)。
2. **根本修正: `CsvGridPane`に新規`m_hwndFilterBackdrop`(無地の`WC_STATIC`)を追加し、フィルタ行の32dipバンド全体を隙間なく覆うようにした。** `m_hwndFilterLabel`/`m_hwndFilterEdit`より先に生成することでz-order上背面に配置し、ラベル/編集欄自体はそのまま前面に表示されつつ、余白部分もこの背景パネルが不透明に覆うことで裏の描画が透けなくなった。

## 完了条件

- [x] 具体的な原因を特定する — `WM_PAINT`が毎回全クライアント領域を描画し、CsvGridPaneのフィルタ行が余白部分を覆っていなかったため
- [x] 修正する — 背景パネル追加(`m_hwndFilterBackdrop`)+不要な描画のスキップ(`handlePaintEvent()`)
- [x] 実機ドッグフーディングで解消を確認する — ユーザー自身が実機で確認済み(2026-08-25)

## 再検証コマンド

```bash
grep -n "m_hwndFilterBackdrop" src/ui/src/csv_grid_pane.cpp src/ui/include/neomifes/ui/csv_grid_pane.h
```
