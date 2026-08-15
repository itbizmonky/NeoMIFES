# Issue 索引

**最終更新:** 2026-08-15 (WI-10完了、メニューバーキーバインド表示陳腐化issueを新規起票)

`docs/issues/` は「実装しなかったこと・先送りしたこと・未解決の技術的負債」を記録する。ADR (`docs/decisions/`) が**行った判断**を記録するのに対し、本ディレクトリは**行わなかった判断とその理由**を記録する。

> ⚠️ **2026-08-04 中間レビューの教訓:** 本索引はそれまで存在せず、18 件の issue が一覧できない状態だった。その結果、`lazy_decode_mmap.md` は Phase 2b3 で実質解消済みなのに**優先度「高」のまま 3 週間放置**され、逆に「設定システムが無い」は **13 回も縮退理由に挙げられながら一度も issue 化されなかった**。
> **運用ルール:** issue を新規起票・解決したら、必ず本索引の該当行も同じセッション内で更新する (CLAUDE.md §11)。

---

## P0 — 出荷不能 (2026-08-04 中間レビューで発見)

これらが 1 つでも残っている限り、いかなる形態でもエンドユーザーへ配布してはならない。詳細は [`gap_analysis.md`](../design/gap_analysis.md) 参照。

**該当なし (2026-08-13時点) — 全 P0 issue が WI-01〜WI-07 完了により解消済み。**

## P1 — 商用品質を満たさない

| Issue | 概要 | 対応 Phase |
|---|---|---|
| [検索が CRLF 行末を考慮しない](search_crlf_line_ending.md) | 正規表現の `$`/`^` が `\r` を行内容として扱う | 未定 (Phase 12 前) |

## P2 — 凍結 / 再評価待ち

| Issue | 概要 | 状態 |
|---|---|---|
| [オーバーレイにフォーカスがある間 Ctrl+S/O/N が届かない](overlay_focus_blocks_file_lifecycle_keys.md) | FindBar/GrepBar/CommandPalette/GotoLineBar/OutlinePane のサブクラスプロシージャが未知のキーを親HWNDへ転送しない | 待機 (5ウィジェット全てへの転送ロジックが必要になった時点で再評価) |
| [メニューバーのキーバインド表示が実行時リマップに追従しない](menu_bar_keybinding_label_stale.md) | `\tCtrl+X` 等の表示は起動時固定、`keybindings.reload`/`.preset.*` 後も再起動まで古いまま (実際のキー入力自体は正しく機能する) | 待機 (メニュー再構築機構が必要になった時点で再評価) |
| [`ts_parser_parse()` の文書サイズ比例コスト](tree_sitter_incremental_parse_cost.md) | 50万行で 155.95ms、DoD ≤50ms 未達。4 フェーズ挑戦し tree-sitter の構造的限界と結論 | 🧊 **凍結** (Phase 12 直前に「達成」か「DoD 改訂」かを判断) |
| [`UndoStack` のメモリ無制限成長](undo_stack_unbounded_memory.md) | 圧縮/ディスクスワップ未実装。時間 DoD は達成済み、メモリは未計測 | 待機 (実メモリ計測が可能になってから) |
| [`TextLayoutCache` のサイズ無制限成長](text_layout_cache_unbounded_growth.md) | LRU 追い出し未実装 | 待機 |
| [`LineIndex` の O(log n) 化](line_index_o_log_n.md) | Phase 7p でインクリメンタル更新は実装済み。残りは低優先度 | 待機 |
| [マッチハイライトの線形走査](match_highlight_linear_scan_scaling.md) | 数万件マッチが発生する経路ができてから再評価 | 待機 |
| [`ReplaceAllCommand` の `extract()` 再走査](replace_all_buffer_snapshot_extract_scaling.md) | 再評価条件は成立済み、ベンチマーク未実施 | 待機 |
| [`MSVC_RUNTIME_LIBRARY` 修正の脆弱性](cmake_msvc_runtime_library_fragility.md) | 将来の Abseil 更新時にのみ顕在化しうる潜在リスク | 待機 |
| [`search`/`utf8_convert` の小規模改善 3 件](search_utf8_convert_minor_cleanup.md) | 正しさ・性能に実害なし | 待機 |

## 対応不能 / 外部要因待ち

| Issue | 概要 | 状態 |
|---|---|---|
| [VB / VBScript 文法にライセンス明記候補が無い](vb_vbscript_grammar_no_licensed_candidate.md) | 全候補が `license: null` の個人リポジトリ | ⛔ **恒久除外** (候補が現れるまで着手不可能) |

## ✅ 解決済み (経緯の記録として保持)

| Issue | 解決 |
|---|---|
| [`PieceTable` を RB-tree へ置換](piece_table_rb_tree.md) | 🟢 Phase 2b2 (2026-07-15) |
| [`OriginalBuffer` の mmap + Lazy Decode 化](lazy_decode_mmap.md) | 🟢 Phase 2b3 (2026-07-15)。1GB 2.03s / private WS 増分 0.46MB 実測 |
| [`NeoMifesCoreApi` と `Document` の API ギャップ](plugin_core_api_document_gap.md) | 🟢 Phase 8b (2026-08-02、ADR-016) |
| [SQL 文法の `parser.c` 未コミット問題](sql_grammar_needs_tree_sitter_cli.md) | 🟢 Phase 7y (2026-08-04、ADR-021 ベンダリング方式) |
| [文書保存機能が存在しない](no_document_save_capability.md) | 🟢 WI-01+WI-02 (2026-08-05、🎉 M1達成。ドッグフーディングでバグ2件発見・修正、実際に編集・保存・コミットまで実証) |
| [メインエディタが IME を処理しない](no_ime_support_in_main_editor.md) | 🟢 WI-06 (2026-08-12)。実機MS-IME確認完了、下線付きインライン表示・候補ウィンドウ追従・1 Undoステップ確定を確認 |
| [ネイティブオーバーレイウィジェットが画面上に一切描画されない](native_overlay_widgets_invisible.md) | 🟢 WI-07 ステップ0 (2026-08-12)。根本原因は `MainWindow::create()` の `WS_CLIPCHILDREN` 欠落。1行追加で解消、TabBarのスクリーンショットで実証 |
| [アプリケーションシェルが未実装](no_application_shell.md) | 🟢 WI-01〜WI-07 (2026-08-13、🎉 M2「アプリケーションとして成立」達成)。ファイルUI/タブ/メニュー/ステータスバー/行番号/横スクロール/アイコンを全て実装、`main.cpp`は398行 |
| [設定システムが存在しない](no_settings_system.md) | 🟢 WI-08 (2026-08-13)。`core::Settings`実装、`kTabWidth`二重定義解消(`SetIncrementalTabStop()`未着手ギャップも同時発見・解消)、フォント/タブ幅/行番号/ミニマップがsettings.json経由で再起動なしに反映されることを実機ドッグフーディングで確認 |

---

## 起票の判断基準

以下に該当する場合は issue を起票する:

1. **設計上の妥協を行った** — 「◯◯が存在しないため簡略版にした」等
2. **DoD を達成できなかった** — 未達のまま次へ進む場合は必ず記録する
3. **将来顕在化する性能・メモリ上のリスクを認識した** — 現時点で実害が無くても記録する
4. **ロードマップの計画と実装が乖離した** — 特に「計画に載っていなかったことが判明した」場合

**CLAUDE.md §11 への追加提言 (`gap_analysis.md` §8.2):** 「◯◯が存在しないため縮退した」という判断を行った場合、**その ◯◯ を必ず本ディレクトリに起票する。同じ理由での縮退が 3 回を超えたら、その基盤の実装を次フェーズ候補に必ず含める。**
