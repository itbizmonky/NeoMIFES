# Issue: IME合成中のテキストが右端を超えても水平スクロールが追従しない (P1 — 🟢 WI-45で解消)

- **起票日:** 2026-09-11(ユーザーの実機ドッグフーディング報告)
- **解決日:** 2026-09-11(WI-45、実装検証で当初案の不備を発見・訂正)
- **対象:** `src/core/include/neomifes/core/viewport.h`/`src/core/src/viewport.cpp`(`ensureColumnVisible()`新設)、`src/render/include/neomifes/render/render_pipeline.h`/`.cpp`(`measureTextColumnWidth()`新設)、`src/app/normal_mode_wiring.cpp`(`handleImeCompositionEvent()`)
- **優先度:** P1(日本語入力という基本操作で、ウィンドウ幅を超える変換文字列を打つたびに発生する実害あるバグ)

## 事実

ユーザー報告: 「日本語入力でウィンドウサイズを超える入力をした場合でも画面はスクロールせずに一番右側の入力文字が見えるようにしたい。」

続けてWI-45の初回修正後、ユーザーから追加報告: 「右端の文字は右ペイン(ミニマップ)で隠れて未だ右端が見えない、右ペインを考慮して右端の文字が完全に見えるようにして欲しい。」

## 原因(2段階)

**1段階目(水平スクロール自体が一切追従しない):** IME合成文字列(`ImeComposition::text`)は一度もDocumentへ書き込まれず、合成中は`ImeComposition::anchorRange`(合成開始位置)が固定されたまま`text`だけが伸びる設計(`render_pipeline.h`のコメント参照)。`Viewport::ensureVisible()`はDocument上のカーソル位置(`TextPos`)からしか駆動されず、`anchorRange`は合成中一切動かないため、`handleImeCompositionEvent()`(合成文字列が伸びるたびに呼ばれるハンドラ)は一度も`ensureVisible()`を呼んでいなかった——水平スクロールを駆動する経路がそもそも存在しなかった。

**2段階目(スクロールはするが実測より少なく、末尾文字が隠れる):** 1段階目の修正で`Viewport::ensureColumnVisible()`(TextPosを経由せず直接カラム値でクランプする、`ensureVisible()`の水平専用版)を新設し、`anchorColumn + text.size()`(UTF-16コード単位数)を目標カラムとして渡す実装をまず行ったが、**実機ドッグフーディングで依然として症状が再現した。** `text.size()`はUTF-16コード単位数であり「1文字=1カラム」という半角文字前提の換算だが、DirectWriteは全角文字(ひらがな・カタカナ・漢字)を半角カラム幅の約2倍で描画するため、実際の描画幅より少ないカラム数でスクロール量を計算してしまい、合成文字列の末尾がスクロール後もなお右ペイン(ミニマップ)の裏に隠れたままになっていた。

## 解決内容

`RenderPipeline`へ新規`measureTextColumnWidth(std::u16string_view text)`を追加。`drawImeCompositionOnLine()`が既に使っているのと同じDirectWrite実測パターン(`IDWriteTextLayout::GetMetrics()`の`width`)を流用し、実測ピクセル幅を`m_charWidthDips`で割ってカラム数に変換(`std::ceil`で切り上げ、実際の幅を下回らないようにする)。`handleImeCompositionEvent()`を`anchorColumn + text.size()`から`anchorColumn + renderPipeline.measureTextColumnWidth(text)`へ変更。

新規統合テスト`RenderTextSmokeTest.MeasureTextColumnWidthCountsFullWidthCharactersAsWiderThanHalfWidth`で、同じ5文字でも全角(「あいうえお」)が半角(`"abcde"`)より広いカラム数として測定されることを確認(半角は厳密に5カラム、全角はそれより大きい値)。

**実機での完全な対話的検証は未完走のまま正直に記録する:** 本環境から外部プロセス経由で`ImmGetContext()`を用いた実IME合成文字列の注入を試みたが、対象ウィンドウの`HIMC`を取得できず(既知の複数モディファイアキー合成制約とは別種の、この環境固有の制約)、実際のIME変換文字列での対話的確認はできなかった。代わりに、①コードレベルでの原因特定(DirectWrite実測値とUTF-16長の乖離)、②その原因を直接証明する単体/統合テスト、③同型の実測パターンが`drawImeCompositionOnLine()`で既に本番稼働中であることの3点を根拠に、修正の正しさを判断した。

## 完了条件

- [x] 水平スクロールが追従しない原因を特定した(合成中はDocumentカーソルが動かないため`ensureVisible()`の経路が発火しない)
- [x] 追従はするが末尾が隠れる原因を特定した(`text.size()`が全角文字の実描画幅を過小評価)
- [x] 両方を修正し、単体/統合テストで検証した
- [ ] 実IME(日本語変換)での対話的な実機確認(環境制約により未完走、上記参照)
