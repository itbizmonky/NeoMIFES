# Issue: `Viewport::ensureVisible()`が短い文書で窓の実容量を誤認し、1行目が不要にスクロールアウトする (P1 — 🟢 WI-44で解消)

- **起票日:** 2026-09-11(ユーザーの実機ドッグフーディング報告)
- **解決日:** 2026-09-11(WI-44)
- **対象:** `src/render/include/neomifes/render/render_pipeline.h`(`visibleLineCount()`/`visibleRowCapacity()`)、`src/app/normal_mode_wiring.cpp`(`handlePaintEvent()`)
- **優先度:** P1(新規ファイルに1文字入力してEnterを押すだけで即座に発現する、基本編集を阻害する実害あるバグ)

## 事実

ユーザー報告: 「NeoMIFESを起動して1行目に入力後、エンターキーで2行目にカーソル移動すると1行目が見えなくなった。Windowに余白がある場合はスクロールするな。」

ウィンドウが数十行分の表示余地を持つにもかかわらず、文書が1〜2行しかない状態でカーソルが最終行に達すると、`Viewport::ensureVisible()`が不要に垂直スクロールし1行目が画面外へ追い出されていた。

## 原因

`Viewport::ensureVisible()`自体のスクロール判定ロジック(`line >= topLine + visibleLineCount`なら`topLine`を進める)は正しく、単体テストでも検証済みだった。問題は`Viewport::m_visibleLineCount`(=ウィンドウの行表示「容量」であるべき値)に、WI-36で`RenderPipeline::visibleLineCount()`が配線されていたこと。

`RenderPipeline::visibleLineCount()`は`visibleLineRange()`の結果幅であり、`visibleLineRange()`内部の壁打ちループは`totalLines`(文書の行数)に達した時点で停止する——つまり**文書が短ければ短いほど値も小さくなる、「今実際に何行分描画されているか」を返す関数**であり、「窓が何行表示できるか(容量)」を返す関数ではなかった。WI-34の元々の用途(`syncVerticalScrollBar()`/`handleVScrollEvent()`のページステップ)ではこの意味で正しかったが、WI-36がこれを`Viewport::setVisibleLineCount()`へもそのまま流用したため、1行しかない文書では`m_visibleLineCount`が`1`になり、カーソルが2行目(0始まりで行1)に達した瞬間`ensureVisible()`が「窓は1行しか表示できない」と誤認して不要にスクロールしていた。

## 解決内容

`RenderPipeline`へ新規`visibleRowCapacity()`を追加。`visibleLineRange()`が内部で使う`computeVisibleLineCount(effectiveHeightPx, m_dpiScale, m_lineHeightDips)`(文書の行数に一切依存しない、純粋にウィンドウの高さから導出される値)をそのまま公開する。`handlePaintEvent()`の`Viewport::setVisibleLineCount()`配線を`visibleLineCount()`から`visibleRowCapacity()`へ切り替えた。`visibleLineCount()`自体は元の用途(スクロールバーのページステップ)のまま変更していない。

新規統合テスト`RenderTextSmokeTest.VisibleRowCapacityIsNotBoundedByShortDocumentLength`で、1行文書に対し`visibleLineCount()==1`(既存の意味通り)かつ`visibleRowCapacity()>1`(ウィンドウの真の容量)であることを確認。実機ドッグフーディングでも、新規ファイルに"line1"と入力しEnterを3回押して4行目までカーソルを進めても1行目が表示され続けることをスクリーンショットで確認済み。

## 完了条件

- [x] 原因を特定した(`visibleLineCount()`と真の窓容量の意味の食い違い)
- [x] 修正を実施し、実機ドッグフーディングで1行目がスクロールアウトしないことを確認した
