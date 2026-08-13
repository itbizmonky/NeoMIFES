# Issue: 設定システムが存在しない (P1 — 後続フェーズを継続的に劣化させている負債)

- **起票日:** 2026-08-04 (中間レビュー、Phase 8f / 7y 完了時点)
- **状態:** ✅ **解決済み (WI-08、2026-08-13、コミット `6a76722`/`0fbd148`/`0b55e86`)**
- **対象:** `src/core/` (新設 `core::Settings`)、および既存のハードコード定数群
- **優先度:** **高 (P1)** — P1 の中では最優先
- **対応 Phase:** [Phase 8.6a](../design/master_roadmap.md) (roadmap v2.1 で新設)
- **親文書:** [`gap_analysis.md`](../design/gap_analysis.md) §4.1

## 事実

設定の読み書き機構は存在しない。フォント・タブ幅・テーマ・キーバインドは全て C++ 定数としてハードコードされている。

`src/app/main.cpp:863` に、この状況を認めるコメントが残っている:

> `there is no settings system to source a configurable value from (same rationale as elsewhere in this file), so a future settings UI would wire ...`

## 本 issue が単なる「未実装機能」ではない理由

**「設定システムが存在しないため」を理由に機能を縮退させた設計判断が、設計文書に 13 箇所記録されている。**

| 箇所 | 縮退内容 |
|---|---|
| roadmap §3.7 (Phase 4b8d) | タブ幅を 4 固定。`Auto` モード (文書統計から多数派を採用) を断念 |
| roadmap §3.7 (Phase 4b8f) | クリップボード分配ルール (`CF_NEOMIFES_MULTICURSOR`・サイクル貼り付け) を断念 |
| roadmap §5.5 (Phase 5c3) | 秀丸互換 Grep 結果ペインを MVP へ縮退 |
| roadmap §7 (Phase 7e) | タブ幅を `render_pipeline.cpp` に**複製**。「2 箇所の手動同期が必要な既知のトレードオフとして受容」 |
| detailed_design §10.x | 同上 (`kTabWidth=4` の二重定義) |
| (他 8 箇所) | — |

**既に具体的な技術的負債が発生している:** `kTabWidth = 4` が `src/app/main.cpp` と `src/render/src/render_pipeline.cpp` に二重定義され、一方を変更すると他方と食い違う。設計文書はこれを「受容した」と記録しているが、**受容ではなく先送りである**。

**設定システムの欠落は単独の欠陥ではなく、後続フェーズ全ての設計を継続的に劣化させ続ける負債として既に作用している。**

## 対応方針 (roadmap §8.6.1)

- `core::Settings` — `%APPDATA%\NeoMIFES\settings.json`
- **形式は JSON** (JSON5 ではない)。roadmap U#7 は JSON5 を第一候補としていたが、[ADR-013](../decisions/ADR-013-json-library.md) で導入済みの nlohmann/json は JSON5 を解釈できず、かつ `core::SearchHistory` (Phase 5c5) が既に素の JSON を採用した前例がある。**同じ判断を踏襲する** (ADR-013 自身が「機械生成/機械読み取り専用ファイルに JSON5 の付加機能は不要」と記録している)
- 初期スコープ: フォントファミリ / フォントサイズ / タブ幅 / タブをスペースで挿入 / 行番号表示 / ミニマップ表示 / 折返し / 自動保存間隔 / テーマ

## 完了条件

- [x] `core::Settings` が実装され、`%APPDATA%\NeoMIFES\settings.json` から読み書きできる
- [x] **既存のハードコード定数の移行を完了している** — 特に `kTabWidth` の二重定義を解消する。移行せずに設定システムだけ作ると負債が残る (`grep -rn "kTabWidth" src/` は定義0件、詳細は`build_plan.md` WI-08節)
- [x] 設定ファイルが壊れている / 存在しない場合、既定値で安全に起動する (実機で壊れたJSONを与えて確認済み)
- [x] 設定変更が再起動なしで反映される (**フォント / タブ幅 / 行番号表示 / ミニマップ表示** — 起票時点の例示「テーマ」ではなくbuild_plan.mdのDoD確定文言に従った。テーマ自体はWI-09の専用スコープであり、WI-08時点ではまだ`render::Theme`という消費者が存在しない)
- [x] 単体テストで「読み込み → 変更 → 保存 → 再読み込み」のラウンドトリップを検証している (`core_settings_test.cpp::SaveThenLoadRoundTripsAllFields`)

## CLAUDE.md §11 チェックリストへの提言 (再発防止)

本 issue の教訓として、[`gap_analysis.md`](../design/gap_analysis.md) §8.2 で以下の追加を提言済み:

> **「◯◯が存在しないため縮退した」という判断を行った場合、その ◯◯ を `docs/issues/` に起票したか。同じ理由での縮退が 3 回を超えたら、その基盤の実装を次フェーズ候補に必ず含める。**

設定システムは **13 回** 縮退理由に挙げられながら、一度も issue 化されず、一度も次フェーズ候補に挙がらなかった。

## 再検証コマンド

```bash
grep -rn "class Settings" --include=*.h src/                   # 0 件なら未解消
grep -rn "kTabWidth" --include=*.cpp --include=*.h src/        # 2 箇所以上なら二重定義が残存
grep -rn "設定システム" docs/ --include=*.md | wc -l           # 縮退理由の出現回数
```
