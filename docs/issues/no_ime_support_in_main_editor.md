# Issue: メインエディタが IME を処理しない (P0 — 日本語市場では単独で出荷を阻む)

- **起票日:** 2026-08-04 (中間レビュー、Phase 8f / 7y 完了時点)
- **解消日:** 2026-08-12 (WI-06、コミット `0baccaa`/`94e2259`/`f233f02`)
- **状態:** ✅ **解消済み**
- **対象:** `src/ui/src/main_window.cpp`、`src/render/src/render_pipeline.cpp`
- **優先度:** ~~最高 (P0)~~ → 解消済み
- **対応 Phase:** [Phase 8.5e](../design/master_roadmap.md) (roadmap v2.1 で §16.1 を実フェーズ化)
- **親文書:** [`gap_analysis.md`](../design/gap_analysis.md) §3.4

## 事実

**メインエディタ (D2D 描画のテキスト領域) は `WM_IME_*` を 1 つも処理していない。**

| 検証 | 結果 |
|---|---|
| `grep -n "WM_IME" src/ui/src/main_window.cpp` | **0 件** |
| `main_window.cpp` が処理する `WM_*` (15 種) | `WM_IME_*` を含まない |
| `ImmGetContext` / `CANDIDATEFORM` / `COMPOSITIONFORM` | **0 件** |
| `imm32.lib` のリンク | **無し** |

IME を扱っているのは以下の 4 ファイルのみ:
- `src/ui/src/find_bar.cpp:331`
- `src/ui/src/grep_bar.cpp:332`
- `src/ui/src/command_palette.cpp:290`
- `src/ui/src/goto_line_bar.cpp:134`

**これらは全て標準 `WC_EDIT` 子コントロールであり、IME を Win32 から無償で得ているだけ**である。しかもハンドリング内容は `WM_IME_STARTCOMPOSITION` / `WM_IME_ENDCOMPOSITION` で「変換中フラグ」を立て、Enter/Escape/F3 の誤爆を防ぐためだけのもの。メインの D2D 描画テキスト領域とは無関係。

## 影響

メインエディタでの日本語入力は、確定文字列が `WM_CHAR` として届くため「**入力自体は成立する**」。しかし:

1. **変換中の未確定文字列がインライン表示されない** — ユーザーは「今何を入力しているか」が画面から分からないまま変換操作をすることになる
2. **変換候補ウィンドウがキャレット位置に追従しない** — `ImmSetCandidateWindow` を呼んでいないため、IME 既定の位置 (ウィンドウ左上や画面隅) に出る

**秀丸 / サクラ / MIFES を「凌駕する」と掲げる日本語エディタとして、この状態は単独で出荷を阻む。**

## なぜ見落とされたか

roadmap v2.0 §16.1「CJK IME (日本語・中国語・韓国語)」は**章としては存在し、実装項目も列挙されていた**:

> `WM_IME_STARTCOMPOSITION` / `WM_IME_COMPOSITION` / `WM_IME_ENDCOMPOSITION` を Direct2D の描画に統合
> 変換中文字列を下線付きインライン表示

**しかし §2 の全フェーズ俯瞰表にフェーズとして現れなかった。** 章はいつまでも実装されない ([`gap_analysis.md`](../design/gap_analysis.md) §6.2 — 同じ構図で「タブ UI」「ステータスバー」「テーマ」も見落とされた)。

roadmap v2.1 で **§8.5.7 (Phase 8.5e) として実フェーズ化**した。

## 実装項目 (roadmap §8.5.7)

- `WM_IME_STARTCOMPOSITION` — 未確定文字列の描画開始。既定の IME 変換ウィンドウを抑止 (`return 0` でデフォルト処理を止める)
- `WM_IME_COMPOSITION` — `ImmGetCompositionStringW(GCS_COMPSTR)` で未確定文字列、`GCS_RESULTSTR` で確定文字列を取得。`GCS_COMPATTR` で変換対象節の属性も取得
- `WM_IME_ENDCOMPOSITION` — 未確定表示のクリア
- `ImmSetCandidateWindow(CFS_CANDIDATEPOS)` — 候補ウィンドウをキャレット位置へ追従
- `render::RenderPipeline` へ未確定文字列のインライン描画を追加 (下線 + 変換対象節のハイライト)
- `src/ui/CMakeLists.txt` に `imm32.lib` のリンク追加

## 検証方針 — 自動テストによる代替を認めない

この自動化環境では修飾キーを伴う合成入力が不調であることが複数セッションで確認されている (`reference_no_win32_gui_automation.md`)。従来はこの制約を理由に「プロセス生存確認 + 統合テスト」で代替してきた。

**本項目については、その代替を認めない。** 日本語がまともに打てないことは製品として単独で出荷を阻む欠陥であり、**実機での手動確認 (MS-IME で「にほんご」と入力し、未確定文字列が下線付きでキャレット位置に表示され、候補ウィンドウがその直下に出ること) を Phase 8.5e の完了条件とする。**

## 完了条件

- [x] メインエディタで未確定文字列が下線付きインライン表示される
- [x] 変換対象節がハイライトされる
- [x] 候補ウィンドウがキャレット位置に追従する
- [x] 変換確定後、確定文字列が正しく `Document` へ挿入される (Undo 1 ステップ)
- [x] 複数カーソル時の挙動が定義されている (`WM_IME_STARTCOMPOSITION` で `collapseToPrimary()`、確定後の複数カーソル復元は行わない)
- [x] **実機で MS-IME による手動確認を完了している** — ユーザーが実機で確認し「問題無いように見える」と報告 (2026-08-12)。**スクリーンショットは取得していない**、口頭確認で代替した
- [ ] 中国語 / 韓国語 IME の確認は Phase 12 へ (本 issue のスコープ外、意図的に未対応のまま)

詳細な設計判断・実装後の確定事項は [`build_plan.md` WI-06](../design/build_plan.md) 参照。

## 再検証コマンド

```bash
# WI-06完了後は複数件ヒットする (WM_IME_STARTCOMPOSITION/WM_IME_COMPOSITION/WM_IME_ENDCOMPOSITION)
grep -n "WM_IME" src/ui/src/main_window.cpp
```
