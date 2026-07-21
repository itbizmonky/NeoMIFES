# NeoMIFES マスターロードマップ v2.0

> 対象要件: [`NeoMIFES_要件定義書.md`](../../NeoMIFES_要件定義書.md) v1.0
> 上位ガイド: [`CLAUDE.md`](../../CLAUDE.md) / 基本設計: [`basic_design.md`](basic_design.md) / 実装詳細: [`detailed_design.md`](detailed_design.md)
> 発行: v1.0 = 2026-07-19 / **v2.0 = 2026-07-19 (Google/MS 責任者視点の徹底レビュー反映、23章構成、UI/UX・世界最速・アクセシビリティ・セキュリティ・エコシステム・開発品質基盤を全面拡充)**

本書は Phase 4b8・5b2・5b3・5c・6〜12 の **実装着手時に迷わない詳細設計** を一気通貫で規定する Plan-of-Record。個別フェーズ着手時に本書の該当章をベースに詳細プランを Plan Mode で起こし、実装後は `detailed_design.md` の対応節へ確定内容を吸収する。

---

## 0. 位置づけ・関連文書

### 0.1 なぜ本書が必要か

Phase 5b 着手時点で「フェーズごとに実装内容が未確定」というブレが顕在化した。要件定義書 §20 の最終目標を一気通貫で写像し、各フェーズの成果物・凌駕ポイント・妥協点を先に確定させることで、実装セッションごとの判断ゆらぎと 「完成に近づいているか」の再確認コストを排除する。**v2.0** は Google/MS のエンジニアリング責任者視点の徹底レビューを反映し、「良いとこ取り」の網羅性・世界最高峰 UI/UX の裏付け・世界最高速の技術要素・エコシステム・アクセシビリティ・セキュリティを全面補強した。

### 0.2 本書と他文書の役割分担

| 文書 | 責務 | 更新タイミング |
|---|---|---|
| 要件定義書 | 何を作るか (What) | v1.0 凍結 |
| 基本設計書 | どういうレイヤ構成か (Structure) | 破壊的変更時のみ |
| **本書 (マスターロードマップ)** | **各フェーズで何をどう作るか (Plan-of-record)** | **各フェーズ完了時に「実装差分」を確定内容として吸収** |
| 詳細設計書 | 実装済み機能のリファレンス (Reference) | フェーズ完了時に本書から吸収 |
| ADR | 個別技術判断の記録 | 判断発生時 |
| RESUME_HERE / TIMELINE | セッション間の受け渡し・時系列 | 各セッション |

### 0.3 更新運用

- **各フェーズ着手前:** 本書の該当章を読み、Plan Mode でセッション個別の詳細プランを起こす
- **各フェーズ完了時:** 実装で確定した詳細を `detailed_design.md` の対応節に吸収し、本書の該当章末尾に「実装後の確定事項/変更点」を追記する
- **凍結セクション:** 本書は「実装前の計画」を残し続けるための文書。実装で確定した内容は詳細設計書側の一次情報となり、本書は歴史的計画として残る

---

## 1. 完成イメージ

### 1.1 一言で

「秀丸の軽さ・MIFES の操作性・サクラの拡張性を、モダン C++23 と Direct2D で書き直した、AI 時代の Windows 標準テキストエディタ」。プログラマ・SE・インフラ運用・技術ライター・SAP コンサル全員の第一選択になる、Windows で最速・最軽量・最も日本語に強く、AI 時代に唯一プラガブルなネイティブエディタ。

### 1.2 差別化される 10 の体験

1. **起動 ≤ 300ms・初期メモリ ≤ 20MB** — Chromium 系エディタと同居でも常駐可
2. **10GB ファイルを 60fps でスクロール** — mmap + Piece Table + Direct2D + Frame pacing
3. **数十 GB ログを ERROR/WARNING 抽出しながら時系列ジャンプ** — 本ソフト最大の差別化点 (Phase 10、12種の組込パターン + カスタム)
4. **完全キーボード完結の操作性** — 秀丸/サクラ/MIFES 互換のプリセット、全機能にキーバインド、Vim/Emacs 互換モードも Phase 8 のプラグイン境界で提供可能
5. **AI 統合が完全プラグイン境界** — AI 無効時は 100% オフライン動作、API キーは Windows Credential Manager (DPAPI) 経由で暗号化。Copilot 型ゴーストテキスト補完 + インラインチャット + RAG + マルチモデル比較を一貫 UX で
6. **CJK IME 一級市民** — DirectWrite Text Analyzer + IME 変換中インライン + grapheme cluster 単位カーソル移動。中韓 IME も同等品質
7. **世界最強の複数カーソル・矩形選択** — VSCode を超える視覚フィードバック、矩形と複数カーソルのシームレス変換、N対N クリップボード分配
8. **Grep 数 GB/s** — Search Worker Pool の完全並列、Piece Table のチャンク単位並列走査、SIMD (SSE4.2/AVX2/AVX-512) 動的 dispatch
9. **プラグインエコシステム** — C ABI + hot-load + サンドボックス、公式マーケットプレース、SDK 完備。VSCode 拡張市場に対抗する Windows ネイティブエコシステム
10. **透明性・プライバシー・アクセシビリティ最上級** — WCAG 2.2 準拠、スクリーンリーダ完全対応、テレメトリは全て opt-in で内容非記録、オープンな脆弱性開示プロセス

### 1.3 ペルソナと利用シーン

Google/MS 流の Persona-Driven Development に従い、v2.0 で明示的に定義する。設計判断の紛争時 (性能 vs. 機能、UI 密度 vs. 学習コスト等) はこのペルソナに立ち返って決める。

| ペルソナ | 主要利用シーン | このペルソナが求める核 |
|---|---|---|
| **P1: 中堅 SAP コンサル (40 代・日本)** | 数 GB の SAP トランザクションログを開き、`ERROR` 抽出 → 時系列で原因特定。CSV での MDM (Master Data Management) データ確認。SAPScript 編集 | **ログ解析モード** (Phase 10) / **巨大ファイル** / **Shift-JIS の完全対応** / **信頼性** (クラッシュしない) |
| **P2: Windows インフラ運用エンジニア (30 代)** | Windows Event Log の Text エクスポート数百 MB を開き、パターン抽出。PowerShell スクリプトを LSP 補完付きで編集。Grep で監査ログ横断 | **ログ解析モード** / **PowerShell シンタックス+LSP** (Phase 7+11) / **Grep 高速** (Phase 5c) |
| **P3: Web 開発者 (20-30 代、VSCode ユーザー)** | TypeScript/React 開発。LSP 補完・診断が必須。マルチカーソル、コマンドパレット、テーマは VSCode 相当を期待 | **LSP** (Phase 11) / **モダン UI/UX** (§13 全体) / **AI 補完 (Copilot 相当)** (Phase 9) / **軽量** (VSCode より速い理由が明確) |
| **P4: SE / 技術ライター (Markdown・技術文書執筆)** | Markdown 執筆、コードスニペット埋込、AI 校正、翻訳。全角/半角混在、ATOK/MS-IME 使用 | **CJK IME 一級市民** / **AI 校正** (Phase 9) / **アウトライン** (Phase 7) / **Zen mode** (§13.5) |
| **P5: OSS 開発者 (C++/Rust)** | 大規模 C++/Rust コードベース。定義ジャンプ、リファクタ、Git ブレーム。Vim キーバインド希望 | **LSP + Git 統合** (Phase 11) / **プラグイン (Vim モード)** (Phase 8) / **高速検索** (Phase 5c) |
| **P6: エンタープライズ管理者 (100 台以上の展開)** | サイレントインストール、ポリシー配布、テレメトリオフ、署名検証必須プラグイン | **署名検証** (Phase 8) / **MSIX + サイレント展開** (§18) / **テレメトリ opt-in** (§19) |
| **P7: エディタホッパー (秀丸/サクラ/MIFES ユーザー)** | 慣れたキーバインドで即使いたい。マクロ資産の互換性 | **プリセット** (§13.1) / **マクロ移行支援** (Phase 11) / **秀丸互換 grep 結果ペイン** (Phase 5c) |

**非ペルソナ (明示的にターゲット外):** macOS/Linux ユーザー、クラウドリアルタイム協調編集ユーザー、Web IDE ユーザー、専用 IDE 依存ユーザー (Xcode/Android Studio)。

### 1.4 競合ポジショニング

競合を「機能一致」ではなく「起動速度 × 巨大ファイル × ネイティブ度」の 3 軸で位置づけ。NeoMIFES は 3 軸全てで既存より上位を狙う。

| 競合 | 起動速度 | 巨大ファイル (>1GB) | ネイティブ度 (メモリ・応答性) | AI | プラグイン | LSP | 日本語 | NeoMIFES との差別化 |
|---|---|---|---|---|---|---|---|---|
| **VSCode** | 遅 (2-5秒) | ×〜△ | ×〜△ (Electron) | ◎ (Copilot) | ◎ | ◎ | ◎ | **軽さ・巨大ファイル・完全ネイティブ** |
| **Sublime Text** | 速 | △ | ○ (自作 GUI) | × | ○ | ○ | △ | **AI・日本語・LSP・完全性・巨大ファイル** |
| **Notepad++** | 速 | △ | ◎ | × | △ | × | ○ | **AI・LSP・巨大ファイル・モダン UI・複数カーソル** |
| **UltraEdit** | ○ | ◎ | ○ | △ | △ | △ | ○ | **AI・モダン UI・OSS ライク エコシステム** |
| **秀丸エディタ** | ◎ | ○ | ◎ | × | ○ (独自マクロ) | × | ◎ | **AI・LSP・モダン UI・OSS ライク・複数カーソル・Git** |
| **サクラエディタ** | ○ | △ | ○ | × | ○ (JS) | × | ◎ | **AI・LSP・モダン UI・大規模ファイル・Direct2D 描画** |
| **MIFES** | ◎ | ◎ | ◎ | × | △ | × | ◎ | **AI・LSP・モダン UI・複数カーソル・Git・プラグイン** |
| **Vim/Neovim** | ◎ | ○ | ◎ | ○ (プラグイン) | ◎ | ◎ | △ | **GUI・学習曲線・日本語・箱を開けたら使える** |
| **Emacs** | 遅 | △ | ○ | ○ (プラグイン) | ◎ | ◎ | ○ | **軽さ・箱を開けたら使える・モダン UI** |

**「NeoMIFES を選ぶ理由の一言」:** 「秀丸の軽さ + MIFES の巨大ファイル力 + サクラのカスタマイズ + VSCode のモダン UI + Copilot 相当の AI + Windows 完全ネイティブ、を全て備えた唯一のエディタ」。

### 1.5 三大エディタからの継承マトリクス (60 機能精査版)

v1.0 の 17 機能を精査し、実際に三大エディタが備える「拾うべき機能」を 60 に細分化。抜けていた秀丸のキーマクロ、サクラのフリーカーソル、MIFES の桁位置ジャンプ等を全て組み込む。

#### A. 編集・入力系

| 機能 | 秀丸 | サクラ | MIFES | 対応 Phase |
|---|---|---|---|---|
| 複数カーソル | ✕ | △ | ✕ | 4b (完了、既に凌駕) |
| 矩形選択 | ○ | ○ | ◎ | 4b8 |
| 縦編集 (縦書き入力) | ○ | ○ | ◎ | 4b8 (研究) |
| フリーカーソル (虚数位置) | ✕ | ◎ | ○ | 4b8 (拡張) |
| タブ⇔スペース変換 | ○ | ○ | ◎ | 4b8 |
| 自動インデント | ○ | ○ | ○ | 4b8 |
| 桁位置ジャンプ | ○ | ○ | ◎ | 4b8 |
| マーカー (Bookmark) | ○ | ○ | ◎ | 4b8 |
| キーマクロ記録・再生 | ◎ | ○ | ○ | Phase 11.3 |
| 文字列一括変換 (URL/HTML/日付) | ○ | ◎ | ○ | Phase 8 (マクロ標準ライブラリ) |

#### B. 検索・置換系

| 機能 | 秀丸 | サクラ | MIFES | 対応 Phase |
|---|---|---|---|---|
| 通常検索 (実装) | ◎ | ○ | ○ | 5a (完了) |
| 複数行マッチ | ◎ | ○ | ○ | 5b1 (完了) |
| 置換 | ○ | ○ | ○ | 5b2 |
| キャプチャグループ ($1..) | ○ | ○ | ○ | 5b2 |
| インクリメンタル検索 | ○ | ○ | ○ | 5b3 |
| Find bar UI | ○ | ○ | ○ | 5b3 |
| **grep** | ◎ | ○ | ○ | 5c |
| **grep 結果からジャンプ** | ◎ | ○ | ○ | 5c |
| **grep 結果一括置換** | ◎ | △ | △ | 5c |
| 複数フォルダ検索 | ○ | ○ | ○ | 5c |
| **検索履歴・置換履歴** | ○ | ○ | ○ | 5b3 |
| **キーワード強調表示 (常時)** | ○ | ◎ | ○ | Phase 7 (連携) |
| **タグジャンプ** (`file.txt(123)` から) | ○ | ○ | ○ | 5c (Grep 結果パーサ) |

#### C. ファイル・エンコーディング系

| 機能 | 秀丸 | サクラ | MIFES | 対応 Phase |
|---|---|---|---|---|
| UTF-8/16/32 対応 | ○ | ○ | ○ | Phase 6 |
| Shift-JIS/EUC-JP/ISO-2022-JP | ○ | ○ | ○ | Phase 6 |
| BOM 切替 | ○ | ○ | ○ | Phase 6 |
| 自動判定 | ○ | ○ | ○ | Phase 6 |
| 改行コード CRLF/LF/CR 切替 | ○ | ○ | ○ | Phase 6 |
| 巨大ファイル (>1GB) | △ | △ | ◎ | Phase 6 (mmap) |
| **10GB ファイル** | ✕ | ✕ | △ | Phase 6 (差別化) |
| 自動保存・バックアップ | ○ | ○ | ○ | Phase 12 |
| 履歴 (最近開いたファイル) | ○ | ○ | ○ | Phase 12 |

#### D. 表示・UI 系

| 機能 | 秀丸 | サクラ | MIFES | 対応 Phase |
|---|---|---|---|---|
| シンタックスハイライト | ○ | ○ | ○ | Phase 7 |
| アウトライン | ◎ | ○ | ○ | Phase 7 |
| 折り畳み | ○ | ○ | ○ | Phase 7 |
| 行番号 | ○ | ○ | ○ | Phase 3 (基本、拡張は Phase 7) |
| ブックマーク列 (行番号左) | ◎ | ○ | ○ | 4b8 |
| **ミニマップ** | ✕ | ✕ | ✕ | §13.3 (差別化) |
| **Breadcrumb** | ✕ | ✕ | ✕ | §13.4 (差別化) |
| **Sticky scroll** | ✕ | ✕ | ✕ | §13.4 (差別化) |
| **Indent guides** | ○ | ○ | ○ | §13.4 |
| **タブ UI (複数ファイル)** | ○ | ○ | ○ | §13.5 |
| **タブグループ・ピン留め** | △ | ○ | △ | §13.5 (差別化) |
| **分割ビュー (画面分割)** | ○ | ○ | ○ | §13.5 |
| **Zen mode (集中モード)** | ✕ | ✕ | ✕ | §13.5 (差別化) |
| ダーク/ライトテーマ | ○ | ○ | ○ | §13.6 |
| 高 DPI | ○ | ○ | ○ | §13.7 (完了) |
| **HDR / 広色域** | ✕ | ✕ | ✕ | §13.7 (差別化) |
| Mica/Acrylic 半透明 | ✕ | ✕ | ✕ | §13.6 (差別化) |
| ステータスバー | ○ | ◎ | ○ | Phase 3+ |
| **コマンドパレット (Ctrl+Shift+P)** | ✕ | ✕ | ✕ | §13.2 (差別化) |
| 日本語フォント最適化 | ○ | ○ | ◎ | Phase 3 (完了) |

#### E. 開発者向け系

| 機能 | 秀丸 | サクラ | MIFES | 対応 Phase |
|---|---|---|---|---|
| **LSP (補完/定義ジャンプ/診断)** | ✕ | △ | ✕ | Phase 11.2 (差別化) |
| **AI 補完 (Copilot 型)** | ✕ | ✕ | ✕ | Phase 9 (差別化) |
| **AI インラインチャット** | ✕ | ✕ | ✕ | Phase 9 (差別化) |
| **AI エージェント (RAG)** | ✕ | ✕ | ✕ | Phase 9 (差別化) |
| DIFF ビュー | ○ | ✕ | ○ | Phase 11.1 |
| Git 統合 (Diff/Blame/Merge) | △ | ✕ | △ | Phase 11.1 (差別化) |
| 3-Way Merge | ✕ | ✕ | ✕ | Phase 11.1 (差別化) |
| ファイル比較 (2ファイル) | ○ | ✕ | ○ | Phase 11.1 |
| CSV Grid ビュー | △ | ○ | ✕ | Phase 10.2 |
| **JSON/XML Tree ビュー** | ✕ | ✕ | ✕ | Phase 10.3 (差別化) |
| **ログ解析モード (自動色分け・フィルタ)** | △ | △ | △ | Phase 10.1 (最大差別化) |
| プラグイン (DLL) | ○ | ○ | △ | Phase 8 |
| **プラグインマーケットプレース** | ✕ | ✕ | ✕ | §20 (差別化) |
| マクロ (独自言語) | ◎ (秀丸マクロ) | ○ (WSH) | ○ | Phase 11.3 (Lua+JS+互換レイヤ) |

**「差別化」ラベルの意味:** その機能は三大エディタ全てが未実装、または実装が極めて限定的で、NeoMIFES が明確に上を行ける領域。**総計 22 の差別化点**。

---

## 2. 全フェーズ俯瞰

| Phase | 内容 | 状態 | 本書該当章 |
|---|---|---|---|
| 0 | 要件・設計 | ✅ 完了 | — |
| 0.5 | ビルド基盤 | ✅ 完了 | — |
| 1 | Win32 骨組み | ✅ 完了 (起動 148ms 実測) | — |
| 2a/2b | Document Engine | ✅ 完了 | — |
| 3a/3b/3c | Rendering | ✅ 完了 (60fps DoD 達成) | — |
| 4a〜4b7 | Editor Core | ✅ 完了 (100 万 Undo DoD 達成) | — |
| 4b8 | 矩形選択・桁位置ジャンプ・マーカー・タブ変換・フリーカーソル・N対N分配・キーボード矩形拡張 (4b8a〜4b8g) | ✅ 完了 | §3 |
| 5a | Search Engine 基盤 | ✅ 完了 | — |
| 5b1 | 複数行マッチ対応 | ✅ 完了 | — |
| 5b2 | 置換 (ReplaceAllCommand + キャプチャ) | ✅ 完了 | §4 |
| 5b3 | Find bar UI + 置換行配線 + コマンドパレット (5b3a/5b3b/5b3c) | ✅ 完了 | §5 |
| 5c1 | GrepService コア (ヘッドレス多ファイル検索) | ✅ 完了 | §5.5 |
| 5c2 | 実行時ファイルを開く機能 (openDocumentAt、ヘッドレス) | ✅ 完了 | §5.5 |
| 5c3 | Grep結果ペインUI (Ctrl+Shift+F、GrepBar) | ✅ 完了 | §5.5 |
| 5c4 | タグジャンプ (F12、tag_jump_parser) | ✅ 完了 | §5.5 |
| 5c5 | 検索履歴永続化 (SearchHistory、Find bar + Grep共有) | ✅ 完了 | §5.5 |
| 6a | Encoding Engine コア (Unicodeファミリー、ヘッドレス) | ✅ 完了 | §6 |
| 6b1 | Shift-JIS/EUC-JPコーデック (Win32ネイティブ変換ラッパー) | ✅ 完了 | §6 |
| 6c1 | 自動判定 (BOM/UTF-8/Shift-JIS/EUC-JP判別、ISO-2022-JP検出は保留) | ✅ 完了 | §6 |
| 6c2 | 行末コード判定 (LineEnding: Crlf/Lf/Cr/Mixed) | ✅ 完了 | §6 |
| 6b2 | ISO-2022-JPコーデック (CP50220、EUC-JP代理オラクル) | ✅ 完了 | §6 |
| 6d | Document/OriginalBuffer統合・10GB mmap一般化 | ✅ 完了 | §6 |
| 7 | シンタックス + アウトライン + 折り畳み + ミニマップ + breadcrumb + sticky scroll | 未着手 | §7 |
| 8 | プラグインエンジン + SDK + サンドボックス | 未着手 | §8 |
| 9 | AI プラグイン (Claude + Copilot 型補完 + RAG) | 未着手 | §9 |
| 10 | ログ解析 / CSV / JSON-XML tree | 未着手 | §10 |
| 11 | Git / LSP / マクロ (Lua + JS + 秀丸互換レイヤ) | 未着手 | §11 |
| 12 | 総合品質保証 + 出荷 | 未着手 | §12 |

---

## 3. Phase 4b8 — 矩形選択・タブ⇔スペース・N対N分配・フリーカーソル・マーカー・桁位置ジャンプ

v2.0 追加機能: フリーカーソル (虚数位置)、マーカー (Bookmark)、桁位置ジャンプ、ブックマーク列。

### 3.1 機能ビジョン
- **凌駕元:** MIFES (矩形編集・桁位置ジャンプ・マーカーの元祖)、サクラ (フリーカーソル)、VSCode/Sublime (現代版複数カーソル)
- **凌駕ポイント:**
  - 既存の複数カーソル基盤 (`SelectionModel` / `MultiCursorEditCommand`) の上に「矩形 = 各行 1 カーソルの集合」というモデルで実装、矩形と複数カーソルをシームレス切替
  - **フリーカーソル** — 行末より右にカーソルを置ける「虚数位置」概念。次の文字入力で自動的にスペースが挿入される。三大エディタでも唯一サクラのみ実装、我々は仮想空白の視覚化と組み合わせて MIFES ライクな桁位置ジャンプの下地にする
  - **マーカー (Bookmark) を Ctrl+F2 で行に付与、F2 でジャンプ**、複数マーカー間の巡回。VSCode の Bookmark 拡張相当を標準機能で
  - N対N クリップボード分配は VSCode 互換に留めず、**「N ↔ M の場合の分配ルール」を設定可能** (行余りは最初の N 行に順次貼り付け、または全カーソルにサイクル貼り付けを選択可)

### 3.2 UI/UX

**キーバインド (デフォルト):**
- `Alt + LMouse ドラッグ` / `Alt + Shift + カーソル移動` — 矩形選択開始・拡大
- `Shift + Alt + I` — 選択範囲を「各行末尾に 1 カーソル」に変換 (VSCode 互換)
- `Ctrl + F2` — 現在行にマーカー付与、`F2` / `Shift+F2` — 次/前マーカーへジャンプ
- `Ctrl + G` — 行番号ジャンプ (`123` 単独) / **桁位置ジャンプ** (`123:45` = 123行45桁)
- `Ctrl+Shift+P` → `Convert Tabs to Spaces` / `Convert Spaces to Tabs` / `Convert Indentation to Tabs` / `Convert Indentation to Spaces`
- `Ctrl + C / Ctrl + V` — 矩形/複数カーソルの N対N分配

**フリーカーソル モード:**
- 設定で有効化 (デフォルト OFF、秀丸/サクラユーザー向けオプション)
- 行末以降をクリック → その仮想位置にカーソル
- 文字入力時、仮想位置と行末の間を自動空白で埋めた上で挿入

**視覚要素 (矩形選択):**
```
   1  int foo = [BAR         ]12345;    // BAR = 矩形選択、[ ] は選択範囲
   2  int qu  = [BAZ]xxxxxxxxxx;        // 短い行は右側の薄い矩形が「仮想空白」
   3  int abc = [FOOBAR    ]789;        //
```

**視覚要素 (マーカー・ブックマーク列):**
```
     ● 1  // マーカー付与行 (● = 赤丸)
       2
     ● 3
       4  // ここへ F2 でジャンプ、次の ● までワンショット
     ● 5
```

### 3.3 データ構造・アルゴリズム

**矩形選択の内部表現:**
```cpp
// core/selection_model.h に追加:
enum class SelectionMode { Normal, Rectangular };

class SelectionModel {
public:
    [[nodiscard]] SelectionMode mode() const noexcept { return m_mode; }
    void setRectangularSelection(TextPos anchor, TextPos active) noexcept;
    // → 内部で anchor.line〜active.line の各行に 1 カーソル生成、
    //   各カーソルの anchor.column = min(anchor.col, active.col)、
    //   active.column = max(anchor.col, active.col) を設定

    // フリーカーソル (Phase 4b8):
    void setFreeCursorEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isFreeCursorEnabled() const noexcept;

private:
    SelectionMode m_mode = SelectionMode::Normal;
    bool          m_freeCursorEnabled = false;
};

// 仮想列位置 (フリーカーソル対応):
struct TextPos {
    LineIndex   line;
    ColumnIndex column;      // UTF-16 CU 単位
    // v2.0: フリーカーソル時のみ、実文字数を超える値を持つことを許容
};
```

**マーカー (Bookmark):**
```cpp
// core/bookmark_manager.{h,cpp} (新規)
class BookmarkManager {
public:
    void toggle(LineIndex line);
    [[nodiscard]] std::optional<LineIndex> next(LineIndex from) const;
    [[nodiscard]] std::optional<LineIndex> previous(LineIndex from) const;
    [[nodiscard]] std::span<const LineIndex> lines() const noexcept;
    // ドキュメント編集時のマーカー追従: EditEvent を購読、行の挿入/削除に応じてマーカー位置を補正

private:
    std::vector<LineIndex> m_lines;   // ソート済み維持
};
```

**タブ⇔スペース変換:**
- 新規 Command: `ConvertIndentationCommand { enum class Target { TabsToSpaces, SpacesToTabs, Auto }; int tabWidth; TextRange scope; };`
- 実装は各行の先頭連続空白を計算 → 変換後文字列 → 複数の `TextEdit` を発行 (既存 Undo 基盤を再利用)
- `Auto` はドキュメント統計で多数派を採用 (行数比 8:2 以上で偏っている場合のみ変換)

**N対N分配クリップボード:**
- `ClipboardService` (新規 `src/core/clipboard_service.{h,cpp}`)
- コピー時: `N` カーソル → `N` 行を `\r\n` で結合 + カスタムフォーマット `CF_NEOMIFES_MULTICURSOR`
- 貼り付け時: 行数 = カーソル数 → 分配、行数 < カーソル数 → **設定で「サイクル貼り付け」** or **「余ったカーソルには空文字」** を選択可 (VSCode を超える柔軟性)、行数 > カーソル数 → 全て 1 カーソルに (VSCode 互換)

### 3.4 性能目標
- 矩形選択作成 (1000 行): ≤ 5ms
- タブ⇔スペース変換 (100000 行): ≤ 100ms
- 10 万カーソル貼り付け: ≤ 200ms
- マーカー追加/削除: ≤ 1ms
- マーカー付き 100 万行編集時のマーカー追従: ≤ 10ms/編集

### 3.5 テスト戦略
- 単体: 矩形範囲 anchor/active swap、仮想空白挿入、N対N の行数不一致 (3 モード)、フリーカーソルの仮想列保持
- 統合: 矩形選択 → Ctrl+C → 別ドキュメントで Ctrl+V → 一致
- Undo/Redo: 矩形挿入・矩形削除・マーカー追加を含む完全逆操作
- 回帰: 既存の複数カーソル (Phase 4b6) が挙動を変えない

### 3.6 影響ファイル (想定)
- **新規:** `src/core/{clipboard_service.{h,cpp}, convert_indentation_command.{h,cpp}, bookmark_manager.{h,cpp}, goto_line_column.{h,cpp}}`
- **変更:** `src/core/selection_model.{h,cpp}` (Rectangular mode, free cursor)、`src/core/multi_cursor_edit_command.cpp` (仮想空白パディング)、`src/ui/main_window.cpp` (Alt+マウス/Alt+Shift+矢印/Ctrl+F2/F2/Ctrl+G のフック)、`src/render/render_pipeline.cpp` (仮想空白の薄い塗り、マーカー列描画)
- **新規テスト:** `tests/unit/core_{rectangular_selection,convert_indentation,bookmark_manager,free_cursor}_test.cpp`、`tests/integration/clipboard_multi_cursor_test.cpp`

### 3.7 実装後の確定事項/変更点 (2026-07-20、Phase 4b8 全サブフェーズ完了)

**§3全体は矩形選択・フリーカーソル・マーカー・桁位置ジャンプ・タブ⇔スペース変換・N対N分配クリップボードの6機能を1章にまとめていたが、実装は5b3同様サブフェーズ(4b8a〜4b8g)へ分割した。全サブフェーズが完了し、Phase 4b8はroadmap上の保留項目を残さず完全に完了した。**

**4b8a (矩形選択の基本機能、2026-07-19完了時点の記録):**
- **キーバインドを`Alt+LMouseドラッグ`から`Shift+Alt+ドラッグ`へ変更(roadmapスケッチから乖離)。** §3.2策定時点では気づかれていなかったが、`Alt+LMouseドラッグ`は既にPhase 4b6dで「直前のAlt+クリックで追加したカーソルを拡張する」ジェスチャーとして使用済みであることが4b8a着手時に判明。実装前にAskUserQuestionでユーザーに確認し、VSCodeの実際の慣習(Alt+クリック=カーソル追加、Shift+Alt+ドラッグ=矩形選択)に合わせる方針で解決。既存のAlt+ドラッグ挙動は無変更のまま維持
- **`SelectionMode`列挙体は採用しなかった。** §3.3のスケッチは`SelectionMode::{Normal,Rectangular}`を導入する想定だったが、既存`SelectionModel::moveAll()`がカーソル集合へ一様適用される設計のおかげで、矩形選択後の矢印キー操作がVSCode同様「N個の独立カーソルへ降格」する挙動を新規コード無しで得られたため、モード概念自体が不要と判明
- **設計検証で2ラウンドのPlan agentレビューを実施し、いずれも実装着手前に重大な不具合を検出・修正した。** 1件目は`setRectangularSelection()`のposition/anchor取り違えバグ(ドラッグがanchorの列を跨ぐとキャレットが視覚的に後退する)、2件目はマウス配線の状態管理不備(既存`altCursorAnchor`との相互作用で矩形選択が乗っ取られる/次のジェスチャーが空振りする)。詳細は`detailed_design.md` §5.3の追記、`docs/history/TIMELINE.md`のセッション記録参照

**4b8b (桁位置ジャンプ):**
- `Ctrl+G`で`ui::GotoLineBar`(単一WC_EDITWのみ、デバウンス・リストボックス不要)を表示。`ui::parseGotoLineInput()`が`"123"`(行のみ)/`"123:45"`(行:桁、共に1始まり)をパースし、`jumpToGotoTarget()`が0始まりへ変換してクランプ

**4b8c (マーカー):**
- **`BookmarkManager`はドキュメント編集(行の挿入/削除)へのマーカー追従を実装しなかった(roadmapスケッチの「EditEventを購読」から乖離)。** 本コードベースにはドキュメント編集イベントを購読する仕組みが存在しない(`Document`は`version()`ポーリングのみ、ADR-010)ため、追従機能自体が本コードベースの既存アーキテクチャでは実現できない既知の制約として明記した
- **マーカーの視覚表示は「行番号・折りたたみを含む本格的なLine Gutter」ではなく、最小限のブックマーク専用ガター(●印のみ、`kGutterWidthDips=24dip`)を新設した。** AskUserQuestionでユーザーに確認済み(本格的なLine Gutterは別途独立した将来フェーズへ意図的に先送り継続)
- **設計検証でPlan agentレビューを実施し、実装着手前にD2D/DirectWriteの座標系バグを検出・修正した。** `IDWriteTextLayout::HitTestTextPosition()`が返すX座標は`DrawTextLayout()`の描画原点とは独立したレイアウトローカル座標であるため、ガター幅ぶんの原点シフトが`drawCaretOnLine`/`drawSelectionOnLine`/`drawMatchOnLine`の3メソッドへ自動反映されない。実装前に全メソッドへ`kGutterWidthDips`の明示的加算を追加して対処

**4b8d (タブ⇔スペース変換):**
- **`ConvertIndentationCommand`という専用コマンドクラスは新設しなかった(roadmapスケッチから乖離)。** `core::computeIndentationConversionEdits()`というヘッドレス純粋関数のみを新設し、その結果を既存`core::ReplaceAllCommand`(Phase 5b2)へそのまま渡す設計とした。「N個の独立したrange-replace編集を1つのUndoステップで」という既存汎用設計を再利用でき、新規コマンドクラスの独自apply/undoロジックが不要だったため
- `Auto`モード(ドキュメント統計で多数派を自動採用)は実装しなかった。設定システムが存在しないため、`tabWidth=4`固定でコマンドパレットに"Convert Tabs to Spaces"/"Convert Spaces to Tabs"の2エントリのみ追加

**4b8e (フリーカーソル):**
- **`TextPos`/`ColumnIndex`の拡張(roadmapスケッチの「実文字数を超える値を許容」)は行わなかった。** `TextPos`は176箇所・28ファイルで使われており拡張は大規模変更になるため、AskUserQuestionでユーザーに確認の上、`document::TextPos`自体は変更せずmain.cpp(UI層)のみで仮想列オフセットを追跡する簡略実装とした。単一プライマリカーソル・キーボードのみが対象で、マウスでの行末より右クリックは対象外
- 視覚要素(roadmapモックアップの「短い行は右側の薄い矩形が仮想空白」)は実装しなかった。`render::CursorVisual::virtualColumnOffset`によるキャレット位置シフトのみ(等幅フォント前提の近似)

**4b8f (N対N分配クリップボード):**
- **カスタムクリップボードフォーマット`CF_NEOMIFES_MULTICURSOR`、および「サイクル貼り付け」等の高度な分配ルール設定は実装しなかった(roadmapスケッチから乖離)。** 設定システムが存在しないため、VSCode等の実際の既定動作である「チャンク数とカーソル数が一致する場合のみ1対1分配、それ以外は全カーソルへ同一テキスト」のみを実装。`ClipboardService`という専用クラスも新設せず、既存`handlePaste()`(`src/app/editor_input.cpp`)の変更のみで対応

**4b8g (キーボード矩形選択拡張 + Shift+Alt+I):**
- `MainWindow`に`onSysKeyDown`フック(`WM_SYSKEYDOWN`)を新設。未消費時は必ず`DefWindowProcW`へフォールスルーし、Alt+F4等のシステムキー既定動作を保持
- `SelectionModel`のprivate`moveOne()`を公開自由関数`moveTextPos()`へ格上げし、`Shift+Alt+矢印`ハンドラがPhase 4b8aの`rectangularAnchor`状態を再利用して`setRectangularSelection()`を呼ぶことで、マウスとキーボードの矩形選択拡張が同じ状態変数を共有
- `SelectionModel::convertToLineEndCursors()`を新設し、`Shift+Alt+I`で選択範囲を各行末尾の1カーソルへ変換
- **既知の制約:** キーボードでの矩形拡大は「短い行を経由した後の元の意図列」を記憶しない(通常の垂直移動が持つ列保持とは異なる簡略実装)

---

## 4. Phase 5b2 — 置換 (ReplaceAllCommand + キャプチャ + Preview)

### 4.1 機能ビジョン
- **凌駕元:** サクラ・秀丸の「全置換」、VSCode の Regex Replace Preview
- **凌駕ポイント:** 100 万件置換を 1 個の Undo/Redo エントリで戻せる (差分エンコード + オプションで圧縮スナップショット)、Preview 段階で影響行数・視覚差分表示、RE2 のパフォーマンスと組み合わせて数 GB ファイルにも耐える

### 4.2 UI/UX
- Phase 5b2 時点では **UI 無し** (ヘッドレスコア実装)
- Phase 5b3 の Find bar 完成後、Find bar 内の `Replace` ボタン/`Ctrl+H` で発火
- Preview UI (Phase 5b3 と同時完成): 「N 件の置換候補、実行しますか?」ダイアログ + 上位 20 件のインラインプレビュー

### 4.3 データ構造・アルゴリズム

**新規 `core::ReplaceAllCommand`:**
```cpp
// src/core/replace_all_command.h
namespace neomifes::core {

class ReplaceAllCommand : public ICommand {
public:
    ReplaceAllCommand(std::vector<search::MatchWithCaptures> matches,
                      std::u16string replacementTemplate,
                      SelectionModel::Snapshot cursorsBefore) noexcept;

    document::EditResult execute(document::Document& doc) override;
    document::EditResult undo(document::Document& doc) override;
    SelectionModel::Snapshot cursorsAfterExecute() const noexcept override;
    SelectionModel::Snapshot cursorsAfterUndo() const noexcept override;

    // Preview 用 (実行前):
    [[nodiscard]] static std::vector<ReplacementPreview>
        preview(const std::vector<search::MatchWithCaptures>& matches,
                std::u16string_view replacementTemplate,
                std::size_t maxItems = 20);

private:
    struct AppliedEdit {
        document::TextRange rangeBefore;
        std::u16string      originalText;
        std::u16string      replacementText;
    };
    std::vector<AppliedEdit>      m_edits;
    std::u16string                m_replacementTemplate;
    SelectionModel::Snapshot      m_cursorsSnapshot;
    bool                          m_executed = false;
};

}  // namespace neomifes::core
```

**アルゴリズム:**
1. `search::SearchService::findAll(doc, query)` で全マッチを取得 (逆順にソート)
2. `execute()` は末尾から順に `doc.replace(range, expandTemplate(replacementTemplate, captures))` を呼ぶ
3. `undo()` は先頭から順に `AppliedEdit::rangeBefore` へ元テキストを戻す
4. `cursorsAfterExecute()` / `cursorsAfterUndo()` は `m_cursorsSnapshot` を返す (カーソル移動を起こさない設計)

**キャプチャグループ対応 (`$1..$9`, `$0`, `$$`, `$&`):**
- `search::Query::regex = true` の場合、`SearchService::findAll` は `MatchWithCaptures { TextRange range; std::vector<std::u16string> captures; };` を返す (RE2 の N-arg match)
- テンプレート展開: `$0` = 全マッチ、`$1..$9` = captures、`$$` = リテラル `$`、`$&` = `$0` の別名、それ以外はリテラル

**大規模置換の最適化 (100 万件):**
- 逆順適用 + 各 `AppliedEdit` は元テキストと置換テキストの参照のみ保持 (共有パターン化)
- Undo 用に元テキスト全体を保持する代わりに、100 万件 → チャンク化して圧縮 (zstd) するオプション (`docs/issues/undo_stack_unbounded_memory.md` の運用と連動)

### 4.4 性能目標
- 100 万マッチ置換: ≤ 5 秒 (差分エンコード適用時 ≤ 2 秒)
- Preview 生成 (上位 20 件): ≤ 100ms
- Undo/Redo: ≤ 100ms (差分再適用のみ)
- メモリ: 元テキスト合計サイズ + オフセット表 (置換前後のオフセットマップ)

### 4.5 テスト戦略
- 単体: 空文字列置換、重複しないマッチ、キャプチャグループ全形式 (`$0..$9, $$, $&`)、regex fail 時の no-op、Preview の上位 N 件切出
- Undo/Redo: 置換後の Undo で完全に元テキスト復元、Redo で再現
- 統合: 検索 → 置換 → もう一度検索
- ベンチマーク: 100 万件の置換

### 4.6 影響ファイル (想定)
- **新規:** `src/core/replace_all_command.{h,cpp}`、`tests/unit/core_replace_all_command_test.cpp`、`tests/bench/replace_all_bench.cpp`
- **変更:** `src/search/include/neomifes/search/search_service.h` (`Match` に captures 追加、`findAll` に captures 返却モード)、`src/search/src/search_service.cpp` (RE2 の N-arg match 呼出し)

### 4.7 実装後の確定事項/変更点 (2026-07-19、Phase 5b2 完了)

実装は §4.3 のスケッチと以下の点で意図的に乖離した。実装確定前の高レベルスケッチと実コードとの差分は、実装セッションで判明した情報を優先し、以下の通り確定させる (`detailed_design.md` §7.1'''に実装リファレンスを記載済み)。

- **core::とsearch::の依存関係: 疎結合を維持 (スケッチから変更)。** §4.3 は `ReplaceAllCommand` が `search::MatchWithCaptures` を直接受け取る設計だったが、これは Phase 5a レビューの Fix#4 (「search は実アプリ本体に未リンクのため RE2/Abseil 取得を `NEOMIFES_BUILD_TESTS` 限定にする」) と衝突すると Plan agent によるレビューで判明。ユーザー確認の結果、`core::ReplaceAllCommand` は `search::` を一切知らない設計に変更し、両者を繋ぐグルーコードは Phase 5b3 (Find bar UI 配線、実際に search が本体へリンクされるタイミング) まで書かないことが確定した
- **`ICommand` の実シグネチャはスケッチと異なっていた。** §4.3 が想定した `document::EditResult execute(document::Document&)` / `SelectionModel::Snapshot` という型はコードベースに存在せず、実際は `ExecutionContext&` / `std::vector<Cursor>` (`command.h`)。§4.3 は実装確定前の高レベルスケッチに過ぎなかったことが確認された
- **ファイル配置は §4.6 の想定通り確定:** `src/core/include/neomifes/core/replace_all_command.{h,cpp}` (roadmap の想定ファイル名と一致)。加えて `MultiCursorEditCommand` と共有する累積オフセットアルゴリズムを新規 `src/core/include/neomifes/core/cumulative_shift_edit.{h,cpp}` に抽出 (§4.3 未記載の追加設計判断)
- **キャプチャグループは `search::Match.groups`(TextRange のみ)として実装、$1-$9 展開は別関数 `search::expandReplacementTemplate()` が担当。** §4.3 が示唆した「`MatchWithCaptures` に展開済みテキストを持たせる」設計ではなく、レンジのみ保持し呼び出し側 (`expandReplacementTemplate`) が `Document` から都度テキストを抽出する設計にした。理由: マッチ時点 (置換適用前) の元ドキュメントに対して安全に抽出できるため、累積オフセット計算との結合を避けられる
- **Preview API・ベンチマーク・チャンク圧縮 Undo は Phase 5b3 以降へ延期。** UI の消費者がまだ無い状態でこれらを作るのは CLAUDE.md ルール3の推測実装にあたるため、本 PR のスコープから明示的に除外した
- **既知の未解決コスト:** `BufferSnapshot::extract()` の O(pieces) 再走査が `ReplaceAllCommand` の大量マッチ処理でボトルネックになりうることが実装時に判明 (`docs/issues/replace_all_buffer_snapshot_extract_scaling.md` に記録、Phase 5b3 で実際の大量マッチ経路ができてから再評価)

---

## 5. Phase 5b3 — Find bar UI + コマンドパレット + マッチハイライト

v2.0 追加: **コマンドパレット** (VSCode 相当) を Find bar と同時実装。共に「WC_EDIT + オーバーレイ + マッチハイライト」の共通実装パターンを持つため、同時開発で工数削減。

### 5.1 機能ビジョン
- **凌駕元:** VSCode の Find bar・Command Palette、Sublime の Command Palette
- **凌駕ポイント:** IME/クリップボード/カーソル点滅は OS に委譲 (WC_EDIT 子コントロール、決定済み) しつつ、マッチハイライトは D2D で全画面 60fps 維持。日本語検索が最初から自然に動く。**コマンドパレットは全機能への統一入口**として、キーボード完結の操作性を担保

### 5.2 UI/UX

**Find bar (Ctrl+F):**
```
┌──────────────────────────────────────────────────────────────────────┐
│  MainWindow                                                          │
│  ┌────────────────────────────────────────────────┐                  │
│  │ Find:  [                              ] Aa Ww .*  ↑ ↓ x   3/12  │
│  │ Repl:  [                              ] Replace  All            │
│  └────────────────────────────────────────────────┘                  │
│    text text [MATCH] text text text                                  │
│    text [match] text [match] text                                    │
└──────────────────────────────────────────────────────────────────────┘
```

**コマンドパレット (Ctrl+Shift+P):**
```
┌──────────────────────────────────────────────────────────────────────┐
│  ┌────────────────────────────────────────────────────────┐          │
│  │ > repl                                                 │          │
│  ├────────────────────────────────────────────────────────┤          │
│  │ ⚙ Replace: Replace All          Ctrl+H                 │  ← 最近使用
│  │ ⚙ Replace: Replace Selection    Ctrl+Alt+H             │
│  │ ⚙ File: Reload from Disk        Ctrl+Shift+R          │  ← ファジー一致
│  │ ⚙ Search: Grep in Folder        Ctrl+Shift+F          │
│  └────────────────────────────────────────────────────────┘          │
└──────────────────────────────────────────────────────────────────────┘
```

**キーバインド:**
- `Ctrl+F` — Find bar、`Ctrl+H` — Find+Replace、`Ctrl+Shift+P` — コマンドパレット
- `F3 / Shift+F3` — 次/前マッチ、`Alt+C/W/R` — Case/Word/Regex トグル、`Esc` — 閉じる
- `Ctrl+G` — 行/桁ジャンプ (§3.2 参照、Ctrl+P も同UIで「@シンボル」対応)
- `Ctrl+P` — Quick Open (最近ファイル/シンボル、Phase 7 完了後は @ でシンボル一覧、# でファイル内シンボル)

### 5.3 データ構造・アルゴリズム

**Find bar (v1.0 と同じ、`FindBarState` パターン):**
```cpp
struct FindBarState {
    HWND hwndFindEdit    = nullptr;
    HWND hwndReplaceEdit = nullptr;
    HWND hwndInfoLabel   = nullptr;
    bool visible         = false;
    bool replaceMode     = false;
    search::Query        currentQuery;
    std::vector<search::Match> currentMatches;
    std::size_t          currentMatchIndex = 0;
    UINT_PTR             debounceTimerId = 0;
};
```

**コマンドパレット (共通実装パターンで v2.0 新規):**
```cpp
struct CommandPaletteState {
    HWND     hwndInput      = nullptr;
    HWND     hwndListView   = nullptr;
    bool     visible        = false;
    std::u16string          query;
    std::vector<CommandDescriptor> allCommands;   // 全登録コマンド (静的 + 動的)
    std::vector<std::size_t>       filteredIndices;
    std::size_t                    selectedIndex = 0;
};

struct CommandDescriptor {
    std::u16string id;                 // "file.reload"
    std::u16string title;              // "File: Reload from Disk"
    std::u16string keybinding;         // "Ctrl+Shift+R" (表示のみ)
    std::function<void()> action;
    int fuzzyScore = 0;                // 動的計算
};
```

**ファジー検索 (VSCode の subword fuzzy を参考):**
- スコア = 連続マッチ度 + 単語境界一致 + 頭文字一致 + 最近使用ボーナス
- 実装は `src/util/fuzzy_matcher.{h,cpp}` に切り出し (Find bar の履歴フィルタリング等でも共用)

**マッチハイライト描画 (Find bar 用):**
- 新規 `render::MatchVisual { document::TextRange range; bool isCurrent; };`
- `RenderPipeline::setMatchVisuals(std::vector<MatchVisual>)` (既存 `CursorVisual`/`setCursorVisuals` と同じパターン)
- `drawMatchesOnLine()` で行の描画パス内に埋め込み
- 現在マッチ (`isCurrent = true`) はより濃い色

**インクリメンタル検索:**
- `WM_COMMAND / EN_CHANGE` を受けてクエリを更新
- **デバウンス:** `SetTimer(hwnd, ID_FIND_DEBOUNCE, 150, nullptr)` で 150ms 待って `SearchService::findAll` 実行
- 大規模ドキュメントで 150ms 内に完了しない場合は非同期化 (Phase 5c と連動、共通の Search Worker Pool 経由)

### 5.4 性能目標
- Ctrl+F → Find bar 表示 → フォーカス: ≤ 50ms
- Ctrl+Shift+P → コマンドパレット表示 (500 コマンド登録済み): ≤ 50ms
- インクリメンタル検索 (10MB ファイル、100 マッチ): ≤ 100ms
- コマンドパレット・ファジー検索 (500 コマンド): ≤ 20ms
- マッチハイライト描画: 60fps を維持 (可視領域のマッチのみ描画)

### 5.5 Phase 5c — Grep / 複数フォルダ検索 / 検索履歴 / タグジャンプ / 秀丸互換 Grep 結果ペイン

v2.0 追加: **検索履歴**、**タグジャンプ** (Grep 結果や error output の `file.txt(123)` パターンからジャンプ)、**秀丸互換 Grep 結果ペイン** UI/UX。

#### 機能ビジョン
- **凌駕元:** 秀丸の Grep、サクラの Grep、ripgrep の速度、VSCode の Search & Replace
- **凌駕ポイント:** Piece Table + RE2 + Boyer-Moore + AVX2 の全ての最適化を組み合わせ、複数ファイル並列で数 GB/s を目指す (§15 参照)。**秀丸互換の結果ペイン** (Grep 結果からダブルクリックでジャンプ、結果内で更に絞り込み検索) を実装

#### 設計要点
- 新規 `search::GrepService` (Search Worker Pool、論理コア数-1 スレッド)
- `GrepQuery { std::vector<std::filesystem::path> roots; std::vector<std::u16string> includeGlobs; std::vector<std::u16string> excludeGlobs; Query query; std::size_t contextLines = 0; };`
- 結果は `std::function<void(GrepMatch)>` コールバック (ストリーミング、UI は途中結果を表示)
- Grep 結果は新規モード `Mode::GrepResult` で表示、行クリックで元ファイルへジャンプ
- **タグジャンプ** — `src/util/tag_jump_parser.{h,cpp}` で `file.txt(123)` / `file.txt:123:45` / `file.txt(123,45)` パターンをパース、Grep 結果以外にビルドエラー出力貼付でもジャンプ可能

#### 検索履歴
- 直近 50 件を `%APPDATA%\NeoMIFES\search_history.json5` に保存
- Find bar / コマンドパレット / Grep ダイアログ全てで共有
- 秀丸のヒストリ検索相当

#### 秀丸互換 Grep 結果ペイン
```
┌──────────────────────────────────────────────────────────────────────┐
│  Grep results: "error" in D:\src, 234 matches, 45 files              │
│  ┌────────────────────────────────────────────────────────┐          │
│  │ D:\src\foo.cpp(12)  if (error) {                       │  ← ダブルクリックで元ファイル
│  │ D:\src\foo.cpp(45)  throw runtime_error("...");        │
│  │ D:\src\bar.cpp(89)  // error handling                  │
│  └────────────────────────────────────────────────────────┘          │
│  [Refine within results: [       ]]  [Replace within results]        │
└──────────────────────────────────────────────────────────────────────┘
```

#### 性能目標
- 数 GB (100 万ファイル) の Grep: ≤ 30 秒
- 途中結果の最初の 100 件表示: ≤ 500ms
- CPU 論理コア数-1 での並列化効率 > 70%
- タグジャンプ (エラー出力貼付から): ≤ 100ms

#### 実装後の確定事項/変更点 (2026-07-21、Phase 5c1・5c2・5c3・5c4・5c5完了 — §5.5全体完了)

**§5.5全体はGrep・複数フォルダ検索・検索履歴・タグジャンプ・秀丸互換Grep結果ペインを1章にまとめているが、実装は4b8・5b3と同じ要領でサブフェーズへ分割した。** 本節は`search::GrepService`コア(ヘッドレス、UIなし、Phase 5c1)、実行時ファイルを開く機能(`neomifes::app::openDocumentAt()`、ヘッドレス、Phase 5c2)、Grep結果ペインUI(`ui::GrepBar`、Ctrl+Shift+F、Phase 5c3)、タグジャンプ(`util::parseTagJumpReference`、F12、Phase 5c4)、検索履歴永続化(`core::SearchHistory`、Phase 5c5)が全て完了した状態を記す。**これでroadmap §5.5(延いては§5全体、5a〜5c5)が完了した。**

- **ワーカースレッドプール(`Search Worker Pool、論理コア数-1スレッド`)は採用しなかった(roadmapスケッチから乖離)。** 本コードベースには`std::thread`/`std::async`等の並行処理が一切存在せず、`search_service.h`が既に「UIが必要とするまで非同期化はしない」と明記していた方針をそのまま踏襲。Phase 5c1にはまだUIが無いため、`SearchService::findAll()`と全く同じ「`std::vector`を同期的に返す」形にGrepServiceも揃えた。スレッド化はUIワイヤリング(結果ペイン)が実際に非ブロッキング性を必要とするサブフェーズで再評価する
- **ストリーミングコールバック(`std::function<void(GrepMatch)>`)も採用しなかった。** 上記と同じ理由により`GrepService::findAll(const GrepQuery&) -> std::vector<GrepMatch>`という同期戻り値形式に統一
- **`contextLines`フィールドは`GrepQuery`に追加しなかった。** 周辺行を表示する消費者(結果ペインUI)がまだ存在しないため、追加は推測実装になる。結果ペインが必要とする時点で追加する
- **既存`search::SearchService::findAll()`/`document::loadUtf8File()`は無改変のまま完全に再利用できた。** `GrepService`は各ファイルを`loadUtf8File()`で`Document`化し、`SearchService::findAll(doc, query)`をそのまま呼ぶだけで正規表現/リテラルマッチングロジックを再実装せずに済んだ — `search_service.{h,cpp}`への変更は1行も無い
- 新規`util::globMatch()`(`*`/`?`のみのファイル名マスク、ASCII範囲のみのcasefold)で`includeGlobs`/`excludeGlobs`を実装。パス全体を対象とするglob言語(`**`等)は対象外
- 存在しないルート・読み込みに失敗したファイル(バイナリ含む)・走査中のエラーは、そのルート/ファイルをスキップするのみで全体を失敗させない設計とした(grep/ripgrepの一般的な挙動、CLAUDE.mdの「システム境界では検証するが起こり得ないシナリオには対応しない」原則に沿う)
- **意図的にスコープ外とした項目 (Phase 5cの後続サブフェーズへ):** `Mode::GrepResult`・結果ペインUI・`render_pipeline`へのマッチビジュアル配線・`main.cpp`のキーバインド配線、タグジャンプパーサ、検索履歴永続化(JSON依存追加はADR起票が必要になる見込み)、`GrepMatch`へのキャプチャグループ

#### Phase 5c2 (実行時ファイルを開く機能) — roadmapスケッチに無かった前提条件の発見

**roadmapの§5.5スケッチは「Grep結果ペインから行クリックで元ファイルへジャンプ」を前提としているが、着手前調査で本コードベースには実行中に任意の別ファイルを開く機能が一切存在しない(起動時の`--open`引数のみ)ことが判明した。** これはGrep結果ジャンプ(5c3)だけでなく将来のタグジャンプ(5c4)にも共通して必要な前提条件であるため、ユーザー確認の上、独立したサブフェーズ(5c2)として先に切り出した。

- 新規`neomifes::app::openDocumentAt()`(`src/app/include/neomifes/app/document_open.h`/`src/app/document_open.cpp`)を、既存`editor_input.h`と同じ「Win32/RenderPipeline非依存・ヘッドレステスト可能」設計で追加。`neomifes_app_input`ターゲット(呼び出し元を持つ既存の実ライブラリ)に追加することで、UIトリガーがまだ無い時点でもMSVC `/WX`+C4505(未参照ローカル関数)を回避しつつ完全にテスト可能にした
- `document::loadUtf8File()`でロードした内容を`Document::operator=(Document&&) noexcept = default`(既存、今回が初の実利用)でその場move-assignすることで、`ExecutionContext`/`RenderPipeline`が保持する`Document*`を一切無効化せずにドキュメントを差し替える設計とした
- 新規`core::UndoStack::clear()`/`core::CommandDispatcher::resetUndoHistory()`/`core::BookmarkManager::clear()`を追加し、ファイル切替時に旧ファイルに対して無意味になる状態(Undo/Redo履歴、ブックマーク、Alt-クリック/矩形選択アンカー、フリーカーソル仮想列)を一括リセットする
- **`main.cpp`は意図的に無変更のまま。** `RenderPipeline`のキャッシュ済みブックマーク/マッチビジュアルと`FindBar`の表示マッチ件数のリセットは、実際のUIトリガー(5c3のGrep結果クリック、5c4のタグジャンプ)を配線する同一コミットでまとめて行う

#### Phase 5c3 (Grep結果ペインUI) — roadmapスケッチからの意図的な乖離

roadmap §5.5の「秀丸互換Grep結果ペイン」構想(ワーカースレッド・ストリーミングコールバック・複数フォルダ・検索履歴共有UI)を、5c1・5c2の各サブフェーズ完了時と同じ理由(非同期基盤が本コードベースに一切存在しない、設定システムが存在しない)でMVPへ縮退した。

- **検索実行はEnterキーによる明示トリガーのみ、キー入力ごとの自動再実行(Find bar式デバウンス)は不採用。** ユーザーに確認の上で確定 — `GrepService::findAll()`はディレクトリ全体を舐める同期処理であり、Find barの単一ドキュメント内インクリメンタル検索と異なりキー入力のたびに実行するとUIが固まるリスクがあるため
- **`ui::GrepBar`はCommandPalette(WC_LISTBOX管理・フォーカス奪取対策)とFindBar(2つのWC_EDITが1つのサブクラスを共有)の設計をそのまま組み合わせただけで実現でき、新規のWin32サブクラス機構は不要だった**
- **入力欄はフォルダパス+クエリの2欄のみ。** フォルダピッカーダイアログ・include/exclude globの入力UI・Case/Whole word/Regexトグルはいずれも意図的に未実装(`GrepQuery`の該当フィールドはデフォルト値のまま)
- **単一フォルダのみ対応。** 複数フォルダ入力(セミコロン区切り等)は追加のパース処理が必要になるため見送り
- **`Mode::GrepResult`のような集中モード管理enumは新設しなかった。** 本コードベースには`Mode`enumが元々存在せず、`FindBar`/`CommandPalette`/`GotoLineBar`と同じ「個々のオーバーレイが独立して`isVisible()`を持つ」規約(相互排他制御なし)をそのまま踏襲
- **Grepヒットを`RenderPipeline`の`MatchVisual`としてエディタ本体にハイライト描画することはしなかった。** 5c1から据え置き済みの方針を維持 — 開いて該当行へジャンプするだけで「結果を素早く辿る」というユーザー価値は満たせる
- **既知の懸念(対処せず記録のみ):** `wireNormalMode()`の引数が19個に達した。`FindReplaceState`導入時(Phase 5b3b)に一度圧縮した経緯があるが、その後もオーバーレイ追加のたびに個別引数が積み増されている。オーバーレイ群を1つの構造体にまとめる再整理は本フェーズのスコープ外(推測実装を避けるため) — 次にオーバーレイを追加する機会があれば着手前に再検討する

#### Phase 5c4 (タグジャンプ) — roadmapスケッチからの意図的な乖離

roadmap §5.5の「タグジャンプ」構想(`file.txt(123)` / `file.txt:123:45` / `file.txt(123,45)`をパース)を、コロン形式を除く形でMVPへ縮退した。

- **括弧形式(`path(line)`/`path(line,column)`、MSVC流)のみサポート、コロン形式(`path:line:column`、GCC/Clang流)は非対応。** Windows絶対パス自体がドライブレター直後にコロンを含む(`C:\...`)ため、コロン形式の区切り文字との曖昧性解消には相応の複雑さが必要になる。本プロジェクトはWindows/MSVC優先であり、現時点で需要のない複雑さを持ち込まない判断
- **相対パスの解決基準は`std::filesystem::current_path()`(プロセスの作業ディレクトリ)。** 「現在開いているファイルのディレクトリ」ではない — 本コードベースには起動後にそれを追跡する状態が無いことに加え、MSVC/MSBuildのビルドエラー出力は常にビルド起動ディレクトリからの相対パスであり、エディタで偶然開いているファイルのディレクトリとは本質的に無関係なため、後者を基準にするのはそもそも意味論的に誤り。前提条件不足ではなく正しい設計判断として`current_path()`基準に確定
- **新規パーサ`util::parseTagJumpReference()`は`neomifes::util`名前空間に配置(`ui::goto_line_parser.h`のような`ui::`ではなく)。** GotoLineBarの単一入力欄の全文を検証する`parseGotoLineInput()`とは性質が異なり、任意の大きな文字列に埋め込まれたパターンを探索する処理(`util::globMatch()`/`util::fuzzyMatchScore()`と同じ種類の問題)であるため
- **起動方法はF12キーのみ、コマンドパレット登録は無し。** ユーザー確認済み。VSCode/Visual Studioの「定義へ移動」と同じ慣習で、現在完全に空いているキー
- **`handleKeyDownEvent()`の`document`引数を`const Document&`から`Document&`へ拡張し、`altCursorAnchor`/`rectangularAnchor`を新規引数として追加した。** `openDocumentAt()`(Phase 5c2)がこれらを必要とするため。`wireNormalMode()`自体の引数は増えていない(両方とも既にその引数として存在しており、`cfg.onKeyDown`ラムダのキャプチャリストに追加するだけで済んだ) — 5c3で記録した「オーバーレイ追加のたびの引数肥大化」懸念とは異なる種類の変化であり、この判断を再考する契機には当たらないと判断
- **マッチ無し・ジャンプ失敗はいずれも静かな無視。** ステータスバー等のフィードバック機構が本コードベースに存在しないため。誤検出(例: 拡張子を持つ識別子)も`openDocumentAt()`が静かに失敗するだけで実害が無い設計とした

#### Phase 5c5 (検索履歴永続化) — roadmapスケッチからの意図的な乖離

roadmap §5.5の「検索履歴」構想(直近50件を`search_history.json5`に保存、Find bar/コマンドパレット/Grepダイアログ全てで共有)を、コマンドパレットを対象外とする形でMVPへ縮退した。

- **コマンドパレットは対象外、Find bar + Grepダイアログの2箇所のみで検索パターン履歴を共有する。** ユーザーに確認の上で確定 — コマンドパレットのクエリは「find」「undo」等のコマンド名(fuzzy検索対象)であり、Find bar/Grepダイアログの検索パターン(正規表現/リテラル文字列)とは意味的に別種のデータ。同じ履歴に混ぜると「テキスト検索中にundoが候補に出る」等の混乱を招くため
- **履歴を辿るキーはCtrl+Up/Ctrl+Down(素のUp/Downではない)。** 着手前調査で、`ui::GrepBar`(いずれの入力欄でも)と`ui::CommandPalette`が既にUp/Downを`moveSelection(±1)`(リストのカーソル移動)に割り当て済みであることが判明したため、既存の意味と衝突しないCtrl修飾版を採用した(本コードベースのどこにも割り当てられていないことをgrep確認済み)
- **`search_history.json5`ではなく`search_history.json`(プレーンJSON)を採用した。** JSON5の追加機能(コメント・末尾カンマ・無引用キー)は機械生成・機械読取専用のファイルには意味を持たず、プレーンJSONの方が実績豊富な軽量ライブラリを選べるため
- **新規外部依存として`nlohmann/json`(ヘッダオンリー、MIT、v3.11.3)を採用した(ADR-013)。** RE2/Abseil(Phase 5a、ADR-002)と同じFetchContentパターンを踏襲
- **UTF-16⇔UTF-8境界変換は新規実装せず、既存`neomifes::encoding::encode()`/`decode()`(Phase 6a〜6d)を再利用した。** JSON側はUTF-8文字列を扱うため境界変換が必要だが、独立UTF-8実装を4つ目増やさず最も汎用な`neomifes::encoding`を使う判断
- **`core::SearchHistory::older()`/`newer()`はステートレス設計にした。** 呼び出し側(FindBar/GrepBar)が「今どのインデックスを辿っているか」という状態を一切保持する必要が無い — Ctrl+Up/Downのたびに「今edit欄に表示されているテキスト」を渡すだけで正しい次のエントリが決まる自己修正的な設計。再入guard等の追加状態管理が不要になった
- **新規`platform::resolveAppDataDir()`を追加した。** `%APPDATA%\NeoMIFES\`ディレクトリ解決の既存ヘルパーが本コードベースに無かったため、`SHGetKnownFolderPath(FOLDERID_RoamingAppData, ...)`の薄いラッパーとして新設(`clipboard.h`と同じパターン)
- **記録タイミングは`onFindNext`/`onFindPrevious`(FindBar)・`onRunQuery`(GrepBar)のみ。** `navigateToMatch()`の他の呼び出し経路(document-focused F3、コマンドパレットの「Find Next」等)では記録しない — `record()`自身がMRUの先頭への移動+重複排除を行うため、後から同じクエリが再記録されても無害な no-op になることを利用し、カスケードするシグネチャ変更を避けた
- **保存タイミングはプロセス終了時(`runMessageLoop()`復帰後)の1回のみ。** 検索のたびに毎回ディスクへ書かない — セッション中はメモリ上のみ、クラッシュ時は当該セッション分の新規追加のみが失われる許容可能なデータロスとした

### 5.6 テスト戦略 (Phase 5b3 + 5c)
- 単体: Find bar の Show/Hide 遷移、F3 のラップアラウンド、Escape でフォーカス復元、コマンドパレットのファジースコア計算、タグジャンプパーサの各パターン
- 統合: Ctrl+F → 日本語入力 → インクリメンタル結果表示、Ctrl+H → 置換 (5b2 と結合)、Ctrl+Shift+P → コマンド実行、Grep → 結果クリックジャンプ
- Grep: 10000 ファイル、include/exclude glob、大文字小文字、正規表現、Refine within results
- 手動 (UI): マッチハイライトが 60fps を維持、コマンドパレットが 20ms 以内応答

### 5.7 影響ファイル (Phase 5b3 + 5c)
- **新規:** `src/ui/{find_bar.{h,cpp}, command_palette.{h,cpp}, grep_result_view.{h,cpp}}`、`src/render/match_visual.h`、`src/search/src/grep_service.{h,cpp}`、`src/util/{fuzzy_matcher.{h,cpp}, tag_jump_parser.{h,cpp}}`、`src/core/search_history.{h,cpp}`
- **変更:** `src/app/main.cpp` (状態変数群、Ctrl+F/Ctrl+H/Ctrl+Shift+P/Ctrl+G/Ctrl+P 配線)、`src/render/render_pipeline.{h,cpp}` (setMatchVisuals / drawMatchesOnLine)、`src/core/mode.h` (Mode::GrepResult)、`src/search/include/neomifes/search/search_service.h` (async `findAllAsync`)
- **新規テスト:** `tests/unit/{ui_find_bar,ui_command_palette,util_fuzzy_matcher,util_tag_jump_parser,search_grep_service}_test.cpp`

### 5.8 実装後の確定事項/変更点 (2026-07-19、Phase 5b3a・5b3b・5b3c 完了)

**§5全体はFind bar + コマンドパレット + Grepをまとめて記述していたが、実装は3段階(5b3a/5b3b/5b3c)に分割した。** 本節は§5.1-5.4(Find bar UI基盤)に対応する**Phase 5b3a**、置換行配線(Ctrl+H)に対応する**Phase 5b3b**、コマンドパレット(§5.2後半)に対応する**Phase 5b3c**が完了した状態を記す。roadmap §5全体はこれで完了、残るは§5.5(Phase 5c、Grep/検索履歴/タグジャンプ)のみ。

- **`ui::FindBar`はsearch::/document::/core::を一切知らない設計(スケッチから変更)。** §5.3の`FindBarState`スケッチは検索状態(`currentQuery`/`currentMatches`/`currentMatchIndex`)をFind bar自身の構造体に持たせる想定だったが、既存`ui::MainWindow`と同じ「Win32機構のみ、上位ドメインを知らない」分離方針を優先し、この状態は`src/app/main.cpp`の`wWinMain`スコープにローカル変数として置いた。Phase 5b2で`core::ReplaceAllCommand`をsearch::から疎結合に保った判断と同じ系統の設計選択(`detailed_design.md` §7.1'''参照)
- **`MatchVisual`は`match_visual.h`ではなく`render_pipeline.h`に配置(スケッチから変更)。** 既存`CursorVisual`が別ファイルではなく`render_pipeline.h`に直接定義されている実際の配置と一貫性を取るため
- **CMakeガード解除は単純な`include(Dependencies)`移動では不十分だった。** `cmake/Dependencies.cmake`はRE2/Abseil**と**GoogleTest/benchmarkの両方を含む1ファイルであり、単純に無条件化するとテスト専用依存まで無条件フェッチされてしまうことが実装時に判明。新規`cmake/TestDependencies.cmake`へGoogleTest/benchmarkを分離し、RE2/Abseilのみを含む`Dependencies.cmake`を無条件`include()`化した。`NEOMIFES_BUILD_TESTS=OFF`でも`NeoMIFES.exe`単独ビルドが成立し、GoogleTest/benchmarkはフェッチされないことを確認済み
- **IME安全性・WM_SYSKEYDOWN・デバウンスタイマーのKillTimerは、設計時のPlan agentレビューで指摘され実装に組み込んだ必須修正。** これらはFind bar UIの本質的な正しさに関わる項目で、スコープ外への先送りは行わなかった(§5.1「日本語検索が最初から自然に動く」という目標に直結するため)
- **既知の未解決コスト:** `drawMatchesOnLine()`の可視行ごと線形走査(`docs/issues/match_highlight_linear_scan_scaling.md`に記録)

**Phase 5b3b (置換行配線、Ctrl+H) 完了分:**
- **`currentQuery`/`currentMatches`/`currentMatchIndex`を`FindReplaceState`構造体へ統合。** Phase 5b3a完了時点で`wireNormalMode`が12引数に達しており、本フェーズのReplace行状態追加を機に統合(詳細は`detailed_design.md` §7.1'''''参照)
- **Find edit / Replace editは同一サブクラスプロシージャを共有**、Tabキー巡回は本アプリのメッセージループが`IsDialogMessageW`を使わないため自前実装(`FindBar::cycleFocus`)

**Phase 5b3c (コマンドパレット、Ctrl+Shift+P) 完了分:**
- **`ui::CommandPalette`は`WC_EDITW`+`WC_LISTBOXW`の2種類の子コントロールを同一サブクラス機構で扱う初のケース。** リストボックスも自分自身に`SetFocus`する標準挙動を持つため、FindBarの「エディット2つ共有」より一段複雑 — 詳細は`detailed_design.md` §7.1''''''参照
- **設計時のPlan agentレビューに加え、実装トレース中にこのセッション自身がもう1件の設計不備(ダブルクリックでのフォーカス奪回とコマンド実行の競合)を発見・修正した。** `isVisible()`確認によるガードを追加(§7.1''''''参照) — Plan agentのレビューだけでは拾いきれない、実装の詳細に踏み込んだトレースでしか見つからないクラスの不具合があることを示す実例
- **登録コマンドは既存実装済みキーバインドの再露出6件のみ**(Find/Find+Replace/Find Next/Find Previous/Undo/Redo)。File Open/Save等の未実装機能はコマンドパレット用に新規実装しない方針を貫いた
- **新規`util::fuzzyMatchScore()`/`ui::filterAndRankCommands()`は`click_tracking.h`/`find_navigation.h`と同じ「Win32非依存の純粋ロジック」パターンを踏襲**、単体テスト12件追加
- **意図的にスコープ外とした項目 (Phase 5cへ延期):** サブメニュー、絵文字アイコン、最近使用ボーナス、検索履歴共有、Quick Open(Ctrl+P)・行ジャンプ(Ctrl+G)、Grep、クリックできるReplace/Allボタン(キーバインドのみ)。UIの消費者/要件確定が別途必要なため

---

## 6. Phase 6 — エンコーディング + 自動判定 + 10GB mmap + 遅延デコード

### 6.1 機能ビジョン
- **凌駕元:** サクラの多言語対応、秀丸の Shift-JIS 品質、MIFES の巨大ファイル対応
- **凌駕ポイント:** 全対応エンコーディングを **自前実装** (依存追加ゼロ、20MB メモリ目標に貢献)、自動判定は 3 段階で 99% 以上の正確性、**10GB ファイルは mmap + 遅延デコードで開始 100ms 以内**

### 6.2 対応エンコーディング (要件定義書 §6)
UTF-8 / UTF-8 BOM / UTF-16 LE / UTF-16 BE / UTF-32 LE/BE / Shift-JIS / EUC-JP / ISO-2022-JP

### 6.3 データ構造・アルゴリズム

**新規モジュール `src/encoding/`:**
```cpp
// include/neomifes/encoding/encoding.h
namespace neomifes::encoding {

enum class Encoding {
    Unknown,
    Utf8, Utf8Bom,
    Utf16Le, Utf16LeBom,
    Utf16Be, Utf16BeBom,
    Utf32Le, Utf32LeBom,
    Utf32Be, Utf32BeBom,
    ShiftJis,
    EucJp,
    Iso2022Jp,
};

enum class LineEnding { Crlf, Lf, Cr, Mixed };

struct DecodeResult {
    std::u16string  text;
    Encoding        detectedEncoding;
    LineEnding      detectedLineEnding;
    std::size_t     invalidByteCount;
};

class Encoder {
public:
    [[nodiscard]] static DecodeResult decode(std::span<const std::byte> bytes,
                                              Encoding hint = Encoding::Unknown);
    [[nodiscard]] static std::vector<std::byte> encode(std::u16string_view text,
                                                        Encoding target,
                                                        LineEnding lineEnding);
};

class EncodingDetector {
public:
    [[nodiscard]] static Encoding detect(std::span<const std::byte> head64k);
};

}  // namespace neomifes::encoding
```

**自動判定の 3 段階:**
1. **BOM 判定** (1μs 以下): 先頭 4 バイトで UTF-8 BOM / UTF-16 LE/BE BOM / UTF-32 LE/BE BOM を確定
2. **文字分布統計** (数 ms): ISO-2022-JP のエスケープシーケンス (`ESC $ B`, `ESC ( B`) 検出 → UTF-8 バリデーション (RFC 3629) → 失敗時 Shift-JIS/EUC-JP 判定
3. **N-gram モデル** (統計で確信度低い時): 日本語 2-gram 頻度表 (組込リテラル、~4KB) と照合し確信度算出

**Shift-JIS 判定のポイント:**
- Shift-JIS 第 1 バイト範囲: `0x81..0x9F` / `0xE0..0xFC`、第 2 バイト範囲: `0x40..0x7E` / `0x80..0xFC`
- EUC-JP 第 1 バイト範囲: `0xA1..0xFE`、第 2 バイト範囲: `0xA1..0xFE`
- Shift-JIS で有効かつ EUC-JP で無効なバイト列 (`0x80..0xA0` 領域など) を優先マーカとして使用

**行末コード判定:**
- 先頭 64KB 中の `\r\n` / `\n` / `\r` の出現回数を数え、多数派採用
- 混在は `LineEnding::Mixed` として記録、UI で警告

**メモリマップドファイル対応 (10GB 対応の核心):**
- 10GB ファイルは全体をデコードせず、`Document::load` が要求した範囲のみデコード
- Piece Table の Original Buffer は「元バイト列 + Encoding タグ」を保持、`pieceView` 要求時に該当範囲をデコード
- 遅延デコードキャッシュ (`docs/issues/lazy_decode_mmap.md` で先取り予告済み) を Phase 6 の副産物として実装
- **Direct Storage 検討:** Windows 11 の Direct Storage API を試験導入、NVMe から直接 GPU/CPU バッファに読出し (§15.3 参照)

### 6.4 性能目標
- 自動判定 (64KB head): ≤ 5ms
- 1MB Shift-JIS ファイル読込 + 全デコード: ≤ 50ms
- 10GB UTF-8 ファイル読込 (mmap + 表示範囲のみデコード): ≤ 100ms
- 10GB ファイルスクロール中の遅延デコード: 60fps を維持
- 全 8 エンコーディング × 全 3 行末で「往復して同一バイト列」を確認するラウンドトリップテスト全通過

### 6.5 テスト戦略
- 単体: 各エンコーディングの代表ファイル、BOM 有無、不正バイト、境界文字 (半角/全角混在、絵文字、combining characters)
- ラウンドトリップ: `encode(decode(bytes)) == bytes` を全エンコーディングで確認
- 自動判定: 「日本語文学作品コーパス」100 ファイルで 99% 以上の判定正確性
- 統合: メモ帳/秀丸/サクラで作った実ファイル、SAP・Oracle・Nginx 出力の実サンプル
- ソーク: 10GB ファイルを 1 時間スクロールしてリーク無し

### 6.6 影響ファイル
- **新規:** `src/encoding/{encoding.cpp, encoder_utf8.cpp, encoder_utf16.cpp, encoder_utf32.cpp, encoder_shift_jis.cpp, encoder_euc_jp.cpp, encoder_iso_2022_jp.cpp, detector.cpp}`、`include/neomifes/encoding/encoding.h`、`tests/unit/encoding_*_test.cpp`、`tests/integration/encoding_roundtrip_test.cpp`
- **変更:** `src/document/document.cpp` (エンコーディング指定 load、遅延デコード)、`src/app/main.cpp` (「エンコーディング指定して開く」メニュー、ステータスバー表示)

### 実装後の確定事項/変更点 (2026-07-20、Phase 6a完了)

**§6全体は対応エンコーディング・3段階自動判定・10GB mmap遅延デコードを1章にまとめているが、実装は4b8・5b3・5cと同じ要領でサブフェーズへ分割した。** 本節は`neomifes::encoding::decode()`/`encode()`/`detectBom()`(Unicodeファミリー10種、ヘッドレス、Phase 6a)のみが完了した状態を記す。他のサブフェーズ(Shift-JIS/EUC-JP/ISO-2022-JP・自動判定・Document統合・10GB mmap一般化)は引き続き未着手。

- **`Encoder`/`EncodingDetector`という2クラス構成は採用しなかった。** roadmapスケッチはクラスベースの`Encoder::decode/encode`(static)と`EncodingDetector::detect`を想定していたが、6aでは`neomifes::encoding`名前空間直下の自由関数`decode()`/`encode()`/`detectBom()`とした — 状態を持たない純粋関数群にクラスの皮を被せる理由が無いため(`util::globMatch()`/`util::parseTagJumpReference()`と同じ「自由関数で十分」判断)
- **`Encoding`enumは10値のみで開始し、Shift-JIS/EUC-JP/ISO-2022-JPは含めない。** 未実装のenumeratorを公開APIに置かない判断(実装が追いついてから6bで追加、enumへの追加は後方互換)
- **`DecodeResult{text, detectedEncoding, detectedLineEnding, invalidByteCount}`という統合戻り値は採用しなかった。** 6aの`decode()`は`std::variant<std::u16string, DecodeError>`のみを返す — 行末コード判定(`detectedLineEnding`)は自動判定サブフェーズ(6c)の関心事であり、6aの「指定されたエンコーディングでバイト列をデコードする」という責務には含まれない
- **`decode()`は`hint`パラメータを取らず、呼び出し側が確定したEncodingを渡す設計にした。** roadmapスケッチの`decode(bytes, hint=Unknown)`は「BOM自動判定込みでデコードする」1関数を想定していたが、6aでは`detectBom()`(BOM検出のみ)と`decode()`(指定されたEncodingでデコードのみ)を分離した — 呼び出し側が`detectBom()`の戻り値をそのまま`decode()`に渡せる設計にすることで、6c以降が追加する非BOM判定手段(文字分布統計・N-gram)を`detectBom()`を経由せず直接`decode()`へ差し込める
- **`util::toUtf8WithOffsets()`(Phase 5a)を再利用せず、独立した新規コーデックとして実装した。** 前者はUTF-16→UTF-8のENCODE方向のみでRE2検索用のバイトオフセット対応表を必ず構築する設計であり、6aが必要とするDECODE方向(バイト列→UTF-16)は無く、オフセット表はコーデック用途では不要なオーバーヘッドになる。`document::OriginalBuffer`の内部UTF-8検証ロジックと合わせ、本コードベースには用途ごとに独立したUTF-8実装が(意図的に)複数存在する
- **`document::loadUtf8File()`/`OriginalBuffer`への統合は一切行わなかった。** `OriginalBuffer`のmmap+チェックポイント方式の遅延デコード機構(Phase 2b3)はUTF-8専用に深く結合しており、他エンコーディングへの一般化は独立した大きなサブフェーズになる見込み(6d以降)
- **メニューバー・ステータスバーは新設しなかった。** 本コードベースに`CreateMenu`/`SetMenu`/ステータスバーウィンドウクラスは一切存在せず、UIトリガーは実際に必要になるサブフェーズ(6d以降)で新設する

### 実装後の確定事項/変更点 (2026-07-20、Phase 6b1完了)

**Shift-JIS(CP932)/EUC-JP(CP20932)を`neomifes::encoding::Encoding`へ追加。ISO-2022-JPは6b2へ分離(下記参照)。**

- **roadmapスケッチが構想していた`encoder_shift_jis.cpp`等の自前JIS X 0208対応表実装は採用しなかった。** これは数千文字規模のUnicode⇔JIS X 0208対応表を記憶から手打ちで生成することを意味し、CLAUDE.mdルール3(推測実装をしない)に照らして転記誤りのリスクが看過できないと判断した。代わりにWin32の`MultiByteToWideChar`/`WideCharToMultiByte`(コードページ932/20932)をラップする新規`neomifes::platform::codepage_convert`(`convertToUtf16`/`convertFromUtf16`)を新設し、`neomifes::encoding`から呼び出す設計にした。roadmap§6.1の「依存追加ゼロ」という目標も、Win32 APIは本プロジェクトが既にDirect2D/DirectWrite同様に前提としているプラットフォームであるため損なわれないと判断
- **エンコード方向の厳格エラー検出に`WC_ERR_INVALID_CHARS`は使えないことを実装時に実機で確認した(ローカルWindows 11、`GetLastError()`が`ERROR_INVALID_FLAGS`を返す)。** decode方向の`MB_ERR_INVALID_CHARS`はCP932/20932で問題なく機能したが、その素直な鏡像であるはずの`WC_ERR_INVALID_CHARS`はDBCSコードページでは非対応。代わりに`WC_NO_BEST_FIT_CHARS`+`lpUsedDefaultChar`出力引数を使い、既定文字への曖昧な置換が発生した場合はエラー扱いにする設計へ切り替えた(`codepage_convert.cpp`の実装コメントに検証過程を記録)
- **`encode()`の戻り値を`std::vector<std::byte>`(6a)から`std::variant<std::vector<std::byte>, EncodeError>`へ変更した。** Unicodeファミリーは全域関数(失敗しない)だがShift-JIS/EUC-JPは非全域(JIS X 0208に無い文字、例えば絵文字は表現不可能)なため。6a完了時点で`encode()`の呼び出し元はテストファイルのみだったため(grep確認済み)、破壊的変更のコストは実質ゼロだった
- **ISO-2022-JPは本サブフェーズ(6b1)に含めず6b2へ分離した。** `WC_ERR_INVALID_CHARS`のISO-2022系コードページ(50220/50221/50222)への対応状況が未検証であること、エスケープシーケンス(`ESC $ B`/`ESC ( B`)によるモード切替という別種の構造を持つこと、P1ペルソナ(SAPコンサル)が明示的に要求しているのはShift-JISのみであることを理由に、CLAUDE.mdルール8(1PR=1責務)に従い分離した
- **既知バイト列(「あ」= Shift-JIS `82 A0`/EUC-JP `A4 A2`、「亜」= Shift-JIS `88 9F`/EUC-JP `B0 A1`)による外部真実性テストを追加した。** encode/decodeの自己ラウンドトリップだけでは、両者が対称的に同じ誤りを持つケースを検出できないため

### 実装後の確定事項/変更点 (2026-07-21、Phase 6c1完了)

**`detectEncoding()`を追加(BOM/UTF-8/Shift-JIS/EUC-JP判別)。ISO-2022-JP検出は実装しない(6b2待ち、理由は下記)。**

- **新規の低レベルバイト走査コードは書いていない。** `detectBom()`/`decode()`(6a/6b1で実装済み)の成功/失敗を組み合わせるだけの4行相当の実装になった。roadmapの「Shift-JIS第1バイト範囲0x81-0x9F...を優先マーカとして使用」という記述は、実装時に「両方decode()が成功する場合のタイブレーカー」として1つの軽量な範囲チェックのみ残った(下記のC1制御コード発見と合わせて設計を1往復させた結果)
- **重要な発見: Windows CP932/CP20932は一部の未割当バイトをC1制御コード(U+0080-U+009F)へ黙って直接マッピングし、`MB_ERR_INVALID_CHARS`指定下でも拒否しない(未文書化)。** 具体的にはShift-JISの単独`0x80`、EUC-JPの`0x80-0x9F`のほぼ全域(SS2シフトバイト`0x8E`単体を除く)。これはPhase 6b1で「MB_ERR_INVALID_CHARSは両コードページで問題なく機能する」と記録した内容の**部分的な誤り**であり、6c1の`detectEncoding()`テスト作成中に発見した。`neomifes::encoding::decodeLegacyCodepageBody()`にデコード結果のC1範囲(U+0080-U+009F)出力を拒否する後処理を追加して修正した(`platform::convertToUtf16()`自体は汎用Win32ラッパーのまま変更せず、JIS固有のこの業務ルールは`encoding.cpp`側に置いた)
- **Shift-JIS/EUC-JPの2バイト表現域(0xA1-0xFE×0xA1-0xFE)はほぼ全域が両コーデックで同時に有効になりうることを実機検証で確認した。** EUC-JPの2バイト目が0xFD/0xFEの場合(Shift-JISのDBCS第2バイト有効範囲は最大0xFCまで)のみ、EUC-JP側が確定的に判別可能。それ以外の大半のケース(例: EUC-JPの「あ」`A4 A2`)はShift-JISの半角カタカナ2文字としても同時に有効な、真に曖昧なケースである。roadmapのN-gramモデル(Stage 3)が本来解決すべき領域であり、6c1はこれを推測せず`nullopt`として扱う
- **行末コード判定(`LineEnding`)は6c1に含めなかった。** 6aの実装後コメントでは「6cの関心事」としていたが、CLAUDE.mdルール8(1PR=1責務)に従いさらに6c2以降へ分離した
- **ISO-2022-JP検出も6c1では実装しなかった。** 着手前の実機検証で、ISO-2022系コードページ(50220/50221/50222)がWin32レベルで厳格な入力検証を一切サポートしないことが判明した(`MB_ERR_INVALID_CHARS`/`WC_ERR_INVALID_CHARS`/`WC_NO_BEST_FIT_CHARS`のいずれも`ERROR_INVALID_FLAGS`、`lpUsedDefaultChar`非NULLは`ERROR_INVALID_PARAMETER`、有効な`dwFlags=0`は不正なエスケープシーケンスをPUA文字へ静かに置換し絵文字等を検知不能な"??"へ静かに変換する)。ISO-2022-JPの検出・デコードを実装するにはこの正確性トレードオフへの対応方針を別途決める必要があり、Phase 6b2として独立させたまま保留とした

### 実装後の確定事項/変更点 (2026-07-21、Phase 6c2完了)

**`detectLineEnding()`を追加(Crlf/Lf/Cr/Mixed判定)。**

- **生バイト列ではなく`decode()`済みのUTF-16文字列を走査する設計にした。** roadmapスケッチは「先頭64KB中の`\r\n`/`\n`/`\r`の出現回数を数え」と生バイト列走査であるかのように読めるが、UTF-16では`\n`(U+000A)が2バイト表現になるため、生バイト単位の走査ではUTF-16入力に対して誤検出/検出漏れが起こる。本プロジェクトの内部標準UTF-16(CLAUDE.md §4)に揃え、`detectEncoding()`→`decode()`→`detectLineEnding(decodedText)`という合成にした
- **「混在」の判定基準は、1件でも異なる規約が混じればMixed。** roadmapの「多数派採用」という表現よりも、直後の「混在はMixedとして記録、UIで警告」という目的を優先した — 少数派を黙って多数派に丸めると、UIが警告すべき状況を検知できなくなるため
- 64KBサンプリング上限は`detectLineEnding()`自身の内部では強制していない(`detectEncoding(head)`と同じ「呼び出し側が渡す範囲を全て走査する」設計)。大ファイルでのサンプリング方針は6d(Document統合)側で決める

### 実装後の確定事項/変更点 (2026-07-21、Phase 6b2完了)

**ISO-2022-JP(CP50220のみ、RFC 1468ベースライン)を`neomifes::encoding::Encoding`へ追加。CP50221/50222(半角カタカナ拡張)は対象外。**

- **重要な発見: CP50220は`dwFlags=0`以外を一切受け付けない。** 6c1完了時点で判明していた「`MB_ERR_INVALID_CHARS`/`WC_ERR_INVALID_CHARS`/`WC_NO_BEST_FIT_CHARS`がいずれも`ERROR_INVALID_FLAGS`」に加え、本フェーズ着手前の追加実機検証で**`lpDefaultChar`/`lpUsedDefaultChar`を個別に(片方だけを)指定しても`ERROR_INVALID_PARAMETER`になる**ことを確認した。これにより「独自センチネル値を注入して置換発生を検知する」という代替戦略も使えないことが確定した
- **decode方向の不正入力検知は、デコード結果にUnicode私用領域(U+E000-U+F8FF)のコードポイントが含まれるかどうかで行う。** `dwFlags=0`の寛容モードは不正なエスケープシーケンス/不正なku-tenペアをエラーにせずPUAへ黙って置換する(実機観測: `U+F8F0`/`U+F8F3`)。正当なISO-2022-JPコンテンツ(ASCII/JIS-Roman/JIS X 0208)がPUAへデコードされることは無いため、6c1のC1制御コード拒否と同じ「後処理での範囲チェック」パターンを踏襲した
- **encode方向は「置換の検知不能」問題をEUC-JP(CP20932)の厳格encodeを代理オラクルとして使うことで回避した。** Windows自身がCP50220とCP20932を共に「Japanese, JIS X 0208-1990 & 0212-1990」という同一の文字集合として文書化していることを根拠に、文字を実際にCP50220へ渡す前にまず`platform::convertFromUtf16(text, 20932)`(6b1で確立済みの厳格EUC-JP encode)が成功するかを確認し、失敗すれば`EncodeError::UnmappableCharacter`として即座に拒否する設計にした。CP932(Shift-JIS、NEC/IBM拡張文字を含みISO-2022-JPより文字集合が広い)を代理に使うより整合性が高い判断。**既知の制約として、CP20932とCP50220の文字集合が理論上完全一致しない可能性(JIS X 0212のどちらかにのみ実装されている稀な文字)は未対処のまま残る** — 発生しても`encode()`が誤って`UnmappableCharacter`を返すという安全側の失敗モードになるため許容した
- **新規`platform::convertToUtf16Lenient()`/`convertFromUtf16Lenient()`をCP50220専用の寛容変換として追加し、既存の厳格版`convertToUtf16()`/`convertFromUtf16()`とは完全に分離した。** 呼び出し元(`encoding.cpp`)側の検証ロジック(PUA範囲チェック・EUC-JPオラクル)と組み合わせて初めて、他エンコーディングと同じ「曖昧な入力は拒否する」規約を維持できる — この層単体では厳格性を持たない
- ISO-2022-JP検出(`detectEncoding()`がエスケープシーケンス`ESC $ B`/`ESC ( B`を認識すること)は本フェーズでも実装していない。別途未スコープの追加として残る

### 実装後の確定事項/変更点 (2026-07-21、Phase 6d完了)

**`OriginalBuffer::openMemoryMapped()`をEncoding引数対応に汎化し、新規`document::loadFile()`で自動判定込みの多エンコーディング読込を実現。これによりroadmap §6全体(対応エンコーディング・自動判定・10GB mmap遅延デコード)が完了した。**

- **mmap+遅延デコードは「バイト単位で文字境界が構造的に分かるエンコーディング」(UTF-8・UTF-16 LE/BE・UTF-32 LE/BE)にのみ一般化し、Shift-JIS/EUC-JP/ISO-2022-JPは既存の`OriginalBuffer::fromU16String()`による一括デコード経路(`neomifes::encoding::decode()`を1回呼ぶだけ)を使う設計にした。** 理由は2点: (1) ISO-2022-JPはエスケープシーケンスによるモード切替という状態を持つため、チェックポイントからの再開時に「そのバイト位置がどのモードか」を別途保持する必要があり、mmap+遅延デコードへの一般化はISO-2022-JP単体で独立した設計課題になる。(2) 対象ペルソナ(SAPコンサル等)がShift-JIS/EUC-JP/ISO-2022-JPで開く想定のファイルは実務上MB級のログ/設定ファイルであり、10GB級の想定は無い(10GBの旗艦シナリオは一貫してUTF-8/UTF-16が対象)
- **UTF-16は`OriginalBuffer`のチェックポイント機構を使わない。** UTF-16源のCUオフセットは常にバイトオフセット/2(サロゲートペアも2個の独立したCUとして扱われるため、UTF-8のような可変長デコードの複雑さが無い)であるため、`viewMemoryMappedUtf16()`は要求されたCU範囲のバイト範囲を直接計算するだけで済む。UTF-32は非BMP文字が1個の4バイトユニットから2 CUを生成するためCUオフセットがバイトオフセット/4から乖離しうる — UTF-8と同じチェックポイント方式(ただし固定4バイトユニットなのでUTF-8の可変長先頭バイト判定より単純)を維持
- **新規`document::loadFile()`は`detectBom()`→`detectEncoding()`→UTF-8フォールバックの順で自動判定し、上記2経路(mmap遅延デコード/一括デコード)へ振り分ける。** `maxBytes`のデフォルトを16GiB(10GB目標+ヘッドルーム)に設定 — 従来`main.cpp`/`app::openDocumentAt()`は`loadUtf8File(path)`を上限指定なしで呼んでおり、512MiBという既定上限のせいでアプリの実際の入口からは「10GB」目標にそもそも到達できていなかった。この上限を上げたことで初めて10GB目標がアプリ経由で到達可能になった
- **`loadUtf8File()`は一切変更しない(シグネチャ・挙動とも既存のまま)。** `search::GrepService`がディレクトリ横断走査で「バイナリ/非UTF-8ファイルは静かにスキップ」という既存の意図的な設計(5c1完了記録)を持つため、`GrepService`はloadUtf8File()を使い続ける。内部実装だけ、汎化した`OriginalBuffer::openMemoryMapped(path, byteOffset, Encoding::Utf8)`を呼ぶ形にリファクタしたが外部から見た挙動は完全に同一
- **ISO-2022-JP自動判定は引き続き未実装の既知の制約として残る。** 平文ISO-2022-JPファイルのバイトは全て0x80未満(7-bit clean)であるため`detectEncoding()`のUTF-8判定に成功してしまい、`loadFile()`はそれをUTF-8として"デコード成功"扱いしてしまう(文字化けするが、エラーにはならない) — `detectEncoding()`へのESCシーケンス認識追加は引き続きスコープ外(6c1/6b2から継続)
- 実測(`BM_LoadFile_100MB`、Release): 207ms — Phase 2b3時点の記録(199ms)と同水準、UTF-8既存経路への性能回帰なし

**Phase 6全体、6a〜6d全サブフェーズ完了。** roadmap §6が要求していた対応エンコーディング・3段階自動判定(N-gramモデルによる曖昧ケース確信度算出を除く)・10GB mmap遅延デコードが揃った。今後の追加候補はISO-2022-JP自動判定・N-gramモデル・「エンコーディング指定して開く」UI(メニュー/ステータスバー基盤が本コードベースに無いため6d時点でも見送り)で、いずれも実需が生じてから改めてスコープする。

---

## 7. Phase 7 — シンタックス + アウトライン + 折り畳み + ミニマップ + Breadcrumb + Sticky scroll + Indent guides + Semantic highlighting

v2.0 大幅拡張: **ミニマップ、Breadcrumb、Sticky scroll、Indent guides、Semantic highlighting** (VSCode 相当の全モダン UI) を Phase 7 に統合。

### 7.1 機能ビジョン
- **凌駕元:** 秀丸のアウトライン解析、VSCode のシンタックス+セマンティックハイライト+全モダン UI
- **凌駕ポイント:** ハイライトは非同期増分解析で 60fps を絶対落とさない。アウトラインは秀丸並みに賢い階層抽出。折り畳みは 100 万行対応。**ミニマップは D2D で GPU 描画、テキストサムネイル + シンタックス色 + スクロール位置ハイライト**

### 7.2 対応言語 (Phase 7 の一次スコープ、要件定義書 §6 対応)
必須: C / C++ / TypeScript / JavaScript / Python / Java / Go / Rust / PHP / HTML / CSS / JSON / XML / YAML / SQL / Markdown / PowerShell / VB / VBS / BAT / Shell / INI / TOML / **SAP ABAP** (P1 対応)

### 7.3 データ構造・アルゴリズム

**シンタックス定義エンジン選定 (Phase 7a で PoC → ADR-013):**
- **一次候補:** TextMate grammar (VSCode エコシステム流用)
- **二次候補:** tree-sitter (WASM 除外版、C API 静的リンク)
- **決定基準 (Phase 7a):** C++/TS/Python 3 言語で以下を比較:
  - 起動時のパーサ ready 時間 (≤ 50ms)
  - 100 万行 C++ の初回全解析 (≤ 5 秒)
  - 増分解析 (1 文字入力後の再解析範囲、tree-sitter は真の増分解析)
  - バイナリサイズ (tree-sitter は言語ごと数百 KB)
- **セマンティックハイライト** — LSP が返す `semanticTokens` を tree-sitter 相当の色付けと重ねる。同じ変数を色でリンク、typo 検出を色差で示す

**モジュール構成 `src/syntax/`:**
```cpp
namespace neomifes::syntax {

enum class TokenKind {
    Text, Keyword, Type, Function, Variable, Number, String,
    Comment, Operator, Punctuation, Preprocessor, Attribute,
    Error, /* Semantic 拡張: */ TypeParameter, Enum, Namespace, Interface,
};

struct Token {
    document::TextRange range;
    TokenKind           kind;
    std::uint16_t       userKind = 0;
    // v2.0: Semantic token modifiers (declaration, readonly, deprecated, etc.)
    std::uint32_t       modifiers = 0;
};

struct FoldRange {
    document::TextRange range;
    std::u16string      preview;
    bool                folded = false;
    // v2.0: sticky scroll 用 - この範囲が画面上部で見切れる時に固定表示する 1 行
    document::TextPos   headerLine;
};

struct OutlineNode {
    std::u16string           name;
    document::TextPos        pos;
    int                      level;
    std::vector<OutlineNode> children;
    // v2.0: breadcrumb 用 - カーソル位置からのパス生成に使用
    document::TextRange      containingRange;
    TokenKind                symbolKind;
};

class SyntaxEngine {
public:
    void registerLanguage(std::u16string_view id, std::unique_ptr<ILanguageDefinition>);
    void attachToDocument(document::Document& doc, std::u16string_view languageId);
    // 増分解析: DocumentChanged イベントを購読し、影響範囲のみ再解析
};

}  // namespace neomifes::syntax
```

### 7.4 ミニマップ (v2.0 新規)

**設計:**
- 右側縦帯 (幅 100-150px、DPI 対応)、テキストを 1/8 スケールで縮小描画
- Direct2D の `D2D1_BITMAP_INTERPOLATION_MODE_LINEAR` で GPU スケーリング
- シンタックスハイライトの色を反映 (完全なテキスト描画ではなく、行ごとに「主要トークン色 + 密度」で描く高速版)
- 現在の可視領域を半透明矩形で強調、ドラッグでスクロール
- クリック時にジャンプ

**性能:**
- 1000 行のミニマップ生成: ≤ 50ms
- スクロール中のミニマップ更新: 60fps 維持
- 100 万行ファイルでも常時表示可能 (可視部分のみ動的生成)

### 7.5 Breadcrumb (v2.0 新規)

**設計:**
- ファイル上部に「ファイル名 > 名前空間 > クラス > 関数」形式のパスを表示
- 各要素クリックで同レベルの兄弟要素のドロップダウン
- カーソル位置から `OutlineNode` を逆引きしてパス生成
- 更新頻度: カーソル移動時にデバウンス 50ms

### 7.6 Sticky scroll (v2.0 新規)

**設計:**
- 画面上部に、現在カーソル位置のスコープを示す行 (関数シグネチャ・クラス宣言等) を固定表示
- スクロールしても消えず、他の関数に入ると内容が変わる
- 実装は `FoldRange::headerLine` を利用し、可視領域最上端で見切れた fold の header 行を通常テキストの上に半透明描画

### 7.7 Indent guides (v2.0 新規)

**設計:**
- インデント階層を薄い縦線で示す
- 現在のカーソル位置のインデントレベルはハイライト (VSCode の "Bracket Pair Colorization" 相当)
- 実装は `LineLayout` にインデント深さを保持、`drawIndentGuidesOnLine` で描画

### 7.8 Rendering との統合
- Rendering の `LineLayout` が `std::vector<Token>` を保持
- `DirectWrite` の `IDWriteTextLayout::SetDrawingEffect` でトークンごとにブラシを設定
- 色定義は Theme (`docs/design/detailed_design.md` §5 の Theme に統合)

### 7.9 非同期増分解析
- Syntax Worker Thread (1 本)
- `DocumentChanged` イベントを受け、変更範囲を含む「解析単位」(TextMate: 影響行〜次の中立点、tree-sitter: 影響サブツリー) だけ再解析
- 解析中は古いトークンを描画に使い続ける (60fps 死守)
- 解析完了後 `PostMessageW(WM_APP+SYNTAX_READY, ...)` で UI スレッドへ通知、`invalidate(range)`

### 7.10 折り畳み / アウトライン
- `FoldingModel` (新規 `src/core/folding_model.{h,cpp}`) がドキュメント論理行 → 表示行の対応表
- `Viewport` が表示行で管理、`Rendering` は表示行で描画、内部で論理行に変換
- 折り畳みマーカは Line Gutter の右端に `+/-`
- アウトライン UI (`src/ui/outline_pane.{h,cpp}`) は右側に折り畳みツリー (Win32 `WC_TREEVIEW`)

### 7.11 性能目標
- 100 万行 C++ ファイルの初回全解析: ≤ 5 秒 (バックグラウンド)
- 1 文字入力後の増分解析: ≤ 50ms
- 折り畳み展開/折りたたみ: ≤ 100ms (10000 fold)
- ミニマップ描画: 60fps
- Breadcrumb 更新: ≤ 50ms
- Sticky scroll 追従: 60fps

### 7.12 影響ファイル
- **新規:** `src/syntax/{syntax_engine.cpp, textmate_grammar.cpp, treesitter_language.cpp (二次候補), token_stream.cpp, outline_extractor.cpp, folding_computer.cpp, semantic_token_provider.cpp}`、`include/neomifes/syntax/syntax.h`、`src/core/folding_model.{h,cpp}`、`src/ui/{outline_pane.{h,cpp}, minimap.{h,cpp}, breadcrumb.{h,cpp}}`、`src/render/{sticky_scroll.{h,cpp}, indent_guides.{h,cpp}}`、`third_party/` にパーサ
- **変更:** `src/render/render_pipeline.cpp` (トークン着色、sticky scroll、indent guides、minimap の描画統合)、`src/render/line_layout.cpp` (Token 保持)、`src/app/main.cpp` (アウトライン/minimap/breadcrumb 配線)、`src/document/document.cpp` (DocumentChanged 通知)

---

## 8. Phase 8 — プラグインエンジン + SDK + サンドボックス + マーケットプレース基盤

### 8.1 機能ビジョン
- **凌駕元:** サクラの JS プラグイン、秀丸のマクロ、VSCode の拡張機能マーケットプレース
- **凌駕ポイント:** C ABI で公開しホットロード可能。**プラグインは Windows AppContainer/Job Object でサンドボックス**、権限モデル明示。**マーケットプレース連携基盤** を Phase 8 で実装、公式マーケットは Phase 12 出荷後に運営開始

### 8.2 UI/UX
- `Ctrl+Shift+X` — プラグイン管理ウィンドウ
- 一覧・有効/無効切替・アンロード・リロード・マーケットプレースからインストール
- プラグイン設定 (JSON5 でスキーマ駆動 UI)
- 権限一覧 (「このプラグインはネットワークアクセス・ファイルシステム・サブプロセス起動を要求しています」)

### 8.3 データ構造・アルゴリズム

**C ABI 境界 `include/neomifes/plugin_sdk.h`:**
```c
#ifdef __cplusplus
extern "C" {
#endif

typedef struct NeoMifesPluginContext NeoMifesPluginContext;
typedef struct NeoMifesDocument      NeoMifesDocument;

typedef struct NeoMifesPluginInfo {
    const wchar_t* id;
    const wchar_t* name;
    const wchar_t* version;
    const wchar_t* author;
    unsigned int   apiVersion;
    // v2.0: 明示的な権限要求
    unsigned int   permissions;   // Network | Filesystem | Subprocess | Registry | Clipboard
} NeoMifesPluginInfo;

typedef struct NeoMifesPluginVTable {
    void (*onLoad)(NeoMifesPluginContext* ctx);
    void (*onUnload)(NeoMifesPluginContext* ctx);
    void (*onDocumentChanged)(NeoMifesPluginContext* ctx, NeoMifesDocument* doc,
                              const wchar_t* changeJson);
} NeoMifesPluginVTable;

__declspec(dllexport) const NeoMifesPluginInfo*  neomifes_plugin_info(void);
__declspec(dllexport) const NeoMifesPluginVTable* neomifes_plugin_vtable(void);

typedef struct NeoMifesCoreApi {
    unsigned int apiVersion;
    void   (*insertText)(NeoMifesDocument* doc, const wchar_t* text, unsigned line, unsigned column);
    void   (*deleteRange)(NeoMifesDocument* doc, unsigned lineStart, unsigned columnStart,
                          unsigned lineEnd,   unsigned columnEnd);
    unsigned (*getLineCount)(NeoMifesDocument* doc);
    void   (*getLineText)(NeoMifesDocument* doc, unsigned line, wchar_t* buffer, unsigned bufferLen);
    void   (*registerCommand)(NeoMifesPluginContext* ctx, const wchar_t* id,
                              void (*callback)(NeoMifesPluginContext*));
    void   (*showToast)(NeoMifesPluginContext* ctx, const wchar_t* message);
    // v2.0: 権限が付与されている場合のみ非 NULL
    // Network
    int    (*httpRequest)(NeoMifesPluginContext* ctx, const wchar_t* url,
                          const char* body, char* responseBuffer, unsigned bufferLen);
    // Filesystem (プラグイン専用ディレクトリのみ)
    int    (*readPluginData)(NeoMifesPluginContext* ctx, const wchar_t* relativePath,
                             char* buffer, unsigned bufferLen);
    int    (*writePluginData)(NeoMifesPluginContext* ctx, const wchar_t* relativePath,
                              const char* data, unsigned len);
} NeoMifesCoreApi;

#ifdef __cplusplus
}
#endif
```

**サンドボックス設計 (v2.0 新規):**
- **プラグインは Job Object でリソース制限** (メモリ・CPU 時間・ハンドル数上限)
- **プロセス分離オプション:** 高危険度プラグイン (network + subprocess 両権限) は別プロセスで実行、IPC (Named Pipe) で API 呼出
- **Windows AppContainer 化 (Phase 8b):** さらに厳しい隔離が必要な場合に AppContainer を使用、Capability に基づく細粒度権限
- **クラッシュ隔離:** 各コールバック呼出を SEH `__try/__except`、クラッシュ時はプラグイン無効化 + ログ + ユーザー通知

**マニフェスト検証:**
- `%APPDATA%\NeoMIFES\plugins\<id>\manifest.json5`
- スキーマ: `id, name, version, author, apiVersion, permissions (network, filesystem, subprocess, registry, clipboard), signature, minCoreVersion, maxCoreVersion`
- 未署名プラグインは初回ロード時に確認ダイアログ (Enterprise 設定で無効化)
- **署名検証** — 本体と同じ Authenticode 検証チェーン

**マーケットプレース基盤 (Phase 8c):**
- `src/marketplace/{client.cpp, catalog.cpp, installer.cpp}` — カタログ取得・インストール
- カタログはシンプルな静的 JSON をホスト (S3 or GitHub Pages)、初期はプラグイン ID + バージョン + ダウンロード URL のみ
- Phase 12 出荷後、`marketplace.neomifes.dev` (仮) で運用開始

### 8.4 性能目標
- プラグインロード: ≤ 100ms/個
- コールバック 1 回のオーバーヘッド: ≤ 10μs (inproc)、≤ 100μs (別プロセス IPC)
- 10 個ロード状態でも起動時間 ≤ 500ms
- サンドボックス化のオーバーヘッド: ≤ 5%

### 8.5 テスト戦略
- サンプル: `plugins/samples/{hello_plugin, word_count, uppercase_command, network_client, filesystem_reader}/`
- 単体: apiVersion 不一致で拒否、`onLoad` throw で無効化、二重ロード検出、ホットアンロードで参照カウント確認、権限違反時の API 呼出拒否
- ソーク: 100 回ロード/アンロード、24 時間でハンドルリーク無し
- セキュリティ: 権限の無い API 呼出時のエラー、サンドボックス突破試行 (fuzz)

### 8.6 影響ファイル
- **新規:** `src/plugin/{plugin_host.{h,cpp}, plugin_manifest.{h,cpp}, plugin_permission.{h,cpp}, plugin_sandbox.{h,cpp}, plugin_ipc.{h,cpp}}`、`src/marketplace/{client.{h,cpp}, catalog.{h,cpp}, installer.{h,cpp}}`、`include/neomifes/plugin_sdk.h`、`plugins/samples/` 5 種、`tests/integration/plugin_load_test.cpp`
- **変更:** `src/app/main.cpp` (プラグインロード配線、`Ctrl+Shift+X`)、`src/core/command_dispatcher.cpp` (プラグイン Command 受入)、`CMakeLists.txt` (SDK ヘッダ配布、サンプルビルド)

---

## 9. Phase 9 — AI プラグイン (Claude 統合 + Copilot 型補完 + RAG + マルチモデル + ローカル LLM)

v2.0 大幅拡張: **Copilot 型ゴーストテキスト補完、RAG (Retrieval-Augmented Generation)、マルチモデル並列比較、ローカル LLM 対応、AI エージェント** を Phase 9 に統合。

### 9.1 機能ビジョン
- **凌駕元:** VSCode + GitHub Copilot + Cursor + Continue.dev の総合体験
- **凌駕ポイント:** 「Windows ネイティブ・完全プラグイン境界・オフライン動作可・プライバシー最上級」を全て備えた統合 AI 体験。**単一エディタで Copilot 型補完・インラインチャット・マルチモデル並列比較・エージェント・RAG・ローカル LLM を全て提供**。API キーは Credential Manager (DPAPI) 経由で暗号化保存、コアには一切漏れない

### 9.2 対応 AI プロバイダ (Phase 9 一次スコープ)
- **クラウド:** Claude (Anthropic) / ChatGPT (OpenAI) / Gemini (Google) / OpenAI 互換 API (Groq/DeepSeek 等)
- **ローカル:** Ollama / llama.cpp / OpenAI 互換ローカルサーバ

### 9.3 提供機能 (要件定義書 §7 + v2.0 拡張)

| 機能 | UI/UX | プロンプト戦略 |
|---|---|---|
| **Copilot 型ゴーストテキスト補完** (v2.0 新規) | カーソル位置に薄いグレー文字で候補、Tab で採用、Esc で拒否 | 前後 100 行をコンテキスト、キー入力ごとに 300ms デバウンス、Fast model (Haiku/GPT-4o-mini 相当) を優先 |
| **インラインチャット** | `Ctrl+I` でカーソル位置に半透明パネル | 選択範囲があれば含める、無ければ前後 300 行 |
| **AI エージェント** (v2.0 新規) | `Ctrl+Shift+A` でエージェントペイン、Tool 使用可 (ファイル読/書、grep、Terminal 起動) | Claude/GPT の Tool use API、複数ステップ実行を可視化 |
| **RAG (ドキュメント全体検索)** (v2.0 新規) | Phase 8 のプラグインで実装、`Ctrl+Alt+Q` で「プロジェクト全体から関連コード抽出 → AI 質問」 | Grep + 埋め込みベクトル検索、上位 K 個のスニペットをコンテキストに |
| **マルチモデル比較** (v2.0 新規) | 同じプロンプトを 2-3 モデルに並列送信、結果を横並び表示 | ユーザーが最良の 1 つを選択して採用 |
| コードレビュー | 選択範囲 + `Ctrl+Alt+R` | プロジェクト設定の Coding Guide を system prompt |
| コード生成 | インラインチャット | 選択位置の前後 300 行を context |
| ログ解析 (P1 用) | ログ解析モード連携 (Phase 10) | ERROR 抽出済み行を context |
| SQL 改善 | 選択範囲 + `Ctrl+Alt+S` | SQL dialect 自動判定 |
| Shell 生成 | インラインチャット | OS = Windows、PowerShell 優先 |
| 文章要約 | 選択範囲 + `Ctrl+Alt+U` | 出力言語 = 入力言語 |
| 翻訳 | 選択範囲 + `Ctrl+Alt+T` | 対象言語をコマンドパレット |
| 説明 | 選択範囲 + `Ctrl+Alt+E` | 適切な粒度で説明 |
| コメント生成 | 選択関数 + `Ctrl+Alt+C` | 言語別 doc-comment スタイル |
| リファクタリング | 選択範囲 + `Ctrl+Alt+F` | AST 情報は使わない (シンプルさ優先) |
| エラー解析 | LSP 診断メッセージから (Phase 11 連携) | 診断 + 該当行を context |

### 9.4 データ構造・アルゴリズム

**AI プラグイン `src/ai/` (別ビルドターゲット、DLL):**
```cpp
namespace neomifes::ai {

struct ChatMessage {
    enum class Role { System, User, Assistant, Tool };
    Role role;
    std::u16string content;
    std::optional<std::vector<ToolCall>> toolCalls;   // v2.0: Tool use API
};

struct GenerateRequest {
    std::vector<ChatMessage> messages;
    int maxTokens         = 4096;
    double temperature    = 0.7;
    std::u16string model;
    // v2.0:
    std::vector<ToolDefinition> tools;   // エージェント用
    bool stream = true;
};

class IAiProvider {
public:
    virtual ~IAiProvider() = default;
    // ストリーミング応答: chunk callback は AI Worker Thread から呼ばれる
    virtual void generate(const GenerateRequest& req,
                          std::function<void(std::u16string_view chunk)> onChunk,
                          std::function<void(std::optional<std::u16string> error)> onComplete) = 0;
    virtual void cancel() = 0;
    // v2.0: 埋め込み (RAG 用)
    virtual std::vector<float> embed(std::u16string_view text) = 0;
};

class ClaudeProvider   : public IAiProvider { /* ... */ };
class OpenAiProvider   : public IAiProvider { /* ... */ };
class GeminiProvider   : public IAiProvider { /* ... */ };
class OllamaProvider   : public IAiProvider { /* ローカル */ };

// v2.0: Copilot 型補完エンジン
class InlineCompletionEngine {
public:
    void requestCompletion(const document::Document& doc,
                           document::TextPos cursor,
                           std::function<void(std::u16string suggestion)> onReady);
    void cancel();

private:
    IAiProvider* m_fastProvider;   // Haiku/GPT-4o-mini 相当
    UINT_PTR     m_debounceTimer = 0;
};

// v2.0: RAG エンジン
class RagIndexer {
public:
    // プロジェクトファイルを埋め込みベクトル化してローカルに保存
    void buildIndex(const std::filesystem::path& root,
                    std::function<void(std::size_t done, std::size_t total)> onProgress);
    // クエリで上位 K 個のスニペットを返す
    std::vector<RagResult> search(std::u16string_view query, std::size_t k);

private:
    // 埋め込みベクトルストア (自前実装、FAISS/Hnswlib 依存無し)
    // `%LOCALAPPDATA%\NeoMIFES\rag\<hash>\` に保存
    std::unique_ptr<VectorStore> m_store;
    IAiProvider*                 m_embedProvider;
};

// v2.0: エージェント (Tool use)
class Agent {
public:
    void run(const std::u16string& userTask,
             std::function<void(const AgentStep&)> onStep);

private:
    IAiProvider*                    m_provider;
    std::vector<ToolDefinition>     m_tools;     // read_file / write_file / grep / run_command
    std::vector<ChatMessage>        m_history;
};

}  // namespace neomifes::ai
```

**HTTP クライアント選定 (Phase 9a で ADR-004 決定):**
- **一次候補:** WinHTTP (Windows 標準、依存無し)
- **二次候補:** libcurl (静的リンク、動作確実性)
- **決定基準:** ストリーミング応答 (chunked transfer / SSE) の実装容易性

**API キー保管:**
- Windows Credential Manager (`CredWriteW` / `CredReadW`)
- Target Name: `NeoMIFES/AI/<provider>`
- Type: `CRED_TYPE_GENERIC` (DPAPI 自動暗号化)
- 設定入力時のみメモリ平文、AI Worker Thread で HTTP ヘッダに埋めた後即 zero-fill

**インラインチャット UI:**
- カーソル位置に半透明パネル (Win32 レイヤードウィンドウ)
- 入力欄 (WC_EDIT) + 応答領域 (Direct2D)
- Enter で送信、Esc でキャンセル (ストリーミング中もキャンセル可能、`CancelHttpRequest`)
- 応答完了後 `Ctrl+Enter` でカーソル位置に挿入 (通常 InsertTextCommand として Undo 可)

**Copilot 型ゴーストテキスト補完 UI:**
- カーソル位置の右側に薄いグレー文字で候補テキスト (Rendering 側で `DrawTextW` に半透明色)
- Tab で採用 (通常の InsertTextCommand として Undo 可)、Esc または他キー入力で拒否
- キー入力ごとに 300ms デバウンス、fast model 優先
- ユーザーが Tab した割合を計測 (プライバシー配慮で opt-in、割合のみ記録)

**プレビュー UI (コードレビュー・リファクタ):**
- 応答が「Diff 形式」の場合、`git diff` 風の差分ビュー
- 承認 → Command 化して apply、拒否 → 破棄

### 9.5 セキュリティ・プライバシー
- **AI コンテキストに含めるデータの明示:** ユーザー選択範囲 + カーソル前後 N 行以外は送信しない (RAG は明示的にユーザーがトリガした時のみ)
- **オプトイン明示:** 初回起動時「AI プラグインを有効化しますか」、無効時は 100% ネットワーク I/O 発生ゼロ
- **監査ログ:** `%LOCALAPPDATA%\NeoMIFES\logs\ai-YYYYMMDD.jsonl` にリクエスト/応答の要約 (トークン数のみ、内容非記録)
- **完全オフライン開発:** Ollama など localhost 完結モードで動作可能
- **キーロガー対策:** API キー入力時は Direct2D 描画で「見えない」入力、コピーペースト以外の手段を優先

### 9.6 性能目標
- API キー未設定時は AI プラグイン非ロード (起動 300ms/20MB への影響ゼロ)
- API キー設定時、AI プラグイン初期化: ≤ 50ms
- ストリーミング応答の最初のチャンク: API 素の応答時間 + 10ms 以内
- Copilot 型補完のキー入力から候補表示: ≤ 500ms (fast model 使用時)
- RAG インデックス構築 (10000 ファイル): ≤ 5 分 (バックグラウンド)
- RAG クエリ: ≤ 100ms (top-K 検索)

### 9.7 テスト戦略
- 単体: プロバイダごとのリクエスト JSON 組立、レスポンス JSON パース、SSE ストリーミング分割、ゴーストテキスト補完のデバウンス、RAG のベクトル類似度計算
- モック: `IHttpClient` インターフェースでテスト用モック
- 統合: プロバイダ切替、キャンセル、タイムアウト、複数プロバイダ並列
- 手動: 各プロバイダで代表機能実行、応答品質を目視確認 (Phase 9 完了判定条件)

### 9.8 影響ファイル
- **新規:** `src/ai/{ai_provider.h, claude_provider.cpp, openai_provider.cpp, gemini_provider.cpp, ollama_provider.cpp, http_client.cpp, sse_parser.cpp, credential_store.cpp, inline_completion_engine.cpp, rag_indexer.cpp, vector_store.cpp, agent.cpp, tool_registry.cpp}`、`src/ui/{inline_chat.{h,cpp}, ai_diff_preview.{h,cpp}, agent_pane.{h,cpp}, multi_model_view.{h,cpp}}`、`plugins/ai_plugin/` (DLL)、`tests/unit/ai_*_test.cpp`
- **変更:** `src/app/main.cpp` (`Ctrl+Alt+*` / `Ctrl+I` / `Ctrl+Shift+A` / `Ctrl+Alt+Q` 配線、初回オプトインダイアログ)、`src/render/render_pipeline.cpp` (ゴーストテキスト描画)、`docs/decisions/ADR-004-http-client.md` (Superseded 記録)

---

## 10. Phase 10 — ログ解析 / CSV / JSON-XML Tree (本ソフト最大の差別化章)

### 10.1 ログ解析モード (要件定義書 §8) — 本ソフト最大の差別化点

v2.0 大幅拡張: **リアルタイムテール、分散トレース ID 対応、Structured Log (JSON/Logfmt) 対応、正規表現テンプレート、統計ダッシュボード** を追加。

#### 機能ビジョン
数十 GB のログを ERROR/WARNING 抽出しながら時系列ジャンプで探索できる、Windows で類を見ないログエディタ。**分散システム時代の SAP/AWS/Azure ログを 1 つのビューで統合探索可能**。

#### 対象 (v2.0 拡張)
SAP / AWS CloudTrail / Azure Monitor / Linux syslog / Windows Event Log Text Export / Apache / Nginx / Oracle alert.log / SAP HANA / Tomcat catalina.out / Java (log4j/logback/logback-json) / Docker / Kubernetes (kubectl logs, JSON) / **OpenTelemetry** / **AWS X-Ray** / **Grafana Loki (Logfmt)** / **Fluentd/Fluent Bit 出力**

#### UI/UX
```
┌──────────────────────────────────────────────────────────────────────┐
│  MainWindow (Log Mode)                                               │
│  ┌─────────┬────────────────────────────────────────────┬─────────┐ │
│  │ Filter  │  2026-07-19 10:15:32 INFO  App started     │ Stats   │ │
│  │ □ INFO  │  2026-07-19 10:15:33 WARN  Config missing  │ INFO 32 │ │
│  │ ☒ WARN  │  2026-07-19 10:15:34 ERROR DB conn failed  │ WARN  5 │ │
│  │ ☒ ERROR │  ... traceId=abc123 spanId=def             │ ERROR 12│ │
│  │ Time    │                                            │ Total 49│ │
│  │ ├ 10:15 │                                            │         │ │
│  │ └ 11:00 │                                            │ Trace   │ │
│  │ Trace   │                                            │ abc123  │ │
│  │ □abc123 │                                            │ (5 msgs)│ │
│  └─────────┴────────────────────────────────────────────┴─────────┘ │
│  [Tail: ON]  [Refresh: 500ms]  [Follow last line: ON]                │
└──────────────────────────────────────────────────────────────────────┘
```

- **左ペイン:** レベルフィルタ + 時系列ツリー + **トレース ID 一覧** (v2.0)
- **中央:** 通常テキスト、行の色分け、**トレース ID ハイライト** (同一 traceId の行を強調)
- **右ペイン (v2.0):** 統計ダッシュボード (レベル別カウント、時間帯別ヒストグラム、トップエラーメッセージ)
- **リアルタイムテール** (v2.0): `IO Watcher` で末尾追加を検知、自動スクロール

#### データ構造・アルゴリズム
```cpp
// src/logmode/log_pattern.h
struct LogPatternRule {
    std::u16string id;
    std::u16string regex;
    std::u16string levelField;
    std::vector<std::u16string> levelMap;
    // v2.0:
    std::u16string traceIdField;   // 分散トレース対応
    std::u16string spanIdField;
    bool           isStructured = false;   // JSON/Logfmt 判定
};

// 組込パターンを %APPDATA% にコピーしてユーザー編集可能に

class LogModel {
public:
    void attach(document::Document& doc, const LogPatternRule& rule);
    struct LogLine {
        document::TextPos    pos;
        std::optional<Timestamp> timestamp;
        LogLevel             level;
        // v2.0:
        std::u16string       traceId;
        std::u16string       spanId;
        std::u16string       message;
    };
    [[nodiscard]] std::span<const LogLine> lines() const noexcept;
    void applyFilter(LogFilter filter);
    // v2.0: リアルタイムテール
    void enableTail(bool enabled);
    // v2.0: 統計
    [[nodiscard]] LogStatistics computeStatistics() const;
};
```

- **インデックス構築は非同期・チャンク単位** (Piece Table のピース単位、100 万行以上でも UI ブロックなし)
- インデックス構築中はプログレスバー、確定範囲から順に色分け反映
- タイムスタンプ検出は「先頭 100 行で最頻の日付フォーマットを推定」→ 全体適用
- 時系列ジャンプは B+Tree
- **トレース ID インデックス:** 同一 traceId の行を連結して表示 (v2.0)

#### 性能目標
- 10GB ログの初回インデックス構築: ≤ 60 秒 (バックグラウンド)
- インデックス構築中のスクロール: 60fps 維持
- レベルフィルタ切替: ≤ 100ms
- 時系列ジャンプ: ≤ 50ms
- リアルタイムテール更新: ≤ 500ms
- 統計ダッシュボード計算: ≤ 200ms

#### 影響ファイル
- **新規:** `src/logmode/{log_pattern.h, log_pattern_loader.cpp, log_model.cpp, log_filter.cpp, timestamp_parser.cpp, trace_indexer.cpp, log_statistics.cpp, log_tail_watcher.cpp}`、`src/ui/{log_mode_pane.{h,cpp}, log_stats_pane.{h,cpp}}`、`assets/log_patterns/*.json5` (組込パターン 16 種、v2.0 で OpenTelemetry/X-Ray/Loki/Fluentd 追加)、`tests/unit/logmode_*_test.cpp`
- **変更:** `src/app/main.cpp` (ログモード検出・切替)、`src/core/mode.h` (Mode::Log)

### 10.2 CSV モード (要件定義書 §9)

#### 機能ビジョン
Excel を使わず 1000 万行の CSV を閲覧・軽編集できる。

#### UI/UX
```
┌──────────────────────────────────────────────────────────────────────┐
│  MainWindow (CSV Mode)                                               │
│  ┌───┬─────────┬──────────┬──────────┬──────────┐                    │
│  │ # │ Name    │ Age      │ City     │ Score    │  ← 列固定 (Freeze)  │
│  ├───┼─────────┼──────────┼──────────┼──────────┤                    │
│  │ 1 │ Alice   │ 30       │ Tokyo    │ 85.5     │                    │
│  │ 2 │ Bob     │ 25       │ Osaka    │ 92.1     │                    │
│  └───┴─────────┴──────────┴──────────┴──────────┘                    │
│  [Filter: City == Tokyo] [Sort: Score desc]                          │
└──────────────────────────────────────────────────────────────────────┘
```
- 列固定、フィルタ、ソート
- セル単位クリック編集 (WC_EDIT 子コントロールを出現)
- TSV 対応、区切り文字自動判定 (`,` / `\t` / `;` / `|`)
- **式列 (v2.0):** SUM/AVG/COUNTIF 等の簡易式列 (Excel の一部相当)

#### データ構造・アルゴリズム
```cpp
class CsvModel {
    document::Document* m_doc = nullptr;
    std::vector<std::vector<std::uint32_t>> m_columnOffsets;
    std::vector<std::u16string>            m_headers;
    std::vector<std::size_t>               m_visibleRows;
};
```
- 列オフセット表は遅延構築 (可視範囲のみ)
- フィルタ後の順序は `m_visibleRows` で保持
- 1000 万行対応: `std::uint32_t` (4 バイト × 列数 × 行数)、超過時は列ヘッダ + 都度パース戦略

#### 性能目標
- 1000 万行 CSV の初回パース: ≤ 30 秒
- 列固定スクロール: 60fps
- フィルタ適用 (100 万行): ≤ 1 秒
- ソート (100 万行): ≤ 3 秒

#### 影響ファイル
- **新規:** `src/csvmode/{csv_model.cpp, csv_parser.cpp, csv_filter.cpp, csv_sorter.cpp, csv_expression.cpp (v2.0)}`、`src/ui/csv_grid_view.{h,cpp}`、`tests/unit/csvmode_*_test.cpp`
- **変更:** `src/app/main.cpp` (CSV モード検出)、`src/core/mode.h` (Mode::Csv)

### 10.3 JSON / XML Tree モード (要件定義書 §10)

#### 機能ビジョン
JSON / XML の階層をツリーで見つつ、テキストとしても編集できる。**三大エディタが持たない差別化点**。

#### UI/UX
```
┌──────────────────────────────────────────────────────────────────────┐
│  MainWindow (JSON Mode)                                              │
│  ┌──────────────────┬─────────────────────────────────────────────┐  │
│  │ ▼ root           │ {                                            │  │
│  │   ▼ users [3]    │   "root": {                                  │  │
│  │     ▼ [0]        │     "users": [                               │  │
│  │       name: Alice│       { "name": "Alice", "age": 30 }, ...    │  │
│  │       age: 30    │                                              │  │
│  └──────────────────┴─────────────────────────────────────────────┘  │
│  [Format] [Validate] [XPath: /root/users[0]/name] [Copy Path]        │
└──────────────────────────────────────────────────────────────────────┘
```
- 左ペイン: ツリー、右ペイン: テキスト
- ノードクリックでテキスト側スクロール、逆にテキストカーソル位置に対応するノードをハイライト
- 整形 / バリデーション / XPath (XML) / JSONPath (JSON)

#### データ構造・アルゴリズム
- JSON: `simdjson` 検討 (Phase 10 着手時 ADR)
- XML: `pugixml` (MIT、軽量)
- 巨大 JSON (1GB+) は SAX 解析 + 部分ツリー展開
- XPath / JSONPath は自前実装

#### 性能目標
- 100MB JSON のツリー構築 (先頭部分のみ): ≤ 500ms
- ノード展開: ≤ 100ms
- 整形: ≤ 1 秒 (10MB JSON)

#### 影響ファイル
- **新規:** `src/tree/{json_parser.cpp, xml_parser.cpp, tree_model.cpp, xpath.cpp, jsonpath.cpp, formatter.cpp}`、`src/ui/tree_view_pane.{h,cpp}`、`tests/unit/tree_*_test.cpp`
- **変更:** `src/app/main.cpp` (JSON/XML モード検出)、`src/core/mode.h` (Mode::JsonTree / Mode::XmlTree)

---

## 11. Phase 11 — Git 統合 / LSP 完全実装 / マクロ (Lua + JS + 秀丸互換)

### 11.1 Git 統合 (要件定義書 §11)

#### 機能ビジョン
- **凌駕元:** 秀丸の DIFF ビュー、VSCode の GitLens
- **凌駕ポイント:** libgit2 で Diff / 3-Way Merge / Blame / Commit / Branch 切替を本体に統合、**GitLens 相当のインライン Blame 表示**

#### UI/UX
- 左ガター (Line Gutter の左) に差分マーカ (Modified/Added/Deleted)
- `Ctrl+Shift+G` — Git ペイン
- Diff: `Alt+D` — 現在ファイルと HEAD、side-by-side / inline 切替
- Blame: `Alt+B` — 行ごとに commit hash + author 表示
- **インライン Blame (v2.0):** 各行末尾に「Author, N ago」を薄く表示 (GitLens 相当、opt-in)

#### 影響ファイル
- **新規:** `src/git/{git_repo.cpp, diff_computer.cpp, blame_reader.cpp, commit_dialog.cpp, inline_blame.cpp}`、`src/ui/{git_pane.{h,cpp}, diff_view.{h,cpp}}`、`tests/integration/git_*_test.cpp`
- **変更:** `src/render/line_gutter.cpp` (差分マーカ)、`src/render/render_pipeline.cpp` (インライン Blame)、`src/app/main.cpp` (Git ペイン配線)、`third_party/libgit2/`

### 11.2 LSP 統合 — 完全実装 (v2.0 大幅拡張)

v2.0 追加: **Semantic tokens, Code lens, Inlay hints, Workspace symbols, Rename, Refactor, Signature help, Document formatting, Code actions** — LSP 3.17 のほぼ全機能。

#### 機能ビジョン
- **凌駕元:** VSCode の LSP
- **凌駕ポイント:** C++ / TypeScript / Python の 3 言語で LSP 3.17 のほぼ全機能を Windows ネイティブ品質で実装

#### 提供機能 (VSCode 相当を目指す)

| LSP 機能 | UI/UX |
|---|---|
| **補完** | `Ctrl+Space` — 自前 D2D ドロップダウン、ゴーストテキスト補完 |
| **定義ジャンプ** | `F12` — 該当ファイルへ、Peek preview `Alt+F12` |
| **ホバー情報** | `Ctrl+マウスホバー` — ツールチップ |
| **診断メッセージ** | 該当行アンダーライン + ガター警告アイコン + 別ペイン一覧 |
| **Semantic tokens** (v2.0) | Phase 7 のシンタックスハイライトと重ねて表示 |
| **Code lens** (v2.0) | 関数上に「N references」「Run test」等のアクションリンク |
| **Inlay hints** (v2.0) | 型名・パラメータ名を薄く表示 |
| **Workspace symbols** (v2.0) | `Ctrl+T` — プロジェクト全体のシンボル検索 |
| **Rename** (v2.0) | `F2` — シンボル名変更、影響ファイル全て一括 |
| **Code actions** (v2.0) | `Ctrl+.` — Quick Fix、Refactor 候補 |
| **Signature help** (v2.0) | `(` 入力時に関数シグネチャ表示、パラメータ強調 |
| **Document formatting** (v2.0) | `Shift+Alt+F` — フォーマッタ実行 |
| **Go to implementation / references** (v2.0) | `Shift+F12` (References) / `Ctrl+F12` (Implementation) |
| **Call hierarchy** (v2.0) | `Ctrl+Shift+H` — 呼出関係ツリー |
| **Type hierarchy** (v2.0) | `Ctrl+Shift+T` — 型階層 (継承関係) |

#### データ構造・アルゴリズム
```cpp
class LspClient {
public:
    static std::unique_ptr<LspClient> spawn(const LspServerConfig& config);
    void initialize(const std::filesystem::path& workspaceRoot);
    void didOpen(const std::filesystem::path& file, std::u16string_view text);
    void didChange(const std::filesystem::path& file, const std::vector<TextEdit>& changes);
    void completion(const std::filesystem::path& file, TextPos pos,
                    std::function<void(CompletionList)> onResponse);
    // v2.0 追加:
    void semanticTokens(const std::filesystem::path& file,
                        std::function<void(std::vector<SemanticToken>)> onResponse);
    void codeLens(const std::filesystem::path& file,
                  std::function<void(std::vector<CodeLens>)> onResponse);
    void inlayHints(const std::filesystem::path& file, TextRange range,
                    std::function<void(std::vector<InlayHint>)> onResponse);
    void rename(const std::filesystem::path& file, TextPos pos, std::u16string newName,
                std::function<void(WorkspaceEdit)> onResponse);
    void codeAction(const std::filesystem::path& file, TextRange range,
                    std::function<void(std::vector<CodeAction>)> onResponse);
    void workspaceSymbol(std::u16string_view query,
                         std::function<void(std::vector<SymbolInfo>)> onResponse);
    // ...
};
```

- 対応サーバ: clangd / typescript-language-server / pylsp
- サーバは子プロセス、stdio 通信
- JSON-RPC 実装は自前 (`std::format` 組立 + 手書きパース、nlohmann/json 依存回避)
- サーバの発見: `%PATH%` 自動検出、無ければ設定ダイアログ

#### 影響ファイル
- **新規:** `src/lsp/{lsp_client.cpp, lsp_protocol.cpp, lsp_config.cpp, lsp_message.cpp, semantic_tokens_provider.cpp, code_lens_provider.cpp, inlay_hints_provider.cpp, rename_provider.cpp, code_action_provider.cpp, workspace_symbol_provider.cpp}`、`src/ui/{completion_popup.{h,cpp}, hover_tooltip.{h,cpp}, diagnostics_pane.{h,cpp}, rename_dialog.{h,cpp}, code_actions_menu.{h,cpp}, workspace_symbol_search.{h,cpp}}`、`tests/integration/lsp_*_test.cpp`
- **変更:** `src/app/main.cpp` (LSP 起動配線、全キーバインド)、`src/render/render_pipeline.cpp` (診断アンダーライン、inlay hints、code lens 描画、semantic token 重ね)

### 11.3 マクロ (要件定義書 §12) — Lua + JavaScript + 秀丸互換レイヤ

v2.0 追加: **秀丸マクロ互換レイヤ** (代表的なコマンドと変数を Lua 上にマップ、既存秀丸マクロ資産の移行を支援)

#### 機能ビジョン
- **凌駕元:** 秀丸マクロ (独自スクリプト言語)
- **凌駕ポイント:** Lua 5.4 と JavaScript (QuickJS) の両対応。**秀丸互換 API レイヤ** で既存秀丸マクロ資産を移行可能

#### 提供 API 範囲
```lua
-- 例: Lua マクロ
local doc = neomifes.currentDocument()
local text = doc:getText(1, 1, 10, 1)
doc:insertText(1, 1, "-- header\n")
neomifes.showToast("Inserted!")
```
- `neomifes.currentDocument()` / `openFile(path)` / `saveFile()` / `showToast()` / `command(id)`
- ドキュメント API はプラグイン SDK と同じ Command 経由 (Undo 対応)

**秀丸互換レイヤ (v2.0):**
- `hidemaru.gettotaltext()` → `neomifes.currentDocument():getText()` にマップ
- `hidemaru.showmessage()` → `neomifes.showToast()` にマップ
- 代表的な 50-100 API をカバー、完全互換ではなく「移行しやすさ」を狙う
- 秀丸マクロを丸ごと動かすわけではない (旨は移行ガイドに明記)

#### 影響ファイル
- **新規:** `src/macro/{macro_engine.cpp, lua_bindings.cpp, quickjs_bindings.cpp, macro_recorder.cpp, hidemaru_compat_layer.cpp}`、`plugins/macro_runtime/`、`tests/unit/macro_*_test.cpp`
- **変更:** `src/app/main.cpp` (マクロランタイムロード、Ctrl+Shift+M 記録)

---

## 12. Phase 12 — 総合品質保証・アクセシビリティ検証・出荷判定

### 12.1 目的
全機能を「Google/MS リリース品質」に引き上げる最終フェーズ。

### 12.2 実施項目

**静的解析:**
- MSVC `/analyze` を **CI に統合**
- clang-tidy: `WarningsAsErrors: '.*'` を Release ビルドで有効化検討

**動的解析:**
- ASan / UBSan / CRT Leak Detection: 全テスト + 24 時間クラッシュソーク
- Application Verifier: ハンドルリーク・ヒープ破壊検出
- MSVC `/GS /guard:cf` (Control Flow Guard)
- **fuzz testing (libFuzzer/AFL++):** encoding parser・regex compile・LSP JSON パーサに対するファズ (v2.0 追加)

**巨大ファイル検証 (手動):**
- 10GB UTF-8 テキストで 60fps スクロール
- 10GB ログをログ解析モードで開いて時系列ジャンプ
- 1000 万行 CSV でフィルタ・ソート

**Undo ソーク:** 100 万回連続 Undo/Redo を 24 時間

**プラグインソーク:** 100 個のプラグインを 24 時間ロード/アンロード、リーク 0

**AI 網羅テスト:**
- 各プロバイダで代表機能を全て実行
- API キー未設定・オフライン・タイムアウト・レート制限のエラーパス
- Copilot 型補完の Tab 採用率計測 (opt-in)

**LSP 網羅テスト:**
- C++ / TS / Python の 3 言語で全 LSP 機能
- サーバクラッシュ時の自動再起動

**アクセシビリティ検証 (v2.0 大幅拡張、§16 参照):**
- **NVDA / JAWS スクリーンリーダで全機能操作可能**
- キーボード完結ナビゲーション (マウス無しで全機能)
- 高コントラストモード (Windows 標準)
- カラーブラインドネスモード (Deuteranopia / Protanopia / Tritanopia)
- WCAG 2.2 AA 準拠 (コントラスト比、フォーカス表示、テキスト代替)

**国際化検証 (v2.0):**
- IME: 日本語 (MS-IME/ATOK) + 中国語 (Pinyin) + 韓国語 (한글)
- 高 DPI: 100%, 125%, 150%, 200%, 300%
- HDR: HDR モニタでのカラー精度

**セキュリティ検証 (v2.0):**
- SBOM 生成 (Software Bill of Materials)
- 依存ライブラリの CVE スキャン
- ペネトレーションテスト (プラグインサンドボックス)
- 脆弱性開示プロセスの実地テスト

**コード署名・配布:**
- Authenticode 署名
- MSIX パッケージング
- Portable Zip 版
- **自動更新機構動作確認** (§18 参照)

### 12.3 出荷判定チェックリスト
- [ ] 起動時間 ≤ 300ms (Release、実機実測、統計 p95)
- [ ] 初期メモリ ≤ 20MB (Release、Working Set)
- [ ] 60fps スクロール (10GB ファイル、Release)
- [ ] 100 万 Undo (24 時間ソーク、メモリ膨張無し)
- [ ] 10GB ファイル対応 (全モード)
- [ ] 数 GB Grep ≤ 30 秒
- [ ] クラッシュ 0 (24 時間ソーク)
- [ ] メモリリーク 0 (Application Verifier)
- [ ] MSVC `/analyze` 新規指摘 0
- [ ] clang-tidy 新規指摘 0
- [ ] ASan/UBSan クラッシュ 0
- [ ] fuzz test 24 時間クラッシュ 0
- [ ] 全単体テスト pass
- [ ] AI プラグイン無効時に本体 100% 動作
- [ ] NVDA/JAWS で全機能操作可能
- [ ] WCAG 2.2 AA 準拠 (自動ツール + 手動確認)
- [ ] 日本語 IME + 中韓 IME 入力動作確認
- [ ] Windows 10 21H2 + Windows 11 動作確認
- [ ] Authenticode 署名バイナリ配布
- [ ] MSIX + Portable Zip 版配布
- [ ] SBOM 生成、CVE 0
- [ ] 自動更新機構 (canary → stable) 動作

---

## 13. UI/UX トップレベル方針 (v2.0 大幅拡張)

### 13.1 キーバインドプリセット
- **NeoMIFES 標準** (VSCode ベース)
- **秀丸互換** (F5=マクロ実行、Alt+F=ファイルメニュー、Ctrl+G=grep 等)
- **サクラ互換** (Ctrl+Enter=改行挿入、Alt+↑↓=行移動 等)
- **MIFES 互換** (F9=保存、F10=閉じる 等、要件定義書と MIFES の実キー体系交差を Phase 4b8 で確定)
- **Vim モード** (Phase 8 のプラグインで提供)
- **Emacs モード** (Phase 8 のプラグインで提供)
- 設定ダイアログでプリセット選択、個別カスタマイズ可

### 13.2 コマンドパレット (v2.0 詳細化、§5.2 参照)
- `Ctrl+Shift+P` (VSCode 互換)
- 全コマンドを ID + 説明 + 現在のキーバインドで表示、あいまい検索
- 最近使用ボーナス、ファジー検索、絵文字アイコン
- サブメニュー対応 (「Change Language Mode」→ 言語一覧)
- Phase 5b3 で Find bar と同時実装

### 13.3 ミニマップ (v2.0 新規、§7.4 参照)
- 右側縦帯、1/8 スケール、GPU スケーリング
- シンタックス色を反映、現在の可視領域を強調
- クリックでジャンプ、ドラッグでスクロール

### 13.4 Breadcrumb / Sticky scroll / Indent guides (v2.0 新規、§7.5-7.7 参照)
- **Breadcrumb:** ファイル上部に「ファイル > 名前空間 > クラス > 関数」パス
- **Sticky scroll:** 画面上部に現在スコープの関数シグネチャ・クラス宣言を固定表示
- **Indent guides:** インデント階層を薄い縦線、現在レベルはハイライト

### 13.5 Zen mode / 分割ビュー / タブグループ / ピン留め (v2.0 新規)
- **Zen mode:** `Ctrl+K Z` で全 UI 装飾を隠し、テキストだけを表示 (執筆集中モード)
- **分割ビュー:** `Ctrl+\` で右分割、`Ctrl+K \` で下分割、複数ビューで同じ/異なるファイル
- **タブグループ:** タブ右クリックで「新しいグループへ移動」、グループ間ドラッグ
- **ピン留め:** よく使うファイルをタブ左端に固定

### 13.6 ダーク/ライト/ハイコントラスト + Mica/Acrylic
- Windows のシステム設定 (`SPI_GETIMMERSIVECOLORSET`) を初期値、ユーザー設定でオーバーライド可
- **Windows 11 Mica/Acrylic** — `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)` で Mica、より透過が必要な場合は Acrylic
- **ハイコントラストモード** — Windows 標準の高コントラスト設定を検出し、専用テーマを自動適用 (アクセシビリティ、§16 参照)

### 13.7 高 DPI / HDR / VRR (Variable Refresh Rate)
- **Per-Monitor DPI V2** (Phase 3 完了)
- **HDR:** `IDXGISwapChain::SetColorSpace1` で HDR10、色空間管理 (v2.0 検討)
- **VRR (可変リフレッシュレート):** `IDXGIOutput::WaitForVBlank` + `DXGI_SWAP_EFFECT_FLIP_DISCARD` で G-Sync/FreeSync 対応、スクロール中のティアリング完全排除 (v2.0 検討)

### 13.8 タッチ・ペン・スタイラス (v2.0 新規)
- **Surface Pro/Studio 向けタッチ:** 2 本指スクロール、ピンチズーム
- **ペン:** マーカー機能 (§3) と組み合わせて「重要な行にペンでチェック」
- **スタイラス:** 手書き入力→ OCR (将来検討、v2 コアには含めず)

### 13.9 タブ UI (v2.0 詳細化)
- タブに色分け (フォルダ別、変更あり/無し、Git 状態)
- ドラッグでタブ順並替、ドラッグで別ウィンドウへ
- タブ右クリック → 全て閉じる、右側全て閉じる、他を全て閉じる (VSCode 標準)
- タブ「前のタブに戻る」ヒストリ (`Ctrl+Tab`)

### 13.10 コンテキストメニュー
- テキスト上右クリック: Cut/Copy/Paste + Command Palette 主要項目
- Line Gutter 右クリック: Bookmark、Breakpoint (LSP デバッガ拡張時)、Blame

---

## 14. パフォーマンス予算表 (全機能横断、v2.0 拡張)

| 機能 | 目標 | 測定条件 | 対応 Phase |
|---|---|---|---|
| 起動 (splash 消失まで) | ≤ 300ms | Release、SSD、Windows 11、p95 | Phase 1 (完了、実測 148ms) |
| 初期メモリ | ≤ 20MB | Release、Working Set | Phase 1 |
| 10GB ファイルオープン (先頭表示) | ≤ 100ms | mmap | Phase 6 |
| スクロール | 60fps | 10GB ファイル、Release | Phase 3 (完了) |
| 100 万 Undo | メモリ ≤ 500MB | 差分 + 圧縮 | Phase 4b (完了) |
| 検索 (1MB) | ≤ 100ms | Phase 5a 実測範囲 | Phase 5a (完了、33-39ms/1MB) |
| 検索 (1GB) | ≤ 30 秒 | チャンク並列 | Phase 5c |
| Grep (数 GB) | ≤ 30 秒 | 論理コア数-1 並列 | Phase 5c |
| 置換 (100 万件) | ≤ 5 秒 | 差分エンコード | Phase 5b2 |
| エンコーディング判定 | ≤ 5ms | 64KB head | Phase 6 |
| シンタックス初回全解析 | ≤ 5 秒 | 100 万行 C++ | Phase 7 |
| シンタックス増分解析 | ≤ 50ms | 1 文字入力後 | Phase 7 |
| ミニマップ描画 | 60fps | 100 万行 | Phase 7 |
| Breadcrumb 更新 | ≤ 50ms | カーソル移動 | Phase 7 |
| コマンドパレット表示 | ≤ 50ms | 500 コマンド | Phase 5b3 |
| コマンドパレット・ファジー検索 | ≤ 20ms | 500 コマンド | Phase 5b3 |
| プラグイン 1 個ロード | ≤ 100ms | サンプル | Phase 8 |
| プラグインコールバック | ≤ 10μs (inproc) / ≤ 100μs (別プロセス) | ホット | Phase 8 |
| AI 応答 (最初のチャンク) | プロバイダ API 素 + 10ms | ストリーミング | Phase 9 |
| Copilot 型補完 | ≤ 500ms | fast model | Phase 9 |
| RAG インデックス (10000 ファイル) | ≤ 5 分 | バックグラウンド | Phase 9 |
| RAG クエリ | ≤ 100ms | top-K | Phase 9 |
| ログインデックス (10GB) | ≤ 60 秒 | バックグラウンド | Phase 10 |
| ログ・リアルタイムテール | ≤ 500ms | ファイル追記後 | Phase 10 |
| CSV パース (1000 万行) | ≤ 30 秒 | 遅延構築 | Phase 10 |
| JSON tree 構築 (100MB) | ≤ 500ms | 先頭部分 | Phase 10 |
| LSP 補完応答 | ≤ 100ms | サーバ準備後 | Phase 11 |
| LSP Semantic tokens 更新 | ≤ 200ms | 1000 行変更 | Phase 11 |
| Git Diff 計算 (1MB ファイル) | ≤ 100ms | libgit2 | Phase 11 |

---

## 15. 世界最高速の裏付け技術要素 (v2.0 新規詳細章)

要件定義書 §5「Windows最速」を掲げるからには、なぜ最速なのかの技術的裏付けが必要。本章は「他エディタが実装していない or 実装が浅い」高速化技術を集約して明示する。

### 15.1 SIMD 動的 dispatch (SSE4.2 / AVX2 / AVX-512 / ARM NEON)

**用途:** 検索 (Boyer-Moore-Horspool)、UTF-8 バリデーション、改行検出、エンコーディング判定、CSV パーサ

**戦略:**
- CPU 起動時に `__cpuid` で対応命令セット検出、`std::function` ポインタでディスパッチ
- SSE4.2 `pcmpistri` 命令で 16 バイト一括文字列マッチ
- AVX2 で 32 バイト一括、AVX-512 で 64 バイト一括
- ARM64 対応時 (将来) は NEON `vmaxvq_u8` 等
- ライブラリ選定: 自作 (依存最小)、または `simde` (SSE/AVX を portable に)

**実装場所 (計画):**
- `src/util/simd/{cpu_features.cpp, memchr_simd.cpp, utf8_validate_simd.cpp, memfind_simd.cpp}`
- Phase 5c (Grep) と Phase 6 (Encoding) で本格利用開始

**性能目標:**
- SSE4.2 有効時: memchr の 4-8 倍高速化
- AVX2 有効時: memchr の 8-16 倍高速化
- CPU dispatch のオーバーヘッド: 起動時のみ 1μs

### 15.2 GPU アクセラレーション (Direct2D + Compute Shader 検討)

**用途:** テキスト描画は既に Direct2D で GPU、追加で **並列テキスト検索の GPU compute shader 検討**

**戦略:**
- Phase 5c 完了後、CPU 並列で目標未達なら compute shader で並列 memchr を PoC
- DirectCompute (CS 5.1) で 1024 スレッド並列走査
- CPU-GPU 転送コストと計算コストのトレードオフを実測 (小さいファイルでは CPU が速い、大きいファイルで GPU が有利)

**リスク:**
- GPU ドライバ品質 (Intel iGPU で不安定なケース)、対応 GPU 検出フォールバック必須
- **判断は Phase 12 に持ち越し**、CPU で目標達成なら不採用

### 15.3 Direct Storage (Windows 11 の高速 I/O API、v2.0 新規)

**用途:** 10GB ファイルオープン時の初回読出しを NVMe から直接高速転送

**戦略:**
- Windows 11 の Direct Storage API (`DirectStorageCreateFactory`) を Phase 6 で試験導入
- 対応環境 (Windows 11 + NVMe SSD) で有効化、それ以外は通常 `CreateFileW` にフォールバック
- 起動時に対応判定、非対応環境ではオーバーヘッド 0

**期待効果:**
- 10GB ファイル読出しが 30-50% 高速化 (NVMe の帯域を CPU バイパスで直接活用)

**リスク:**
- API がゲーム界向け発祥のため、テキストエディタでの実用実績が乏しい
- **PoC → 効果不十分なら不採用**

### 15.4 Frame pacing / VRR (Variable Refresh Rate)

**用途:** スクロール中のティアリング完全排除、可変リフレッシュレートモニタでのなめらかさ

**戦略:**
- `IDXGISwapChain2::GetFrameLatencyWaitableObject()` でフレームレイテンシ waitable を取得、`WaitForSingleObject` でフレームタイミング同期
- G-Sync / FreeSync モニタでは可変リフレッシュに従いフレーム提出
- `DXGI_SWAP_EFFECT_FLIP_DISCARD` + `DXGI_PRESENT_ALLOW_TEARING` の組合せを VRR モニタで有効化

**実装場所:**
- `src/render/render_device.cpp` の `beginFrame`/`endFrame` に統合
- Phase 3c の frame skip 機構と組合せ

**性能目標:**
- 60Hz モニタ: 60fps 固定、ジッター ≤ 1ms
- 144Hz モニタ: 144fps 追従、ジッター ≤ 0.5ms
- VRR モニタ: 30-144Hz 可変、スムーズ

### 15.5 キャッシュ最適レイアウト・false sharing 回避

**用途:** Piece Table のノード配置、複数カーソル配列、行インデックス B+Tree

**戦略:**
- 頻繁アクセスされるノード (`Piece` 構造体) を CPU キャッシュライン (64 バイト) にアライン、`alignas(64)`
- スレッド間で共有される可変フィールドは別キャッシュラインに分離 (false sharing 回避)
- Piece Table の RB-Tree ノードは pool allocator で連続配置、キャッシュ密度向上
- **プロファイラで L1/L2 miss を実測** して調整 (Intel VTune / AMD μProf)

### 15.6 Lock-free 並行データ構造

**用途:** UI Thread ↔ Worker スレッド間のイベントキュー、Search Worker Pool のワークキュー

**戦略:**
- 基本設計 §2.4 の MPSC キューは既に Lock-free 前提
- `moodycamel::ConcurrentQueue` (単一ヘッダ、MIT) を候補依存として検討 (Phase 8 の実装時)
- または自作の SPSC ring buffer (依存追加ゼロ、性能十分)

### 15.7 その他の最適化技術要素

- **Compile-time computation:** `constexpr` を積極活用、実行時計算を減らす
- **Template metaprogramming:** SIMD dispatch を型で分岐、実行時分岐オーバーヘッド 0
- **Profile-Guided Optimization (PGO):** リリースビルドで PGO 適用、ホットパスの分岐予測改善
- **Link-Time Optimization (LTO):** 全モジュール横断で inline / dead code elimination
- **Fast startup tuning:** `/DELAYLOAD` で DirectWrite/D2D の DLL 遅延ロード、起動時間削減

---

## 16. 国際化・アクセシビリティ (v2.0 新規)

### 16.1 CJK IME (日本語・中国語・韓国語)

**日本語 (MS-IME / ATOK / Google 日本語入力):**
- `WM_IME_STARTCOMPOSITION` / `WM_IME_COMPOSITION` / `WM_IME_ENDCOMPOSITION` を Direct2D の描画に統合
- 変換中文字列を下線付きインライン表示 (basic_design.md §3.4 の方針を踏襲)
- 候補ウィンドウ位置を `ImmSetCompositionWindow` でカーソル直下に固定
- 全角/半角混在時のカーソル移動は grapheme cluster 単位 (Unicode 16 UAX #29 準拠)

**中国語 (Pinyin / Wubi):**
- 日本語と同じ WM_IME 系メッセージで動作
- 縦棒形の候補ウィンドウ位置調整

**韓国語 (한글 IME):**
- 「Choseong + Jungseong + Jongseong」の合成入力、`WM_IME_COMPOSITION` で処理

### 16.2 RTL (Right-to-Left) 対応

**対象:** アラビア語 / ヘブライ語 (要件定義書には無いが、世界最高峰を掲げるため v2.0 で含める)

**戦略:**
- DirectWrite の `IDWriteTextLayout` は BiDi (Unicode Bidirectional Algorithm) を標準サポート
- Direct2D の描画方向は左→右で書きつつ、実際のグリフ配置は UAX #9 に従い自動
- カーソル移動は logical order (テキストデータ順)、視覚位置は visual order
- RTL ドキュメント全体の RTL 表示は `IDWriteTextLayout::SetReadingDirection` で切替
- **限定的実装:** RTL 対応はレンダリングのみ、UI 全体 (メニュー等) は LTR のまま (v2 スコープ)

### 16.3 Grapheme cluster / Unicode 16

**用途:** カーソル移動、削除、選択範囲の単位

**戦略:**
- Unicode 16 の UAX #29 「Text Boundaries」に準拠した grapheme cluster iteration
- 実装: `src/util/unicode/grapheme_cluster.{h,cpp}` (自作、テーブルは Unicode CLDR から生成)
- 絵文字合成 (家族絵文字 `👨‍👩‍👧‍👦` = 7 コードポイント) を 1 セル 1 カーソル位置として扱う
- Combining characters (`é` = `e` + U+0301) も 1 grapheme

**性能目標:**
- 1MB ドキュメントの grapheme cluster count: ≤ 10ms

### 16.4 UI Automation / スクリーンリーダ対応

**用途:** NVDA / JAWS / Windows Narrator でエディタ全機能操作

**戦略:**
- Win32 標準の UI Automation Provider を実装 (`IRawElementProviderSimple`, `ITextProvider`)
- テキスト領域は `TextPattern` で公開、行/文字/選択範囲/カーソル位置を通知
- 全ダイアログ・ボタン・チェックボックスに `AutomationId` 付与
- スクリーンリーダテスト: NVDA + JAWS 手動確認 (Phase 12)

**実装場所:**
- `src/ui/ui_automation_provider.{h,cpp}`
- Phase 7 完了後、`RenderPipeline` にアクセシビリティレイヤ追加

### 16.5 高コントラスト・カラーブラインドネス

**高コントラストモード:**
- Windows 標準の高コントラスト設定を `SPI_GETHIGHCONTRAST` で検出
- 検出時は専用テーマ (背景=黒/白、テキスト=対比最大色) を自動適用

**カラーブラインドネスモード (v2.0 新規):**
- 設定で選択可: None / Deuteranopia (赤緑) / Protanopia (赤緑) / Tritanopia (青黄)
- シンタックスハイライトのパレットを色覚差別化のあるものに切替
- Diff の Add/Remove 色を色相ではなく明度/形状で区別 (`+` 記号 + 濃緑 vs. `-` 記号 + 濃赤)

### 16.6 WCAG 2.2 AA 準拠

**チェック項目:**
- テキストコントラスト比 ≥ 4.5:1 (通常テキスト) / ≥ 3:1 (大文字)
- フォーカスリング常時表示、キーボード完結ナビゲーション
- ARIA 属性 (UI Automation 相当) の適切な付与
- テキスト代替 (アイコンボタンに代替テキスト)
- 動作アニメーションを止められる (Reduce Motion 対応)

---

## 17. セキュリティ (v2.0 新規)

### 17.1 プラグインサンドボックス (§8.3 参照、詳細)

**レベル 1 (デフォルト):** 同プロセス内 SEH 隔離 + 権限マニフェスト検証
- クラッシュ隔離のみ、悪意あるプラグインは制限しきれない
- 起動時に「未署名プラグインのロードを許可しますか」ダイアログ

**レベル 2 (高危険度権限):** Job Object でリソース制限
- Network 権限を要求するプラグイン全て
- メモリ・CPU 時間・ハンドル数の上限

**レベル 3 (Phase 8b、将来検討):** Windows AppContainer で完全隔離
- Capability に基づく細粒度権限
- ファイルシステムアクセスは Broker 経由

### 17.2 Code signing / SBOM

**Code signing:**
- 本体 exe: Authenticode 署名必須 (basic_design.md §6.6)
- 標準プラグイン DLL: 本体と同一証明書
- サードパーティプラグイン: 署名検証オプション (Enterprise 設定で必須化可能)
- タイムスタンプサーバ: DigiCert / Sectigo (RFC 3161)

**SBOM (Software Bill of Materials):**
- CycloneDX 形式で生成 (`cyclonedx-cli` を CI に統合)
- 依存ライブラリ (RE2, Abseil, libgit2, Lua, QuickJS, simdjson, pugixml, gtest, benchmark) の全 CVE を追跡
- 出荷前に SBOM + CVE 0 を確認

### 17.3 脆弱性開示プロセス (v2.0 新規)

- **セキュリティ連絡先:** `security@neomifes.dev` (仮)、PGP 鍵公開
- **開示ポリシー:** 90 日の Coordinated Disclosure、報告者クレジット (希望時)
- **バグバウンティ:** 出荷後の状況を見て判断 (Phase 12 以降)
- **Advisory:** GitHub Security Advisories を利用、CVE 番号取得

### 17.4 データ暗号化

**保存データ:**
- Undo データ (ディスクスワップ、`%LOCALAPPDATA%\NeoMIFES\undo\`): DPAPI 暗号化 (ユーザー固有鍵)
- Session データ (`%APPDATA%\NeoMIFES\sessions\`): 平文可 (ユーザーが手動編集する想定)
- AI API キー: Credential Manager (DPAPI 自動、§9.5)
- RAG インデックス (`%LOCALAPPDATA%\NeoMIFES\rag\`): DPAPI 暗号化 (プロジェクト情報を含むため)

**転送データ:**
- LSP 通信: 子プロセス stdio のためローカル、暗号化不要
- AI API 呼出: TLS 1.3 必須 (WinHTTP / libcurl どちらも標準対応)
- 自動更新: TLS 1.3 + 署名検証

### 17.5 権限最小化原則

- 本体 exe は User 権限で動作、Administrator を要求しない
- `%APPDATA%` / `%LOCALAPPDATA%` にのみ書込み、`Program Files` には書かない
- レジストリ書込みは HKCU のみ (HKLM に触らない)
- MSIX パッケージング時は Capability を最小限に (`internetClient` のみ、AI 有効時)

---

## 18. リリース・配布・自動更新 (v2.0 新規)

### 18.1 配布形態

| 形態 | 対象 | 特徴 |
|---|---|---|
| **MSIX パッケージ** | P3/P4/P5 (一般ユーザー) | Microsoft Store 配布、自動更新、サンドボックス、Capability 明示 |
| **Portable Zip** | P5 (OSS 開発者) / P7 (エディタホッパー) | 解凍即使用、レジストリ非使用、USB メモリ持ち歩き可 |
| **MSI インストーラ** | P6 (エンタープライズ) | サイレントインストール、ポリシー配布 (GPO)、署名検証 |

### 18.2 自動更新機構

**戦略:**
- **カナリア → ステーブル** の 2 チャネル (安定性向上のため v2 では stable のみ、canary は v3 検討)
- 起動時に `updates.neomifes.dev/latest.json` (仮) を確認、新版があれば「更新可能」通知
- ユーザー同意でダウンロード、次回起動時に適用
- 適用は別プロセス (`neomifes-updater.exe`) で実行、本体を上書きしてから再起動
- **ロールバック:** 更新後 24 時間以内に起動 3 回連続失敗すると自動的に前版へロールバック
- **差分更新 (bsdiff):** 数十 MB のアプリ全体ダウンロードを差分数 MB に圧縮

**プライバシー:**
- 更新チェック時に送信するのは「現在のバージョン + OS 情報 (Win10/11)」のみ
- テレメトリと独立 (テレメトリ opt-out でも更新は動作)

**実装場所 (計画):**
- `src/updater/{update_checker.{h,cpp}, update_downloader.{h,cpp}, update_applier.{h,cpp}}`
- 別 exe `neomifes-updater.exe`

### 18.3 リリースサイクル

- **月次リリース (安定版):** バグ修正・小機能追加
- **四半期リリース (機能追加):** 大きな新機能、Phase 単位のマイルストーン
- **年次リリース (メジャー):** アーキテクチャ変更・破壊的変更 (プラグイン API のメジャー更新等)

### 18.4 リリースノート・変更ログ

- `CHANGELOG.md` を Keep a Changelog 形式で維持
- リリースノートは GitHub Releases + アプリ内 「What's New」画面
- 破壊的変更は必ずマイグレーションガイド添付

---

## 19. KPI / SLO / メトリクス (v2.0 新規、テレメトリ opt-in)

### 19.1 プロダクト成功指標 (KPI)

| 指標 | 目標 | 測定方法 |
|---|---|---|
| **DAU (Daily Active Users)** | 出荷 1 年で 10 万 | opt-in テレメトリ (匿名ユニーク ID) |
| **リテンション (7 日)** | 60% 以上 | opt-in テレメトリ |
| **リテンション (30 日)** | 40% 以上 | opt-in テレメトリ |
| **プラグインインストール率** | ユーザーの 30% 以上 | opt-in テレメトリ |
| **AI 機能利用率** | AI 有効化ユーザーの 50% 以上が毎日使用 | opt-in テレメトリ |
| **クラッシュ率** | ≤ 0.01% セッション | クラッシュレポート (opt-in) |
| **Net Promoter Score (NPS)** | ≥ 50 | 定期アンケート (opt-in) |

### 19.2 パフォーマンス SLI / SLO

| SLI | SLO | 測定 |
|---|---|---|
| 起動時間 | p95 ≤ 300ms, p99 ≤ 500ms | opt-in テレメトリ |
| フレームレート | p95 ≥ 60fps (スクロール中) | opt-in テレメトリ |
| 検索応答時間 | p95 ≤ 100ms (10MB ファイル) | opt-in テレメトリ |
| AI 応答最初のチャンク | p95 ≤ API 素 + 20ms | opt-in テレメトリ |

### 19.3 エラー・クラッシュ SLO

- クラッシュ 0.01%/セッション以下
- LSP サーバダウン率: 0.1%/日以下
- プラグインクラッシュ隔離成功率: 99.9%

### 19.4 テレメトリのプライバシー原則

- **完全 opt-in** (初回起動時のダイアログでデフォルト OFF)
- 送信内容は明示的な同意項目のみ (`docs/telemetry.md` にリスト、ユーザーが選択)
- 個人特定情報 (パス名・ファイル内容・API キー・URL) 一切送信しない
- 送信データはハッシュ済み集計値のみ、raw ログ非保管
- Windows Credential Manager 経由の匿名ユニーク ID (opt-out で削除)

---

## 20. エコシステム戦略 (v2.0 新規)

### 20.1 プラグインマーケットプレース

**フェーズ:**
- **Phase 8 (基盤):** マーケットプレースクライアント実装、静的カタログでロード
- **Phase 12 出荷後 (運営):** `marketplace.neomifes.dev` (仮) 立ち上げ、初期は公式プラグイン 10 種
- **1 年後:** サードパーティプラグイン受付開始、レビュー体制構築

**登録プラグイン (公式初期):**
1. Vim モード
2. Emacs モード
3. AI Copilot 相当 (Phase 9 の一部)
4. Git 拡張 (Phase 11 の一部)
5. LSP マネージャ (Phase 11 の一部)
6. Markdown プレビュー
7. LaTeX プレビュー
8. HTML プレビュー
9. Docker 統合
10. AWS CLI 統合

**品質基準:**
- 全プラグイン Authenticode 署名必須
- ネットワーク権限プラグインは追加審査
- 動的解析 (24 時間ソーク) 通過必須

### 20.2 テーマギャラリー

- カラースキーム (シンタックスハイライト + UI 色) の共有プラットフォーム
- VSCode テーマ変換ツール (`.vscode/themes/*.json` → NeoMIFES 形式)
- 初期は 20 種の公式テーマ (ダーク 10 + ライト 10)

### 20.3 スニペット / マクロ共有

- Snippet 形式: TextMate 互換 (`.snippet.json`)、VSCode との相互運用
- マクロ形式: Lua / JavaScript / 秀丸互換
- 公式リポジトリ + コミュニティ Gist 連携

### 20.4 コミュニティ運営

- **公式:** GitHub Issues (バグ・機能要求)、GitHub Discussions (Q&A)
- **Discord:** リアルタイム質問・雑談
- **月次オフィスアワー:** 開発者が Q&A、機能ロードマップ共有

### 20.5 ライセンス戦略

- **本体:** OSS (Apache License 2.0 検討、GPL 感染を避けるため MIT/BSD/Apache 系)
- **AI プラグイン:** 別ライセンス可 (商用モデル API を扱うため)
- **プラグイン:** ライセンスは開発者選択、マーケットプレースは各プラグインのライセンス表示

---

## 21. 開発品質基盤 (v2.0 新規)

### 21.1 テストピラミッド

| レイヤ | 数量目標 | 実行時間 | 実行タイミング |
|---|---|---|---|
| **単体 (unit)** | 数千 | ≤ 60 秒 | 全 PR CI |
| **統合 (integration)** | 数百 | ≤ 5 分 | 全 PR CI |
| **E2E (UI 自動化)** | 数十 | ≤ 15 分 | 毎日夜間 |
| **ソーク (24 時間)** | 3-5 種 | 24 時間 | 週次 |
| **fuzz (libFuzzer)** | 10-20 targets | 常時 | 継続バックグラウンド |

**現状 (2026-07-19):** 単体 279、統合 少数、E2E/ソーク/fuzz 未整備。Phase 12 に E2E/ソーク/fuzz を集中整備。

### 21.2 パフォーマンス回帰検出

**戦略:**
- google/benchmark で全 Phase のベンチマークを整備 (現状: Phase 3 render_frame_bench, Phase 4a core_undo_stack_bench, Phase 5a search_find_all_bench の 3 種)
- CI で `main` ブランチとの差分を計測、退化 5% 超は警告 / 10% 超は fail
- ベンチマークダッシュボード (Grafana/Datadog 検討) で長期トレンド追跡

### 21.3 フィーチャーフラグ

**用途:**
- 新機能を安全に段階リリース (canary → stable)
- A/B テスト (opt-in ユーザー限定)
- Kill switch (問題発覚時に無効化)

**実装:**
- `src/util/feature_flags.{h,cpp}` — 設定ファイル + テレメトリで受信
- コード内: `if (features::isEnabled("copilot_completion")) { ... }`

### 21.4 クラッシュ収集

**戦略:**
- SEH ハンドラでクラッシュダンプ (`.dmp`) 生成
- ユーザー同意で `crashes.neomifes.dev` (仮) にアップロード (opt-in)
- Symbol Server で PDB 解決、シンボル付きスタックトレース取得
- 重複クラッシュのグルーピング (Sentry.io 相当を自作、または OSS `Sentry` の Windows 対応版を検討)

### 21.5 Bisect ツール

**用途:** リグレッションの原因コミット特定

**戦略:**
- `git bisect` を GUI 化する内蔵ツール (Phase 12 以降)
- ユーザーが「以前は動いた、今は動かない」と報告 → 2 つのバージョン間で自動 bisect スクリプト
- CI ビルドアーティファクトを保存 (S3 相当)、bisect 時にダウンロードして検証

### 21.6 ドキュメント

**開発者向け:**
- API リファレンス (Doxygen 自動生成)
- アーキテクチャ図 (basic_design.md + master_roadmap.md)
- 貢献ガイド (`CONTRIBUTING.md`)
- コード規約 (CLAUDE.md)

**エンドユーザー向け:**
- ユーザーマニュアル (`docs/user/`)
- キーバインドリファレンス
- プラグイン開発ガイド (`docs/plugin_dev/`)
- 秀丸マクロ移行ガイド (`docs/migration/hidemaru_to_neomifes.md`)

### 21.7 Windows シェル統合 (v2.0 新規)

**機能:**
- **右クリックメニュー:** 「NeoMIFES で開く」を Explorer に追加 (レジストリ登録 or Package Manifest の `FileTypeAssociation`)
- **Jump List:** タスクバーのアイコン右クリックで「最近開いたファイル」「最近のプロジェクト」「新規ウィンドウ」
- **Taskbar thumbnail preview:** ウィンドウのミニプレビューにドキュメント名を表示 (`DwmSetWindowAttribute`)
- **Windows Terminal 連携:** `wt neomifes.exe <path>` で開く公式サポート
- **File Explorer 検索インデックス連携 (検討):** Windows Search で NeoMIFES で編集中のファイルをインデックス

---

## 22. リスク・未決事項の再整理 (v2.0 拡張)

| # | リスク/未決 | 対応 Phase | 判断方法 |
|---|---|---|---|
| R2 | 正規表現エンジン | Phase 5a で RE2 採用済 (ADR-002)、Hyperscan 再評価は Phase 5c の Grep 実測後 | 計測 |
| R3 | シンタックス定義 (TextMate vs tree-sitter) | Phase 7a で PoC | 計測 (§7.3) |
| R4 | LSP 統合の複雑性 | Phase 11a で C++/TS/Python 限定 | スコープ制限 |
| R5 | プラグイン DLL ホットアンロードでのリーク | Phase 8 + Phase 12 ソーク | Application Verifier |
| U#3 | 正規表現エンジン最終選定 | R2 と同じ | — |
| U#4 | シンタックス定義形式 | R3 と同じ (ADR-013) | — |
| U#5 | マクロ言語同梱範囲 | Phase 11.3 で Lua + QuickJS + 秀丸互換レイヤ | 本書 |
| U#6 | LSP 初期対応言語 | Phase 11.2 で C++/TS/Python | 本書 |
| U#7 | 設定ファイル形式 | JSON5 第一候補、Phase 6 完了後に最終確定 | — |
| U#8 | 自動更新機構 | Phase 12 で MSIX + カナリア方式 | §18 |
| U#9 | AI プロバイダの HTTP クライアント | Phase 9a で PoC (WinHTTP vs libcurl) | ADR-004 更新 |
| U#10 | JSON パーサ (simdjson の Windows ABI 適合性) | Phase 10 着手時 | ADR |
| U#11 | Git 統合の libgit2 ライセンス互換 | Phase 11.1 着手時 | 弁護士確認不要範囲で自己判断可 |
| **U#12** (v2.0 新規) | **GPU compute shader 検索の実用性** | Phase 5c 後、CPU 未達なら PoC | 計測 |
| **U#13** (v2.0 新規) | **Direct Storage API のエディタ用途実用性** | Phase 6 で PoC | 計測 |
| **U#14** (v2.0 新規) | **Windows AppContainer プラグインサンドボックスの深堀** | Phase 8b | セキュリティ実測 |
| **U#15** (v2.0 新規) | **HDR / 広色域対応の要否** | Phase 12 前に判断 | ユーザー要望 |
| **U#16** (v2.0 新規) | **RTL 対応の実装深度** | Phase 12 前に判断 | 対象市場 |
| **U#17** (v2.0 新規) | **秀丸マクロ互換レイヤのカバレッジ** | Phase 11.3 | 実マクロ資産の検証 |
| **U#18** (v2.0 新規) | **テレメトリの送信内容項目確定** | Phase 12 前 | プライバシーレビュー |
| **U#19** (v2.0 新規) | **マーケットプレース運営体制** | Phase 12 出荷後 | 事業計画 |
| **U#20** (v2.0 新規) | **ライセンス確定** (本体 Apache 2.0 vs MIT vs GPL) | Phase 12 前 | 弁護士確認 |

---

## 23. 更新履歴

| 日付 | 版 | 変更 |
|---|---|---|
| 2026-07-19 | v1.0 | 初版発行 (Phase 5b1 完了後、Phase 4b8/5b2/5b3/6-12 の実装詳細を一気通貫で規定、16 章構成、1183 行) |
| 2026-07-19 | v2.0 | **Google/MS 責任者視点の徹底レビュー反映**。18 項目の構造的欠陥を全て改善。**23 章構成に拡張**: (§1) ペルソナ・競合ポジショニング・60 機能継承マトリクス新設、(§3-11) 各 Phase に秀丸/サクラ/MIFES 固有機能 (フリーカーソル・マーカー・桁位置ジャンプ・キーマクロ・秀丸互換 grep 結果ペイン等) を追加、(§7) ミニマップ・Breadcrumb・Sticky scroll・Indent guides・Semantic highlighting を統合、(§9) Copilot 型補完・RAG・エージェント・マルチモデル並列・ローカル LLM 対応、(§10) リアルタイムテール・分散トレース・統計ダッシュボード、(§11) LSP を Semantic tokens/Code lens/Inlay hints/Rename 等 15 機能に拡張、(§13) UI/UX に Zen mode・分割ビュー・タブグループ・Mica/Acrylic・HDR/VRR・タッチ/ペン、(§15) 世界最高速の裏付け技術要素章 (SIMD/GPU/Direct Storage/Frame pacing) 新規、(§16) 国際化・アクセシビリティ章 (CJK IME・RTL・grapheme cluster・UI Automation・WCAG 2.2) 新規、(§17) セキュリティ章 (サンドボックス・SBOM・脆弱性開示・データ暗号化) 新規、(§18) リリース・配布・自動更新章 (MSIX/Portable/差分更新/カナリア) 新規、(§19) KPI/SLO/テレメトリ章 (opt-in・プライバシー原則) 新規、(§20) エコシステム戦略章 (マーケットプレース・テーマ・スニペット・コミュニティ・ライセンス) 新規、(§21) 開発品質基盤章 (テストピラミッド・回帰検出・フィーチャーフラグ・クラッシュ収集・bisect・ドキュメント・シェル統合) 新規、(§22) リスク・未決事項 12 → 20 に拡張 |
