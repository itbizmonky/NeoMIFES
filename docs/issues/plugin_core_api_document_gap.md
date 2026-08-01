# Issue: `NeoMifesCoreApi` と `document::Document` の API ギャップ

- **起票日:** 2026-08-01 (Phase 8a 実装中に判明)
- **対象:** [`docs/design/master_roadmap.md`](../design/master_roadmap.md) §8(`NeoMifesCoreApi`スケッチ)、[`src/document/include/neomifes/document/document.h`](../../src/document/include/neomifes/document/document.h)
- **優先度:** 中 (Phase 8b 着手時の必須前提条件)
- **関連:** [ADR-015](../decisions/ADR-015-plugin-host-c-abi-seh.md)

## 背景

`docs/design/master_roadmap.md` §8 のプラグイン SDK スケッチは`NeoMifesCoreApi`として`insertText`/`deleteRange`/`getLineCount`/`getLineText`/`registerCommand`/`showToast`/`httpRequest`/`readPluginData`/`writePluginData`を構想している。Phase 8a(最小限 PoC)の着手前調査で、このうちドキュメント操作系(`getLineText`、および行+桁からオフセットへの変換)が、現状の`document::Document`の公開 API では実装できないことが判明した。

### 具体的なギャップ

`document::Document`(`document.h`確認済み)が公開する主なメソッドは以下の通り:
- `insertText(TextPos pos, ...)` / `eraseRange(TextRange range)` / `replaceRange(...)` — いずれも**コード単位オフセット(`TextPos`)**を引数に取る
- `offsetToLine(TextPos) -> LineNumber` / `lineToOffset(LineNumber) -> TextPos` — 行番号⇔オフセットの相互変換は存在する
- `lineCount() -> std::uint64_t` — これは roadmap スケッチの`getLineCount`にそのまま対応する

一方、roadmap スケッチの`getLineText(int line) -> std::wstring`のような「行番号を渡すと1行分のテキストを返す」API、および「行+桁(0-based column)からオフセットへ変換する」API は`Document`に存在しない。`RenderPipeline::extractLineText()`(Phase 7o、Sticky scroll実装時に新設)が概念的に近いが、これは`RenderPipeline`のprivateヘルパーであり(`m_cachedSnapshot`/`m_document`両方への参照を前提とする描画パイプライン内部の実装)、`document::Document`の公開 API として抽出されたものではない。

`insertText`/`eraseRange`のような「編集」系はオフセットベースの既存 API がそのまま使えるが、プラグイン作者が期待する自然なインターフェース(「3行目5桁目に文字を挿入する」)は、この橋渡し(行+桁→オフセット変換)がプラグインホスト側かDocument側のどちらかに実装されて初めて成立する。

## 対応方針 (今回は延期)

Phase 8a は`NeoMifesCoreApi`自体を実装せず、DLL 読み込み+`onLoad`/`onUnload`呼び出し+SEH クラッシュ隔離のみに範囲を絞った(ADR-015参照)。推測でこのブリッジを設計するとCLAUDE.mdルール3(推測実装をしない)に反するため、Phase 8b着手時に以下を検討する:

1. **`document::Document`に`getLineText(LineNumber) -> std::u16string`を新設するか、`RenderPipeline::extractLineText()`と同等のロジックを`Document`側へ移設するか。** 現状`extractLineText()`は`render::RenderPipeline`のprivateメソッドであり、これをそのまま`document::Document`の公開APIとして持たせるべきか、あるいはプラグインホスト層に別途薄いアダプタを置くべきか、レイヤ責務分離(CLAUDE.md §3のレイヤードアーキテクチャ)の観点から判断が必要。
2. **行+桁(0-based column)→オフセット変換 API の要否。** UTF-16コード単位ベースの桁指定にするか、グラフェムクラスタベースにするか(絵文字・結合文字を考慮するか)は、プラグインSDKの文字コード契約(現状`plugin_sdk.h`はUTF-16LEを前提とする設計方針だが未確定)と合わせて決める必要がある。
3. **`insertText`/`deleteRange`をプラグイン側へ公開する際の同時実行安全性。** `document::Document`はADR-009等の既存設計で「単一UIスレッドのみ」を前提としており、プラグインコールバックがどのスレッドで呼ばれるか(Phase 8aは`PluginHost::load()`/`unload()`を呼び出し元スレッドと同一で実行、UIスレッド前提)を`NeoMifesCoreApi`の契約として明記する必要がある。

## 完了条件 (Phase 8b着手時)

- [ ] `Document`(またはブリッジ層)に行番号→テキスト取得APIが存在し、単体テストで検証済み
- [ ] 行+桁→オフセット変換の文字コード契約(UTF-16コード単位/グラフェムクラスタ)がADRまたはSDKヘッダコメントで明記されている
- [ ] `NeoMifesCoreApi`のスレッド安全性契約(呼び出し可能スレッド)が明記されている
- [ ] `plugin_sdk.h`に`NeoMifesCoreApi`構造体が追加され、`PluginHost`経由でプラグインへ渡される
