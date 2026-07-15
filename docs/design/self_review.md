# NeoMIFES 設計セルフレビュー v1.5

> 対象: [`CLAUDE.md`](../../CLAUDE.md) / [`basic_design.md`](basic_design.md) / [`detailed_design.md`](detailed_design.md) / [`docs/decisions/`](../decisions/)
> 初回レビュー日: 2026-07-14
> 最終更新日: 2026-07-15 (Phase 2b2 完了後の包括レビュー)
> レビュー観点: (A) 要件充足性 / (B) 設計整合性 / (C) 性能目標達成可能性 / (D) リスク / (E) 実装可能性
>
> ⚠️ **§A〜§G は Phase 0 (要件確認・初回設計) 時点のスナップショットとして保存されている歴史的記録。**
> **現在の状態を知りたい場合は §H (残リスク一覧) と §I (次アクション) を見ること。** §A〜§G の内容は当時の判断根拠を追うための一次資料として意図的に凍結している (TIMELINE.md と同じ「変更しない」原則)。

## 更新履歴

- **v1.1 (2026-07-14):** ユーザー確認 (4 項目) + 技術決定 (4 項目) 反映。F-1 〜 F-4 の全項目に対応。ADR-001〜005 発行。要件カバレッジ 82% → 100% 到達。
- **v1.2 (2026-07-14):** Phase 1 完了時 (R1 起動 0.3s の計測基盤達成、R10 は Phase 2b 引継ぎ)。
- **v1.3 (2026-07-14):** Phase 2a 完了 (Document Engine MVP、31 単体テスト + 2000 反復プロパティテスト)、Phase 2b1 完了 (B-1 pieceView / B-2 AddBuffer チャンク化)。ADR-006 (Path-Copying RB-Tree) 発行。R10 実装形態を確定。
- **v1.4 (2026-07-15):** Phase 2b2 完了 (Step 1: PieceTree insert/split、Step 2: eraseRange CLRS 13.4 + PieceTable 内部差し替え)。ADR-006 を ADR-007 (Mutable RB-Tree) が Supersede — path-copying は実装コスト・性能目標未達リスクにより撤回。プロパティテスト 20,000 反復化。R11 (Piece Tree 永続化複雑性) を解消側に更新。新たに判明した制約: LineIndex の O(log n) 化は tree 設計上不可能と判明し撤回 ([`line_index_o_log_n.md`](../issues/line_index_o_log_n.md))。
- **v1.5 (2026-07-15):** Phase 2b2 完了後の包括レビュー。ドキュメント鮮度の不整合を多数発見・修正 (本ファイルのタイトル版数ズレ、§G/§I の陳腐化、RESUME_HERE.md の古い `git init` 指示、Issue の完了条件チェック漏れ)。**CI ベンチマーク実測値を初めて確認** (§H 参照) — `PieceTable::insert` 276ns で目標 500ns を達成、`snapshot` は 1000 piece 規模でのみ確認 (100K piece 規模は未検証)。再発防止のため CLAUDE.md に「セッション終了時チェックリスト」を新設。

判定記号: ✅ 充足 / ⚠️ 要補強 / ❌ 未対応

---

## A. 要件充足性チェック (要件定義書 §6-16 との突合)

### A-1. 編集機能 (§6)
| 要件 | 判定 | コメント / 追記必要事項 |
|---|---|---|
| 複数タブ | ✅ | UI Shell に TabBar |
| **複数ウィンドウ** | ⚠️ | 基本設計に **単一プロセス** 前提が強く、複数トップレベルウィンドウの記述が薄い。→ 補強必要 (§B-1 で対応案) |
| 矩形選択 | ✅ | detailed §5.1 |
| **縦編集** | ⚠️ | 用語が「縦書き」か「列(カラム)編集」か曖昧。**MIFES 由来の "縦編集" = 列単位の追記/削除** と解釈すべき。設計にその明記なし → 補強必要 |
| 複数カーソル | ✅ | |
| アウトライン | ✅ | Syntax Engine |
| 折り畳み | ✅ | |
| **行番号** | ⚠️ | UI 要素として明記なし。Rendering に「Line Gutter」レンダラを追加すること |
| ブックマーク | ✅ | Editor Core |
| コード補完(LSP) | ✅ | Phase 11 |
| シンタックスハイライト | ✅ | |
| **インデント/自動整形** | ⚠️ | 詳細設計未言及。Command として `edit.autoIndent`, `edit.format` を Phase 4-7 で規定必要 |
| 文字コード変更 | ✅ | |
| 改行コード変更 | ⚠️ | 判定は記載あるが「変換 Command」の記載不足 |
| BOM切替 | ⚠️ | 同上。Command 化必要 |
| **タブ⇔スペース変換** | ❌ | **設計書に一切なし**。Command 追加必須 |
| 自動保存 | ✅ | |
| **バックアップ** | ⚠️ | 「自動保存」と別概念。**旧ファイル `.bak` 生成** の方針明記なし |
| 履歴 | ⚠️ | セッション履歴 (Recent) と Undo 履歴が混在しがち。分離明記必要 |
| 最近開いたファイル | ⚠️ | SessionManager が扱う想定だが明記なし |

### A-2. 検索機能 (§6)
| 要件 | 判定 | コメント |
|---|---|---|
| 通常検索 | ✅ | |
| **インクリメンタル検索** | ❌ | **明記なし**。SearchService に `findIncremental` API を追加すべき |
| 正規表現 | ✅ | RE2 予定 (ADR 化) |
| Grep | ✅ | |
| 複数フォルダ検索 | ✅ | Grep 相当 |
| **置換 / 複数置換** | ❌ | **設計書に置換 Command なし**。`ReplaceAllCommand`, `ReplaceInFilesCommand` 追加必須 |
| 巨大ファイル検索 | ✅ | |
| **検索履歴** | ⚠️ | ConfigManager 側か SessionManager 側か未定義 |

### A-3. エンコーディング (§6) ✅ 全て充足

### A-4. 対応ファイル (§6)
- 拡張子ベースの Mode 判定が UI Shell / ModeManager にあるが、**具体マッピングテーブル未定義**。→ `config.json5` にデフォルト定義を持たせる方針を明記すべき

### A-5. AI (§7) ✅ 完全プラグイン化・本体独立を明記済

### A-6. ログ解析モード (§8) ✅ 基本設計網羅 (時系列ジャンプ、レベル抽出、色分け、フィルタ、タイムスタンプ解析)

### A-7. CSVモード (§9) ✅ 1000万行対応、列固定/フィルタ/ソート/TSV対応 明記

### A-8. JSON/XML (§10) ✅ Tree/整形/バリデーション/XPath/JSONPath 明記

### A-9. Git (§11) ✅ libgit2 で網羅

### A-10. マクロ (§12)
| 要件 | 判定 | コメント |
|---|---|---|
| Lua | ✅ | |
| JavaScript | ⚠️ | QuickJS "検討" 段階。**要件は必須** → Phase 内で確定必要 |
| **Python** | ❌ | 「プラグイン経由」で片付けたが、**要件は本体機能として列挙**。標準プラグイン同梱を明記すべき |
| 独自マクロ | ⚠️ | 「独自マクロ言語」なのか「マクロ記録機能」なのか要件曖昧 → ユーザー確認必要 (Issue) |
| API 公開 | ✅ | プラグイン API 共通で提供 |

### A-11. プラグイン (§13) ✅ DLL / ホットロード / SDK

### A-12. UI (§14)
- Windows10/11、ダーク/ライト、高DPI、キーボード主体 → ✅
- **日本語最適化** → ⚠️ フォント (Yu Gothic UI / BIZ UDGothic / メイリオ) の優先順序、和文プロポーショナル対応、和文欧文混植の DirectWrite 設定が未記載

### A-13. セキュリティ (§15)
- クラッシュ耐性、自動復旧、自動保存、例外処理 → ✅
- **ログ出力** → ⚠️ ロガーモジュール未設計 (ETW or 自前ローテーティング)
- **署名対応** → ⚠️ プラグイン署名は記述、**本体 exe 署名 (Authenticode)** は未記述

### A-14. 品質 (§16)
- ASan → ✅
- **UBSan** → ⚠️ **MSVC の UBSan サポートは限定的**。clang-cl での併用ビルドを CI に追加すべき

---

## B. 設計整合性チェック

### B-1. マルチウィンドウ vs シングルプロセス
**問題:** 基本設計 §2.3 で「本体プロセス: シングルプロセス、マルチスレッド」と断言しているが、要件は「複数ウィンドウ」を明記。
**対応方針:** シングルプロセスのまま、`MainWindow` を複数インスタンス化する構成 (VS Code / Sublime と同じ) を明記すること。プロセス分離型は起動時間要件 (0.3s) に反するため採用しない。
**修正必要箇所:** `basic_design.md` §2.3, §3.1 の追記

### B-2. LineIndex の重複定義感
`detailed_design.md` §3.1 で `PieceTable` が `LineIndex` を持ち、§3.2 で `LineIndex` 独立クラスとしても定義。**責務境界が曖昧**。
**対応方針:** LineIndex は **Piece Tree 内の順序統計木に埋め込む** (§3.1 記述) で統一し、§3.2 は「クエリ API」のみを提供するファサードとする、と明記。

### B-3. 内部文字型 `char16_t` vs `wchar_t`
`CLAUDE.md` は `std::u16string` (UTF-16) を標準と規定、詳細設計もこれに従う。Win32 API は `wchar_t` (LPCWSTR) を要求するため境界での reinterpret_cast が発生する。**明示的な変換ヘルパを util に用意** することを詳細設計に明記すべき。

### B-4. `std::expected` の可搬性
C++23 の `std::expected` は VS 17.13 以降で完全実装。CI 対象 VS バージョンを Phase 0 で確定させる ADR 起票必要。

### B-5. `import std;`
CLAUDE.md では「段階採用」と記載。まず include ベースで開始、Phase 5-6 頃に切替評価 でよい。

### B-6. RTTI 無効 (`/GR-`) と `dynamic_cast`
詳細設計 §19.1 で `/GR-` を規定。プラグイン境界は C ABI なので影響ないが、**内部で `dynamic_cast` を禁止** することを CLAUDE.md に明記すべき (バーチャル基底 + tag dispatch で代用)。

### B-7. `MVVM 採用しない` の一貫性
CLAUDE.md、基本設計とも MVVM 非採用を明記 → 一貫している。ただし Command パターンは MVVM の一部でもあるため、「Command は Command パターンとして使用、ViewModel は作らない」と補足するとよい。

### B-8. AI キーストア
詳細設計 §21 で「DPAPI + Credential Manager」と記載。**ユーザースコープの DPAPI** で問題ないか、ローカル管理者との共有シナリオを Issue 化。

---

## C. 性能目標達成可能性

### C-1. 起動 0.3s
- **達成可能性:** ▲ (難しいがギリギリ狙える)
- リスク: DirectWrite / Direct2D の COM 初期化コスト、初回グリフキャッシュ生成
- **推奨:** 「空白ウィンドウを 0.15s 以内に表示 → 残初期化は非同期」戦略を追加明記
- **PoC 必須** (Phase 1 完了ゲート)

### C-2. メモリ 20MB
- **達成可能性:** ▲ (プラグインなし・ドキュメントなし状態の指標として)
- リスク: DirectX ランタイムだけで数 MB、DirectWrite フォントキャッシュも数 MB
- **推奨:** 「初期起動」の定義を「空ドキュメント表示直後、GC 相当なし」で明確化。要件書に確認要 → Issue

### C-3. 10GB ファイル
- **達成可能性:** ○ (Memory-mapped + Piece Table で技術的に可能)
- リスク: 改行インデックス構築コスト、UTF-8 な原本の UTF-16 変換コスト (メモリ 2倍)
- **推奨:** **原本は生バイトのまま扱い、必要範囲だけ UTF-16 に変換** する Lazy Decode 方式を採用。詳細設計に追記必要

### C-4. 60fps
- **達成可能性:** ○ (Direct2D + キャッシュで十分達成可能)
- リスク: 選択範囲が大きいとき、複数カーソル 1000 個級の描画
- **推奨:** カーソル数上限を設計値として決定 (例: 10,000)

### C-5. 100万 Undo
- **達成可能性:** ○ (パッキング + 圧縮 + スワップで達成可能)
- リスク: メモリ予算 256MB がユーザー環境で厳しいケース
- **推奨:** 予算を設定で変更可能に (これは detailed に記載済)

### C-6. 数GB検索
- **達成可能性:** ○ (BMH + SIMD で 1GB/s 目安)
- **PoC 必須** (Phase 5)

---

## D. リスク再評価

| # | リスク | 深刻度 | 対応期限 |
|---|---|---|---|
| R1 | 10GB × 20MB の両立 | 高 | Phase 1 PoC |
| R2 | RE2 vs Hyperscan 選定 | 中 | Phase 5 前に ADR |
| R3 | シンタックス自作コスト | 中 | Phase 7 前に TextMate 実装評価 |
| R4 | UBSan MSVC 弱い | 中 | CI に clang-cl ビルド追加 |
| R5 | libgit2 静的リンクのライセンス | 中 | ADR (GPL 例外条項確認) |
| R6 | AI プラグインの API キー漏洩 | 高 | DPAPI + プロセス分離の是非を再検討 |
| R7 | tree-sitter を後入れする際の再設計コスト | 中 | Syntax Engine の内部 IR を tree-sitter 互換に寄せる |
| **R8 (NEW)** | **`std::expected` の VS バージョン依存** | 中 | Phase 0 で最低 VS バージョン確定 |
| **R9 (NEW)** | **複数ウィンドウ設計の後付け** | 中 | 基本設計 §2.3 修正で解消 |
| **R10 (NEW)** | **10GB ファイルの UTF-16 変換メモリ倍加** | 高 | Lazy Decode を詳細設計に追記 |

---

## E. 実装可能性 (フェーズ計画妥当性)

CLAUDE.md §7 のフェーズ計画は妥当だが、以下 2 点調整推奨:

1. **Phase 2 (Document Engine) と Phase 3 (Rendering) を並走** させると、相互のインターフェース調整コストが下がる (別担当なら並列化)
2. **Phase 0.5** を追加: 「ビルドシステム & CI & 静的解析パイプライン整備」。これがないと Phase 1 の性能計測ができない

---

## F. 修正アクション一覧 (対応済み)

### F-1. basic_design.md — 完了 ✅
- [x] §2.3: 複数ウィンドウ = 単一プロセス複数 `MainWindow` インスタンス明記
- [x] §3.1: 行番号 (Line Gutter) レンダラ責務
- [x] §3.4: 日本語フォント優先順 (BIZ UDGothic → Yu Gothic UI → Meiryo) + DirectWrite 和文欧文混植設定
- [x] §6.5 新規: ログ (自前 JSON Lines ローテータ) 方針
- [x] §6.6 新規: 本体 exe Authenticode 署名の方針

### F-2. detailed_design.md — 完了 ✅
- [x] §3.1: **Lazy Decode** (原本は生バイトのまま、範囲限定 UTF-16 変換) の設計
- [x] §5.1.1: 縦編集 (列編集) Command 群 (`ColumnInsert/Delete/Overwrite/Append`)
- [x] §6.1.1: 標準 Command 一覧 (`edit.autoIndent`, `edit.formatDocument`, `edit.tabsToSpaces`, `edit.spacesToTabs`, `file.changeEncoding`, `file.changeLineEnding`, `file.toggleBom`, `file.backup`, `file.recent.open` 等)
- [x] §7: `ReplaceAllCommand`, `ReplaceInFilesCommand`, `IncrementalFindService`
- [x] §22.1: `char16_t` ↔ `wchar_t` 変換ヘルパ (util)
- [x] §15: Python マクロ (標準プラグイン `python_macro.dll`) 方針、キー操作記録マクロ
- [x] §22.2: `dynamic_cast` 禁止方針
- [x] §6.1.2: `.bak` バックアップ生成方針、Recent Files 永続化先

### F-3. CLAUDE.md — 完了 ✅
- [x] §7: **Phase 0.5** (CI/ビルド/静的解析) を追加
- [x] §4 コーディング規約: `dynamic_cast` 禁止、最低 MSVC バージョン明記

### F-4. Issue / ADR — 完了 ✅
- [x] [ADR-001](../decisions/ADR-001-build-system.md): CMake 3.28+ + Ninja + MSVC v143 採用
- [x] [ADR-002](../decisions/ADR-002-regex-engine.md): RE2 単独採用
- [x] [ADR-003](../decisions/ADR-003-syntax-definition.md): TextMate 互換 (tree-sitter は Phase 7 後に評価)
- [x] [ADR-004](../decisions/ADR-004-http-client.md): WinHTTP (AI プラグイン内で完結)
- [x] [ADR-005](../decisions/ADR-005-min-msvc-version.md): VS 2022 17.13+
- [x] Issue: **縦編集 = 列編集 (MIFES 由来)** に確定
- [x] Issue: **独自マクロ = キー操作記録** に確定
- [x] Issue: **初期起動 20MB = 空ドキュメント表示後の Working Set** に確定
- [x] Issue: **マクロ言語同梱範囲 = Lua + JS(QuickJS) + Python(標準プラグイン) + キー記録** に確定
- [ ] Issue: **AI プロセス分離の是非** — Phase 9 前に再評価 (現状は DLL 内)
- [ ] Issue: **libgit2 ライセンス運用** — Phase 11 前に法務確認
- [ ] Issue: **LSP クライアント自作 vs 既存** — Phase 11 で比較評価
- [ ] Issue: **tree-sitter 導入時期** — Phase 7 完了後に評価

---

## G. 総合評価 (v1.1 時点、Phase 0 のスナップショット — 歴史的記録)

> このセクションは Phase 0 完了時点 (2026-07-14) の評価であり、**更新していない**。現在の総合評価は §H・§I を参照。

- **要件カバレッジ:** **100%** (F-1 / F-2 の追記により全項目対応)
- **設計整合性:** B-1 (複数ウィンドウ)、B-2 (LineIndex 重複)、B-3 (`char16_t` 境界) が全て修正済
- **性能達成可能性:** 起動 0.3s が依然最難関。Lazy Decode 導入によりメモリ 20MB 目標のリスクは大幅低減
- **推奨判断:** **Phase 0.5 (CI/ビルド整備) 着手可**。Phase 1 着手前に「起動 0.3s / メモリ 20MB」の PoC ゲートを設ける必要あり。

### G'. 総合評価 (v1.5 現在)

- **進行状況:** Phase 0 〜 Phase 2b2 完了。CI (Debug/Release/clang-tidy) は継続的に green
- **要件カバレッジ:** 100% (設計レベル)。実装は Document Engine (Phase 2) まで完了、Rendering 以降 (Phase 3+) は未着手
- **性能目標の実測状況:**
  - 起動時間: 🟢 CI 実測 22ms (目標 300ms の 7%)
  - `PieceTable::insert`: 🟢 CI 実測 243〜276ns (Release、目標 500ns 未満を達成)
  - `PieceTable::snapshot`: 🟡 **100K piece 規模で実測 1.196ms、目標 1ms を約20%超過** ([`piece_table_rb_tree.md`](../issues/piece_table_rb_tree.md) 参照)。1000 piece 規模からの線形外挿 (0.35ms 予測) は大きく外れており、外挿に頼らず実測することの重要性を示す事例となった。ブロッカーではないが Phase 2b3 完了後に再評価予定
  - メモリ 20MB / 10GB ファイル / 60fps / 100万 Undo: 未実測 (該当実装が Phase 2b3 以降)
- **設計整合性:** Piece Tree の実装形態は ADR-006 → ADR-007 で方針転換したが、Public API は不変のまま実装差し替えで完了 — 当初の「ヘッダは変えない」設計方針が実際に機能した好例
- **推奨判断:** **Phase 2b3 (mmap + Lazy Decode + 1GB bench) 着手可**。着手前に §H の R10/C-1 (UTF-8 チャンク境界分割) を設計に織り込むこと。

## H. 残リスク一覧 (v1.4 Phase 2b2 完了時更新)

| # | リスク | 深刻度 | 対応期限 | 状態 |
|---|---|---|---|---|
| R1 | 起動 0.3s の実現可能性 | 高 | Phase 1 PoC | 🟢 **CI 実測 22ms** (0.3s 目標の 7%)。Phase 3 で Direct2D 化後に再測定 |
| R6 | AI プラグインの API キー漏洩・プロセス分離の是非 | 中 | Phase 9 前に再評価 | 未着手 |
| R10 | Lazy Decode の実装複雑性 (デコードキャッシュ整合性) | 中 | Phase 2b3 で実装 + テスト | 🟡 設計確定 ([`docs/issues/lazy_decode_mmap.md`](../issues/lazy_decode_mmap.md))。**Phase 2b3 が次回の主対象** |
| R11 | Piece Tree の delete 実装複雑性 | 中 | Phase 2b2 | 🟢 **解消**。ADR-006 (path-copying) は実装難度・性能未達リスクにより ADR-007 (mutable RB) に置換。CLRS 13.4 実装 + 20K 反復プロパティテスト + RB invariant テストで検証済み (CI 確認待ち) |
| R12 (NEW) | LineIndex の O(log n) 化不可能と判明 | 低 | 実用上は許容 | 🟡 **仕様として受容**。tree 集約は piece 内改行位置を持たないため原理的に不可。現状の O(N) 再構築 + O(log n) クエリを維持。将来必要になれば [`docs/issues/line_index_o_log_n.md`](../issues/line_index_o_log_n.md) の案で対応 |
| — | libgit2 ライセンス | 低 | Phase 11 前 |
| — | LSP クライアント方式 | 低 | Phase 11 |
| — | tree-sitter 併用時期 | 低 | Phase 7 後 |

## I. 次アクション (v1.1 版、Phase 0 時点 — 歴史的記録、全項目完了済み)

1. ✅ **ユーザー確認完了** (縦編集/独自マクロ/マクロ言語/ビルド、および正規表現/シンタックス/設定形式/20MB計測)
2. ✅ **設計書修正完了** (F-1〜F-3)
3. ✅ **ADR-001〜005 発行完了**
4. ✅ Phase 0.5 完了 — CMake / GitHub Actions / clang-tidy / ASan / googletest / google-benchmark 全て稼働
5. ✅ Phase 1 完了 — 起動 0.3s / メモリ 20MB は独立 PoC ドキュメント新設ではなく `--measure-startup`/`--measure-memory` CLI フラグとして実装 (`docs/phase_reports/phase_1_report.md` 参照。当時想定していた `docs/pocs/` ディレクトリは新設しなかった)

### I'. 次アクション (v1.5 現在)

1. **次:** Phase 2b3 (OriginalBuffer mmap + Lazy Decode + 1GB load bench) — 詳細は [`RESUME_HERE.md`](../handoff/RESUME_HERE.md) 参照
2. Phase 2b3 着手前に `document_piece_table_bench.cpp` へ 100K piece 規模のケースを追加し、snapshot 目標を外挿でなく実測で検証する
3. Phase 2b 完了時点で `docs/phase_reports/phase_2b_report.md` を1本発行し、2b1〜2b3 をまとめて総括する (CLAUDE.md §11 のフェーズレポート運用ルール参照)
