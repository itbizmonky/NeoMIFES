# ADR-010: Rendering Engine は Document Engine に直接依存する (`neomifes_render` → `neomifes_document`)

- **ステータス:** Accepted
- **決定日:** 2026-07-16 (Phase 3b 着手時)
- **関連:** [ADR-008](ADR-008-com-raii-comptr.md)、[ADR-009](ADR-009-deferred-device-init.md)、`docs/design/detailed_design.md` §4.3/§4.4、CLAUDE.md §3 レイヤードアーキテクチャ図

## コンテキスト

Phase 3b では `RenderPipeline` が `Document` の内容を実際に DirectWrite で描画する必要がある。`detailed_design.md` §4.3 は「`PieceTable::snapshot()` はフレームごとに呼ばない」ことを既にガードレールとして明記しており (100K piece 規模で 1.2〜1.5ms、16.6ms フレーム予算の約7%を消費するため)、§4.4 はその実装として「`RenderPipeline` が `Document*` を保持し、バージョンカウンタを比較する方式が最小」と推奨している。これを実装するには `neomifes_render` が `neomifes::document::Document`/`BufferSnapshot` を直接参照できる必要がある。

CLAUDE.md §3 のレイヤ図は以下の通り (上位が下位にのみ依存):
```
[UI Shell] → [Editor Core] → [Rendering Engine] → [Document Engine] → [Search/Encoding] → [Plugin] → [AI Plugin]
```
Rendering Engine は Document Engine より**上位**に描かれており、上位→下位への依存はレイヤ規約上正しい方向である。`src/document/CMakeLists.txt` を確認済みで、document 側は render に一切依存しない (循環なし)。

## 選択肢

1. **`neomifes_render` が `neomifes_document` に直接依存 (採用):** `RenderPipeline` が非所有 `const Document*` を保持し、`Document::version()` と前回キャッシュ時のバージョンを比較して変化時のみ `snapshot()` を呼ぶ
2. **app層 (`main.cpp`) が仲介:** `Document` から行文字列を都度抽出し、`RenderPipeline` には抽出済みのプレーンな文字列データだけを渡す。`neomifes_render` は `neomifes_document` を一切参照しない
3. **中立的な `ITextSource` 抽象を新設:** `Document` と将来の他データソース (例: 差分ビュー) を共通インターフェースの背後に隠す

## 決定

**選択肢1 (直接依存) を採用する。**

## 根拠

- **バージョン比較ロジックの置き場所として自然:** §4.3 のガードレール (snapshot をフレームごとに呼ばない) は「レンダラが変更を検知してキャッシュを再取得する」責務であり、レンダーループを持つ `RenderPipeline` 自身がこれを持つのが単一責任の観点で妥当。選択肢2ではこのロジックが `main.cpp` (合成ルート) に漏れ出し、`main.cpp` が本来持つべきでない「毎フレーム何を再抽出すべきか」の判断を背負うことになる
- **循環依存が発生しない:** レイヤ図の順序を確認した通り、Rendering Engine → Document Engine は許可された依存方向であり、`neomifes_document` 側に render への参照は一切ない
- **選択肢3 (ITextSource 抽象) は時期尚早:** 現時点で `Document` 以外にレンダリング対象となるデータソースは存在せず、消費者も `RenderPipeline` 1つのみ。CLAUDE.md 絶対ルール3「推測実装をしない」に反する早すぎる抽象化になる。将来2つ目の実装が必要になった時点で導入を再検討する

## 影響

### 実装
- `src/render/CMakeLists.txt`: `target_link_libraries` の `PUBLIC` に `neomifes::document` を追加 (`render_pipeline.h` が `document::Document*`/`document::LineNumber` を公開APIに露出するため)
- `RenderPipeline::setDocument(const document::Document*)` で非所有ポインタを受け取る。呼び出し側 (`main.cpp`) が `Document` の生存期間を `RenderPipeline` より長く保つ責務を負う
- `Document` に `version()` (`std::uint64_t`, 単一UIスレッド前提のため atomic 不要、ADR-009 参照) を追加

### 却下した選択肢の理由 (補足)
選択肢2は「app層を薄く保つ」という一見の美徳はあるが、実際には差分検知ロジックの置き場所が構成ルートに押し付けられるだけで、責務の所在としては悪化する。選択肢3は消費者が1つしかない段階での抽象化コストが実利を上回る。

## 将来の再評価タイミング
- Document 以外のレンダリング対象 (例: プラグインが提供する仮想テキストビュー、差分表示) が具体的に必要になった時点で `ITextSource` 相当の抽象化を再検討する

## 参考
- `docs/design/detailed_design.md` §4.3/§4.4
- `docs/handoff/RESUME_HERE.md` §6 (Phase 3b 着手時に確認する設計課題一覧)
