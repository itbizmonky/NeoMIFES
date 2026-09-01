# Issue 索引

**最終更新:** 2026-09-01 (v1出荷判定🎉M5達成後の次フェーズ全5件対応完了 — `json_tree_ui_population_hang.md`/`json_syntax_highlight_large_file_open_hang.md`/`undo_redo_active_usage_soak_not_performed.md`解決・`csv_per_cell_index_memory_scaling.md`/`search_grep_multi_gb_performance_gap.md`部分対応)

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
| [本物の Authenticode 証明書が未取得](authenticode_certificate_not_acquired.md) | 署名機構自体は自己署名証明書で実装・動作確認済み、実配布には本物の証明書購入(ユーザー判断)が必要 | 未定 (ユーザーの証明書取得待ち) |
| [CSVモードのper-cellインデックスが大規模ファイルで大きなメモリを消費する](csv_per_cell_index_memory_scaling.md) | 🟡 2026-09-01部分対応。`CsvCell`を24→16バイト/セルへ圧縮(662MBで実測WorkingSet約1.97GB)。10GB規模への根本対応(遅延インデックス化)はユーザー承認のもと対象外確定、リスクは残存 | 対象外確定 (遅延インデックス化は再設計コストが大きいため見送り) |
| [検索・Grepが「数GB ≤ 30秒」目標を実測で満たさない](search_grep_multi_gb_performance_gap.md) | 🟡 2026-09-01部分対応。真因はRE2ではなくUTF-8変換(toUtf8WithOffsets、ASCII高速パス追加で約38%削減)。GrepServiceのファイルあたり固定オーバーヘッドは対象外のまま残存 | 部分対応 (GrepServiceの多ファイルケースは未対応) |
| [主要テキスト編集領域がUI Automation/スクリーンリーダーへ内容を一切公開していない](text_surface_no_screen_reader_exposure.md) | メニュー・ステータスバー・ダイアログは正しく公開されるが、Direct2D直接描画の本文領域は`ControlType.Custom`/`Name=''`で内容が一切取得できない | 未定 (UI Automation TextPattern実装は大規模な新規サブシステム) |

## P2 — 凍結 / 再評価待ち

| Issue | 概要 | 状態 |
|---|---|---|
| [オーバーレイにフォーカスがある間 Ctrl+S/O/N が届かない](overlay_focus_blocks_file_lifecycle_keys.md) | FindBar/GrepBar/CommandPalette/GotoLineBar/OutlinePane のサブクラスプロシージャが未知のキーを親HWNDへ転送しない | 待機 (5ウィジェット全てへの転送ロジックが必要になった時点で再評価) |
| [メニューバーのキーバインド表示が実行時リマップに追従しない](menu_bar_keybinding_label_stale.md) | `\tCtrl+X` 等の表示は起動時固定、`keybindings.reload`/`.preset.*` 後も再起動まで古いまま (実際のキー入力自体は正しく機能する) | 待機 (メニュー再構築機構が必要になった時点で再評価) |
| [`ts_parser_parse()` の文書サイズ比例コスト](tree_sitter_incremental_parse_cost.md) | 50万行で 155.95ms、DoD ≤50ms 未達。4 フェーズ挑戦し tree-sitter の構造的限界と結論 | 🧊 **凍結** (Phase 12 直前に「達成」か「DoD 改訂」かを判断) |
| [`UndoStack` のメモリ無制限成長](undo_stack_unbounded_memory.md) | 圧縮/ディスクスワップ未実装。2026-09-01追記: ヘッドレスプローブで1pushあたり約4.06バイトと実測(AddBufferのappend-only設計に起因、線形増加で加速無し)、100万件規模でも約4MB程度と推定され256MB予算を大きく下回る見込み | 待機 (実UIを通した100万件規模の実測はまだ無い) |
| [`TextLayoutCache` のサイズ無制限成長](text_layout_cache_unbounded_growth.md) | LRU 追い出し未実装 | 待機 |
| [`LineIndex` の O(log n) 化](line_index_o_log_n.md) | Phase 7p でインクリメンタル更新は実装済み。残りは低優先度 | 待機 |
| [マッチハイライトの線形走査](match_highlight_linear_scan_scaling.md) | 数万件マッチが発生する経路ができてから再評価 | 待機 |
| [`ReplaceAllCommand` の `extract()` 再走査](replace_all_buffer_snapshot_extract_scaling.md) | 再評価条件は成立済み、ベンチマーク未実施 | 待機 |
| [`MSVC_RUNTIME_LIBRARY` 修正の脆弱性](cmake_msvc_runtime_library_fragility.md) | 将来の Abseil 更新時にのみ顕在化しうる潜在リスク | 待機 |
| [`search`/`utf8_convert` の小規模改善 3 件](search_utf8_convert_minor_cleanup.md) | 正しさ・性能に実害なし | 待機 |
| [`asan` プリセットがCIに常設化されていない](asan_preset_not_in_ci.md) | WI-13でローカル初回実行しDoD自体は満たしたが、以後の継続検証機構が無い | 待機 (CI実行時間とのトレードオフ検討) |
| [Phase 10.1 v2.0拡張候補が未実装](phase_10_1_v2_extended_patterns.md) | リアルタイムテール/分散トレース/構造化ログ/統計ダッシュボード/SAP・AWS・Azure等ベンダー固有パターンは実データ入手まで意図的に先送り (WI-14a) | 待機 (WI-14c MVP達成後、実データ入手時に再評価) |
| [CSVグリッドが末尾改行由来の暗黙の空行を表示してしまう](csv_grid_shows_trailing_implicit_empty_row.md) | `CsvModel`の既存仕様(WI-16a、Document全体と一貫)がグリッドUIで視覚的ノイズとして露呈。データ欠落・誤りは無い | 待機 (要望が出るかPhase 10.2次期UI改善サブWI着手時に再評価) |
| [`tree-sitter-xml`が約505階層超のネストで整形式入力を誤検知する](xmltree_deep_nesting_misparse_limit.md) | クラッシュではなく`ERROR`ノードへの安全な縮退。実用上の発生頻度は極めて低いと想定 | 待機 (実例が確認された場合に再評価) |

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
| [`parseJsonTree()`が病的に深いネストでスタックオーバーフローしうる](json_tree_worker_deep_nesting_stack_overflow.md) | 🟢 WI-15c (2026-08-19)。`DepthLimitSax`によるSAX事前深度チェック(`kMaxJsonNestingDepth=200`)を追加、`nlohmann::ordered_json::parse()`を呼ぶ前に弾く設計に確定 |
| [設定システムが存在しない](no_settings_system.md) | 🟢 WI-08 (2026-08-13)。`core::Settings`実装、`kTabWidth`二重定義解消(`SetIncrementalTabStop()`未着手ギャップも同時発見・解消)、フォント/タブ幅/行番号/ミニマップがsettings.json経由で再起動なしに反映されることを実機ドッグフーディングで確認 |
| [CSVグリッドのフィルタ行付近に表示異常](csv_grid_filter_row_visual_glitch.md) | 🟢 WI-16f (2026-08-25)。原因はWM_PAINTが常に裏のテキストビューを描画しフィルタ行の余白から透けて見えていたこと。背景パネル(`m_hwndFilterBackdrop`)追加で解消、実機確認済み |
| [`OriginalBuffer` のデコードキャッシュ無制限蓄積による OOM](decode_cache_unbounded_growth.md) | 🟢 v1出荷判定 (2026-08-31)。10GBファイルのログ解析モードでシステムメモリ枯渇を実機で確認、非キャッシュAPI+ストリーミングAPI追加で解消(10GBファイルのPrivateメモリ 20GB超→1.22GB、初回インデックス構築113.7秒→26.99秒)。`LineIndex::build()`等9箇所を修正 |
| [JSON/XMLツリーUIが大規模ファイルでUIスレッドを長時間ハングさせる](json_tree_ui_population_hang.md) | 🟢 2026-09-01。実際の原因は`WC_TREEVIEW`への大量`TVM_INSERTITEMW`呼び出し(issueの推定原因`WC_LISTVIEW`は誤りと判明、標準プローブで実測)。しきい値ベースの「全展開(小規模)/遅延ロード+階層キャップ(大規模)」で解消、145万要素で実測トグル9ms・展開303ms |
| [大規模JSONファイルを開くだけでJSON構文ハイライトが長時間UIをハングさせる](json_syntax_highlight_large_file_open_hang.md) | 🟢 2026-09-01。真因は`extractOutline()`がシンボルテーブルが空(JSON含む19言語)でも無条件にフルパースしていたこと。空テーブルなら即座に空を返す早期リターンで解消、47秒→約1秒(約47倍改善)。C++等アウトライン対応言語への回帰無しをドッグフーディングで確認 |
| [「100万Undo(24時間ソーク)」が実際にはUndo/Redoを回さないアイドル確認だった](undo_redo_active_usage_soak_not_performed.md) | 🟢 2026-09-01。ヘッドレスプローブで`core::UndoStack`を直接駆動し5分間・約14億操作の能動的ソークを実施。`UndoStack`自体はリークしないことを確認(`push()`が`m_redo.clear()`を正しく実行)。観測された線形増加(非加速)は既知の`AddBuffer` append-only設計に起因、`undo_stack_unbounded_memory.md`で追跡中 |

---

## 起票の判断基準

以下に該当する場合は issue を起票する:

1. **設計上の妥協を行った** — 「◯◯が存在しないため簡略版にした」等
2. **DoD を達成できなかった** — 未達のまま次へ進む場合は必ず記録する
3. **将来顕在化する性能・メモリ上のリスクを認識した** — 現時点で実害が無くても記録する
4. **ロードマップの計画と実装が乖離した** — 特に「計画に載っていなかったことが判明した」場合

**CLAUDE.md §11 への追加提言 (`gap_analysis.md` §8.2):** 「◯◯が存在しないため縮退した」という判断を行った場合、**その ◯◯ を必ず本ディレクトリに起票する。同じ理由での縮退が 3 回を超えたら、その基盤の実装を次フェーズ候補に必ず含める。**
