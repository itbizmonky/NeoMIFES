# NeoMIFES マスターロードマップ v2.1

> 対象要件: [`NeoMIFES_要件定義書.md`](../../NeoMIFES_要件定義書.md) v1.0
> 上位ガイド: [`CLAUDE.md`](../../CLAUDE.md) / 基本設計: [`basic_design.md`](basic_design.md) / 実装詳細: [`detailed_design.md`](detailed_design.md)
> **併読必須: [`gap_analysis.md`](gap_analysis.md) — 2026-08-04 中間レビューによる商用化ギャップ分析 (本書と同格の Plan-of-Record 補遺)**
> 発行: v1.0 = 2026-07-19 / v2.0 = 2026-07-19 (Google/MS 責任者視点の徹底レビュー反映、23章構成) / **v2.1 = 2026-08-04 (中間レビュー反映。Phase 8.5「アプリケーションシェル」/ 8.6「製品化基盤」/ 12'「MVP 出荷判定」を新設、フェーズ順序を製品価値順へ再編、60機能マトリクスの非フェーズ参照を是正)**

本書は Phase 4b8・5b2・5b3・5c・6〜12 の **実装着手時に迷わない詳細設計** を一気通貫で規定する Plan-of-Record。個別フェーズ着手時に本書の該当章をベースに詳細プランを Plan Mode で起こし、実装後は `detailed_design.md` の対応節へ確定内容を吸収する。

> ⚠️ **v2.1 での最重要変更:** 2026-08-04 の中間レビューにより、**本書 v2.0 が「アプリケーションシェル」(ファイル保存・複数文書・設定・IME・ウィンドウクローム) にフェーズを一度も割り当てていなかった**ことが判明した。その結果、Phase 8f まで完了した時点でも NeoMIFES は**編集内容をファイルに保存できない**。詳細と是正計画は [`gap_analysis.md`](gap_analysis.md) を参照。**Phase 9 以降の全新機能は Phase 8.5 / 8.6 完了まで凍結する。**

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
| 🚀 **[`build_plan.md`](build_plan.md) (v2.1 新設)** | **実行順の作業指示書 (Execution)。次に何をどの順でどう作るか。コンテキスト無しのセッションが単独で着手できる粒度** | **作業単位 (WI) 完了ごと** |
| **[`gap_analysis.md`](gap_analysis.md) (v2.1 新設)** | **本書の計画と実コードの乖離 (Gap)。P0/P1 ギャップと Phase 再編根拠** | **中間レビュー時 / ギャップ解消時 / Phase 12' 直前に全面再検証** |

> 🚀 **実装セッションは本書ではなく [`build_plan.md`](build_plan.md) から始めること。** 本書は 2,900 行あり、最初から読むのは非効率かつ有害 (どこから手を付けるべきかが分からない)。`build_plan.md` の各作業単位が「本書のどの章を読むべきか」を指定するので、それに従う。
> **本書と `build_plan.md` が矛盾したら: 実行順は `build_plan.md`、機能仕様は本書が正。**
| ADR | 個別技術判断の記録 | 判断発生時 |
| RESUME_HERE / TIMELINE | セッション間の受け渡し・時系列 | 各セッション |

**本書と `gap_analysis.md` の関係:** 本書は「**何を作る計画か**」、`gap_analysis.md` は「**計画したのに作られていないもの / 計画にすら載っていなかったもの**」を管理する。次フェーズ候補を選定する際は、**必ず両方を参照する** (本書だけを見ると、v2.0 で 8 フェーズ続いた「エンジン層の延長線上から次を選ぶ」偏りが再発する — `gap_analysis.md` §8.3)。

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

> ⚠️ **v2.1 での是正 (`gap_analysis.md` §6.2):** v2.0 のマトリクスは「対応 Phase」欄に **`§13.5` のような章番号を書いた行が 8 行あった**。章はフェーズではないため、これらは永久に実装されないまま「計画済み」と誤認された。v2.1 で**全て実 Phase 番号へ是正**した。
> さらに、**三大エディタ全てが当然に備えるため「継承すべき差分」として認識されず、60 機能に一度も列挙されていなかった機能群**(ファイル保存・ファイルを開く・IME・メニューバー等)を **§C / §D へ追加**した。マトリクスの目的は「競合に勝つ差分の管理」だが、**競合と同じ土俵に立つための前提機能を漏らしては意味がない**という反省による。

#### A. 編集・入力系

| 機能 | 秀丸 | サクラ | MIFES | 対応 Phase |
|---|---|---|---|---|
| 複数カーソル | ✕ | △ | ✕ | 4b (完了、既に凌駕) |
| 矩形選択 | ○ | ○ | ◎ | 4b8 |
| 縦編集 (縦書き入力) | ○ | ○ | ◎ | **未計画** — v2.0 は「4b8 (研究)」としたが研究も実装も行われていない。Phase 12 前に要否を判断 (U#21) |
| フリーカーソル (虚数位置) | ✕ | ◎ | ○ | 4b8 (拡張) |
| タブ⇔スペース変換 | ○ | ○ | ◎ | 4b8 (完了) |
| 自動インデント | ○ | ○ | ○ | **Phase 8.6e** — v2.0 は「4b8」としたが**実装されないまま 4b8 が「保留項目なし」で完了宣言された** (`gap_analysis.md` §6.3) |
| 桁位置ジャンプ | ○ | ○ | ◎ | 4b8 (完了) |
| **全選択 (Ctrl+A)** | ○ | ○ | ○ | **Phase 8.6e** — v2.0 で列挙漏れ、未実装 |
| **行複製 / 行移動 / 行削除** | ○ | ○ | ○ | **Phase 8.6e** — v2.0 で列挙漏れ、未実装 |
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
| **ファイルを保存する (Ctrl+S)** | ○ | ○ | ○ | **Phase 8.5a/8.5b** — v2.0 で**列挙漏れ**。三大エディタ全てが当然に備えるため「差分」として認識されず、8 フェーズ見落とされた (`gap_analysis.md` §3.1) |
| **名前を付けて保存 (Ctrl+Shift+S)** | ○ | ○ | ○ | **Phase 8.5b** — v2.0 で列挙漏れ |
| **ファイルを開く (Ctrl+O / D&D)** | ○ | ○ | ○ | **Phase 8.5b** — v2.0 で列挙漏れ。現状は起動時 `--open` のみ (`gap_analysis.md` §3.2) |
| **新規ファイル (Ctrl+N)** | ○ | ○ | ○ | **Phase 8.5b** — v2.0 で列挙漏れ |
| **未保存変更の警告** | ○ | ○ | ○ | **Phase 8.5a/8.5b** — v2.0 で列挙漏れ |
| UTF-8/16/32 対応 | ○ | ○ | ○ | Phase 6 (完了) |
| Shift-JIS/EUC-JP/ISO-2022-JP | ○ | ○ | ○ | Phase 6 (完了) |
| BOM 切替 | ○ | ○ | ○ | Phase 6 (判定/読込のみ完了) + **Phase 8.5a (切替後の保存)** |
| 自動判定 | ○ | ○ | ○ | Phase 6 (完了) |
| 改行コード CRLF/LF/CR 切替 | ○ | ○ | ○ | Phase 6 (判定/読込のみ完了) + **Phase 8.5a (切替後の保存)** |
| 巨大ファイル (>1GB) | △ | △ | ◎ | Phase 6 (mmap、完了) |
| **10GB ファイル** | ✕ | ✕ | △ | Phase 6 (差別化、完了) |
| 自動保存・バックアップ | ○ | ○ | ○ | **Phase 8.6d** — v2.0 は「Phase 12」としたが Phase 12 は品質保証フェーズであり機能実装フェーズではない (カテゴリエラー) |
| 履歴 (最近開いたファイル) | ○ | ○ | ○ | **Phase 8.6d** — 同上 |
| **クラッシュ復旧** | ○ | ○ | ○ | **Phase 8.6d** — 要件定義書 §15 必須、v2.0 でマトリクス列挙漏れ |

#### D. 表示・UI 系

| 機能 | 秀丸 | サクラ | MIFES | 対応 Phase |
|---|---|---|---|---|
| シンタックスハイライト | ○ | ○ | ○ | Phase 7 (完了、22 言語) |
| アウトライン | ◎ | ○ | ○ | Phase 7f/7g (完了) |
| 折り畳み | ○ | ○ | ○ | Phase 7i/7j (完了) |
| 行番号 | ○ | ○ | ○ | **Phase 8.5f** — ガター自体は 4b8c で新設済みだが**ブックマーク専用で行番号は未描画** |
| ブックマーク列 (行番号左) | ◎ | ○ | ○ | 4b8c (完了) |
| **ミニマップ** | ✕ | ✕ | ✕ | Phase 7v/7w (完了、差別化) |
| **Breadcrumb** | ✕ | ✕ | ✕ | Phase 7h (完了、差別化) |
| **Sticky scroll** | ✕ | ✕ | ✕ | Phase 7o (完了、差別化) |
| **Indent guides** | ○ | ○ | ○ | Phase 7e (完了) |
| **横スクロール** | ○ | ○ | ○ | **Phase 8.5g** — v2.0 で列挙漏れ、未実装。**画面幅を超える行の右端に到達できない** (`gap_analysis.md` §4.2) |
| **タブ UI (複数ファイル)** | ○ | ○ | ○ | **Phase 8.5d** — v2.0 は「§13.5」(章番号) を書いておりフェーズ化されていなかった |
| **タブグループ・ピン留め** | △ | ○ | △ | **Phase 12 以降 (未計画、差別化)** — v2.0 の「§13.5」を是正 |
| **分割ビュー (画面分割)** | ○ | ○ | ○ | **Phase 12 以降 (未計画)** — v2.0 の「§13.5」を是正 |
| **Zen mode (集中モード)** | ✕ | ✕ | ✕ | **Phase 12 以降 (未計画、差別化)** — v2.0 の「§13.5」を是正 |
| ダーク/ライトテーマ | ○ | ○ | ○ | **Phase 8.6c** — v2.0 の「§13.6」を是正。要件定義書 §14 必須 |
| 高 DPI | ○ | ○ | ○ | Phase 3 (完了) |
| **HDR / 広色域** | ✕ | ✕ | ✕ | **Phase 12 以降 (未計画、差別化)** — 要否は U#15 で判断 |
| Mica/Acrylic 半透明 | ✕ | ✕ | ✕ | **Phase 12 以降 (未計画、差別化)** — v2.0 の「§13.6」を是正 |
| ステータスバー | ○ | ◎ | ○ | **Phase 8.5f** — v2.0 の「Phase 3+」を是正 (「+」が指すフェーズは存在しなかった) |
| **メニューバー** | ○ | ○ | ○ | **Phase 8.5f** — v2.0 で列挙漏れ、未実装 |
| **右クリックコンテキストメニュー** | ○ | ○ | ○ | **Phase 8.5f** — v2.0 で列挙漏れ、未実装 |
| **日本語 IME インライン変換** | ○ | ○ | ◎ | **Phase 8.5e (WI-06, 2026-08-12 完了)** — v2.0 は §16.1 に章として記載したのみでフェーズ化されていなかったが、実フェーズ化して解消 |
| **コマンドパレット (Ctrl+Shift+P)** | ✕ | ✕ | ✕ | Phase 5b3c (完了、差別化) |
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

> **v2.1 改訂:** 中間レビュー ([`gap_analysis.md`](gap_analysis.md)) を受け、Phase 8f 以降の順序を **技術レイヤ順から製品価値順へ再編**した。新設された Phase 8.5 / 8.6 / 12' が最優先であり、**Phase 9 (AI) は最後尾へ移動**した (理由: CLAUDE.md が「エディタ本体は AI 無しでも 100% 動作しなければならない」と定めているが、本体が 100% 動作していない段階で AI を積むのはこの原則と矛盾するため)。

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
| 7a | 構文解析エンジン選定 (ADR-014、tree-sitter採用) + C++単一言語ヘッドレスPoC | ✅ 完了 | §7 |
| 7b | C++シンタックスハイライトのDocument/RenderPipeline統合 (実際に色付け表示) | ✅ 完了 | §7 |
| 7c | 非同期シンタックス再解析 (Syntax Worker Thread、全文書再解析のまま非同期化) | ✅ 完了 | §7 |
| 7d | シンタックス多言語対応 (Python追加) + 言語ディスパッチ機構の一般化 | ✅ 完了 | §7 |
| 7e | Indent guides (インデントガイド) | ✅ 完了 | §7 |
| 7f | アウトライン抽出 (OutlineNode、ヘッドレス) | ✅ 完了 | §7 |
| 7g | アウトラインUI統合 (OutlinePane、WC_TREEVIEW、Ctrl+Shift+O) | ✅ 完了 | §7 |
| 7h | Breadcrumb (カーソル位置のシンボルパス表示) | ✅ 完了 | §7 |
| 7i | 折り畳み コア基盤 (FoldingModel、キーボードトグルのみ、ガタークリックは次候補) | ✅ 完了 | §7 |
| 7j | 折り畳み ガター+/-クリックトグル (`hitTestFoldMarker()`) | ✅ 完了 | §7 |
| 7k | 真の増分再解析 コア基盤 (`document::EditDelta` + `syntax::IncrementalParser`、ヘッドレス) | ✅ 完了 | §7 |
| 7l | 真の増分再解析の SyntaxWorker 統合 (edits蓄積キュー+RenderPipeline配線) | ✅ 完了 | §7 |
| 7m | `ts_tree_get_changed_ranges()`によるトークン部分更新 (増分再解析の性能対応) | ✅ 完了 | §7 |
| 7n1 | 追加言語対応 バッチ1 (C/JavaScript/Java/Go/Rust/JSON) | ✅ 完了 | §7 |
| 7o | Sticky scroll | ✅ 完了 | §7 |
| 7p | LineIndexインクリメンタル更新 (`applyInsert`/`applyErase`、Phase 7k性能リグレッション修正) | ✅ 完了 | §7 |
| 7q | IncrementalParser差分返却化 (`TokenPatch`/`applyTokenPatch()`、真のO(edit size)化を試行) | ✅ 完了 (DoD未達、§7.11参照) | §7 |
| 7r | 追加言語対応 バッチ2 (HTML/CSS/Shell/YAML/TOML/XML) | ✅ 完了 | §7 |
| 7s | 追加言語対応 バッチ3 (TypeScript/TSX/PHP/Markdown) | ✅ 完了 | §7 |
| 7t | 可視範囲スコープ化トークン再設計 (`reparseRange()`、永続トークン列を廃止) | ✅ 完了 (小〜中規模文書でDoD達成、大規模文書は未達・§7.11参照) | §7 |
| 7u | `TSInput`コールバックAPI採用 (大規模文書のDoD達成を試行) | ❌ 全面revert (性能後退のため、`docs/issues/tree_sitter_incremental_parse_cost.md`参照) | §7 |
| 7v | ミニマップ (簡易版・スクロール追従型、文書全体俯瞰は次候補) | ✅ 完了 | §7.4 |
| 7w | ミニマップ「文書全体俯瞰型」拡張 (遅延ポピュレーション方式) | ✅ 完了 | §7.4 |
| 7x | 追加言語対応 バッチ4 (PowerShell/INI/Batch — 個人メンテナ文法、SQLはparser.c未コミットで対象外、VB/VBScriptはライセンス不明で恒久除外) | ✅ 完了 | §7 |
| 7y | 追加言語対応 バッチ5 (SQL — 事前生成parser.cベンダリング方式、ADR-021) | ✅ 完了 | §7 |
| 7z〜 | tree-sitter内部実装のさらなる調査(50万行DoD未達の解消) | ⏭️ 次候補 (Phase 7、保留中) | §7 |
| 8a | プラグインエンジン 最小限PoC (C ABI + LoadLibraryW + SEHクラッシュ隔離、ADR-015) | ✅ 完了 | §8 |
| 8b | `NeoMifesCoreApi`橋渡し実装 (insertText/deleteRange/getLineCount/getLineText、ADR-016) | ✅ 完了 | §8 |
| 8c | Job Objectによるプラグイン資源制限 (`ActiveProcessLimit=1`のみ、ADR-017) | ✅ 完了 | §8, §17.1 |
| 8d | `permissions`権限モデル (自己申告ビットフィールド + NULL関数ポインタ・ゲート、ADR-018) | ✅ 完了 | §8, §17.1 |
| 8e | showToast ヘッドレス実装 (`ui::ToastState`、ADR-019。`registerCommand`は延期) | ✅ 完了 | §8 |
| 8f | registerCommand ヘッドレス実装 (`ui::PluginCommandRegistry`+既存SEHトランポリン再利用、ADR-020。CommandPalette実配線は延期) | ✅ 完了 | §8 |
| **— ここまでエンジン層。以下 v2.1 で再編 (`gap_analysis.md` §7) —** | | | |
| **8.5a** | **文書保存基盤** (`document::saveFile()`、mmap 解放 + `ReplaceFileW` アトミック置換、`isDirty()`、エンコード/改行/BOM 指定書き出し) | ⏭️ **最優先 (P0)** | §8.5 |
| **8.5b** | **ファイルライフサイクル UI** (Ctrl+S / Ctrl+Shift+S / Ctrl+O / Ctrl+N、`IFileDialog`、`WM_DROPFILES`、未保存警告) | ✅ **完了・🎉 M1達成 (2026-08-05)** | §8.5 |
| **8.5c** | **`main.cpp` 解体 + 複数文書モデル** (`app::EditorSession` / `app::Workspace` 新設。main.cpp を 2,439 行 → 361 行へ縮小) | ✅ **完了 (WI-04, 2026-08-07)** | §8.5 |
| **8.5d** | **タブ UI** (`ui::TabBar`、Ctrl+Tab / Ctrl+W / Ctrl+PgUp・PgDn) | ✅ **完了 (WI-05, 2026-08-11)** | §8.5 |
| **8.5e** | **IME 完全対応** (`WM_IME_*`、未確定文字列のインライン描画、`CANDIDATEFORM` キャレット追従) | ✅ **完了 (WI-06, 2026-08-12)** | §8.5, §16.1 |
| **8.5f** | **ウィンドウクローム** (メニューバー / `HACCEL` / ステータスバー / タイトル / コンテキストメニュー / `.rc`・`.ico`・`.manifest`) | ✅ **完了・🎉 M2達成 (WI-07, 2026-08-13)** | §8.5 |
| **8.5g** | **横スクロール** (`leftColumn`、`WM_HSCROLL`。長い行の右端への到達) | ✅ **完了 (WI-03, 2026-08-05)** | §8.5 |
| **8.6a** | **設定システム** (`core::Settings`、JSON。ハードコード定数 13 箇所を移行、`kTabWidth` 二重定義を解消) | ✅ **完了 (WI-08, 2026-08-13)** | §8.6 |
| **8.6b** | **キーバインド設定** (`HACCEL` の設定ファイル化、秀丸/サクラ/VSCode プリセット) | ✅ **完了 (WI-10, 2026-08-15)** | §8.6, §13.1 |
| **8.6c** | **テーマ** (ダーク / ライト / ハイコントラスト。ハードコード `D2D1_COLOR_F` を `Theme` 経由へ) | ✅ **完了 (WI-09, 2026-08-14)** | §8.6, §13.6 |
| **8.6d** | **自動保存・バックアップ・クラッシュ復旧・最近開いたファイル** | ✅ **完了 (WI-11, 2026-08-15)** | §8.6 |
| **8.6e** | **基本編集の穴埋め** (Ctrl+A、自動インデント、行複製/移動/削除) | ✅ **完了 (WI-12, 2026-08-15、🎉M3)** | §8.6 |
| **12'** | **MVP 出荷判定** (新設。「秀丸/サクラの代替として実用に耐える」状態で一度出荷し実ユーザーの反応を得る) | ✅ **完了 (WI-13, 2026-08-16、🎉M4)** | §12.4 |
| 10.1 | ログ解析モード ヘッドレス基盤 (**最大の差別化点。v2.1 で AI より前倒し**) | 🎉 **完結 (WI-14a〜d完了、2026-08-18)** | §10.1 |
| 10.3 | JSON/XML Tree モード (**三大エディタが持たない差別化点**) | 🎉 **完結 (WI-15a〜i完了、2026-08-25。XPath自前実装+真の左右分割ペイン化まで到達)** | §10.3 |
| 10.2 | CSV モード | 🎉 **列固定達成 (WI-16a〜g完了、2026-08-25。式列のみ未着手、WI-16h以降)** | §10.2 |
| 11.1 | Git 統合 | **🎉 完結 (WI-17a〜f完了、2026-08-25): ヘッドレス基盤+非同期化+EditorSession/Workspace配線+左ガター差分マーカーUI+Gitペイン+Diffビュー(インライン統合diff)まで全実装済み。Blame/Commit/Branch切替/3-Way Merge/Side-by-side表示は🧊凍結 (2026-08-23)** | §11.1 |
| 11.2 | LSP 完全実装 | 🧊 **凍結 (2026-08-23、build_plan.md §0参照)** | §11.2 |
| 11.3 | マクロ (Lua + JS + 秀丸互換レイヤ) | 🧊 **凍結 (2026-08-23)** | §11.3 |
| 9 | AI プラグイン (Claude + Copilot 型補完 + RAG) | 🧊 **凍結 (2026-08-23。「本体はAI無しでも100%動作」原則自体は維持、単に本体側の機能追加を優先しAI実装は見送り)** | §9 |
| 12 | 総合品質保証 + 正式出荷(22項目フル版) | 🧊 **凍結 (2026-08-23)。§12.5の軽量版チェックリストへ置き換え** | §12 |
| (凍結) | 8g: AppContainer サンドボックス | 🧊 凍結 (Phase 12 まで) | §8, §17.1 |
| (凍結) | 7z: 大規模文書の増分解析 DoD 再挑戦 (50万行で 155.95ms、4 回挑戦し未達) | 🧊 凍結 (`gap_analysis.md` §4.4) | §7.11 |

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

> **実装状況 (2026-08-04、Phase 7y完了時点):** ✅ 完了22言語 — C++(7a)・Python(7d)・C/JavaScript/Java/Go/Rust/JSON(7n1)・HTML/CSS/Shell/YAML/TOML/XML(7r)・TypeScript/Tsx/PHP/Markdown(7s)・PowerShell/Ini/Batch(7x)・SQL(7y)。TypeScriptは`.ts`/`.tsx`で2つの独立した完全な文法(`typescript`/`tsx`)を使い分ける設計にし、PHPは`php`のみ採用(`php_only`は埋め込み専用で対象外)、Markdownはブロック文法(`tree-sitter-markdown`)のみ採用(`tree-sitter-markdown-inline`は言語注入機構が本コードベースに無いため対象外、段落内の強調/リンク等は無彩色のまま)。PowerShell/Ini/Batchは`tree-sitter/`・`tree-sitter-grammars/`両org不在の個人メンテナ文法(`airbus-cert/tree-sitter-powershell`・`justinmk/tree-sitter-ini`・`wharflab/tree-sitter-batch`)を実地調査の上で採用。SQL(`DerekStride/tree-sitter-sql`)は上流に`parser.c`が無いため、ビルド時にtree-sitter CLIを導入するのではなく開発機上で一度だけ生成した`parser.c`を`third_party/tree-sitter-sql-generated/`へベンダリングして対応(ADR-021) — 詳細は§7実装後の確定事項(7y)参照。**残りVB・VBScript・SAP ABAP(P1)は恒久的または当面の対象外:** VB/VBScriptは調査した全候補がライセンス不明(`license: null`)のため恒久除外。SAP ABAPは未調査のまま継続保留。

### 7.3 データ構造・アルゴリズム

**シンタックス定義エンジン選定 (Phase 7a で決定 → ADR-014、下記「実装後の確定事項」参照):**
- **採用:** tree-sitter (WASM 除外版、C API 静的リンク)。Phase 0 時点のADR-003(TextMate互換文法採用)をPhase 7a着手前レビューで見直した — C++向けTextMate文法インタプリタの既製ライブラリが存在しないことが判明したため
- 起動時のパーサ ready 時間・増分解析・バイナリサイズ等の実測値は「実装後の確定事項」参照
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

### 実装後の確定事項/変更点 (2026-07-31、Phase 7v完了 — ミニマップ 簡易版・スクロール追従型)

Phase 7u revert完了後、ユーザーが次候補としてミニマップ(推奨案)を選んだ。表示範囲モデルについてAskUserQuestionで確認し、「文書全体を常に俯瞰表示するVSCode型」ではなく**「まず簡易版(スクロール追従型)を実装し、実アプリでの使い心地・性能を実測してから、文書全体俯瞰型への拡張を別フェーズで検討する」(推奨案)**が選ばれた。

- **描画方式は、上記スケッチの「`D2D1_BITMAP_INTERPOLATION_MODE_LINEAR`によるGPUスケーリング」ではなく、既存の`FillRectangle`/`SolidColorBrush`による直接描画を採用した。** 上記スケッチ自体が「1/8スケールで縮小描画」と「GPU補間スケーリング」という技術的に矛盾する2つの手法を並記しており、Breadcrumb/Sticky scrollが同様のroadmapスケッチ(`src/ui/minimap.{h,cpp}`という新規ファイル構想)より遥かにシンプルな「`render_pipeline.cpp`内に閉じた直接D2Dプリミティブ描画」に落ち着いた前例に倣った。新規ファイル・CMake変更なし
- **ミニマップの「窓」(表示対象の行範囲)は、既存の`m_requestedTokenRange`(Phase 7t由来)を再利用せず、`computeDesiredTokenRange()`から窓計算部分だけを`widenedVisibleLineRange()`として新規抽出し共有する設計にした。** `m_requestedTokenRange`は`ensureSyntaxTokensCoverVisibleRange()`がシンタックスハイライトOFF時に早期returnして一切更新しないメンバであり、これに依存するとハイライトOFF時にミニマップの窓が`{0,0}`のまま固定されるバグになるため
- **行の色決定は「その行で最初に見つかった着色トークンの色」のみを使う設計にした。** 密度表現(1行内の複数トークンをセグメント別に描く)はv1では不採用、幅だけ行の長さに比例させた単色1本の`FillRectangle`で近似
- **`drawVisibleLines()`側の変更は不要と判明した。** `drawTextLine()`は元々65536DIPの巨大レイアウトボックスでNO_WRAP描画しており実クリップは常にレンダーターゲットの物理境界任せなので、ミニマップは`drawVisibleLines()`の**後**に不透明な背景矩形で右端を上書きするだけで済む
- **`hitTestMinimap(xPx,yPx)`と`minimapLineAtY(yPx)`を分離した。** クリック開始時はX範囲チェックが必要だが、ドラッグ継続中はWindowsの通常のスクロールバーのつまみドラッグと同様、掴んだ後はX座標が帯の外にずれても追従すべきため、Y座標のみで判定する`minimapLineAtY()`をコアとして分離し`onMouseDrag`はこちらを呼ぶ
- **ベンチマーク実測(Release、`--measure-frame`、5万行合成文書スクロール):** avgFrameNs≈16.53ms(Phase 3c以来の既存ベースライン「avgFrameNs≈16.5ms」と同水準) — ミニマップ描画による有意なフレーム時間の悪化は確認されなかった
- **実アプリ視覚確認:** ミニマップ帯の表示(シンタックス色反映・現在可視範囲の強調矩形)、クリックによるジャンプ(スクロール位置が実際に変化することをスクリーンショット2枚の比較で確認)を実施

**スコープ外(意図的、後続フェーズへ):** 文書全体俯瞰表示(VSCode型)、フォールドされている行のミニマップ内での特別扱い(可視行のみを詰めて描く精緻化)、密度表現の精緻化、テーマ対応、キーボードショートカットでのミニマップ表示/非表示トグル。詳細は`detailed_design.md` §10.23参照。

### 実装後の確定事項/変更点 (2026-08-01、Phase 7w完了 — ミニマップ「文書全体俯瞰型」拡張)

Phase 7v完了後、ユーザーが次候補としてミニマップ文書全体俯瞰型拡張(推奨案)を選んだ。着手前調査で最大の技術的障壁(可視範囲外の行の色情報をどう取得するか — `RenderPipeline::m_tokens`はPhase 7t以降「可視範囲+マージンのみ」しか保持しない設計)を特定し、3方式(遅延ポピュレーション/バックグラウンドフルパース+行サマリー配列/色なし密度表示)をAskUserQuestionで提示、**「遅延ポピュレーション」(推奨案)が選ばれた**: 初期表示は全体グレー(未計算)、スクロールで実際に見た範囲だけ`m_tokens`経由で後から色を埋める。新規の全文書フルパースパイプライン(Phase 7a実測: 100万行で約6.6秒、DoD「≤5秒」未達)・`EditDelta`購読による差分更新(このコードベースには編集追従の仕組みが一件も存在しない)はいずれも不採用。

- **ミニマップの「窓」を`[0, totalLines)`固定にし、`widenedVisibleLineRange()`をミニマップから完全に切り離した。** `drawMinimap()`は元々`m_document->lineCount()`を直接呼んでいたため、新規のクランプ/マージン計算なしで「常に文書全体」を表現できた。結果として`widenedVisibleLineRange()`の呼び出し元は`computeDesiredTokenRange()`(トークンリクエスト範囲の計算)1箇所のみに戻り、Phase 7v時点で生じていた「シンタックスハイライトOFF時にミニマップの窓が`{0,0}`に固定される」構造的懸念自体が解消された
- **`viewport_math.h`にバケット化の純粋関数2つ(`computeMinimapBucketCount()`/`minimapBucketStartLine()`)を追加した。** バケット数は「ミニマップ帯の高さで収まる最大行数」と「総行数」の小さい方に丸めるため、小規模文書では自動的にPhase 7vと同じ1行=1バケットへ縮退する(退行ではなく一般化)。バケット代表行は`(bucket * totalLines) / bucketCount`という各バケット独立計算(累積加算ループではない)で誤差蓄積を避けた
- **色の蓄積は「行番号ベースの`std::vector<MinimapLineColorState>`」を新設し、「バケット番号ベース」は不採用にした。** バケット境界は物理サイズ(リサイズ)と総行数(編集)の両方に依存する可変値であり、バケット番号キーだとリサイズのたびに過去に取得した色情報が無意味になるため。`MinimapLineColorState`は`std::uint8_t`基底の8値enum(Unpopulated/PlainText/Keyword/Type/String/Number/Comment/Preprocessor)で、100万行文書でも約1MBに収まる — Phase 7t/7uが解消した「`m_tokens`が文書全体を保持すると130〜200MB」という問題を再導入しない
- **蓄積配列のクリア/リサイズは`refreshDocumentCacheIfStale()`(既存のversion変化検知の一元窓口)に統合し、新規の編集追従コードを一切書かなかった。** 1文字編集ごとに配列全体を`assign(lineCount(), Unpopulated)`で丸ごと再初期化する、最もシンプルな設計を意図的に選んだ(CLAUDE.mdルール10)
- **ヒットテスト(`minimapLineAtY()`)と強調矩形(`drawMinimapViewportHighlight()`)を、`widenedVisibleLineRange()`依存の離散行オフセット計算から「Y座標 ÷ 帯の高さ = 行番号 ÷ 総行数」という連続的な比例配分へ書き換えた。** 強調矩形には新規に最小高さ`kMinHighlightHeightDips=2.0F`(未チューニングの初期値)を導入 — 100万行文書で可視行50行程度だと矩形が0.035px相当になり実質不可視になるため
- **`main.cppは無変更で済んだ。** `hitTestMinimap()`/`minimapLineAtY()`/`applyAsyncSyntaxTokens()`いずれも公開シグネチャを変更しなかったため、Phase 7t→7uで確立した「内部実装だけの差し替え」パターンがここでも成立した
- **ベンチマーク実測(Release、`--measure-frame`、5万行合成文書スクロール):** avgFrameNs≈16.50ms(Phase 7v時点の既存ベースライン「avgFrameNs≈16.53ms」と同水準) — バケット化ロジック追加による有意なフレーム時間の悪化は確認されなかった
- **実アプリ視覚確認:** 1454行の実C++ファイル(`render_pipeline.cpp`自身)を開き、ミニマップ帯が文書全体を俯瞰表示すること(初回描画時に`m_lineHeightDips`未測定によるフォールバックで文書全体が一度に着色された)、強調矩形が現在可視範囲を示すこと、ミニマップ下端クリックで文書末尾付近へジャンプし強調矩形も追従することをスクリーンショット比較で確認

**スコープ外(意図的、後続フェーズへ):** バケット代表色の精度向上、複数言語混在の考慮、テーマ対応、小規模文書でのバー高さ上限キャップ、高速連続スクロール時の古い応答による蓄積配列への一時的誤書き込みの根本対処(`SyntaxWorker`ペイロードへの世代番号追加、別スコープ)、フォールド行のミニマップ内特別扱い、密度表現の精緻化・表示トグル、Breadcrumb/Sticky scroll帯との重なり。詳細は`detailed_design.md` §10.24参照。

### 実装後の確定事項/変更点 (2026-08-01、Phase 7x完了 — 追加言語対応 バッチ4)

Phase 8a(プラグインエンジン最小限PoC)完了後、ユーザーから「次のPhaseに進め」と指示された。AskUserQuestionでPhase 8b候補と残タスク(残り6言語バッチ4/tree-sitter内部実装調査)を提示し、**残り6言語対応バッチ4(SQL/PowerShell/VB/VBS/BAT/INI)**が選ばれた — Phase 7n1/7rで「公式org不在・コミュニティ文法のみ」として一度対象外にした経緯があり、現在の状態を`gh api`で再確認する必要があった。

- **`gh api`によるGitHub直接確認(CLAUDE.mdルール3、記憶からの推測ではない)で、想定より品質の低い状況が判明した。** VB/VBScriptは調査した全候補(`CodeAnt-AI/tree-sitter-vb-dotnet`26★含む)が`license: null`(ライセンス不明)で対象化不可。SQL(`DerekStride/tree-sitter-sql`、243★・MIT・アクティブ)は最有力候補だが`src/`に`parser.c`がコミットされておらず`scanner.c`のみ — `grammar.js`から`tree-sitter generate`(tree-sitter CLI、Node.js依存)で生成する必要があり、ADR-014が確立した「生成済みparser.cを直接参照する」前提が崩れる。PowerShell(`airbus-cert/tree-sitter-powershell`、81★・MIT)・INI(`justinmk/tree-sitter-ini`、36★・Apache-2.0)・Batch(`wharflab/tree-sitter-batch`、13★・MIT)は既存パターンでビルド可能な候補が見つかった
- **この状況をAskUserQuestionで提示し、PowerShell/INI/Batchの3言語のみ実装(推奨案)が選ばれた。** VB/VBScriptはライセンス不明のため恒久除外、SQLは新規ビルド依存(Node.js/tree-sitter CLI)の導入コストが高いため別途検討(Phase 7y以降)として本バッチのスコープから外した
- **PowerShellの`scanner.c`の著作権表示が"Copyright (c) Microsoft Corporation"だったことを実ファイル確認で発見した。** 個人メンテナのGitHub org配下だが、実装の出自(PowerShell本家のトークナイザ由来と見られる)自体の信頼度は高いと判断した一因になった。PowerShellはリリースタグが無かったため、`GIT_TAG`にコミットSHA(`e7bd348c`)を直接指定した(`GIT_SHALLOW FALSE`、shallow cloneと特定コミット指定の組み合わせを避けるため)
- **実機probe(2種類、通常のノードダンプ+`walkTree()`相当ロジックを再現したトークンシミュレーション)を実装前に行い、正確な期待値を得た。** PowerShellは`$true`/`$false`/`$null`が独立したブール/null型ノードではなく通常の`variable`ノードとして現れる(PowerShell自体がこれらを自動変数として扱う言語仕様)ことを確認し、`comparison_operator`(`-gt`/`-lt`等)を意図的にテーブル未登録のままにした — `-and`/`-or`が無名トークンとして現れ`classifyAnonymousLeaf()`で自然にPunctuation色になるため、`comparison_operator`も同じ色に揃えて一貫性を保つ判断
- **PowerShellの`command_argument_sep`(コマンドと引数の間の空白)が独自の無名リーフノードとして現れることを実機確認した。** ほとんどの言語は空白にノードを持たないが、この文法は明示的にノード化しており、`classifyAnonymousLeaf()`により意図せずPunctuation色になる(データ欠落ではなく無害な副次効果として許容)
- **INI/Batchの一部ノード(INIの`section_name`、Batchの`echo_off`)は非leaf(複数の子を持つ)だが、テーブルに登録することで`isAtomicNode()`が全体を1トークンとして扱う設計にした。** 登録しない場合、区切り文字(`[`/`]`や`@`)だけが着色され本体テキストがトークンストリームから欠落する既知のパターン(Phase 7n1のRust `line_comment`以来の確立済み対処)を踏襲
- **実アプリ視覚確認は`--open`引数でPowerShell/INI/Batchサンプルファイルを開き、プロセスが2秒後もクラッシュせず生存していることを確認する軽量スモークテストで実施した。** 3言語とも問題なし
- **ローカルDebug/Release/ubsan全905件green、clang-tidy新規警告0を確認した。** テストファイル群(`syntax_syntax_test.cpp`/`syntax_outline_test.cpp`/`syntax_incremental_parser_test.cpp`)には警告が多数出たが、全て「整数リテラルの小文字`u`サフィックス」というPhase 7a以来ファイル全体で一貫している既存スタイル、または私が変更していない既存コード行(自分の追加した`using`宣言により行番号がシフトしただけ)であることを1件ずつ確認し、新規パターンではないと判断した

**スコープ外(意図的、後続バッチへ):** SQL(`parser.c`未コミット、tree-sitter CLI/Node.js依存の新規導入が必要)、VB/VBScript(ライセンス不明の文法しか存在せず恒久除外)、SAP ABAP(未調査のまま継続保留)、新3言語の`extractOutline()`シンボル抽出ロジック本体、`RenderPipeline`/`SyntaxWorker`/`main.cpp`への変更(Phase 7dで確立済みの汎用ディスパッチがそのまま機能するため不要)。詳細は`detailed_design.md` §10.25参照。

### 実装後の確定事項/変更点 (2026-08-04、Phase 7y完了 — 追加言語対応 バッチ5・SQL)

Phase 8f(`registerCommand`ヘッドレス実装)完了後、ユーザーから「次のPhaseに進め」と指示された。AskUserQuestionでroadmap上の次候補(AppContainerサンドボックス/大規模文書の性能DoD再挑戦/SQL文法対応)を提示し、**SQL文法対応(推奨案)**が選ばれた — Phase 7xが唯一「候補文法はあるが`parser.c`未コミットのため対象外」として据え置いていた最後の言語。

- **`parser.c`生成方式についてAskUserQuestionでユーザーに再確認した。** Phase 7xの想定通り`DerekStride/tree-sitter-sql`(v0.3.11)は`src/`に`scanner.c`のみで`parser.c`が無く、上流CMakeLists自身が`tree-sitter generate`で都度生成する設計だった。「tree-sitter CLIをビルド依存として導入し毎回生成する」案と「開発機上で一度だけ生成しベンダリングする」案を提示し、**ベンダリング(推奨案)**が選ばれた — CI 3ジョブへの新規ツールプロビジョニング追加、および「ビルド時に第三者バイナリを実行する」という本プロジェクト初のリスクカテゴリを避けるため。詳細は[ADR-021](../decisions/ADR-021-sql-grammar-vendored-generation.md)参照。
- **tree-sitter CLI(v0.26.11、Node.js不要のスタンドアロンWindowsバイナリ、コア本体と同一バージョン)を開発機上で一度だけ実行し、`parser.c`(17.3MB)を生成した。** 生成物のサイズが現在の`.git`全体(約30MB)に対して大きな割合であることが判明したため、ベンダリング続行の可否をAskUserQuestionで再確認し、「このまま17MBをコミット」が選ばれた(tree-sitter-cppの`parser.c`も同等サイズであり、SQL文法の構造上自然な規模と判断)。
- **`third_party/tree-sitter-sql-generated/`を新設し、`parser.c`(生成)+`scanner.c`(上流コピー)+`tree_sitter/{parser.h,alloc.h,array.h}`(生成、他の全文法が自分の`src/tree_sitter/`に持つのと同種のランタイムヘッダ)+`LICENSE`+`NOTICE.md`(由来・再生成手順)を配置した。** 当初`tree_sitter/`ヘッダ一式のコピーを失念しビルドが`fatal error C1083`で失敗したため、実機ビルド検証で発見・追加した(CLAUDE.mdルール3、記憶からの推測ではなく実測で確認)。
- **実機probe(2段階、追加でTRUE/FALSE/NULL・JOIN/CASE・DDL・CTE・ドル引用文字列の構造も確認)で、`tree-sitter-sql`が356種類の`keyword_*`名前付きノード型を持つと判明した。** 他の全20言語と異なりキーワードが匿名トークンではなく個別の名前付きリーフのため、既存の`classifyAnonymousLeaf()`ヒューリスティックが効かない。356個の明示的テーブルエントリを書く代わりに、`classifyLeaf()`へ「テーブル未登録の名前付きリーフの型名が`keyword_`で始まるなら`Keyword`」という1行の汎用規則を追加した — SQL専用の特殊対応ではなく、同じ命名規則を使う将来のどの文法にも自動的に効く一般化。
- **`literal`ノードが(a)真の文字列/数値リテラル(リーフ)と(b)`TRUE`/`FALSE`/`NULL`(`keyword_true`等を子に持つラッパー、非リーフ)の両方に使われる同一型名だと2段階目のprobeで判明した。** 当初`literal`をテーブルへ追加する設計だったが、これは`isAtomicNode()`の「テーブル登録済み型は無条件にリーフ扱い」という性質により(b)を誤って`literal`のテーブル値に上書きしてしまう(TRUE/FALSE/NULLがKeywordではなく別の色になる)ため、`literal`はテーブルから意図的に除外した。結果、(b)は正しく子(`keyword_true`等)まで降りてKeyword分類され、(a)はリーフのまま`TokenKind::Text`(専用の色分けなし、許容するトレードオフ)になる。単体テストの初期実装(トークン数の手計算)がこの2段階目のprobeを行う前に書いたものだったため実際に1件off-by-oneで失敗し、修正した。
- **既存のPhase 7x以前からの`DetectLanguageTest.RejectsNonRecognizedExtensions`(`.sql`が非対応であることを検証する既存テスト)が、SQL対応追加後に失敗することが判明した。** 対応拡張子が変わった既存テストの更新漏れであり、`.sql`の主張を削除して修正した(新設した`RecognizesSqlExtension`が正の主張を担う)。
- **ローカルDebug/Release/ubsan全966件green、clang-tidy新規警告0(未変更の対照ファイルと同一の3行の既知ノイズ「`/Zc:__STDC__`等の引数未使用」のみ、実コードへの指摘なし)を確認した。** 実アプリで`.sql`サンプルファイル(コメント・DDL・DML一通り含む)を`--open`で開き、3秒後もプロセスが生存していることを確認した。

**スコープ外(意図的):** `extractOutline()`のSQL向けシンボル抽出ロジック本体(既存の全非Cpp/Python言語と同じ空`SymbolTable`)、`RenderPipeline`/`SyntaxWorker`/`main.cpp`への変更(既存の汎用ディスパッチがそのまま機能するため不要)、文字列/数値リテラル自体への専用色分け(上記`literal`の型名レベルの限界)、tree-sitter CLIを将来のビルド依存として導入する案の再検討。詳細は`detailed_design.md` §10.26参照。

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

> **Phase 7o完了時点の確定:** 上記設計スケッチ通り`FoldRange::headerLine`(`render::FoldVisual`/`core::FoldingModel`、Phase 7i/7j)を再利用する形で実装した。Breadcrumb(Phase 7h)のすぐ下に、現在の`topLine`を包含する最も内側の折り畳まれていないfold regionの見出し行を固定表示する。**該当regionが無ければ帯自体を描画しない動的高さ**を採用した(Breadcrumbの「常に固定高さの帯を描く」前例とは異なる判断、詳細は`detailed_design.md` §10.17参照)。ネストした複数regionを積み上げるVSCode相当のスタック表示・シンタックスハイライト・クリックジャンプは意図的にスコープ外(v1は最も内側の1行のみ、プレーンテキスト)。

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

> **Phase 7k完了時点の確定:** 「変更範囲を含む解析単位だけ再解析」を実現する下層(`document::EditDelta`による編集範囲追跡 + `syntax::IncrementalParser`による`ts_tree_edit()`ベースの真の増分再解析)をヘッドレスに実装した。詳細・ベンチマーク実測値は本ファイル §7 の「実装後の確定事項/変更点 (2026-07-28、Phase 7k完了)」を参照。
>
> **Phase 7l完了時点の確定:** 上記の「Syntax Worker Thread」への統合を完了した。`SyntaxWorker`(Phase 7c実装)の「保留中のリクエストは最新の1件のみ保持し古いものは破棄する」キューモデルを、「`edits`を蓄積し1件も取りこぼさない」モデルへ置き換え、実際に`syntax::IncrementalParser`を使って再解析するようになった。**ただし本節が本来目指す「変更範囲だけ再解析」による性能向上は、`reparse()`が呼び出しのたびにトークン列全体を`walkTree()`で再構築する既知のボトルネック(Phase 7k実測: 約321ms/call、DoD「≤50ms」未達)によりまだ実現できていない** — スレッド統合という別軸の正しさ(取りこぼし無し)は達成したが、体感速度の改善は`ts_tree_get_changed_ranges()`による変更範囲限定トークン抽出(次サブフェーズ)を待つ。詳細は本ファイル §7 の「実装後の確定事項/変更点 (2026-07-28、Phase 7l完了)」を参照。
>
> **Phase 7m完了時点の確定:** `ts_tree_get_changed_ranges()`による変更範囲限定トークン抽出を実装し、`reparse()`のトークン列全体`walkTree()`再構築ボトルネックを軽減した(5万行での増分再解析実測: 約321ms/call→約148ms/call、全文書再解析1243ms比で約8.4倍)。**ただし着手前に期待していた「漸近的改善」(文書サイズに依存しない一定コスト)は実測で否定された** — 50万行版の同一ベンチマークがほぼ文書サイズに比例してコストが増加することを確認し、`reparse()`が依然として「呼び出しのたびに文書全体サイズのトークン列を確保・シフトする」設計のままであることが根本原因と判明した(達成できたのは定数倍の高速化であり、複雑度クラスの変更ではない)。roadmap本節のDoD「≤50ms」は5万行でも未達のまま。詳細は本ファイル §7 の「実装後の確定事項/変更点 (2026-07-28、Phase 7m完了)」を参照。

### 7.10 折り畳み / アウトライン
- `FoldingModel` (新規 `src/core/folding_model.{h,cpp}`) がドキュメント論理行 → 表示行の対応表
- `Viewport` が表示行で管理、`Rendering` は表示行で描画、内部で論理行に変換
- 折り畳みマーカは Line Gutter の右端に `+/-`
- アウトライン UI (`src/ui/outline_pane.{h,cpp}`) は右側に折り畳みツリー (Win32 `WC_TREEVIEW`)

> **Phase 7f着手前調査で判明:** アウトライン抽出(シンボルツリーの計算)と折り畳み(表示行変換)は当初想定より規模の異なる別サブフェーズだと判明した。`core::Viewport`/`RenderPipeline`は論理行=表示行という前提でハードコードされており、真の折り畳みにはCore+Rendering層を横断する変換の差し込みが必要。アウトライン抽出は`OutlineNode`ツリーを返すヘッドレス関数として先に独立させ(Phase 7f、下記実装後の確定事項参照)、折り畳み・`outline_pane`のWC_TREEVIEW UI統合は後続サブフェーズへ据え置いた。
>
> **Phase 7i完了時点の確定:** 上記2行目の「`Viewport`が表示行で管理、内部で論理行に変換」という二重座標系案は不採用となった。実際には`document::LineNumber`を全レイヤーで論理行番号のまま維持し、`RenderPipeline`の描画/hitTest/移動キー補正の3消費箇所だけに「隠れた行をスキップするローカルなウォーク」を追加する方式にした(`Viewport`/`SelectionModel`は無改修)。ガター折り畳みマーカ自体は`+/-`ではなく▶(折畳)/▼(展開)のシェブロンで実装し、**クリックでのトグルは未実装のまま次サブフェーズへ据え置いた**(v1はコマンドパレット経由のキーボードトグルのみ)。詳細は本ファイル §7 の「実装後の確定事項/変更点 (Phase 7i完了)」を参照。

### 7.11 性能目標
- 100 万行 C++ ファイルの初回全解析: ≤ 5 秒 (バックグラウンド)
- 1 文字入力後の増分解析: ≤ 50ms — **Phase 7k(321ms)→7m(148ms)→7q(103ms)→7t(15.65ms、5万行)と4段階で改善し、2026-07-30時点で小〜中規模文書ではDoD達成。大規模文書(50万行)では依然未達(155.95ms)。** Phase 7tで`IncrementalParser::reparseDelta()`/`TokenPatch`/`applyTokenPatch()`(永続トークン列へのO(文書サイズ)シフト)を廃止し、呼び出し側が指定した範囲(可視範囲+プリフェッチ余白)だけをその都度ウォークして返す`reparseRange()`へ全面置換した(`RenderPipeline::m_tokens`も文書全体ではなく可視範囲のみを保持)。5万行では103ms→15.65ms(約6.6倍)でDoD達成、50万行でも989ms→155.95ms(約6.4倍)の改善は得たが、**narrow window(可視範囲のみ要求)とfull document(文書全体を要求)がほぼ同一コスト(155.95ms vs 155.45ms)になった**ことから、ボトルネックは`applyTokenPatch()`から`ts_parser_parse_string_encoding()`自体(文書サイズに比例するtree-sitter自身の再解析コスト、この関数は文字列ベースAPIの制約で常に文書全体のテキストを要求する)へ完全に移ったと判明。大規模文書のDoD達成には`TSInput`コールバックAPI採用(`document::BufferSnapshot`/`PieceTable`に対して実装し、文書全体のテキスト実体化・再解析自体を回避する)という、本フェーズよりさらに大きな別のアーキテクチャ変更が必要と判明、次サブフェーズの課題として明記(詳細は本ファイル§7のPhase 7t完了note、`detailed_design.md`§10.22参照)
- 折り畳み展開/折りたたみ: ≤ 100ms (10000 fold)
- ミニマップ描画: 60fps
- Breadcrumb 更新: ≤ 50ms
- Sticky scroll 追従: 60fps

### 7.12 影響ファイル
- **新規:** `src/syntax/{syntax_engine.cpp, textmate_grammar.cpp, treesitter_language.cpp (二次候補), token_stream.cpp, outline_extractor.cpp, folding_computer.cpp, semantic_token_provider.cpp}`、`include/neomifes/syntax/syntax.h`、`src/core/folding_model.{h,cpp}`、`src/ui/{outline_pane.{h,cpp}, minimap.{h,cpp}, breadcrumb.{h,cpp}}`、`src/render/{sticky_scroll.{h,cpp}, indent_guides.{h,cpp}}`、`third_party/` にパーサ
- **変更:** `src/render/render_pipeline.cpp` (トークン着色、sticky scroll、indent guides、minimap の描画統合)、`src/render/line_layout.cpp` (Token 保持)、`src/app/main.cpp` (アウトライン/minimap/breadcrumb 配線)、`src/document/document.cpp` (DocumentChanged 通知)

### 実装後の確定事項/変更点 (2026-07-21、Phase 7a完了)

**構文解析エンジンにtree-sitterを採用(ADR-014、ADR-003を置き換え)。C++単一言語のヘッドレスPoC(`neomifes::syntax::parseCpp()`)のみ完了、Document/Rendering統合・非同期増分解析・他22言語対応・アウトライン・折り畳み・ミニマップ・Breadcrumb・Sticky scroll・Indent guides・Semantic highlightingは全て後続サブフェーズへ据え置き。**

- **ADR-003(TextMate互換文法採用、Phase 0決定)を着手前レビューで見直した。** ADR-003の「`.tmLanguage.json`は100+言語分MIT/BSDで整備済み、コピペで導入可能」という根拠は文法**定義ファイル**の再利用可能性の話であり、それを解釈する**インタプリタ**のC++向け実装が存在するかとは別問題だった。Web調査でTextMate文法インタプリタの成熟した実装はTypeScript(`vscode-textmate`)・C#(`TextMateSharp`)・Java(`eclipse/tm4e`)にしか存在せず、C++向け既製ライブラリが無いことを確認。採用するにはインタプリタ本体(数千行規模)を新規に手書きする必要がありCLAUDE.mdルール3(推測実装をしない)に照らしリスクが高いと判断し、AskUserQuestionでユーザーに確認の上tree-sitterへ切替(推奨案)
- **`tree-sitter-cpp`(や他言語グラマー)の独自CMakeLists.txtを直接`add_subdirectory()`しない設計にした。** 各グラマーリポジトリのCMakeLists.txtには`find_program(TREE_SITTER_CLI tree-sitter)`を使い`src/parser.c`を`src/grammar.json`から再生成しようとする`add_custom_command`があり、`tree-sitter`CLIが環境に無いとビルドが失敗することをスタンドアロンprobeで実機確認した(既にコミット済みの`parser.c`をそのまま使えば十分なのに)。`FetchContent_Declare(... SOURCE_SUBDIR "does-not-exist")`(ソースはpopulateするが`add_subdirectory()`はしない、公式ドキュメント記載のイディオム)で各グラマーのCMakeLists.txtを経由せず、フェッチ済みソースの`src/parser.c`(+ 文法によっては`src/scanner.c`)を直接参照する自前の`add_library`ターゲットを立てる方式に確定
- **root`project()`に`LANGUAGES C`を追加した。** tree-sitter/tree-sitter-cppは初のC言語依存で、既存のCXX専用ビルドツリーへの増分reconfigureで`CMAKE_C_COMPILE_OBJECT`等が未設定になる問題を実機で確認、フルクリーン再構成後に解消したが、root project()でC言語を明示宣言する方が堅牢と判断
- **`ts_parser_parse_string_encoding(..., TSInputEncodingUTF16LE)`で`std::u16string`を直接パースできることを確認、UTF-8への往復変換は不要にした。** バイトオフセット÷2が常に正確なUTF-16コードユニットオフセットになる(document::TextPosの規約と一致)
- **`TokenKind`はroadmapスケッチ(Function/Operator/TypeParameter/Enum/Namespace/Interface/Attribute/Error + modifiersビットフィールド)から大きく縮小し、Text/Keyword/Type/Variable/Number/String/Comment/Punctuation/Preprocessorの9値のみ実装した。** Function(呼び出し/宣言の文脈判定が必要)・Operator(tree-sitter-cppの匿名トークン集合に明確な境界が無い)は未実装のまま公開APIに置かない判断(Phase 6aの`Encoding`enum同様の規約)。残りはLSPセマンティックトークン(Phase 11以降)の関心事
- **ノード種別→TokenKindの対応表は`tree-sitter-cpp` v0.23.4の`node-types.json`(230件の名前付きノード型)を実機参照し、かつ実際のパーサ出力(既知のC++スニペット)で交差検証して構築した。** 記憶からの推測を避けるため(CLAUDE.mdルール3)。名前付きleafノードは個別テーブル、匿名leafノード(キーワード/演算子/記号、約200種)は「英字のみならKeyword、`#`始まりならPreprocessor、引用符ならString、それ以外はPunctuation」という構造的ルールで分類 — C++の文法上、演算子/記号トークンに純英字のものが存在しないという性質を利用した一般化(個別列挙よりも他言語への展開が効く設計)
- **`walkTree()`は`TSTreeCursor`を使ったイテレーティブなpre-order走査(再帰なし)。** C++呼び出しスタックの深さに依存しない設計 — tree-sitterのカーソルAPI自体が内部スタックを持つ標準的な技法をそのまま採用(自己流の工夫ではない)
- **ベンチマーク実測(Release、`BM_ParseCpp_Synthetic`):** 5万イテレーション(実質30万行、UTF-16で約10.8MB)を1977msで解析、1行あたり約6.6μs。**100万行換算で約6.6秒 — roadmap目標(≤5秒)にはまだ届いていない。** 非同期化前の同期単発パースのベースライン値として記録(Phase 5a `SearchService::findAll()`が最初のベンチマークで「数GBファイルでも高速」目標に届いていなかったのと同じ位置づけ)。今後の非同期増分解析(Phase 7c以降想定)導入でsynchronous full-reparseという設計自体が置き換わる見込みのため、現時点での追加最適化はCLAUDE.mdルール10(憶測で最適化しない)に照らし見送り

### 実装後の確定事項/変更点 (2026-07-24、Phase 7b完了)

**C++単一言語をDocument/RenderPipelineへ統合し、実際にエディタ上で色付け表示するようにした。多言語対応・非同期増分解析・折り畳み・アウトライン・ミニマップ・Breadcrumb・Sticky scroll・Indent guides・Semantic highlighting・Themeシステムは全て後続サブフェーズへ据え置き。**

- **Theme(色定義)システムは本コードベースに存在しないと判明した。** roadmap本節(§7.8)は「色定義はTheme(`detailed_design.md` §5)に統合」としていたが、実際の`detailed_design.md` §5はEditor Core章でありThemeは未実装。既存`RenderPipeline`が選択色・マッチ色・ブックマーク色を全て`ensureXBrush()`内の`constexpr D2D1_COLOR_F`定数でハードコードしている既存パターンをそのまま踏襲し、トークン色6種(Keyword/Type/String/Number/Comment/Preprocessor、VSCode Dark+準拠)もハードコード定数とした。ユーザー設定可能なThemeシステムの新設は本サブフェーズのスコープ外
- **`IDWriteTextLayout::SetDrawingEffect(effect, range)` + `ID2D1DeviceContext::DrawTextLayout()`が範囲ごとに異なる`ID2D1Brush`を自動的に使う標準機構であることを確認し、カスタム`IDWriteTextRenderer`を書かずにトークン別配色を実現した。** ただし`TextLayoutCache`(ADR-011)はデバイスロスト時も明示的にクリアされない設計のため、色ブラシをキャッシュ済みレイアウトへ"焼き込む"(cache miss時のみ`SetDrawingEffect`する)設計にすると、デバイス再生成後に古いブラシへのダングリング参照が残ってしまう。**この問題を回避するため、`SetDrawingEffect`は`TextLayoutCache`のヒット/ミスに関わらず`drawVisibleLines()`側で毎フレーム再適用する方式にした** — `TextLayoutCache`自体・デバイスライフタイム関連コードは一切変更していない
- **`drawTokensOnLine()`は`m_tokens`(`parseCpp()`が左→右ソート済みで返すことをテストで保証済み)に対する二分走査を、可視行ループ全体を跨いで前進する`tokenCursor`で実装した。** `O(可視行数 × 全トークン数)`ではなく`O(可視範囲と重なるトークン数)`の一回の前進走査で済む設計。複数行にまたがるトークン(ブロックコメント等)は`tokenCursor`をまだ進めない(そのトークンのrange.endがまだ現在行のlineStartを超えていない限り)ことで正しく複数行にわたって再訪される
- **`document::Document`は自分のロード元パスを保持しないため、`main.cpp`に新規状態`currentDocumentPath`を追加した。** 起動時(`--open`)・F12タグジャンプ成功時・Grep結果ジャンプ成功時の3箇所で更新し、`neomifes::app::isCppSourceFile()`(拡張子ベース、大文字小文字無視、`.cpp/.cc/.cxx/.h/.hpp/.hxx/.hh`)の判定結果を`RenderPipeline::setSyntaxHighlightingEnabled()`へ渡す。汎用言語レジストリ(roadmapスケッチの`SyntaxEngine::registerLanguage()`)は2言語目が実際に増えるまで作らない判断(CLAUDE.mdルール3)
- **`--measure-frame`モードは対象外のまま維持した。** `currentDocumentPath`追跡・`setSyntaxHighlightingEnabled()`呼び出しは`Normal`モードの配線内だけに閉じ、既存のフレーム計測ベースライン(ADR-011)への影響を避けた
- **非同期増分解析(Syntax Worker Thread、roadmap §7.9)は据え置いたまま。** `refreshDocumentCacheIfStale()`内で`Document::version()`変更を検知した時だけ同期的に`syntax::parseCpp()`を全文書に対して再実行する — 7aのベンチマーク(100万行で約6.6秒)により、大規模ファイルでは編集のたびに視認できるカクつきが出ることは既知の制約として記録するのみに留めた

### 実装後の確定事項/変更点 (2026-07-24、Phase 7c完了)

**7bで据え置いた非同期化(roadmap §7.9「Syntax Worker Thread」)に着手した。全文書再解析はそのまま、実行するスレッドをUIスレッドから外しただけ — 真の増分再解析(tree-sitterの`ts_tree_edit()`)は引き続き後続サブフェーズへ据え置き。本プロジェクト初の`std::thread`導入。**

- **`detailed_design.md` §16(スレッド安全性)・`buffer_snapshot.h`のヘッダコメント("safe to hand out to arbitrary threads (search, syntax, plugin workers)")が、この非同期syntaxワーカーを元から想定していたことを着手前調査で確認した。** 推測ではなく既存ドキュメントの記述で裏付けた設計方針
- **真の増分再解析はスコープ外とした。** `document::Document`が編集範囲(開始位置・削除長・挿入長)を観測者へ通知する仕組み(EditEvent購読)が本コードベースに一切存在しない(Phase 4b8cのブックマーク追従見送り記録で既に確認済みの制約)ため、今回は「全文書再解析はそのまま、実行スレッドだけ変える」ことに絞った
- **非同期再解析中は古いトークンを表示し続けない設計にした。** roadmap §7.9のスケッチは「解析中は古いトークンを描画に使い続ける」としているが、これは真の増分パース(変更されていない範囲のオフセットが不変)を前提にした記述。今回は全文書再解析のため、1文字の編集でも以降の全トークンのオフセットがずれる可能性があり、古いトークンをそのまま描画すると間違った位置に間違った色を塗る危険がある。`Document::version()`が変わった時点で`m_tokens`を即座にクリアし(素の単色表示に一時的に戻る)、非同期結果が届いた時点で再度色付けする — 安全性を優先した意図的なroadmapからの乖離
- **リクエストの合流は「単一スロットの最新リクエストのみ保持」方式にした。** ワーカーが処理中に新しいリクエストが来たら、まだ着手していない保留分を新しい方で上書きする(キューに積まない)。高速タイピング中に版が古いリクエストを律儀に処理する無駄を避ける
- **`neomifes::render::SyntaxWorker`が`neomifes::ui::MainWindow`のメッセージ定数を参照する設計は、CLAUDE.mdのレイヤ依存規則(`[UI Shell] → [Rendering Engine]`、下位は上位を知らない)に違反することを実装中に発見した。** `kMsgSyntaxTokensReady`(WM_APP+2)をrender::側に定義し直し、`ui::MainWindow`には型を一切知らない汎用の`onAppMessage`フック(`onCommand`と同じ「wParam/lParam未解釈のまま転送」パターン)を新設して解決した。`ui::`は`render::`/`syntax::`への依存を一切持たないまま維持
- **ワーカースレッドは`RenderPipeline::attach()`後(`m_hwnd`が有効になってから)遅延生成する。** 当初`setSyntaxHighlightingEnabled(true)`の初回呼び出し時点で生成する設計を検討したが、そのメソッドは`RenderPipeline::attach()`より前(`main.cpp`の起動シーケンス)に呼ばれるため`m_hwnd`がまだ`nullptr`になりうると判明し、`refreshDocumentCacheIfStale()`(`render()`経由でのみ到達、`attach()`後保証済み)側での遅延生成に変更した。`--measure-frame`/`-startup`/`-memory`はシンタックスハイライトを一切有効化しないため、これらの計測モードでは背景スレッドは1つも生成されない
- **clang-tidyの静的解析器(`clang-analyzer-cplusplus.NewDeleteLeaks`)がヒープ確保したトークンベクタの「リーク」を誤検知した。** `PostMessageW`経由でmain.cppの`onAppMessage`フック(別翻訳単位)へ所有権を譲渡する設計のため、単一翻訳単位しか見えない静的解析器には正当な移譲を検出できない。`PostMessageW`が失敗した場合(例: シャットダウン中の競合)に限り`unique_ptr`側で確実に回収するようガードした上で、既知の誤検知として`NOLINTNEXTLINE`で抑制した

### 実装後の確定事項/変更点 (2026-07-24、Phase 7d完了)

**7a〜7cで一貫して「2言語目が実際に増えるまで汎用の言語ディスパッチ機構は作らない」と据え置いていた判断に、Python(2言語目)を実際に追加することで着手した。多言語対応と汎用化を同時に行うことで、C++単独では検証できなかった抽象の妥当性を実データで確認した。**

- **`tree-sitter-python`(v0.25.0)は`tree-sitter-cpp`と全く同じCMake回避パターン(`SOURCE_SUBDIR "does-not-exist"` + 自前`add_library`)がそのまま流用できることを、実装着手前のスタンドアロンprobeで確認した。** 新規のCMakeパターンリスクは発生せず、Phase 7aで確立した手順の再適用のみで済んだ
- **`classifyAnonymousLeaf()`(匿名リーフを構造的に分類する関数、「全ASCII英字ならKeyword、それ以外はPunctuation」というルール)が、C++用に書いたコードを一切変更せずPythonにもそのまま通用することを実機確認した。** Pythonの`async`/`await`/`lambda`/`and`/`or`/`not`/`is`等の全キーワードも、`:=`/`==`/`@`等の全演算子・記号も、この構造的ルールと矛盾しなかった — Phase 7aの設計時点で「C++の文法上、演算子/記号トークンに純英字のものが存在しないという性質を利用した一般化」と記録していた狙い通りの結果になった
- **`TokenKind`(Text/Keyword/Type/Variable/Number/String/Comment/Punctuation/Preprocessor)も無変更のままPythonに通用した。** Pythonの`True`/`False`/`None`/`...`(ellipsis)はC++の`true`/`false`/`this`/`null`と同じくKeyword扱いにした(VSCode Dark+等の慣例に合わせた判断)。Pythonの型注釈(`x: int`)はtree-sitter-pythonの文法上プレーンな`identifier`ノードにしかならず、C++の`primitive_type`/`type_identifier`に相当する専用ノードが無いため、TokenKind::Typeは一度もPythonトークンに割り当てられない — 既知の限界として記録するのみに留めた(型注釈の意味解析にはLSP統合、Phase 11以降が必要)
- **RenderPipeline側の描画コード(`drawTokensOnLine`/`tokenBrush`/`ensureTokenBrushes`)は1行も変更していない。** Phase 7bで作った6色ブラシがPythonトークンにもそのまま使えることが、TokenKindの言語非依存設計が正しかったことの実証になった
- **f-string(`f"...{expr}..."`)の補間構造を標準プローブで確認した。** `string_start`/`string_content`/`{`(匿名Punctuation)/式(通常トークンとして再帰的に分類)/`}`(匿名Punctuation)/`string_end`という構造で、補間式の中身(識別子等)は文字列の外側にある通常のコードと全く同じ色分けになる(VSCode等の一般的なf-string表示と同じ視覚効果)
- **既知の限界として記録: `string_content`が`escape_sequence`を含む場合、`string_content`ノード自体はleafでなくなり(子ノードを持つcompound node)、`escape_sequence`前後のプレーンテキスト部分にはトークンが一切生成されない(無色表示になる)。** 例えば`"hi\n"`の`"hi"`部分。標準プローブの完全ツリーダンプで確認した構造的事実 — walkTreeがleafノード(`child_count()==0`)のみを訪問する設計のため、compound化した`string_content`の「子ノードでカバーされない自身のテキスト範囲」は捕捉されない。修正にはcompoundノードの「子の隙間」を埋める追加ロジックが必要だが、C++側の`Operator`非分離等と同種の受容済み制約として扱い、本フェーズのスコープには含めなかった
- **`neomifes::app::isCppSourceFile()`を`detectLanguage()`(`std::optional<syntax::Language>`を返す)へ完全に置き換えた。** `.py`/`.pyw`/`.pyi`をPython、既存の`.cpp`等をC++として認識。シバン行によるPython判定は、C++判定も拡張子のみである対称性を優先し見送った
- **`SyntaxWorker::m_pending`は当初`std::optional<PendingRequest>`(snapshot+languageの組)として実装したが、clang-tidyの`bugprone-unchecked-optional-access`が`m_cv.wait()`の述語(`m_pending.has_value()`)と後続の`request->`アクセスの相関を追跡できず誤検知した。** `std::shared_ptr<const BufferSnapshot> m_pending`(nullptrで「保留なし」を表す元の設計)+ 独立した`syntax::Language m_pendingLanguage`(`m_pending != nullptr`の間だけ意味を持つ)という2フィールド構成に変更し、`std::optional`自体を使わないことで誤検知を構造的に回避した

### 実装後の確定事項/変更点 (2026-07-25、Phase 7e完了)

**roadmap §7.7の「Indent guides」(インデント階層を薄い縦線で表示)を実装した。新規Document API・新規スレッド不要、`RenderPipeline`の既存描画パターンへの追記のみで完結した。**

- **roadmapの「現在のカーソル位置のインデントレベルはハイライト (VSCode の Bracket Pair Colorization相当)」という記述は、2つの別機能を混同した誤記だと判明した。** Bracket Pair Colorizationは対応する括弧同士を色分けする全く別機能で、Indent guidesとは無関係。実際に実装したのはVSCodeの「アクティブなインデントガイド」(カーソルが乗っている行のガイドを明るく表示)機能で、かつ`FoldingModel`(ブロック/スコープ範囲検出)が未実装のため、VSCode本家のようなスコープ全体のハイライトではなく**カーソルが乗っている行1行分のガイドだけを明るく表示する簡略版**にした
- **`src/render/line_layout.cpp`(roadmapスケッチが想定していたToken専用保持クラス)は実在しないと改めて確認した。** Phase 7a〜7d同様、`RenderPipeline`が全ての描画対象状態を直接保持し`drawXxxOnLine()`群を呼ぶ既存パターンにIndent guidesもそのまま従わせた(`drawGutterOnLine`/`drawTokensOnLine`と同列の新規`drawIndentGuidesOnLine`)。新規クラスは作らなかった
- **インデント桁数の計算はDirectWriteのタブ描画(`SetIncrementalTabStop`)に一切依存させず、エディタ自身の独立したタブ幅モデルで行う設計にした。** 新規`neomifes::render::computeIndentColumns()`(スペース+1、タブは次のタブ幅倍数まで前進)は`core::computeIndentationConversionEdits()`(Phase 4b8d)と同じ意味論に揃えたが、実装は独立させた(共有ヘルパーへの統合はスコープ外)
- **タブ幅はユーザー設定不可の固定値4を`render_pipeline.cpp`に複製した。** `main.cpp`の`kTabWidth`(Phase 4b8dのタブ⇔スペース変換コマンドで確立済み)と同じ値だが、設定システム自体が本コードベースに存在しないため2箇所の手動同期が必要になる既知のトレードオフとして受容した
- **ガイド本数は「先頭空白桁数 ÷ タブ幅」(floor)、VSCodeと同じ規約。** 空行/空白のみの行は前後の非空行から推測せず、自分自身の先頭空白桁数のみで判定する(VSCodeの「空行はコンテキストからガイドを継承する」機能は非対応)
- **常時描画・トグル不可の設計にした。** 既存のキャレット/ガター/選択ハイライトも同様に常時描画のため、シンタックスハイライトの`setLanguage(nullopt)`のようなON/OFFスイッチを設ける根拠が無いと判断。`--measure-frame`の合成ベンチマーク文書は先頭空白を一切含まない行のみで構成されているため、実測ベンチマーク値への影響は「桁数0→ガイド0本」の早期リターンのみで実質ゼロだった(実測確認済み)

### 実装後の確定事項/変更点 (2026-07-25、Phase 7f完了)

**roadmap §7.10の「アウトライン」(関数/クラス/構造体/名前空間のシンボルツリー)を、折り畳みとは独立したヘッドレス機能として先に実装した。`neomifes::syntax::extractOutline()`はDocument/RenderPipeline/UIに一切依存せず、`outline_pane`(WC_TREEVIEW)配線・折り畳みは後続サブフェーズへ据え置いた。**

- **`OutlineNode::symbolKind`はroadmapスケッチの`TokenKind`型指定を採用せず、新規`enum class SymbolKind { Function, Class, Struct, Namespace }`を新設した。** `TokenKind`はPhase 7aでリーフレベルのテキスト着色専用に設計されており、`Function`/`Class`/`Namespace`等は「呼び出しと定義の文脈判定が必要」という理由で意図的に未実装のまま公開APIに置かれていない。アウトライン抽出は複合ノード(定義そのもの)だけを訪問するためこの問題自体は起きないが、リーフ分類と複合ノード分類という無関係な2つの関心事を1つのenumに混在させないため独立させた
- **アウトライン抽出は`syntax.h`の`parseCpp()`/`parsePython()`/`parse()`とツリーを共有しない、独立した2回目のパースとして実装した。** ファイルを開いた時/変更が落ち着いた時のみ低頻度で呼ばれる想定(トークン着色のような毎編集ではない)であり、ツリー共有によるパース回数削減はベンチマーク根拠の無い時期尚早な最適化と判断(CLAUDE.mdルール10)
- **C++の`function_definition`は`"name"`フィールドを持たず、`"declarator"`フィールドの中に名前が入れ子になっている構造を、スタンドアロンprobeでの実機確認(node-types.json照合)を経て実装した。** `pointer_declarator`/`function_declarator`は`"declarator"`という named field で子を公開するが、**`reference_declarator`はnode-types.jsonで`"fields": {}`(フィールド無し、位置引数のみ)と確認した — pointer/referenceで文法構造が非対称という、tree-sitter-cpp自身の設計上の性質だった。** 当初この非対称性を見落とし、`getRef(int& x)`(reference_declarator経由)のケースで名前解決が失敗するテストが4件発覚し、`declaratorChild()`ヘルパー(named fieldを優先し、無ければ最初の named positional child にフォールバック)を追加して解消した
- **C++/Pythonの両文法が関数定義ノードを同じ`"function_definition"`という型名で持つため、ノード型名だけでの分岐が言語混同バグを引き起こすことをテストで発見・修正した。** Pythonの`function_definition`は(C++と異なり)`"name"`フィールドを直接持つ素直な構造だが、`resolveSymbolName()`がノード型名のみで分岐していたためPython関数もC++専用のdeclarator-unwrapパスに誤って送られ、名前解決が関数本体全体のテキストにフォールバックしてしまっていた。`Language`引数を`resolveSymbolName()`/`walkForOutline()`/`extractOutline()`に通し、`Language::Cpp && nodeType == "function_definition"`の場合のみC++専用パスを通すよう修正した
- **`walkForOutline()`は当初再帰実装で書いたが、clang-tidyの`misc-no-recursion`指摘を受けて明示スタックによる反復実装に書き換えた。** AST深さは編集対象のソースファイル依存であり安全に有界ではない(`piece_tree.cpp`のRB木走査のようなO(log n)保証が無い) — `syntax.cpp`の`walkTree()`が同じ理由で`TSTreeCursor`ベースの反復実装を採用していた前例と同じ判断。ネストしたOutlineNodeツリーを構築する必要があるため単純なcursor走査では足りず、`scanStack`(再帰呼び出しスタック相当)と`resultLevels`/`pendingSymbols`(各再帰呼び出しのローカル変数相当)からなる明示的な2段スタック構成にした

### 実装後の確定事項/変更点 (2026-07-26、Phase 7g完了)

**roadmap §7.10の「アウトライン」の残り半分、UI統合(`outline_pane`、WC_TREEVIEW)を実装した。Ctrl+Shift+Oで右ドッキング・フル高さのシンボルツリーパネルをトグル表示し、クリックで同一ドキュメント内の該当位置へジャンプする。**

- **`WC_TREEVIEW`はこのコードベース初出のコントロール型で、通知が`WM_COMMAND`ではなく`WM_NOTIFY`で届くことが判明した。** `FindBar`/`GrepBar`/`CommandPalette`が使ってきたWC_EDIT/WC_LISTBOXとは別チャンネルのため、`MainWindowConfig`に新規`onNotify`フック(`onCommand`/`onAppMessage`と同じ「未解釈のまま転送」形)を追加した。`InitCommonControlsEx`の`dwICC`に`ICC_TREEVIEW_CLASSES`を追加(既存`ICC_STANDARD_CLASSES`のみでは`WC_TREEVIEWW`クラスが登録されない)
- **アウトライン項目を選択したら即座にジャンプするが、パネルは閉じない設計にした。** `FindBar`/`GrepBar`/`CommandPalette`は全て「アクション実行後に隠れる」設計(検索/コマンド実行という単発ツールの性質)だが、アウトラインは複数シンボルを連続して見て回るナビゲーション補助(VSCodeのOutlineビューと同じ性質)であるため意図的に異なる挙動にした。Escapeキーで明示的に閉じる
- **`ui::OutlinePane`は`syntax::OutlineNode`を直接知らない。** 新規`ui::OutlineItem`(UI専用ミラー型、`targetPos`は解釈しないopaqueな`std::uint64_t`)を公開APIとし、`syntax::OutlineNode → ui::OutlineItem`の変換は新規`neomifes::app::buildOutlineItems()`(ヘッダオンリー)がapp層で行う — 既存の「`ui::`はWin32機構のみ、ドメイン型は呼び出し側が変換して渡す」原則をそのまま踏襲
- **ジャンプは`app::openDocumentAt()`を使わない。** `OutlineNode::pos`は既に開いている同一ドキュメント内の絶対`document::TextPos`であり、別ファイルを開く操作ではないため、既存`jumpToGotoTarget()`と同型の「同一ドキュメント内ジャンプ」(`selectionModel.moveAllTo()` → `viewport.ensureVisible()` → `syncRenderStateAndInvalidate()`)をそのまま踏襲した。行/桁変換も不要(`OutlineNode::pos`は既に0始まりの絶対オフセット)
- **パネルは右ドッキング・フル高さのオーバーレイにした(`FindBar`/`GrepBar`/`CommandPalette`の固定サイズボックスとは意図的な逸脱)。** アウトライン閲覧はドキュメント全体のシンボル構造を見渡す用途であり、`GrepBar`の`kListHeightDips=240`のような固定高さでは実用に耐えないと判断した。`RenderPipeline`の描画幅は狭めない(既存オーバーレイと同じ「重ねるだけ」設計を維持)
- **視覚確認中、Win32 `EnumChildWindows`による構造検証(キーボード入力が使えない制約への対処、詳細後述)で既存の潜在バグを発見・修正した。** `FindBar`/`CommandPalette`/`GotoLineBar`/`GrepBar`/`OutlinePane`は全て`wireNormalMode()`の`onDeferredInit`(`WM_PAINT`初回完了後に走る投稿メッセージ)内で`.create()`されるが、位置決めは`cfg.onResize`(`WM_SIZE`)からしか呼ばれない。`WM_SIZE`は`MainWindow::create()`内の`ShowWindow()`呼び出し時に一度だけ先に発火し、その時点ではこれらのコントロールがまだ存在しないため、後から作られても二度と自動で位置決めされず、ユーザーが手動でウィンドウをリサイズするまでプレースホルダ座標(`0,0,10,10`)に居座り続けるバグだった。`OutlinePane`は`create()`成功直後に`::GetClientRect`+`::GetDpiForWindow`で明示的に`onParentResized()`を呼ぶことで解消したが、既存4オーバーレイの同じ問題は本フェーズのスコープ外として別タスクへ切り出した(1PR=1責務、CLAUDE.mdルール8)
- **この環境の自動化キーボード入力では、Ctrl/Shift等の修飾キーを伴うショートカットを合成できないことが判明した。** `SendKeys`・`keybd_event`・`SendInput`の3種の入力合成APIいずれを使っても、送信直後に`GetAsyncKeyState`で確認すると修飾キーが「押されていない」ままだった — OSレベルの非同期キー状態テーブル自体が更新されておらず、この自動化サンドボックス環境が合成された修飾キー入力そのものを受け付けない制約と判明した(プレーンな文字タイピングはWM_CHARとして正常に届く)。この制約により、Ctrl+Shift+Oを実際に押してパネルが開く様子はスクリーンショットで確認できなかったため、代わりに`EnumChildWindows`でコントロールの生成・位置・サイズを直接検証する方式で代替した(詳細は`reference_no_win32_gui_automation.md`メモリ参照)

### 実装後の確定事項/変更点 (2026-07-26、Phase 7h完了)

**roadmap §7.5の「Breadcrumb」を実装した。カーソル位置が所属するシンボルパス(`outer > Widget > getValue`形式)を、Phase 7f/7gで作った`OutlineNode`ツリー資産の逆引きで常時表示する。**

- **`syntax::findBreadcrumbPath(pos, tree)`は`OutlineNode::containingRange`(Phase 7fで「将来のBreadcrumb逆引き用」と明記していたフィールド)の線形探索で実装した。** 当初は`extractOutline()`が返す木の浅さ(シンボル定義の入れ子のみ、生AST深さではない)を根拠に通常の再帰で実装したが、`src/`配下は`WarningsAsErrors: '*'`(`src/.clang-tidy`)のため`misc-no-recursion`は深さの証明可能性に関係なく自己再帰を一律検出・エラー化することが判明し、Phase 7fの`walkForOutline()`と同じ理由で明示ループへ書き換えた(木自体の浅さの主張自体は変わらず正しい、lint都合の実装選択であることをヘッダコメントに明記)
- **`render::CursorVisual`に新規`bool isPrimary`フィールドを追加した。** `core::Cursor::isPrimary`は既存だが`RenderPipeline`側の`CursorVisual`には保存されておらず、「どのカーソルが主カーソルか」をBreadcrumbが判別できなかったため、`main.cpp`の`syncRenderStateAndInvalidate()`の1行を拡張して転送するようにした。デフォルト値`= false`により既存の部分的designated initializer構築箇所は無修正で済んだ
- **Breadcrumb用アウトライン木のキャッシュ(`m_cachedOutline`)は`m_tokens`と同じタイミング(`refreshDocumentCacheIfStale()`、ドキュメントバージョン変更時)で同期的に再計算する設計にした。** シンタックストークン着色と同様に非同期化(`SyntaxWorker`拡張または新規ワーカー)する発想もあったが、ベンチマーク根拠が無い時期尚早な最適化と判断し見送った(CLAUDE.mdルール10)。Phase 7bが最初は同期トークン抽出で出荷し、Phase 7cで初めて非同期化した前例と同じ順序を踏襲。既知の制約として「非常に大きなファイルでは編集直後にBreadcrumbが一時的に古くなりうる」ことを許容する
- **垂直座標系に新規`kBreadcrumbHeightDips`オフセットを導入した。** 既存の水平オフセット`kGutterWidthDips`(ガター幅)が担っていた「このファイル内の全x座標消費箇所が合意する必要がある」という構造を縦方向にもミラーし、`drawVisibleLines()`の描画開始y座標・`hitTest()`のyDip変換(帯内クリックは先頭行にクランプ)・`computeVisibleLineCount()`への実効高さ引数の3箇所を更新した。`computeVisibleLineCount()`自体のシグネチャは変更せず、呼び出し側で実効高さを計算する方式を維持した(Windows SDK非依存の純粋関数という性質を保つ)
- **実アプリ視覚確認で、Breadcrumb実装とは別に既存の潜在バグを1件発見・修正した。** `wireNormalMode()`の`onDeferredInit`は`renderPipeline.attach()`/`setDocument()`を呼ぶ一方、`syncRenderStateAndInvalidate()`(カーソル状態をRenderPipelineへ反映する関数)を一度も呼んでいなかったため、`m_cursorVisuals`はユーザーが最初にカーソルを動かすまで空のままだった。ファイルを`--open`で開いた直後のスクリーンショットでBreadcrumbが完全に無表示だったことから発覚 — この根本原因はBreadcrumb固有ではなくキャレット描画自体にも及ぶ(起動直後はキャレットも実質不可視だった)ため、`onDeferredInit`末尾の`::InvalidateRect()`を`syncRenderStateAndInvalidate()`呼び出しに置き換える形で、Breadcrumbとキャレット両方を同時に解消した(1箇所の共有ロジックの根本修正であり、Phase 7gの「他4オーバーレイの同種バグは別タスクへ切り出す」判断とは異なり、切り分けが不可能な性質のバグだったため同一PR内で修正)
- **実アプリでC++ファイル(namespace > class > method の3階層)を開き、`--open`起動直後(カーソル移動なし)と矢印キーでのカーソル移動後の両方でBreadcrumbが正しく表示・更新されることをスクリーンショットで確認した。** 矢印キーは修飾キーを伴わないため、Phase 7gで判明した「修飾キー付きショートカットは合成入力できない」制約の対象外で、通常のSendKeys+スクリーンショット手法がそのまま機能した

### 実装後の確定事項/変更点 (2026-07-26、Phase 7i完了)

**roadmap §7.10の「折り畳み」のコア基盤(`core::FoldingModel`、キーボードトグルのみ)を実装した。ガター+/-クリックでのトグルは意図的に次のサブフェーズへ据え置いた。**

- **roadmap原案の「`Viewport`が表示行(display line)空間を管理し内部で論理行へ変換する」二重座標系は不採用にした。** `document::LineNumber`はコードベース全体で「論理行番号」の意味のまま維持し、`RenderPipeline`の描画(`drawVisibleLines()`)・`hitTest()`・移動キー補正(`editor_input.cpp`)の3消費箇所それぞれに「隠れた行をスキップする」ローカルなウォークを追加する方式にした。`core::Viewport`は自身のヘッダコメントが予言していた通り無改修のまま、`core::SelectionModel`も無改修のまま実現できた — 二重座標系はViewport/RenderPipeline/SelectionModel全体を横断する遥かに大きな改修になる一方、ベンチマーク根拠のない先行実装になる(CLAUDE.mdルール10)ため見送った
- **折り畳み対象領域はPhase 7f/7gの`syntax::OutlineNode`(関数/クラス/構造体/名前空間)をそのまま流用した。** `{}`ブレースマッチングによる任意ブロック(if/for/while等)の折り畳みは新規AST走査基盤が要る別スコープとして意図的に外した。新規`app::buildFoldRegions()`(`fold_bridge.h`)が`OutlineNode`ツリーを平坦化して`core::FoldRegion`のリストに変換する — Phase 7fの`walkForOutline()`が`misc-no-recursion`指摘を2回連続で受けた教訓を踏まえ、この関数は最初から明示スタックによる反復実装で書いた
- **`core::FoldingModel`は`core::BookmarkManager`と同じ「編集追従なし」制約をそのまま踏襲した。** このコードベースにはEditEvent購読機構が無いため、折り畳み領域はファイルを開いた時点で1回+アウトラインパネル(Ctrl+Shift+O)を開くたびに再計算(既存の`refreshOutlinePane()`が既に`extractOutline()`を呼んでいるためコスト0で相乗り可能)のみで、毎編集での再計算はしない。既存の折り畳み状態は見出し行番号(`headerLine`)で照合し、まだ存在する領域は畳んだまま維持する(`setFoldableRegions()`)
- **カーソルが隠れた行に入り込むケースを、移動キー(Up/Down/PageUp/PageDown)とジャンプ経路とで異なる補正方式にした。** 移動キー(`editor_input.cpp`の新規`snapPastHiddenLine()`)は最短距離で折り畳み範囲の境界へスナップする。同一ドキュメント内ジャンプ(Ctrl+G、F2ブックマーク次/前)は着地行を含む全ての折り畳みを`FoldingModel::revealLine()`で自動展開する。**別ファイルへのジャンプ(Grep結果、タグジャンプF12)は`openDocumentAt()`が`Document`を丸ごと差し替えるため、展開ではなく`foldingModel.setFoldableRegions({})`で全クリアする方式にした** — 展開のままだと旧ファイルの行番号キーの折り畳み領域が新ファイルの無関係な行を隠す実害あるバグになると実装中に気づき、`findReplaceState`/`renderPipeline.setBookmarkedLines({})`と同じ「`openDocumentAt()`が内部でリセットしないものは呼び出し側でリセットする」既存パターンを踏襲した。アウトラインパネルからのジャンプとマウスクリックは着地点の性質上そもそも補正不要(前者は常にシンボル見出し行、後者は`hitTest()`自体が可視行しか歩かない構造)
- **v1はキーボード操作のみとし、ガター+/-クリックでのトグルは次のサブフェーズへ据え置いた。** 折り畳みマーカ(▶/▼)自体はガターに常時描画するが、クリック判定(`hitTestFoldMarker()`、マウスディスパッチへの新規配線)は追加しない — Phase 4b8dが「タブ⇔スペース変換」をまずコマンドパレット経由のみで出荷した前例と同じ判断。新規コマンド`view.toggleFoldAtCursor`(「Fold/Unfold at Cursor」)をコマンドパレットに追加し、主カーソル行が折り畳み見出しならその領域をトグルする
- **`applyMovementKey()`が内部リンケージ(`editor_input.cpp`の無名namespace内)でmain.cppから直接呼べないことが実装中に判明し、計画を修正した。** 当初計画は`applyMovementKey()`に直接`folding`引数を追加する想定だったが、実際の公開APIである`handleKeyDown()`(`editor_input.h`)側にデフォルト`nullptr`の`const core::FoldingModel*`引数を追加し、内部で`applyMovementKey()`へ伝播する方式に修正した(既存呼び出し・テストは無改修)
- **ローカル検証中、Phase 7iの隠れた行スキップロジック追加により`RenderPipeline::drawVisibleLines()`が`readability-function-cognitive-complexity`(閾値25に対し実測31)でclang-tidyエラーになる新規違反を検出・修正した。** 折り畳みヘッダマーカー描画ロジックを含む1行分の描画処理全体を新規`drawTextLine()`private関数へ抽出し(`computeCaretDraws()`のPhase 4b7a抽出と同じ理由)、`drawVisibleLines()`自体は「可視行を数えながら歩き、可視行ごとに`drawTextLine()`を呼ぶ」骨格だけに単純化した
- **実アプリ視覚確認は、起動時に自動生成されるガター折り畳みマーカー(▼)の表示のみに限定した。** 「Fold/Unfold at Cursor」コマンド自体はコマンドパレット(Ctrl+Shift+P)経由でのみ到達可能だが、Phase 7gで判明済みの「この自動化環境は修飾キーを伴う合成入力を受け付けない」制約によりコマンドパレット自体を開けないため、トグル操作そのものの対話的確認はできなかった。ネストしたC++ファイル(namespace > class > 2メソッド + 独立関数)を`--open`起動した直後のスクリーンショットで、全ての折り畳み可能な見出し行(namespace/class/関数3件)にのみ展開チェブロン(▼)が表示され、1行に収まるメンバ(`int m_value = 0;`等)には表示されないことを確認した。トグル後の実際の折り畳み表示(`{…}`マーカー・隠れた行のスキップ)は`tests/integration/render_text_smoke_test.cpp`の`FoldedRegionRendersWithoutErrorAndHitTestSkipsHiddenLines`(`setFoldRegions()`で直接`folded=true`を設定し`render()`成功+`hitTest()`が隠れた行を飛ばすことを確認)で代替した

### 実装後の確定事項/変更点 (2026-07-26、Phase 7j完了)

**Phase 7iが意図的に据え置いた唯一の残課題、ガター+/-クリックでの折り畳みトグルを実装した。これによりroadmap上の「折り畳み」機能が名実ともに完結した。**

- **`RenderPipeline::hitTestFoldMarker(xPx, yPx)`はマーカー自体の描画幅(▶/▼、約7dips)ではなく、ガター全幅(`[0, kGutterWidthDips)`)×フォールド見出し行をクリック可能領域とした。** VSCode等の折り畳み矢印も比較的寛容なヒット領域を持つ一般的慣習に合わせ、小さすぎるターゲットへの精密クリックをユーザーに要求しない判断
- **`hitTest()`内にインライン実装されていた「可視行をrowOffset分歩いて対象論理行を求める」ウォークを、新規private関数`visibleLineAtRow()`へ抽出し`hitTest()`/`hitTestFoldMarker()`の両方から共有した。** 抽出しなければ`drawVisibleLines()`のendLineExclusive計算と合わせて同種のロジックが3箇所に重複する状態になっていた
- **`main.cpp`の`cfg.onMouseDown`の先頭で`hitTestFoldMarker()`をチェックし、値があれば`foldingModel.toggleFold()` → `syncFoldingState()` → 即returnして通常の`hitTest()`/`dispatchMouseDown()`経路を完全にバイパスする設計にした。** クリック回数(`clickCount`)は無視し、シングル/ダブル/トリプルクリックいずれもフォールド見出し行のガタークリックなら常にトグルする(トグルボタン的UIの一般的挙動)
- **実装中、この1個の`if`チェックの追加だけで`wireNormalMode`のcognitive complexityが26(閾値25)を超過した。** `tryToggleFoldMarker()`という別関数へ処理自体を切り出しても、呼び出し元の`if (...) return;`という分岐がラムダ内に残っている限り複雑度は下がらないと判明し、`onKeyDown`/`onChar`/`onSysKeyDown`で既に確立していた「ラムダは薄いラッパーのみ、本体は`handleXEvent()`という独立関数に完全移譲する」パターンへ、`onMouseDown`ハンドラ全体(`dispatchMouseDown()`呼び出しを含む既存ロジックごと)を初めて合わせて解消した(新規`handleMouseDownEvent()`)
- **実アプリでのマウスクリック合成(`SetCursorPos`+`mouse_event(MOUSEEVENTF_LEFTDOWN|LEFTUP)`)により、ガター上のフォールドマーカークリックでの折り畳み/展開の往復トグルを実際にスクリーンショットで確認できた。** Phase 7g/7hで判明していた「Ctrl/Shift等の修飾キーを伴う合成キーボード入力は受け付けない」制約は、マウスクリック自体(修飾キー無し)には適用されないことが実証された — 折り畳みコマンド(コマンドパレット限定、Phase 7i)とは異なり、ガタークリックはキーボードショートカットを一切経由しないため、この自動化環境から完全に対話的検証ができた最初の折り畳みUI操作になった
- **視覚確認中、本機能とは無関係な環境ノイズ(フォーカスウィンドウへの迷子キー入力とみられるIME変換候補の混入)を観測したが、原因調査の結果`tryToggleFoldMarker()`はキーボード/IME処理に一切触れずreturnするコードであることを確認し、無関係な外部要因と判断した。** 折り畳みトグル自体の正しさ(マーカー反転・`{…}`表示・隠れた行のスキップ・展開への復元)は複数回の独立した実行で再現性を持って確認できている

**スコープ外(意図的、後続サブフェーズへ):** マウスドラッグでの複数行一括トグル、フォールドマーカーのホバー時ビジュアルフィードバック、フォールドマーカークリック直後のドラッグ時のアンカー整合性改善(既知の軽微なエッジケースとして許容)。詳細は`detailed_design.md` §10.12参照。

### 実装後の確定事項/変更点 (2026-07-28、Phase 7k完了)

**§7.9「非同期増分解析」が繰り返し「Documentに編集範囲追跡が無い」と記録してきた技術的負債に着手し、`document::EditDelta`(編集差分追跡)+`syntax::IncrementalParser`(tree-sitterの`ts_tree_edit()`を使った真の増分再解析)をヘッドレスに実装した。SyntaxWorker統合・RenderPipeline配線は次サブフェーズ(Phase 7l)へ意図的に据え置いた。**

- **`document::Document`に`EditDelta`(開始位置・旧終端位置・新終端位置、それぞれ論理行番号+桁位置つき)+`takePendingEdits()`(蓄積した差分を排出)を追加した。** `insertText()`/`eraseRange()`/`replaceRange()`の3メソッド全てが、旧側の位置情報を`PieceTable`変更前に、新側を変更後に計算する(`Document`は編集をその場で行うため、変更後に旧い行構造を問い合わせる手段が無いため)。`edit_commands.cpp`の全コマンド(execute/undo双方)はこの3メソッドを直接呼ぶため、Undo/Redoは新規の分岐無しに自動的にカバーされた
- **`LineIndex::build()`のO(N)フルスキャンは新規コストではないと判断した。** `Document::ensureLineIndex()`は既存の「次の問い合わせ時に1回だけ再構築」設計(`docs/issues/line_index_o_log_n.md`で意図的に許容済みの制約)を持ち、`RenderPipeline`は既に毎フレームこれらを呼ぶため、`EditDelta`計算のために`offsetToLine()`を呼んでも、既に発生する再構築を1箇所前倒しするだけで新たな漸近コストは生まれない
- **`neomifes::syntax::IncrementalParser`(新規)は前回の`TSTree`を保持し、`ts_tree_edit()`で各編集を順に適用してから`ts_parser_parse_string_encoding()`に渡すことでtree-sitterのサブツリー再利用を活かす。** 既存の`walkTree()`/leaf分類テーブル(`namedLeafKindsForCpp()`等)は`syntax.cpp`の匿名namespaceから新規`syntax_internal.h`(`src/`内の非公開ヘッダ、`include/`ではない)へ切り出し、`syntax.cpp`(既存の単発フルパース)と`incremental_parser.cpp`の両方から共有する形にリファクタした
- **正しさは「増分再解析結果が同じ最終テキストへの全文書再解析結果と完全一致する」ことを単体テストで直接証明した。** 単一文字挿入/削除・複数行にまたがる置換・改行挿入(行構造そのものが変わる編集)・3回連続の編集・Pythonでも確認 — tree-sitterの`TSInputEdit`(バイトオフセット+行/桁の3点×2)を正しく構築できているかは、この種の直接比較でしか実質的に検証できない
- **ベンチマーク実測(roadmap本節のDoD「1文字入力後の増分解析: ≤ 50ms」に対する評価、CLAUDE.mdルール10):** 5万行の合成C++ソース(既存`BM_ParseCpp_Synthetic`と同一)に対し、全文書再解析(`parseCpp()`)が**約1.3秒**であるのに対し、単一文字の置換編集を挟んだ増分再解析は**約320ms**(約4倍高速化、tree-sitterのサブツリー再利用自体は機能している)。**ただしDoDの≤50msには未達。** 編集位置を文書の中央/末尾近くに変えても測定値がほぼ変わらないことから(326ms/341ms/321ms)、`IncrementalParser::reparse()`が呼び出しのたびに`walkTree()`で結果の`TokenKind`列**全体**を再構築するO(文書サイズ)のコストが支配的だと判明した — tree-sitterの内部再解析自体が真に増分的であっても、その後の「トークン列全体を作り直す」後処理がボトルネックを作っている。**次サブフェーズ(Phase 7l)での対応方針として、tree-sitterの`ts_tree_get_changed_ranges()`(変更のあった範囲だけを返すAPI)を使い、変更範囲のトークンだけを再抽出して`RenderPipeline`側の既存`m_tokens`へマージする設計が必要になる**(トークン列を毎回丸ごと差し替える現行の`applyAsyncSyntaxTokens()`方式からの転換)
- **スコープを意図的に2段階へ分割した判断は妥当だったと確認できた。** `SyntaxWorker`の「保留中のリクエストは最新の1件のみ保持し古いものは破棄する」設計は、1つでも編集を取りこぼすと`ts_tree_edit()`の前提が崩れ木のバイトオフセットが永久に狂うため、真の増分再解析とは原理的に両立しない。この整合性問題とスレッド安全性の設計は、ヘッドレスな正しさの証明(本フェーズ)とは独立した検討が必要であり、分離して正解だった

**スコープ外(意図的、Phase 7lへ):** `SyntaxWorker`への統合(「破棄して最新のみ残す」キューモデルを「全編集を順序通り適用するキュー」へ置き換える設計)、`RenderPipeline::refreshDocumentCacheIfStale()`の書き換え、`ts_tree_get_changed_ranges()`を使った変更範囲限定トークン抽出+`m_tokens`へのマージ(上記ベンチマーク考察で判明した新規スコープ)、アウトライン抽出の増分化。詳細は`detailed_design.md` §10.13参照。

### 実装後の確定事項/変更点 (2026-07-28、Phase 7l完了)

**Phase 7kが意図的に据え置いた「`SyntaxWorker`への統合」に着手し、`syntax::IncrementalParser`が実際に使われる機能になった。** roadmap §7の「真の増分再解析」ラインはこれで完結したが、性能面のDoD(≤50ms)はまだ別のボトルネック(下記参照)により未達のまま。

- **`SyntaxWorker`のキューモデルを「保留中のリクエストは最新の1件のみ保持し古いものは破棄する」から「`edits`を蓄積(追記、上書きしない)し1件も取りこぼさない」へ刷新した。** `snapshot`/`language`/リセットフラグは最新のもので上書きするが、`document::EditDelta`の蓄積ベクタだけは`requestParse()`が呼ばれるたびに追記されワーカーがpickupするまで消えない。ワーカーがpickupするまでに複数回`requestParse()`が呼ばれても、蓄積された全編集を1回の`IncrementalParser::reparse()`呼び出し(`std::span`で複数編集を順に`ts_tree_edit()`適用できる、Phase 7k実装済み)にまとめて渡す設計にした
- **ドキュメント切り替え時の別ハザードを実装前に発見した。** `IncrementalParser::reparse()`は`edits`が空でも、保持木が非nullなら無条件にtree-sitterの再解析ヒントとして渡してしまう(`ts_tree_edit()`で明示的に伝えない限り「何も変わっていない」とtree-sitterが仮定してしまう)。F12タグジャンプ/Grep結果ジャンプ等で無関係な別ファイルへ切り替わった直後に空`edits`だけを渡すと、無関係な保持木を使った誤った再解析結果になりうる。対策として`SyntaxWorker::requestParse()`に明示的な`resetIncrementalState`引数を追加し、真のときはワーカーが保持中の`IncrementalParser`インスタンスを新規構築で丸ごと差し替える(新規インスタンスは保持木が`nullptr`から始まるため、`reset()`のような専用メソッドは不要)
- **「ドキュメントが切り替わった」の既存シグナルとして`RenderPipeline::setLanguage()`をそのまま再利用した。** `setLanguage()`は既に`m_hasCachedSnapshot = false`を立てる設計だったため、`refreshDocumentCacheIfStale()`内で更新前の値を`const bool forceFullReparse = !m_hasCachedSnapshot;`として捕捉するだけで「初回呼び出し」と「ドキュメント切り替え」の両方を検出でき、新規フラグを追加する必要がなかった
- **`RenderPipeline::m_document`を`const document::Document*`から`document::Document*`へ変更した。** `Document::takePendingEdits()`が非const(蓄積ベクタを`std::exchange`で排出する)メソッドのため。既存の全呼び出し箇所(`version()`/`snapshot()`/`lineCount()`/`offsetToLine()`/`lineToOffset()`)はいずれもconstメソッドのみを呼んでいたため後方互換で、`setDocument()`の唯一の実引数(`main.cpp`の非const`Document`)とも問題なく整合した
- **既存の`render_syntax_worker_test.cpp`の「無関係な2つのDocumentを連続要求→古い方は破棄される」テストは、Phase 7lで意図的に廃止する挙動そのものをピン留めしていたため書き直した。** 新版は同一Documentへの連続編集を間を置かず2回要求し、ワーカーが2回を1回にまとめて処理しても最終トークンが最終テキストの全文書再解析と完全一致することを確認する形にし、「取りこぼしが無い」という新しい契約自体を検証するテストへ転換した。加えて`resetIncrementalState`単体の効果(同一言語のまま無関係な別ドキュメントへ切り替えても保持木が正しく破棄されること)を検証する新規テストを追加した
- **性能面: 本フェーズは「取りこぼさないスレッド統合」という正しさの軸のみを達成し、体感速度の改善はまだ実現していない。** `IncrementalParser::reparse()`自体がPhase 7kで既に判明していた「呼び出しのたびにトークン列全体を`walkTree()`で再構築する」ボトルネック(約321ms/call、50,000行合成ソース)を抱えたままのため、本フェーズの変更だけでは実測値は変わらない。次サブフェーズで`ts_tree_get_changed_ranges()`による変更範囲限定抽出へ転換して初めて、roadmap §7.11のDoD「≤50ms」に近づける見込み
- **実アプリでの視覚確認は、この自動化環境のスクリーンショット手法(Phase 7b以来確立していたはず)が本セッションでは機能しなかった。** `GetWindowRect`/`IsWindowVisible`は正常値を返しウィンドウは実在するが、その領域を`CopyFromScreen`で撮ると常にデスクトップが写り込み、ウィンドウ中心への実クリックでもフォーカスが移らないことまで確認した(=このセッションの自動化からは実際に見えている画面にウィンドウが合成されていないことの一貫した証拠)。恒久的な退行と断定はせず次回再検証する前提で、今回は自動テスト(`render_syntax_worker_test.cpp`の非同期ワーカー統合テスト4件、全てpump-and-wait方式で実スレッド・実メッセージ配送を検証)+プロセス生存確認(ファイルを開いた状態で約2分間`Responding=True`を維持、新規ミューテックス/条件変数ロジックがデッドロックしていないことの間接証拠)で代替した

**スコープ外(意図的、後続サブフェーズへ):** `ts_tree_get_changed_ranges()`による変更範囲限定トークン抽出(`walkTree()`全件再構築の解消、DoD達成に必要)、アウトライン抽出(`extractOutline()`)の増分化、複数言語を同時に保持するワーカー設計。詳細は`detailed_design.md` §10.14参照。

### 実装後の確定事項/変更点 (2026-07-28、Phase 7m完了)

**Phase 7l/7kが繰り返し据え置いてきた性能課題(`IncrementalParser::reparse()`が呼び出しのたびにトークン列全体を`walkTree()`で再構築するボトルネック)に着手した。tree-sitterの`ts_tree_get_changed_ranges()`を使い、実際に構造が変化した部分木だけを再抽出する内部最適化を実装したが、実測の結果「漸近的改善ではなく定数倍改善」であったことが判明した — 期待と実測が食い違った結果を隠さず記録する。**

- **`IncrementalParser`の公開契約は一切変更しなかった。** `reparse()`は引き続き「`text`+`edits`を渡すと全文書再解析と完全一致する完全なトークン列を返す」契約のまま、内部実装だけを「未変更の部分木は前回のトークンを再利用し、変更のあった部分木だけ新規に`walkTree()`で再抽出する」形へ差し替えた。これにより`render::SyntaxWorker`/`RenderPipeline`/`main.cpp`への変更は一切不要になり、Phase 7lで構築したばかりの統合コードに触れずに済んだ
- **`ts_tree_get_changed_ranges()`単体では不十分であることを実装中に実測で発見した。** 同APIは「新旧木で構文構造(祖先ノード)が変化した範囲」のみを報告し、数字の直後に数字を挿入して1つのリーフが伸びるだけ(構造自体は変わらない)といった境界接触型の変更では**空配列を返す**ことを、実際に失敗するテストのデバッグ出力で確認した。対策として、各editの文字通りの範囲(`ts_range_edit()`でバッチ内の後続editを通じて最終座標へ前方伝播した`computeDirtyRangesInFinalCoordinates()`)も「変更範囲」として無条件に扱うよう設計した — 木の構造差分と文字通りの編集範囲は、互いに補い合う別々の情報源だと判明した
- **範囲の重なり判定を「接触も重なりとみなす」包含的な判定に変更する必要があった。** 純粋な削除(ゼロ幅の変更範囲)がノード境界を正しく検出できない失敗が実測で見つかったため、通常の`TextRange`(半開区間、接触は重ならない)とは異なる、意図的に緩い判定を採用した
- **正しさの検証は既存の「増分再解析結果 == 全文書再解析結果」というテストオラクルをそのまま踏襲した。** 内部実装(位置シフト・部分木プルーニング・マージ)を個別に手検証する必要がなく、境界条件(文書先頭/末尾)・未終端コメント挿入による構造カスケード・複数editバッチ・4回連続の増分再解析・Pythonを含む7件の新規テストケースを追加し、いずれも既存の全文書再解析と完全一致することで正しさを証明した。テスト作成・デバッグの過程で2件、自分が書いたテスト自体のオフセット計算ミス(実装ではなくテスト側の誤り)を自己発見・修正した
- **ベンチマーク実測(CLAUDE.mdルール10):** 5万行の合成C++ソースで、単一文字編集を挟んだ増分再解析は約148ms/call(全文書再解析1243ms比で約8.4倍高速、Phase 7kの旧実装321ms比で約2.2倍高速) — 確かな、実質的な改善が得られた
- **ただし着手前に期待していた「漸近的改善」(文書サイズに依存しない一定コスト)は、大規模文書での追加ベンチマークにより明確に否定された。** 50万行(10倍)版の同一ベンチマークが約1419ms/call(ほぼ10倍)となり、文書サイズにほぼ比例してコストが増加することを確認した。根本原因を分析した結果、`reparse()`が依然として「呼び出しのたびに文書全体サイズのトークン列を確保・返却する」設計のままであり、`shiftTokensForEdits()`(前回のトークン列を位置シフトする処理)が保持トークン列**全体**を毎回舐める設計であることが判明した — `walkTreeIncremental()`自体は変更範囲だけを効率よく再抽出できているが、その前後の「全トークンをシフトする」「全トークンを新しいベクタとして確保・返却する」というO(文書サイズ)のコストは解消されないまま残った。**達成できたのは、tree-sitterのAPI呼び出し(木のトラバース・型判定・ハッシュマップ検索を伴う相対的に高価な操作)を安価な配列操作へ置き換えたことによる定数倍の高速化であり、計算量クラス自体の変更ではない。** roadmap本節のDoD「≤50ms」は5万行の最良ケースでも未達のまま
- **真にO(編集サイズ)を達成するには、`IncrementalParser`の公開契約自体を変更し、完全なトークン列ではなく変更分だけの差分を返す設計への転換が必要と判明した。** 呼び出し側(`SyntaxWorker`/`RenderPipeline`)がその差分を永続化済みのトークン列へマージする責務を負うことになり、これはPhase 7kが当初のroadmapスケッチから意図的に外した設計(本フェーズの設計方針2参照)そのものである。本フェーズはブラスト半径を`IncrementalParser`単体に抑えるために意図的にこれを避けたが、次にDoD達成を目指すなら、この契約変更に正面から取り組む必要がある

**スコープ外(意図的、後続サブフェーズへ):** `IncrementalParser`の公開契約を「差分のみ返却」へ変更する設計(真のO(編集サイズ)達成に必要、`SyntaxWorker`/`RenderPipeline`側のマージロジック新設を伴う大規模変更)、残り21言語対応、ミニマップ、Sticky scroll。詳細は`detailed_design.md` §10.15参照。

### 実装後の確定事項/変更点 (2026-07-28、Phase 7n1完了)

**Phase 7mで「残り21言語対応」が次候補として浮上したのに続き、実際に着手した。§7.2の必須23言語のうちC++/Pythonの2言語のみ対応だった状態から、tree-sitter公式organization配下で最新リリースタグが確認できた6言語(C・JavaScript・Java・Go・Rust・JSON)をバッチ1として追加した。**

- **21言語を1PRで一括対応せず、信頼度の高い6言語のバッチへ意図的に絞った。** 各言語の文法追加は「FetchContent追加+ビルド確認+実機probeでのnamedLeafKindsForX()テーブル検証+parseX()実装+テスト」という機械的だが検証コストの高い作業(Phase 7a/7dで確立した規律)であり、21言語分を無検証で一括投入することはCLAUDE.mdルール3(推測実装をしない)に反すると判断した。TypeScript(1リポジトリに2文法が同居し別のCMake取り扱いが要る)、PHP/HTML/CSS/XML/YAML/SQL/Markdown(外部スキャナや複雑な文法が多い)、PowerShell/VB/VBS/BAT/Shell/INI/TOML(コミュニティ文法で事前検証コストが高い)、SAP ABAP(roadmap上もP1)は次バッチ(7n2以降)へ据え置いた
- **`Language`→`TSLanguage*`の対応を`syntax_internal.h`の`detail::tsLanguageFor()`へ一元化した。** 2言語時代は`syntax.cpp`/`incremental_parser.cpp`それぞれが独自の対応switchを持っていても問題なかったが、8言語化にあたり`outline.cpp`の`extractOutline()`が持っていた**2値の三項演算子(`language == Cpp ? tree_sitter_cpp() : tree_sitter_python()`)が、Cpp以外の全ての言語を無言でPython文法として誤ってパースする潜在バグ**だったことが判明した(コンパイルは通ってしまうため、実際に新言語のファイルを開いて初めて症状が出る類のバグ)。一元化により3ファイルの重複switchを1箇所に統合し、この種の同期漏れを構造的に防いだ
- **outline抽出は「正しい文法選択+安全な空結果」のみ今回対応し、シンボル抽出ロジック本体(関数/クラス/構造体の実際の認識)は次バッチへ据え置いた。** `symbolTableFor()`が新6言語には空の`SymbolTable`を返すため、これらの言語を開いてもBreadcrumb/アウトラインペインは空のまま(クラッシュや誤った言語での誤パースはしない) — `outline.h`が元々文書化している「認識できる定義が無ければ空ベクタを返す」契約の範囲内の、安全側の劣化
- **実機probe中に、tree-sitter-rustの`line_comment`/`block_comment`が非葉ノードであることを発見した(記憶からの推測ではなく実際のパーサ出力で確認、CLAUDE.mdルール3)。** 子として`//`/`/*`/`*/`の区切り文字だけを持ち、コメント本文はどの子ノードにも属さない — 既存の`walkTree()`(Phase 7aから「`child_count()==0`が葉」という前提)ではこの区切り文字だけがPunctuationとして誤分類され、本文が無彩色のままトークンストリームから丸ごと欠落する。対策として`isAtomicNode()`(「真の葉、またはLeafKindTableに直接エントリを持つ名前付きノードなら子を持っていても降りない」)へ一般化し、`walkTree()`(全文書再解析)と`walkTreeIncremental()`(Phase 7m増分再解析パス)の両方に適用した
- **この一般化の副作用として、Pythonの文字列エスケープ内の平文部分が無彩色になっていた既知のギャップ(Phase 7dで「KNOWN, ACCEPTED gap」として文書化済み)が意図せず解消された。** `string_content`ノードが`escape_sequence`子を持つ非葉ケースで、`isAtomicNode()`が同ノードを(既にテーブルに登録済みのため)atomicと判定し、平文+エスケープシーケンスを1つのStringトークンとして丸ごと着色するようになった。既存の`syntax_syntax_test.cpp`のテストがこの新しい(改善された)挙動に合わせて更新された — 意図した設計目標ではなく偶発的な副産物だが、退行ではなく改善であるためテストのコメントに明記した
- **バックティック(`` ` ``)を`classifyAnonymousLeaf()`の引用符扱いに追加した。** Go の生文字列リテラルとJavaScriptのテンプレート文字列がどちらも区切り文字にバックティックの無名リーフを使うことを実機probeで確認し、`"`/`'`と同じくString着色対象へ加えた(既存の`"`/`'`の扱いと一貫)
- **実アプリでの視覚確認は、スクリーンショット自動化がこのセッションでは無関係かつ不適切なウィンドウ内容を誤って撮影する不具合が発生し(即座に削除、他に保存・共有せず)、信頼できないと判断して中断した。** Phase 7lで記録した「ウィンドウは実在するが`CopyFromScreen`が別の画面を写す」という不調の再発に加え、今回は誤った内容を撮影する新しい失敗モードが確認された。代替として、新6言語のサンプルファイル(`.c`/`.js`/`.java`/`.go`/`.rs`/`.json`)を実際に開いてクラッシュしないこと・プロセスが`Responding=True`を維持することを確認し、正しさの証明は自動テスト(777件全green、6言語分の分類テスト・拡張子検出テスト・outline安全性テスト・Rust増分再解析テストを含む)に委ねた

**スコープ外(意図的、後続バッチへ):** TypeScript/PHP/HTML/CSS/XML/YAML/SQL/Markdown/PowerShell/VB/VBS/BAT/Shell/INI/TOML/SAP ABAP、新6言語のoutlineシンボル抽出ロジック本体、`RenderPipeline`/`SyntaxWorker`/`main.cpp`への変更(Phase 7dで確立済みの汎用ディスパッチがそのまま機能するため不要)。詳細は`detailed_design.md` §10.16参照。

### 実装後の確定事項/変更点 (2026-07-28、Phase 7o完了)

**roadmap §7.6のSticky scrollを実装した。依存基盤(`core::FoldingModel`/`FoldRegion`、Phase 7i/7j)と隣接する類似機能(`Breadcrumb`、Phase 7h)がどちらも完成済みだったため、4候補(Sticky scroll/ミニマップ/残り15言語バッチ2/`IncrementalParser`差分返却化契約変更)中もっとも具体的にスコープが固まっていた。**

- **`m_foldRegions`(Phase 7i)は「折り畳み中かどうか」に関わらず全foldable regionの`headerLine`/`endLineInclusive`を保持していることを確認し、Sticky scrollに必要な「現在の`topLine`を包含する、折り畳まれていないregion」の判定を既存データ構造だけで実現した。** `main.cpp`側の新規配線は一切不要だった
- **`RenderPipeline::setTopLine()`の「まだ誰も呼んでいない」という既存のヘッダコメントが、実際には`main.cpp`の`syncRenderStateAndInvalidate()`が毎フレーム`viewport.topLine()`を渡しているにもかかわらず古いまま(Phase 3b時代の記述)残っていたことを発見し、本フェーズで併せて修正した(CLAUDE.md §11のドキュメント鮮度チェック)**
- **`drawBreadcrumb()`(Phase 7h)を直接のテンプレートとして採用し、背景ブラシ(`m_breadcrumbBackgroundBrush`)も新規追加せず再利用した。** Sticky scroll行のテキストはBreadcrumbと同様プレーンテキスト(シンタックスハイライト無し)とし、v1のスコープを意図的に絞った
- **Sticky scrollの帯は「該当regionが無ければ高さ0(帯自体を描かない)」という動的な高さを採用した(Breadcrumbの「常に固定高さの帯を描く」前例とは異なる判断)。** この結果、`drawVisibleLines()`のy起点・実効高さ計算と`hitTest()`/`hitTestFoldMarker()`のyDipオフセットが従来ハードコードしていた`kBreadcrumbHeightDips`を、新規共有ヘルパー`reservedTopHeightDips()`(`isLineHidden()`/`visibleLineAtRow()`と同じ「3箇所以上で使う小さな共有ヘルパー」パターン、Phase 7i/7j踏襲)へ一元化した
- **実アプリでの視覚確認は、対象ウィンドウへの合成キーボード入力(矢印キー・PageDown、いずれも修飾キー無し)が今回反応しなかったため断念した。** ウィンドウ所有プロセスIDの一致は`GetWindowThreadProcessId()`で確認済みで対象ウィンドウの取り違えではない — Phase 7l(スクリーンショットで何も見えない)・Phase 7n1(無関係なウィンドウを誤って撮影)に続き、今回は入力合成そのものが機能しないという3つ目の失敗モードが確認された。代替として、`setTopLine()`を直接呼ぶ統合テスト4件(帯の表示/非表示/折り畳みregion除外/ネスト内側region選択)とプロセス生存確認で検証した

**スコープ外(意図的、後続サブフェーズへ):** ネストした複数regionのスタック表示(VSCode相当の「外側→内側を複数行積み上げる」表示)、Sticky scroll行のシンタックスハイライト、行クリックでのジャンプ機能、ミニマップ・残り15言語対応バッチ2・`IncrementalParser`差分返却化契約変更。詳細は`detailed_design.md` §10.17参照。

### 実装後の確定事項/変更点 (2026-07-29、Phase 7p完了 — 性能リグレッション緊急修正)

**新機能追加ではなく、Phase 7j〜7oの12コミットをまとめてpushした直後のCI失敗を受けた緊急のバグ修正。「次のPhaseへ進め」ではなくユーザーの「確認せよ」指示でCI状況を調べた結果、`Build & Test`両ジョブが6時間のジョブ上限でキャンセルされていたことが発端。**

- **原因はPhase 7k (`document::EditDelta`導入) が持ち込んだ性能リグレッションだった。** `Document::insertText()`等が編集の都度`offsetToLine()`を呼ぶようになったことで、`m_lineIndexDirty = true`セット直後の呼び出しが毎回`LineIndex::build()`のO(文書長)フルスキャンを誘発していた。当時の設計判断(「RenderPipelineが毎フレーム払っていたコストの前倒しに過ぎない」)は、Document自身が高頻度に自己呼び出しする経路の存在を見落としていた
- **`neomifes_core_bench.exe`の`BM_UndoStack_PushOneMillion`(既存、Phase 4完了時にADR-012の根拠として追加されたベンチ)がこの回帰を検出する形になった。** 100万回の逐次1文字挿入がΣi(i=1..1,000,000)≈5×10¹¹相当のO(N²)となり、CI上で実質ハングしていた
- **[`docs/issues/line_index_o_log_n.md`](../issues/line_index_o_log_n.md)が2026-07-15時点で既に示唆していた「案C」(build()を全rebuildでなく変更範囲のみの差分更新にする)を採用した。** `LineIndex::applyInsert()`/`applyErase()`を新設し、`Document`の3変更メソッドが`m_lineIndexDirty`を立てる代わりにこれらを直接呼ぶよう書き換え、インデックスを常時クリーンに保つ設計にした。`Document`の公開契約(`offsetToLine`/`lineToOffset`/`EditDelta`の値)は一切変更していない
- **実測値(Release、ローカル):** `BM_UndoStack_PushOneMillion` 412.5ms(修正前: CI 6時間タイムアウトで未完走)、`BM_UndoStack_UndoOneMillion` 267.1ms。CLAUDE.mdルール10(性能改善はベンチマーク根拠)・要件定義書§5「Undo: 100万回以上」の実測裏付けとなった
- **Phase 7k〜7o時点のローカル検証(Debug/Release/ubsan/clang-tidy、各フェーズのセッションで実施済み)がこの回帰を捉えられなかった理由:** `core_undo_stack_bench.cpp`はCIの「ベンチマークスモーク実行」ステップでのみ実行され、`ctest`本体には含まれない(google-benchmark実行ファイルは`ctest`のテストケースとして登録されていない)。各フェーズのローカル検証では`ctest`を実行していたが、ベンチマークスモーク実行そのものは明示的に呼ばなければ走らないため、この回帰はローカルで再現されずCIで初めて顕在化した

**教訓・再発防止:** 高頻度に呼ばれる可能性のあるコアAPI(`Document`の変更メソッド等)に新しい計算を追加する際は、「他のどこかで既に払われているコストの前倒し」という主張は、その計算の**呼び出し元自身が高頻度ループの内側にいないか**を必ず確認する。CIのベンチマークスモーク実行は`ctest`とは独立したステップであるため、性能に関わる変更をレビューする際は明示的にベンチマーク実行ファイルをローカルで走らせて確認する。

**スコープ外(意図的):** `offsetToLine`/`lineToOffset`自体のO(log n)化(issue doc本来のスコープ、PieceTreeのツリー集約化=案A/B)は引き続き未着手。詳細は`detailed_design.md` §10.18参照。

### 実装後の確定事項/変更点 (2026-07-29、Phase 7q完了 — IncrementalParser差分返却化、DoD未達)

**Phase 7pのCI green確認後、ユーザーから「次のPhaseへ進め」と指示された。roadmap §7の残り候補(IncrementalParser契約変更/残り15言語バッチ2/ミニマップ)をAskUserQuestionで提示し、IncrementalParser契約変更(推奨案)が選ばれた** — `incremental_parser.h`のヘッダコメント自体が「差分のみ返却する契約への変更が必要」と既に明記していた、Phase 7k〜7mからの唯一の積み残し課題。

- **`IncrementalParser::reparse()`(完全なトークン列を返す契約)を`reparseDelta()`(差分`TokenPatch`のみ返す契約)へ完全に置き換えた。** `TokenPatch{invalidatedRange, shiftAmount, replacementTokens}`+新規公開関数`applyTokenPatch(tokens, patch)`(マージ処理、O(tokens.size()+replacementTokens.size())の単一線形パス)を新設。永続トークン列の保持責務を`IncrementalParser`(Phase 7mの`lastTokens`)から呼び出し側(`render::SyntaxWorker::workerLoop()`)へ移した — `RenderPipeline::applyAsyncSyntaxTokens()`は無変更のまま(マージ後の完全なトークン列を今まで通り受け取るだけ)で済み、影響範囲を`neomifes::syntax`/`SyntaxWorker`内部に閉じ込められた
- **tree-sitter公式ヘッダで`ts_node_descendant_for_byte_range(TSNode, start, end)`(「指定バイト範囲をspanする最小のノードを返す」)の存在を直接確認し、Phase 7mの`walkTreeIncremental()`(木全体をpre-order走査しつつ変更されていないノードだけ既存トークンをスプライスする複雑なロジック)を、「変更範囲を包含する最小の祖先ノードを1回で特定し、そのノード配下だけを既存の`detail::walkTree()`(rootノード引数を取る汎用関数、Phase 7aから無変更のまま再利用)で新規に歩く」というシンプルな設計に置き換えた。** `shiftTokensForEdits()`/`walkTreeIncremental()`/`nodeOverlapsAnyChangedRange()`等、Phase 7mのロジックの大半を削除できた
- **実装直後のテスト(`SingleCharacterDeleteMatchesFullReparseOfNewText`)で1件のバグを発見・修正した。** 純粋な削除編集(`"12"→"1"`)の無効化範囲がゼロ幅([18,18)バイト)になり、`ts_node_descendant_for_byte_range()`がノード境界上のこのクエリに対して「削除により縮んだnumber_literalノード」ではなく「無関係な直後の`;`トークン」を返してしまい、削除後も残るべき"1"というNumberトークンが完全に欠落するバグだった。`computeDirtyRangesInFinalCoordinates()`で、ゼロ幅になる範囲の開始位置を1コード単位(2バイト)後退させることで、クエリが常に非ゼロ幅になり削除位置の直前のノードを確実に含むよう修正した
- **実測(Release、ローカル): `BM_IncrementalReparse_SingleCharEdit`(5万行) 103ms、`_LargeDocument`(50万行) 989ms。** Phase 7m比で約30%の定数倍改善(148ms→103ms、1419ms→989ms)を達成したが、比率(約9.6倍/文書サイズ10倍)は依然としてほぼ線形であり、**roadmap DoD「≤50ms」は未達のまま。** 原因は`applyTokenPatch()`自体が「無効化範囲より後ろの全既存トークンをシフトする」というO(永続トークン列サイズ)の線形走査であり、tree-sitter側の再walkコストをO(edit size)化しても、マージ処理自体が文書サイズに比例するボトルネックとして残ったため。これはPlan策定時に「スコープ外」として明記していたリスクがそのまま現実になったもので、CLAUDE.mdルール10(「この最適化はXという性質を持つはずだ」という期待は大規模文書での追加ベンチマーク実測なしに完了報告に書いてはならない、Phase 7mで確立した規律)に従い正直に記録する
- **真のO(edit size)達成には、永続トークン列自体のデータ構造の再設計(可視範囲のみ保持する等、「シフトが実際に編集近傍のトークンだけに触れれば済む」構造)が必要と判明した。** 本フェーズはtree-sitterに面した半分(再walkの範囲限定)のみをスコープとし、この構造変更は意図的に次サブフェーズへ据え置いた

**教訓:** 「この設計変更で計算量クラスが変わるはず」という期待は、ボトルネックが1箇所ではなく複数箇所(今回はtree-sitter側の再walkコストと、呼び出し側のマージコストの2箇所)に分散している場合、片方だけを直しても全体としては改善しきれないことがある。大規模文書での実測(9.6倍という具体的な比率)によって、残っているボトルネックが「マージ処理のO(N)性」だと定量的に特定できたこと自体が、次のサブフェーズの設計を正しく方向づける成果になった。

**スコープ外(意図的、後続サブフェーズへ):** 永続トークン列のデータ構造再設計(真のO(edit size)化)、複数の独立した変更範囲を個別のTokenPatchとして返す設計、残り15言語対応バッチ2、ミニマップ。詳細は`detailed_design.md` §10.19参照。

### 実装後の確定事項/変更点 (2026-07-29、Phase 7r完了 — 追加言語対応 バッチ2)

**Phase 7qのCI green確認後、ユーザーから「次のPhaseへ進め」と指示された。roadmap §7の残り候補(残り15言語バッチ2/ミニマップ)をAskUserQuestionで提示し、残り15言語バッチ2(推奨案)が選ばれた** — Phase 7n1で確立した6言語追加の機械的な手順(GitHub API直接確認→実機probe→`namedLeafKindsForX()`テーブル→`parseX()`実装→`detectLanguage()`拡張)をそのまま再利用できる、見通しの立てやすい候補だった。

- **着手前調査(`gh api`によるGitHub直接確認、CLAUDE.mdルール3)で、roadmap §7.2の残り15言語のうち9言語がtree-sitter公式org(`tree-sitter/`)・準公式org(`tree-sitter-grammars/`)配下に存在することを確認した。** うちTypeScript/PHP/Markdownの3言語は1リポジトリに複数の`src/`ディレクトリ(文法)が同居する構造(TS: `typescript`/`tsx`、PHP: `php`/`php_only`、Markdown: `tree-sitter-markdown`/`tree-sitter-markdown-inline`)と判明し、「どちらを主要文法とするか」の追加の設計判断が必要になった。AskUserQuestionでユーザーに確認し、**単一`src/`構造の6言語(HTML/CSS/Shell/YAML/TOML/XML)に絞る案(推奨)が選ばれ**、TypeScript/PHP/Markdownは次バッチへ据え置いた。SQL/PowerShell/VB/VBS/BAT/INIは公式org配下に存在せずコミュニティ文法のみのため、Phase 7n1の判断方針を踏襲し対象外とした
- **YAMLの`src/`ディレクトリは`parser.c`/`scanner.c`に加え`schema.core.c`/`schema.json.c`/`schema.legacy.c`という3つの追加Cファイルを要することを実機ビルドで確認した(YAML文法自体がスキーマ検証をスキャナに埋め込んでいるため)。** XMLは`xml/`+`dtd/`の2ディレクトリ構成だが`xml/`が明確に主要文法(DTDは補助的なスキーマ言語)であり曖昧さが無いため単一文法として扱った
- **TOMLの`string`ノードとXMLの`AttValue`ノードが、どちらもPhase 7n1で発見したtree-sitter-rustの`line_comment`/`block_comment`と同種の「非葉ノード(引用符の無名子2つのみ、内容を持つ子ノードが無い)」であることを実機probeで確認した。** 既存の`isAtomicNode()`(Phase 7n1で「真の葉、またはLeafKindTableに直接エントリを持つ名前付きノード」へ一般化済み)のテーブルへ両ノードを登録することで、登録しなければ引用符内のテキスト自体がトークンストリームから丸ごと欠落するバグを未然に防いだ(TOMLの`"value"`、XMLの`attr="val"`の`val`部分)
- **YAMLのマッピングキーと値がどちらも同じ`string_scalar`ノード型を共有し、XMLの要素タグ名と属性名がどちらも同じ`Name`ノード型を共有することを実機probeで確認した(文法自体の構造的な曖昧さ)。** キーと値/タグ名と属性名を区別せず同じTokenKindで着色する、という文法の制約をそのまま受け入れる設計にした(JSONのオブジェクトキーと文字列値が同じ`string_content`を共有する既存の前例と同種のトレードオフ)
- **6言語すべてで、Phase 7n1確立の「正しい文法選択+空`SymbolTable`」パターン(outline抽出のシンボル本体は次バッチへ据え置き)をそのまま踏襲した。** `detail::tsLanguageFor()`の一元化switchに6ケースを追加するだけで済み、Phase 7n1が修正した「2値三項演算子による誤パース」バグの再発を構造的に防げた
- **実アプリでの視覚確認は、過去複数セッションでスクリーンショット/入力合成が不安定だったことを踏まえ、`--open`引数でHTML/YAMLサンプルファイルを実際に開きプロセスが3秒後もクラッシュせず生存していることを確認する軽量スモークテストに切り替えた。** 正しさの証明は自動テスト(834件全green、6言語分の分類テスト・拡張子検出テスト・outline安全性テスト・YAML増分再解析テストを含む、うち構造テストは全て実機probe出力からトークン列を手計算し検証)に委ねた

**スコープ外(意図的、後続バッチへ):** TypeScript/PHP/Markdown(複数文法サブディレクトリの主要文法選択判断が必要)、SQL/PowerShell/VB/VBS/BAT/INI/SAP ABAP(公式org不在)、新6言語のoutlineシンボル抽出ロジック本体、`RenderPipeline`/`SyntaxWorker`/`main.cpp`への変更(Phase 7dで確立済みの汎用ディスパッチがそのまま機能するため不要)。詳細は`detailed_design.md` §10.20参照。

### 実装後の確定事項/変更点 (2026-07-29、Phase 7s完了 — 追加言語対応 バッチ3)

**Phase 7r完了後、ユーザーから「次のPhaseへ進め」と指示された。roadmap §7の残り候補(残り9言語バッチ3/永続トークン列のデータ構造再設計/ミニマップ)をAskUserQuestionで提示し、残り9言語バッチ3(推奨案)が選ばれた** — Phase 7rで意図的に据え置いたTypeScript/PHP/Markdownが実質的な対象(SQL/PowerShell/VB/VBS/BAT/INIは公式org不在のため引き続き対象外)。

- **`gh api`/`curl`によるGitHub直接確認(CLAUDE.mdルール3)で、TypeScript(`tree-sitter/tree-sitter-typescript` v0.23.2)が`typescript/`(`.ts`向け)と`tsx/`(`.tsx`向け、JSX拡張込み)の2つの独立した完全な文法を持つが、Phase 7rでPHP/Markdownが直面した「どちらを主要文法とするか」という曖昧さはそもそも存在しないと判明した。** 両ディレクトリとも`parser.c`+`scanner.c`+`grammar.json`+`node-types.json`を完備し、拡張子で使い分ける設計(公式`CMakeLists.txt`自身が`typescript`/`tsx`を並列`add_subdirectory()`している)。このため`Language::TypeScript`(`.ts`/`.mts`/`.cts`)と`Language::Tsx`(`.tsx`)の2つのenumeratorを追加した(Language 1つに絞る判断は不要)
- **PHP(`tree-sitter/tree-sitter-php` v0.24.2)の`php/`(完全な文法、`<?php ?>`タグ+埋め込みHTML込み)と`php_only/`(タグなしの純PHPコードのみ、他言語への埋め込み用途)は、実際に`.php`ファイルを開く用途では`php/`が明確に唯一の正解であり、こちらもPHP/Markdownで警戒した曖昧さは実質存在しないと判明した。** `php_only/`は対象外、`php/`のみ採用
- **Markdown(`tree-sitter-grammars/tree-sitter-markdown` v0.5.3)の`tree-sitter-markdown/`(ブロックレベル)と`tree-sitter-markdown-inline/`(インラインレベル: 強調/リンク/インラインコード等)は、PHPと異なり「主要文法を選ぶ」構造ではなく、tree-sitterの言語注入(language injection)機構でブロック文法がインライン文法を段落テキストへ注入する設計(nvim-treesitter等が採用する標準パターン)と判明した。** `neomifes::syntax`には言語注入の仕組みが存在せず、新設は`walkTree()`/`IncrementalParser`双方への非自明な拡張を要するため、CLAUDE.mdルール10(ベンチマーク根拠のない先行複雑化を避ける)に従いv1は`tree-sitter-markdown`(ブロック文法)のみ採用、インライン文法は対象外とした。段落内の強調/リンク等は無彩色のまま(既存のHTML raw_text/CSS plain_valueと同種の受容済み簡略化) — ただし`` ` ``(バックティック、classifyAnonymousLeaf()の既存の引用符扱い)と`*`(Punctuation)が偶発的にインライン区切り文字として着色される副次効果を確認した(意図した機能ではなく、既存ロジックの無害な副産物)
- **TypeScript/TSXの`scanner.c`はどちらもリポジトリルート直下の`common/scanner.h`を相対`#include`で参照する(実機ファイル確認済み)ため、追加の`target_include_directories`設定は不要と確認した。** TypeScriptの`namedLeafKindsForTypeScript()`はJavaScriptの表(Phase 7n1)と大部分を共有する設計にし(tree-sitter-typescriptがtree-sitter-javascriptの文法を拡張する公式アーキテクチャ)、TSXはTypeScriptの表をそのまま再利用する形にした(JSX固有の新規named leaf型は実機probeで確認されなかったため)
- **TypeScriptの`predefined_type`ノード(組み込み型キーワード)は非leaf(子1つ、親と同一範囲を覆う無名子のみ)だが、TOMLの`string`/XMLの`AttValue`と異なりデータ欠落バグではない(子が既に全範囲をカバーしている)。** それでもテーブルへ登録し、Cpp/Rustの`primitive_type`と一貫してTypeとして着色する設計にした(データ欠落回避ではなく、言語間一貫性のための意図的な選択)
- **実アプリでの視覚確認は、`--open`引数でTypeScript/Markdownサンプルファイルを開きプロセスが3秒後もクラッシュせず生存していることを確認する軽量スモークテストで実施した。** 2つのサンプルを連続起動した際に2つ目が即座に終了する事象が一度発生したが、`Stop-Process -Force`直後に次のインスタンスを起動したことでADR-009の単一インスタンス用Named Mutexがまだ解放されておらず後発インスタンスが起動ハンドオフとして即終了しただけと判明(単独実行では再現せず、実際のクラッシュではない)

**スコープ外(意図的、後続バッチへ):** SQL/PowerShell/VB/VBS/BAT/INI/SAP ABAP(公式org不在)、Markdownのインライン文法+言語注入機構の新設、新4言語のoutlineシンボル抽出ロジック本体、`RenderPipeline`/`SyntaxWorker`/`main.cpp`への変更(Phase 7dで確立済みの汎用ディスパッチがそのまま機能するため不要)。詳細は`detailed_design.md` §10.21参照。

### 実装後の確定事項/変更点 (2026-07-30、Phase 7t完了 — 可視範囲スコープ化トークン再設計)

**Phase 7s完了後、ユーザーから次のPhaseとして「永続トークン列のデータ構造再設計」(推奨案)が選ばれた** — Phase 7qが明示的に積み残した唯一の宿題であり、`incremental_parser.h`のヘッダコメント自身が「Reaching true O(edit size) end-to-end would require restructuring how the persisted token list itself is stored」と本フェーズの設計を予告していた。

- **根本原因は「永続トークン列が常に文書全体をカバーする」という前提そのものにあると特定した。** `RenderPipeline::m_tokens`は可視範囲だけでなく文書全体のトークンを保持し続けており、だからこそ「一部を無効化して残りをシフトする」`applyTokenPatch()`のO(既存トークン数)コストが避けられなかった。**`m_tokens`を「可視範囲(+プリフェッチ余白)だけをカバーする」設計に変え、`TokenPatch`/`applyTokenPatch()`/`SyntaxWorker::workerLoop()`内の`persistedTokens`(Phase 7q)を丸ごと廃止し、毎回リクエストされた範囲だけを新規に(マージ無しで)ウォークして返す**、質的に異なるアーキテクチャへ置き換えた
- **`drawTokensOnLine()`を直接読解し、この置き換えが安全であることを確認した。** この関数は`m_tokens`(ソート済み)に対する単調な`tokenCursor`スイープであり、「トークンが無い区間はデフォルトブラシで描画される」を既に前提として実装されていた(既存コード変更不要)
- **新API`IncrementalParser::reparseRange(text, edits, rangeStartByte, rangeEndByte)`は、Phase 7qの`reparseDelta()`より実装がシンプルになった。** `ts_tree_get_changed_ranges()`による変更範囲特定→`computeDirtyRangesInFinalCoordinates()`によるマージ→無効化範囲/シフト量の計算、という一連の処理が全て不要になり、呼び出し側が渡した範囲を`ts_node_descendant_for_byte_range()`で直接ノード解決して`detail::walkTree()`するだけになった。返却契約は「要求範囲を**少なくとも**カバーする」(tree-sitterの最小包含ノードの性質上、要求範囲より広がることがある)
- **`SyntaxWorker`は単一バックグラウンドスレッドで直列に1件ずつリクエストを処理する設計(Phase 7c以来不変)であるため、レスポンスに「実際にカバーした範囲」を含める必要はないと判明した。** 古いレスポンスが新しいレスポンスより後に届くという競合は構造的に起こり得ず、`kMsgSyntaxTokensReady`のペイロード形状・`main.cpp`のハンドラ・`RenderPipeline::applyAsyncSyntaxTokens()`のシグネチャは全て無変更で済んだ(Phase 7l/7qのような複数ファイル同時変更に比べ影響範囲が小さく収まった)
- **「可視範囲が変わったら再リクエストする」トリガーを、`Document::version()`変化ゲート(`refreshDocumentCacheIfStale()`の早期return)とは独立させる必要があると判明した。** 純粋なスクロール(編集なし)では`version()`が変わらずこの関数の本体まで到達しないため、新規`RenderPipeline::ensureSyntaxTokensCoverVisibleRange()`を新設し、`renderOnce()`から毎フレーム無条件で呼ぶことで「編集された」「スクロールで可視範囲が要求済み範囲からはみ出た」の両トリガーを1箇所に統合した
- **余白サイズ(可視行数と同じだけ上下に1画面分)は未ベンチマークの出発点とし、大きくジャンプした場合(Ctrl+End等)は新しく見えた範囲が非同期応答到着まで一時的に無彩色になる仕様にした。** これはPhase 7c/7l以来既に受容されている「編集直後、非同期応答が届くまで無彩色」という仕様の自然な拡張であり、新しいUXカテゴリではないと判断し追加のユーザー確認は求めなかった
- **ベンチマーク実測(Release、`BM_ReparseRange_SingleCharEdit_*`):** 5万行narrow window 15.65ms(Phase 7qの103msから約6.6倍、roadmap §7.11のDoD「≤50ms」達成)。50万行narrow window 155.95ms・50万行full document 155.45ms(ほぼ同一) — **narrow windowとfull documentのコストが一致したことから、ボトルネックが`applyTokenPatch()`から`ts_parser_parse_string_encoding()`自体(文書サイズに比例するtree-sitter自身の再解析コスト、文字列ベースAPIの制約で常に文書全体のテキストを要求する)へ完全に移ったと確認した。** 大規模文書のDoD達成には、tree-sitterの`TSInput.read`コールバックAPIを`document::BufferSnapshot`/`PieceTable`に対して実装し、文書全体のテキスト実体化・再解析自体を回避する、本フェーズよりさらに大きな別のアーキテクチャ変更が必要と判明した(詳細は`detailed_design.md` §10.22参照)

**スコープ外(意図的、後続フェーズへ):** `ts_parser_parse_string_encoding()`/`BufferSnapshot::extract()`自体の文書全体依存コスト解消(`TSInput`コールバックAPI採用、次フェーズ候補)、余白サイズのチューニング、大きなジャンプ時の一時的無彩色表示の緩和、`extractOutline()`(Breadcrumb)の可視範囲スコープ化(Phase 7h以来の独立した同期・全文書解析のまま継続)。

### Phase 7u — `TSInput`コールバックAPI採用 (2026-07-31、実装完了後に全面revert)

Phase 7t完了後、ユーザーが次候補として選んだ`TSInput`コールバックAPI採用に着手した。Phase 7tの実測(narrow window/full documentのコストがほぼ同一)から「`ts_parser_parse_string_encoding()`が毎回文書全体のテキスト実体化を要求すること自体がボトルネック」と仮説を立て、tree-sitterのコールバックベースAPI(`TSInput`+`ts_parser_parse()`)へ切り替える実装(`neomifes::syntax::TextSource`/`TextChunk`、`neomifes::render::BufferSnapshotTextSource`)を行った。実装は正しく完成し、Debug/Release/ubsanの870テスト全てgreen、clang-tidy新規警告0を確認した。

**しかし一時的な診断計測で、当初の仮説が誤りだったと判明した:**

- `BufferSnapshotTextSource::read()`は50万行文書の増分再解析で**実際に1回・8192バイトしか呼ばれていない**(文書全体1億1500万バイト中)ことを確認 — 遅延読み込みメカニズム自体は設計通り完璧に動作していた
- にもかかわらず`ts_parser_parse()`単体のコストは約300〜325msで、`ts_tree_edit()`のコスト(0.02〜0.05ms)は無視できるほど小さかった
- Phase 7tが除外していた`BufferSnapshot::extract()`(文書全体実体化)のコストを別途計測すると**わずか19.07ms**であり、Phase 7tの実際のエンドツーエンドコスト(`extract()`+`ts_parser_parse_string_encoding()`)は公正には約175msだった
- **つまりPhase 7uの新方式(約300〜325ms)は、旧方式の公正な合計(約175ms)より約1.8倍遅い、明確な性能後退だった。** 真のボトルネックはテキスト実体化コストではなく、tree-sitterの`ts_parser_parse()`自身が保持木(`old_tree`)を使った再解析で内部的に払うコスト(木の再利用可否判定等、実際に読み直すバイト数とは無関係)にあると強く示唆される

この結果をユーザーに報告し、**Phase 7u実装の全面revertを承認された。** `incremental_parser.h`/`.cpp`・`syntax_worker.cpp`はPhase 7t完了時点のコード(文字列ベース`ts_parser_parse_string_encoding()`)に戻し、`BufferSnapshotTextSource`関連の新規ファイル3点は削除した。詳細な計測値・今後の検討候補(tree-sitter内部実装の読解、バージョンアップ追跡等)は[`docs/issues/tree_sitter_incremental_parse_cost.md`](../issues/tree_sitter_incremental_parse_cost.md)に記録した。roadmap §7.11のDoD「1文字入力後の増分解析: ≤50ms」は大規模文書(50万行)で引き続き未達のまま、次の対応方針は未定。

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

### 8.7 実装後の確定事項 (Phase 8a 完了、2026-08-01)

上記 §8.1〜§8.6 は Phase 8 の完全な v2.0 ビジョンを規定しているが、CLAUDE.md §7 の Phase 8 DoD 自体は「サンプル DLL 動作」の一点のみである。着手前にユーザーへスコープ縮小案を提示し、**「最小限 PoC」**(DLL 読み込み+`onLoad`/`onUnload`呼び出し+SEH クラッシュ隔離のみ)が選ばれた。詳細は [ADR-015](../decisions/ADR-015-plugin-host-c-abi-seh.md) 参照。

**スコープ縮小の理由:**
- `NeoMifesCoreApi`(§8.3 の`insertText`/`getLineText`等)は、`document::Document`に行番号→テキスト取得 API・行+桁→オフセット変換 API が存在せず、そのままでは実装できないと着手前調査で判明した(`docs/issues/plugin_core_api_document_gap.md`)。
- `permissions`ビットフィールド・AppContainer/Job Object サンドボックス・別プロセス IPC・`manifest.json5`+署名検証・マーケットプレースは、いずれも「DLL がロードできてコールバックが呼べる」という土台が無いまま設計すると推測実装になる(CLAUDE.mdルール3)。

**実装した内容(§8.3 の C ABI 境界からの変更点):**
- `NeoMifesPluginInfo`から`permissions`フィールドを削除(Phase 8b以降で追加)。`NeoMifesPluginVTable`は`onLoad`/`onUnload`のみ(`onDocumentChanged`は延期)。
- `NeoMifesPluginContext`を、§8.3 スケッチの不透明ハンドルから`void* userData`を持つ透過的な構造体に変更(Win32 の`GWLP_USERDATA`と同種のC ABIイディオム、テストが`onLoad`/`onUnload`の実行を実DLL経由で観測するために必要)。
- `NeoMifesCoreApi`自体を丸ごと延期(上記参照)。

**SEH クラッシュ隔離の実測結果:** `crashing_plugin`(null ポインタ書き込みによる`EXCEPTION_ACCESS_VIOLATION`)・`throwing_plugin`(`std::runtime_error`の throw、ホストは`/EHsc`ビルドだが間接関数ポインタ経由の呼び出しのため捕捉可能)の両方について、Debug/Release(MSVC)・ubsan(clang-cl)の全構成で「プラグインの異常がホストプロセスをクラッシュさせない」ことを実測で確認した(推測ではなく`tests/integration/plugin_load_test.cpp`で証明、CLAUDE.mdルール3)。この SEH トランポリンは**セキュリティ境界ではない**(同一プロセス内のため意図的な悪意あるプラグインからは保護できない、真の隔離は Phase 8b 以降)。

**次候補 (Phase 8b〜):** ~~`NeoMifesCoreApi`橋渡し設計、または AppContainer サンドボックス — どちらを先行するかは着手前にユーザーへ確認する。~~ → `NeoMifesCoreApi`橋渡し設計が選ばれ、Phase 8bで完了(§8.8参照)。

### 8.8 実装後の確定事項 (Phase 8b 完了、2026-08-02)

§8.7 が延期した`NeoMifesCoreApi`のうち、ドキュメント操作系4関数(`insertText`/`deleteRange`/`getLineCount`/`getLineText`)を実装した。詳細は [ADR-016](../decisions/ADR-016-plugin-core-api-bridge.md) 参照。

**実装した内容(§8.3 の C ABI 境界からの変更点):**
- `document::Document`に`lineText(LineNumber)`/`lineColumnToOffset(LineNumber, uint32_t)`の2メソッドを新設(`RenderPipeline::extractLineText()`とは意図的に実装を共有せず、性能文脈の違いを理由に分離)。
- `plugin_sdk.h`に`NeoMifesCoreApi`構造体(`insertText`/`deleteRange`/`getLineCount`/`getLineText`のみ、`registerCommand`/`showToast`/ネットワーク・ファイルシステム系関数は`permissions`モデルとUI側の受け皿が無いため引き続き延期)、独立した`NEOMIFES_CORE_API_VERSION`、`NeoMifesPluginContext`への`coreApi`/`document`フィールドを追加。
- `NeoMifesPluginVTable`のシグネチャは無変更(`coreApi`/`document`はcontextフィールド経由で渡す設計、既存4サンプルプラグインとのソース互換性を維持)。
- `neomifes::plugin::PluginHost`自体は`document::Document`型に一切依存させず(CLAUDE.md §3レイヤリング、Plugin EngineはDocument Engineより下位)、実際のブリッジ実装(`buildPluginCoreApi()`/`toNeoMifesDocument()`)は`src/app/`(document::/plugin_sdk::双方に依存できる既存の糊付け層、`document_open.h`/`outline_bridge.h`と同型)に配置。

**実測による検証:** 新規サンプルプラグイン`document_editing_plugin`(`onLoad`が`ctx->coreApi->insertText()`を実際に呼ぶ)を新規統合テスト`tests/integration/plugin_document_editing_test.cpp`でロードし、実DLL境界を越えた`NeoMifesCoreApi`往復が本物の`document::Document`を正しく変更することを実測で確認した(推測ではなく証明、CLAUDE.mdルール3)。Debug/Release/ubsan全構成・全931件green。

**既知のギャップ(意図的、次候補へ):** `NeoMifesCoreApi`は権限モデル(`permissions`)が無いため**セキュリティ境界ではなく**、ロード済みの任意プラグインが無制限にドキュメントを編集できる。プラグイン発の編集は`core::CommandDispatcher`/`UndoStack`を経由しないため`Ctrl+Z`で取り消せない。両方ともADR-016に明記の上、次候補として据え置いた。

**次候補 (Phase 8c〜):** ~~AppContainer サンドボックス / `permissions`権限モデル / `registerCommand`・`showToast`(UI側受け皿の設計) / 大規模文書の性能DoD再挑戦 / SQL文法対応 — 着手前にユーザーへ確認する。~~ → AppContainerサンドボックスが選ばれたが、着手前調査で既存の同一プロセス内アーキテクチャへ後付け不可能と判明(別プロセス+IPC全面再設計が必須、ADR-015が一度却下した規模)。Job Object資源制限のみへ縮小し、Phase 8cで完了(§8.9参照)。

### 8.9 実装後の確定事項 (Phase 8c 完了、2026-08-02)

§17.1「レベル2」(Job Objectでリソース制限)を実装した。詳細は[ADR-017](../decisions/ADR-017-plugin-job-object-sandbox.md)参照。

**実装した内容(§17.1のスケッチからの変更点):**
- 「メモリ・CPU 時間・ハンドル数の上限」のうち、実際に有効化したのは`JOB_OBJECT_LIMIT_ACTIVE_PROCESS`(`ActiveProcessLimit=1`)のみ。メモリ/CPU時間制限は、プラグインが現状ホストと同一プロセスで動作するため「プラグインだけ」を対象にできず、プロセス全体(ホスト本体含む)へ適用すると本プロジェクトの中核価値「10GBファイル対応」と衝突すると判明したため意図的に見送った。ハンドル数上限はそもそもWin32 Job Object APIに該当する`LimitFlags`ビットが存在しないと判明した(roadmapスケッチ自体が実装不可能な項目を含んでいた)。
- 新規`neomifes::plugin::ensureProcessSandboxed()`/`queryActiveJobLimits()`(`src/plugin/plugin_sandbox.h`/`.cpp`)。`PluginHost::load()`からは自動フックしない(既存の共有テストバイナリを汚染する副作用が判明したため、独立APIとして設計)。

**実測による検証:** `tests/integration/plugin_sandbox_test.cpp`で、サンドボックス化後に子プロセス生成(`CreateProcessW`)が失敗し、かつ呼び出し元プロセス自身は生存し続けることをローカル実機(Debug/Release/ubsan全構成)で確認した(推測ではなく実証、CLAUDE.mdルール3)。

**次候補 (Phase 8d〜):** ~~AppContainerサンドボックス(別プロセス+IPC全面再設計が前提、真に必要になった時点で再評価) / `permissions`権限モデル / `registerCommand`・`showToast`(UI側受け皿の設計) / 大規模文書の性能DoD再挑戦 / SQL文法対応 — 着手前にユーザーへ確認する。~~ → `permissions`権限モデルが選ばれ、Phase 8dで完了(§8.10参照)。

### 8.10 実装後の確定事項 (Phase 8d 完了、2026-08-02)

`permissions`自己申告ビットフィールド + NULL関数ポインタ・ゲートを実装した。詳細は[ADR-018](../decisions/ADR-018-plugin-permission-model.md)参照。

**実装した内容(§8.3のスケッチからの変更点):**
- §8.3スケッチの5カテゴリ(`Network`/`Filesystem`/`Subprocess`/`Registry`/`Clipboard`)はいずれも対応するCoreApi関数が未実装のため、実効性の無い予約ビットとしてそのまま残した。実際にゲートしたのは新規追加した`NEOMIFES_PLUGIN_PERMISSION_DOCUMENT`のみ(スケッチには無いカテゴリ、Phase 8bで実装済みの`insertText`/`deleteRange`/`getLineCount`/`getLineText`4関数に対応)。
- enforcementはスケッチが示した通り「権限が無ければ関数ポインタをNULLにする」方式を採用し、新規エラーコードは追加しなかった — NULL経由の呼び出しはPhase 8aの既存SEHトランポリンがそのまま捕捉し`OnLoadCrashed`として報告する。
- `manifest.json5`+Authenticode署名検証+確認ダイアログは全て見送った。プラグインの発見・インストールディレクトリ構造自体が本コードベースに存在せず、マニフェストファイルを置く場所が無いため。

**実測による検証:** `tests/integration/plugin_document_editing_test.cpp`の新規テストで、新設サンプル`permission_denied_plugin`(`NEOMIFES_PLUGIN_PERMISSION_NONE`を宣言しつつ`insertText`を無条件呼び出し)がNULL関数ポインタ経由でクラッシュし、`OnLoadCrashed`として報告され、かつ文書が一切変更されないことをローカル実機(Debug/Release/ubsan全934件green)で確認した。

**次候補 (Phase 8e〜):** ~~AppContainerサンドボックス(別プロセス+IPC全面再設計が前提、真に必要になった時点で再評価) / `registerCommand`・`showToast`(UI側受け皿の設計) / 大規模文書の性能DoD再挑戦 / SQL文法対応 — 着手前にユーザーへ確認する。~~ → `registerCommand`・`showToast`実装が選ばれ、着手前調査で`showToast`のみへ縮小された上でPhase 8eで完了(§8.11参照)。

### 8.11 実装後の確定事項 (Phase 8e 完了、2026-08-02)

`NeoMifesCoreApi::showToast`をヘッドレスな`ui::ToastState`状態層のみで実装した。詳細は[ADR-019](../decisions/ADR-019-plugin-show-toast-headless.md)参照。

**実装した内容(ユーザー選択時点のスコープからの変更点):**
- 着手前調査で`registerCommand`と`showToast`の実装難易度が本質的に非対称と判明した(`registerCommand`は「コールバックを保存し後で安全に呼び出す」新しい安全性契約が必要、`showToast`は既存の同期呼び出し契約に収まる)ため、AskUserQuestionで再提示し`showToast`のみへスコープを縮小した。`registerCommand`は次サブフェーズへ延期。
- 実Win32トーストウィジェット(ポップアップウィンドウ・自動消滅タイマー)は新設せず、`ui::ToastState`(ヘッダオンリー、「現在表示すべきメッセージ1件」のみ保持する純粋状態クラス)に留めた。本コードベースの既存UIウィジェット(FindBar/GrepBar/GotoLineBar/CommandPalette)はいずれも自動テスト対象になっておらず正しさの検証を実アプリ視覚確認のみに依存してきたため、`main.cpp`無改修のまま検証可能な形にする必要があったことが理由。
- `showToast`は権限ゲートしない(常に非NULL)と決定した。roadmap原案の5予約カテゴリのいずれも「トースト表示」に意味的に合致せず、低リスクな表示専用機能に新カテゴリを推測導入しない判断。

**実測による検証:** `tests/integration/plugin_toast_test.cpp`で、新規サンプル`toast_plugin`(`NEOMIFES_PLUGIN_PERMISSION_NONE`を宣言しつつ`showToast`を呼び出し)がNULL関数ポインタ経由のクラッシュを起こさず、`ui::ToastState`が実際に更新されることをローカル実機(Debug/Release/ubsan全942件green)で確認した(推測ではなく実証、CLAUDE.mdルール3)。

**次候補 (Phase 8f〜):** ~~AppContainerサンドボックス(別プロセス+IPC全面再設計が前提、真に必要になった時点で再評価) / `registerCommand`(実行時コマンド登録API+SEH保護された遅延呼び出し機構が前提) / 大規模文書の性能DoD再挑戦 / SQL文法対応 — 着手前にユーザーへ確認する。~~ → tree-sitter内部実装調査(不採用と結論)を経て`registerCommand`実装が選ばれ、Phase 8fで完了(§8.12参照)。

### 8.12 実装後の確定事項 (Phase 8f 完了、2026-08-03)

`NeoMifesCoreApi::registerCommand`をヘッドレスな`ui::PluginCommandRegistry`状態層のみで実装した。詳細は[ADR-020](../decisions/ADR-020-plugin-register-command.md)参照。

**実装した内容(§8.3のスケッチからの変更点):**
- スケッチの`registerCommand(ctx, id, callback)`に`title`引数を追加した(`CommandDescriptor::title`が表示に必須の非オプショナルフィールドであるため、スケッチが`CommandDescriptor`確定前に書かれたための逸脱)。
- SEH保護された遅延呼び出し機構は新規に書かず、Phase 8aの既存`invokePluginCallbackSafe`を無名namespaceから公開昇格して再利用した(コールバックシグネチャが`onLoad`/`onUnload`と完全に同じため)。
- `ui::CommandPalette`への実行時登録API・`main.cpp`配線・プラグインunload時の登録済みコマンド自動クリーンアップは全て次サブフェーズへ延期した。`PluginHost`が今も`main.cpp`へ配線されていないため(Phase 8a〜8eと同じ「ヘッドレスのみ」方針)、実際にパレットへ供給する仕組みは今回は作らなかった。

**実測による検証:** `tests/integration/plugin_command_test.cpp`で、新規サンプル`command_plugin`(`NEOMIFES_PLUGIN_PERMISSION_NONE`を宣言しつつコマンドを登録し、後から実行されたコールバックが`showToast`を呼ぶ)の遅延呼び出しが正しく`ctx->coreApi`まで到達すること、および新規サンプル`crashing_command_plugin`(登録したコマンドのコールバックが意図的にクラッシュ)の実行が`load()`/`unload()`の呼び出しスタック外でもSEHトランポリンで隔離されることをローカル実機(Debug/Release/ubsan全956件green)で確認した。**プラグインunload後にstaleなコマンドを呼び出す自動テストは、ubsanプリセット(AddressSanitizer)が実際のヒープuse-after-freeを正しく検出・報告したため削除した** — ASanが本来の役目を正しく果たした結果であり、`registerCommand`実装の不具合ではない(詳細はADR-020参照)。

**次候補 (Phase 8g〜):** ~~AppContainerサンドボックス(別プロセス+IPC全面再設計が前提、真に必要になった時点で再評価) / 大規模文書の性能DoD再挑戦 / SQL文法対応 — 着手前にユーザーへ確認する。~~ → SQL文法対応が選ばれ、Phase 7yで完了(§7実装後の確定事項(7y)参照。Phase 7トラック(syntax highlighting)の項目のためPhase 7yとして実施、8g自体は未完了のまま次候補にAppContainer/性能DoD再挑戦のみが残る)。

---

## 8.5. Phase 8.5 — アプリケーションシェル (v2.1 新設、**最優先**)

> **本章は 2026-08-04 中間レビュー ([`gap_analysis.md`](gap_analysis.md)) により新設された。**
> v2.0 のロードマップは Phase 1〜12 の全てを**技術レイヤ名**で命名しており (Document Engine / Rendering / Editor Core / …)、CLAUDE.md §3 のレイヤ図と 1:1 対応していた。しかし「**アプリケーションシェル**」— ファイルライフサイクル・ウィンドウ管理・入力方式 — はそのレイヤ図のどこにも存在せず、**結果としてフェーズが一度も割り当てられなかった**。本章はその是正である。

### 8.5.1 機能ビジョン

**「エンジン群」を「製品」にする。** 本フェーズ完了時点で、NeoMIFES は初めて次の一文が真になる:

> NeoMIFES.exe を単体で起動し、ファイルを開き、日本語を入力し、保存して終了できる。

### 8.5.2 Definition of Done

- [ ] `Ctrl+O` でファイルを開き、`Ctrl+S` で保存し、再度開くと編集内容が保持されている
- [ ] `Ctrl+Shift+S` で別名保存でき、その際に文字コード・改行コード・BOM を選択できる
- [ ] 未保存のまま閉じようとすると警告が出る
- [ ] 10 個のファイルをタブで開き、`Ctrl+Tab` で切り替えられる (各タブが独立した Undo 履歴を保持)
- [ ] 日本語 IME の未確定文字列がキャレット位置にインライン表示され、候補ウィンドウが追従する
- [ ] 画面幅を超える長い行の右端まで横スクロールで到達できる
- [ ] ステータスバーに 行:桁 / 文字コード / 改行コード / 選択文字数 が表示される
- [ ] `src/app/main.cpp` が **500 行以下** (現状 2,053 行)
- [ ] 既存の全性能 DoD (起動 ≤300ms / 60fps / 10GB) を維持している
- [ ] **ドッグフーディング: NeoMIFES 自身のソースを NeoMIFES で編集してコミットできる** (`gap_analysis.md` §8.1)

### 8.5.3 サブフェーズ 8.5a — 文書保存基盤

**最大の設計課題: mmap 中のファイルへの上書き。**

Phase 6d で `OriginalBuffer` は 10GB ファイルを `CreateFileW(GENERIC_READ)` + `MapViewOfFile` で読み取り専用マップしている。**自身がマップしているファイルへ直接書き戻すことはできない**。

**採用方針 (実装時に probe で検証すること):**

```
1. 同一ディレクトリに一時ファイル <name>.neomifes-tmp を作成
2. Piece Table を先頭から走査し、指定エンコード/改行コードへ変換しつつ一時ファイルへ書き出す
   (全文を u16string へ実体化しない — 10GB 対応の生命線。BufferSnapshot のピース単位で流す)
3. 一時ファイルを flush + close
4. OriginalBuffer のマップを解放 (元ファイルへのハンドルを完全に手放す)
5. ReplaceFileW(元, 一時, バックアップ) でアトミック置換 (ACL/タイムスタンプが保たれる)
6. 保存後の新ファイルを再度 mmap し、Piece Table を「単一の Original ピース」へ再構築
   (= 保存によって Add Buffer の断片が畳まれる。Undo 履歴との関係を要設計)
```

**未決事項 (実装時に判断):**
- **U#22:** 手順 6 の Piece Table 再構築を行うと、既存の `UndoStack` が保持する `TextRange` は依然有効か (オフセットは不変なので有効なはずだが、`BufferSnapshot` の寿命と `shared_ptr` の参照関係を実機で確認すること)
- **U#23:** 保存中にファイルが他プロセスにロックされていた場合の挙動 (`ReplaceFileW` 失敗時、一時ファイルを残すか消すか)

**影響ファイル:** `src/document/{document.h,document.cpp}` (`saveFile()` / `isDirty()` / `markSaved()`)、`src/document/src/original_buffer.cpp` (マップ解放 API)、`src/platform/src/file_mapping.cpp`、`src/encoding/` (書き出し方向の変換は Phase 6b1/6b2 の `convertFromUtf16Lenient` が既に存在)

#### 実装後の確定事項/変更点 (2026-08-04、Phase 8.5a完了)

**着手前probeにより、上記「採用方針」の手順4・6 (mmap解放 → 再mmap → Piece Table単一ピース再構築) は不要と判明し、実装から除外した。** `ReplaceFileW(target, replacement, backup)` は `target` が `FILE_SHARE_READ|WRITE|DELETE` でmmap開きっぱなしのままでも成功し、置換後の旧mmapビューは孤立したまま旧内容を返し続け、新規オープンは新内容を返す (実機probeで確認済み)。**`OriginalBuffer`のmmap構造は一切変更しない。** これによりU#22 (Undo履歴の`TextRange`整合性) はPiece Table再構築自体が発生しないため解消、U#26は「マップ解放は不要」で解消。

**U#23は「エラーコード分岐」ではなく「失敗後の実ファイル存在チェック (`fs::exists`)」で解決した。** probeで `ERROR_FILE_NOT_FOUND`(2) が「targetが存在しない (新規ファイル)」と「replacementが存在しない (呼び出し側バグ)」の両方で返り、エラーコード単体では区別できないと判明したため。`ReplaceFileW`失敗後は `fs::exists(path)` → `fs::exists(backupPath)` の順に実ファイル状態を見て、新規ファイルなら`MoveFileExW`フォールバック、backupのみ残っていれば復元、それも失敗すれば `SaveError::OriginalFileAtRisk` を返す。

**設計レビュー (Plan agent) で判明した2つの追加課題への対応:**
- **Save As/新規ファイルの成功に`ReplaceFileW`だけでは足りない** (replace専用、create-or-replaceではない) — 失敗かつtarget不在なら `MoveFileExW(temp, target, MOVEFILE_REPLACE_EXISTING)` へフォールバックする設計を追加。
- **行境界のみのチャンク分割は、CR-onlyファイルや改行を含まない巨大な1行で1チャンク=文書全体に退化し、境界メモリ制約が破れる** (`Document::lineCount()`が`'\n'`のみを数える既存挙動のため) — 行数上限 (`kLinesPerChunk=4096`) とコード単位上限 (`kMaxChunkCodeUnits=2^20`) のハイブリッドチャンク分割を採用。

詳細は [`detailed_design.md` §3.4](detailed_design.md#34-filesaver-wi-01実装2026-08-04) 参照。

### 8.5.4 サブフェーズ 8.5b — ファイルライフサイクル UI

- `Ctrl+S` / `Ctrl+Shift+S` / `Ctrl+O` / `Ctrl+N` / `Ctrl+W`
- `IFileOpenDialog` / `IFileSaveDialog` (COM。既存 ADR-008 の `ComPtr` 流儀を踏襲)
- `WM_DROPFILES` によるドラッグ&ドロップ (`DragAcceptFiles`)
- 未保存警告ダイアログ (`TaskDialogIndirect` — Windows 10/11 標準の外観)
- `WM_CLOSE` での確認 (`MainWindow` の既存 `onClose` フックを拡張)

#### 実装後の確定事項/変更点 (2026-08-04、WI-02完了)

**`onClose`フックは新規追加だった。** 上記記述は「既存の`onClose`フックを拡張」としていたが、着手前調査で`MainWindowConfig`には`onWindowCreated`/`onFirstPaint`/`onDeferredInit`/`onResize`/`onKeyDown`/`onSysKeyDown`/`onChar`/`onMouseWheel`/`onMouseDown`/`onMouseDrag`/`onCommand`/`onAppMessage`/`onNotify`の13種のみ存在し`onClose`は無かったと判明 (`build_plan.md` WI-02節が着手前に訂正済み)。新規`onClose`(戻り値`bool`、未設定時=true=閉じてよい、`onSysKeyDown`の「未設定=false」と逆極性)・`onDropFiles`の2フックを`main_window.h`/`.cpp`へ追加した。

**`document::LoadResult`に`lineEnding`フィールドを追加する設計へ簡略化した。** 当初想定していた「ロード時メタデータを運ぶ新しい共有関数」は不要と判明し、`hadBom`/`detectedEncoding`と同じ形で`LoadResult`に統合、`openDocumentAt()`の戻り値も`std::variant<LoadedFileMeta, LoadError>`へ変更した (`LoadedFileMeta{hadBom, encoding, lineEnding}`)。全5箇所の「ファイルを開く」呼び出し元 (起動時・F12・Grep結果クリック・Ctrl+O・D&D) が同一ロジックを共有するため、複数箇所での実装乖離が構造的に起きない。

**設計レビューで実装前に検出・修正した3件の実害あるバグ(詳細は`build_plan.md` WI-02節参照):** (1) `CoInitializeEx`未呼び出し (`IFileOpenDialog`/`IFileSaveDialog`が`CO_E_NOTINITIALIZED`で即失敗する)、(2) 改行コード検出の境界プレフィックス走査が偶然CRLFペアを分断すると一貫したCRLFファイルを`Mixed`と誤判定し無言でLFへ書き換わりうるバグ、(3) Ctrl+Nを素朴実装すると直前ファイルの削除済み内容がUndo経由で新規文書へ混入するデータ破損経路。いずれも実装前のPlan agent設計レビューで検出し、コード自体には一度も現れていない。

**既知の未対応事項:** オーバーレイ (FindBar/GrepBar/CommandPalette/GotoLineBar/OutlinePane) がフォーカスを持っている間はCtrl+S/O/Nが届かない。`docs/issues/overlay_focus_blocks_file_lifecycle_keys.md`に起票。

**🎉 M1 (ドッグフーディング開始) を達成した (2026-08-05)。** 実装・自動テスト・ローカルビルド検証完了後、ユーザーが実際にドッグフーディングを試みた結果、自動テストと軽量な生存確認だけでは検出できなかった実害あるバグを2件発見した — (1) Ctrl+O後にウィンドウ移動等の無関係な再描画まで新しい文書内容が表示されない (`RenderPipeline::render()`の粗粒度フレームスキップが文書スワップを偶然の`documentVersion`一致で「変化なし」と誤判定)、(2) マウスホイールでEOFを超えてスクロールし続けると`Viewport`内部のtopLineが無制限に増え続ける (`applyMouseWheelScroll()`が上限クランプを欠いていた)。両方とも根本原因を特定・修正し、回帰テストで実証した。その後ユーザーが実際に`README.md`をNeoMIFESで開いて編集・`Ctrl+S`保存・`git diff`確認・`git commit`まで完走し、M1のDoDを正式に満たした。詳細は`docs/handoff/RESUME_HERE.md` §3.69参照。

### 8.5.5 サブフェーズ 8.5c — `main.cpp` 解体 + 複数文書モデル (**8.5d より必ず先**)

**現状の問題:** `src/app/main.cpp` は 2,053 行。`Document` / `SelectionModel` / `CommandDispatcher` / `Viewport` / `FoldingModel` / `BookmarkManager` / `FindReplaceState` / `RenderPipeline` / 全ウィジェット / 全キーバインド / 全モード遷移を単一の `wWinMain` スコープのローカル変数群として保持している。

**新設する型:**

```cpp
// src/app/include/neomifes/app/editor_session.h
// 「1 つの開いている文書」に紐づく全状態。タブ 1 枚 = EditorSession 1 個。
class EditorSession {
    document::Document        m_document;
    core::SelectionModel      m_selection;
    core::CommandDispatcher   m_dispatcher;   // Undo 履歴を含む
    core::Viewport            m_viewport;
    core::FoldingModel        m_folding;
    core::BookmarkManager     m_bookmarks;
    std::optional<syntax::Language> m_language;
    std::filesystem::path     m_path;
    bool                      m_isUntitled;
    // ...
};

// src/app/include/neomifes/app/workspace.h
// EditorSession の集合 + アクティブタブ。1 ウィンドウ = 1 Workspace。
class Workspace {
    std::vector<std::unique_ptr<EditorSession>> m_sessions;
    std::size_t m_activeIndex = 0;
    // openFile() / closeSession() / activate() / hasUnsavedChanges()
};
```

**`main.cpp` に残すもの:** `wWinMain`、ウィンドウ生成、メッセージループ、`Workspace` と `RenderPipeline` の所有のみ。キーバインド処理は `src/app/editor_input.cpp` と新設のコマンドテーブルへ移す。

**CLAUDE.md 絶対ルール 4 の遵守:** 本サブフェーズは新機能を 1 つも足さない**純粋なリファクタリング**である。既存の全テストが無変更で green を保つことが唯一の完了条件。

**実装後の確定事項 (2026-08-07、WI-04 完了、コミット `c58245e`/`8237ec4`/`2c549d0`/`3480b5f`):** 上記スケッチ通り `EditorSession`/`Workspace` を新設し、`main.cpp` を **2,439 行 → 361 行** まで縮小した (着手時点の実測は本節記載の 2,053 行ではなく 2,439 行だった — WI-03 完了時点までに増えていたぶんを本 WI 冒頭で実測・訂正)。**当初の「安全な進め方」3 段階(EditorSession 新設 → Workspace 新設 → キーバインド群を editor_input.cpp へ移設)だけでは約 650 行までしか縮まらないと実装途中で判明した** — `wireNormalMode()` とその依存関数群(約 46 関数・約 1,780 行)は `RenderPipeline`/`HWND`/`ui::` ウィジェットに依存しており、Win32 非依存を維持する `editor_input.cpp` には移せないため。これらを新規 `src/app/normal_mode_wiring.h`/`.cpp` へ切り出すステップ3bを追加し、さらに `wWinMain` 本体より前に走るプロセス起動前処理(コマンドライン解析・多重起動チェック・DPI/共通コントロール初期化・起動時 Document 構築)を `src/app/launch_setup.h`/`.cpp` へ分離して初めて 500 行の DoD を満たせた。いずれも本節の既定方針「main.cpp に残すのは wWinMain/ウィンドウ生成/メッセージループ/Workspace と RenderPipeline の所有のみ」を字義通り満たすための精緻化。**ファイル配置も訂正:** `src/app/src/workspace.cpp` ではなく実際の慣習通り `src/app/workspace.cpp`(平坦なディレクトリ構成)とした。詳細な設計判断(状態の振り分け根拠・`CommandDispatcher` のポインタ安定性制約による move/コピー禁止・`EditorSession::language()` を意図的にキャッシュしない理由)は `build_plan.md` WI-04 の「実装後の確定事項」を参照。

### 8.5.6 サブフェーズ 8.5d — タブ UI

- `ui::TabBar` (Win32 `WC_TABCONTROL` を使うか自前 D2D 描画かは実装時判断。既存ウィジェットは `WC_EDIT`/`WC_LISTBOX`/`WC_TREEVIEW` の標準コントロール路線)
- `Ctrl+Tab` / `Ctrl+Shift+Tab` / `Ctrl+W` / `Ctrl+PgUp` / `Ctrl+PgDn` / `Ctrl+1`〜`Ctrl+9`
- タブに未保存マーカー (●) を表示
- **設計判断が必要な点:** `render::SyntaxWorker` はタブごとに持つか 1 個を共有するか。共有する場合、タブ切替時に `resetIncrementalState=true` で保持木を捨てる必要がある (Phase 8d で確立済みの経路がそのまま使える)

**実装後の確定事項 (2026-08-08、WI-05 完了、コミット `4f9bced`/`fe037d7`/`62edf0c`/`57acef8`):** `WC_TABCONTROL` を採用。`SyntaxWorker` は共有のまま (`syncViewForActiveSession()` の `setLanguage()` 呼び出しが保持木破棄を毎回強制するため正しく動作する、体感が悪化すれば分離を再検討)。`Workspace::openFile()` の戻り値を `document_open.h::openDocumentAt()` と同じ `std::variant<size_t, LoadError>` 規約へ拡張。`Ctrl+PgUp`/`Ctrl+PgDn` は既存の `applyMovementKey()` が `ctrlDown` を見ていなかった間隙を突きタブ切替へ意図的に再割り当てした。ステップ2のドッグフーディングで `initCommonControls()` に `ICC_TAB_CLASSES` が欠落し `WC_TABCONTROLW` が未登録のままだった実害あるバグを発見・修正。**同じドッグフーディングで、`TabBar` だけでなく `FindBar`/`GrepBar`/`CommandPalette`/`GotoLineBar`/`OutlinePane` を含む全ネイティブ Win32 オーバーレイウィジェットが画面上に一切描画されない、WI-05 固有ではない全社的な不具合を発見した (`docs/issues/native_overlay_widgets_invisible.md`、🔴 未解決)。** 5 つの仮説 (DXGI flip-model/DWM合成無効化/RDPセッション/低コントラスト/`WM_PAINT`枯渇) を検証し全て否定したが根本原因は未特定のまま、ユーザーの指示で本格調査は将来セッションへ引き継いだ。この既知の制約下で、WI-05 の DoD 検証は Win32 API 構造確認 (`TCM_GETITEMCOUNT`) と単体テスト (`app_workspace_test.cpp`/`app_tab_index_math_test.cpp`/`ui_tab_bar_test.cpp`) で代替した。詳細な設計判断は `build_plan.md` WI-05 の「実装後の確定事項」を参照。

### 8.5.7 サブフェーズ 8.5e — IME 完全対応 (roadmap §16.1 の実フェーズ化)

**現状:** `src/ui/src/main_window.cpp` が処理する 15 種の `WM_*` に `WM_IME_*` は 1 つも含まれない。Find bar 等の `WC_EDIT` 子コントロールだけが Win32 から IME を無償で得ている。

**実装内容:**
- `WM_IME_STARTCOMPOSITION` — 未確定文字列の描画開始。既定の IME ウィンドウを抑止
- `WM_IME_COMPOSITION` — `GCS_COMPSTR` で未確定文字列、`GCS_RESULTSTR` で確定文字列を取得
- `WM_IME_ENDCOMPOSITION` — 未確定表示のクリア
- `ImmSetCandidateWindow` (`CFS_CANDIDATEPOS`) — 候補ウィンドウをキャレット位置へ追従
- `render::RenderPipeline` に未確定文字列のインライン描画 (下線 + 変換対象節のハイライト) を追加
- **`imm32.lib` のリンク追加が必要** (`src/ui/CMakeLists.txt`)

**検証:** この自動化環境では修飾キー合成入力が不調 (`reference_no_win32_gui_automation.md`) だが、**IME だけは実機での手動確認を必須とする**。日本語が打てないことは製品として単独で出荷を阻む欠陥であり、自動テストによる代替を認めない。

**実装後の確定事項 (2026-08-12、WI-06 完了、コミット `0baccaa`/`94e2259`/`f233f02`):** 上記の設計方針通り実装した。未確定文字列は`drawBreadcrumb()`等と同型の「毎フレーム使い捨て`IDWriteTextLayout`によるオーバーレイ描画」を採用し、真の行内リフローは不採用(5箇所の列計算への影響を避けるため)。`HIMC`のRAIIには既存`HandleGuard`が対応できない「解放にhwnd・himc両方を要するペア」用に新規`platform::ImeContext`を新設。複数カーソルは`WM_IME_STARTCOMPOSITION`で`collapseToPrimary()`を呼び、確定後の復元は`ReplaceRangeCommand::cursorsAfterExecute()`の既存契約により追加コード無しで自然に成立するため行わない。確定文字列の1 Undoステップ化は`WM_IME_COMPOSITION`を`DefWindowProcW`へ一切フォワードしないことから機械的に導かれる(フォワードするとWindows既定処理が`GCS_RESULTSTR`から1コード単位ごとの`WM_CHAR`を生成し、`tryMerge()`未実装(ADR-012)のため複数のUndoステップに分裂してしまう)。実機MS-IME確認をユーザーが実施し「問題無いように見える」との報告を受けた(スクリーンショットは未取得、口頭確認で代替)。詳細は`build_plan.md` WI-06および`docs/history/TIMELINE.md` Session 84参照。

### 8.5.8 サブフェーズ 8.5f — ウィンドウクローム

- **メニューバー** (`CreateMenu` / `AppendMenuW`): ファイル / 編集 / 検索 / 表示 / ツール / ヘルプ
- **アクセラレータテーブル** (`CreateAcceleratorTable` / `TranslateAcceleratorW`): 現在 `editor_input.cpp` と `main.cpp` に散在する `if (ctrlDown && vkCode == 'X')` の連鎖を `HACCEL` + コマンド ID へ集約。**これが 8.6b (キーバインド設定) の前提**
- **ステータスバー** (`STATUSCLASSNAME`): 行:桁 / 選択文字数 / 文字コード / 改行コード / INS-OVR / 言語
- **行番号ガター**: 4b8c で新設したブックマーク専用ガター (24dip) を拡張し行番号を描画
- **ウィンドウタイトル**: `<ファイル名> [*] - NeoMIFES`
- **コンテキストメニュー** (`WM_CONTEXTMENU` + `TrackPopupMenu`)
- **リソース**: `neomifes.rc` / `neomifes.ico` / `neomifes.manifest` (DPI awareness / Common Controls v6 / `requestedExecutionLevel`)

**実装後の確定事項 (2026-08-13、WI-07 完了、コミット `c0f296b`〜`68a53ee` の計11件):** 上記の設計方針通りに実装し、🎉 M2 (アプリケーションとして成立) を達成した。着手前に発見したP0 issue([`native_overlay_widgets_invisible.md`](../issues/native_overlay_widgets_invisible.md))の根本原因調査をステップ0として先行実施し、`MainWindow::create()`の`windowStyle`に`WS_CLIPCHILDREN`が欠落していたことが原因と判明・1行修正で解消した(issueは解決済みへ移動済み)。`ui::CommandId` + `dispatchCommand()`という単一チョークポイントを新設しHACCEL/メニューバー両方から共有する設計にしたが、**Find/Grep/CommandPalette/Outline/GotoLineの各トグルキーは意図的にHACCEL化しなかった**(グローバルアクセラレータへ昇格させるとオーバーレイウィジェットのフォーカス中`WC_EDIT`より先にキーを奪う競合が判明したため、`command_dispatch.h`冒頭コメントに理由を明記)。**リソースは`.rc`/`.ico`のみで`.manifest`は新設しなかった** — `.rc`が`RT_MANIFEST`リソースを一切定義しない設計にすることで、`main.cpp`既存のリンカプラグマ製マニフェスト(Common Controls v6依存)との衝突を回避したため(上表の「リソース」記述は歴史的スケッチとして残すが、実装はこの通り簡略化された)。行番号ガターは固定幅ではなく桁数に応じた動的幅で実装(当初想定を上回る形)。INS/OVRは表示だけでなく実編集動作まで実装、既存`MultiCursorEditCommand`の再利用によりUndo/Redoが追加コード無しで自動対応した。詳細は`build_plan.md` WI-07の「実装後の確定事項」参照。

### 8.5.9 サブフェーズ 8.5g — 横スクロール

**現状:** `WM_HSCROLL` / `leftColumn` 相当が皆無。`drawTextLine()` は 65536 DIP の巨大レイアウトボックスに `NO_WRAP` で描画し、実クリップをレンダーターゲット境界任せにしている。**画面幅を超える行の右端には到達できない。**

**影響範囲が広い:** `RenderPipeline` の X 座標計算 (キャレット / 選択 / マッチハイライト / Indent guides / ガター) 全てに `-leftColumnDips` のオフセットが波及する。**早期着手が望ましい** (後になるほど波及先が増える)。折返し表示 (word wrap) は本サブフェーズのスコープ外とし、Phase 12 以降で判断する。

**実装後の確定事項 (2026-08-05、WI-03 完了、コミット `6052da8`):** 上記の設計方針通りに実装した。X座標オフセットが波及した箇所は`drawCaretOnLine`/`drawSelectionOnLine`/`drawMatchOnLine`/`drawIndentGuidesOnLine`/`hitTest()`/`drawTextLine()`のテキスト描画起点/`drawFoldedHeaderMarker`呼び出しの計7箇所(新設`RenderPipeline::leftColumnOffsetDips()`ヘルパーに集約)。**着手前調査で設計方針にはなかった追加要件が1件判明した:** `drawGutterOnLine()`(ブックマーク/フォールドマーカー)は背景を塗りつぶさないため、横スクロールしたグリフがガター領域へはみ出す構造的な穴があり、`drawTextLine()`のテキスト由来描画のみを`PushAxisAlignedClip`/`PopAxisAlignedClip`で保護した。`FrameState`に`leftColumn`を追加し粗粒度フレームスキップの再発(直前セッションの`m_documentGeneration`バグと同型)を予防。本コードベース初のネイティブスクロールバー(`WS_HSCROLL`/`WM_HSCROLL`)を`MainWindow`に追加。垂直方向の`Viewport::setVisibleLineCount()`が実運用で一度も呼ばれていない既存の潜在バグを発見したが、WI-03のスコープ外として本セッションでは未修正(詳細は`build_plan.md` WI-03の「実装後の確定事項」参照)。

---

## 8.6. Phase 8.6 — 製品化基盤 (v2.1 新設)

### 8.6.1 サブフェーズ 8.6a — 設定システム (P1 最優先)

**負債の実態:** 「設定システムが存在しないため」という理由で機能を縮退させた設計判断が、**設計文書に 13 箇所記録されている** (`gap_analysis.md` §4.1)。`kTabWidth=4` は `main.cpp` と `render_pipeline.cpp` に**二重定義**され、「手動同期が必要な既知のトレードオフ」として受容されている。

**実装:**
- `core::Settings` — `%APPDATA%\NeoMIFES\settings.json` (ADR-013 の nlohmann/json を再利用。JSON5 は U#7 で第一候補とされていたが、`SearchHistory` が既に素の JSON を採用した前例に倣い JSON とする)
- 初期スコープ: フォントファミリ / フォントサイズ / タブ幅 / タブをスペースで挿入 / 行番号表示 / ミニマップ表示 / 折返し / 自動保存間隔 / テーマ
- **既存ハードコード定数の移行を完了条件に含める** — 移行せずに設定システムだけ作ると負債が残る

**✅ 実装完了 (WI-08、2026-08-13)。実装後の確定事項:**

- `insertSpacesForTab`(タブをスペースで挿入)・`autoSaveIntervalSeconds`・`themeName`はスキーマとして永続化のみ行い、消費者側の実装(キー入力パス配線・WI-11・WI-09)は各専用WIへ据え置いた。折返しは要件定義書自体が未確定のスコープ外項目。
- `kTabWidth`の二重定義解消にあたり、`IDWriteTextFormat::SetIncrementalTabStop()`が本コードベースで一度も呼ばれておらず、既存の2つの`kTabWidth`コピーのいずれもリテラル`'\t'`文字の実描画幅を制御していなかったという未発見のギャップを特定・解消した(詳細は`build_plan.md` WI-08節)。
- フォント・タブ幅の変更には`TextLayoutCache::getOrCreate()`の明示的な`clear()`が必須(同キャッシュは`document::LineNumber`のみをキーとし、渡された`textFormat`/幅/高さを再検証しない契約のため)。
- 設定変更手段は専用ダイアログではなく、`settings.json`の手動編集+コマンドパレット限定コマンド`settings.reload`とした。汎用設定ダイアログはWI-08のスコープ外。

### 8.6.2 サブフェーズ 8.6b — キーバインド設定

8.5f で `HACCEL` へ集約したキーバインドを設定ファイル化し、roadmap §13.1 のプリセット (NeoMIFES 標準 / 秀丸 / サクラ / VSCode) を実現する。

**✅ 実装完了 (WI-10、2026-08-15)。実装後の確定事項:**

- **プリセットは `neomifes`/`hidemaru`/`sakura`/`vscode` の4種のみ。** §13.1 が列挙する「MIFES 互換」プリセットは対象外とした — build_plan.md の WI-10 節自体が最初から「NeoMIFES 標準 / 秀丸 / サクラ / VSCode の 4 種を同梱」とし、build_plan.md が Plan-of-Record として §13.1 に優先する (CLAUDE.md 絶対ルール3、矛盾時はユーザーに確認する原則に基づき、build_plan.md 側の記述を実装対象として確定させた)。Vim/Emacs モードは §13.1 記載通り Phase 8 プラグイン提供のまま、WI-10 のスコープ外。
- **スコープは「広範囲」— `ui::CommandId` 34個全てが対象。** 既存 `HACCEL` 16個に加え、`normal_mode_wiring.cpp` にハードコードされていた残り18個(Find*/Grep/CommandPalette/Outline/GotoLine/Bookmark*/TagJump/Copy/Cut/Paste/Undo/Redo/ToggleOverwriteMode)も含めた。詳細・根拠は `build_plan.md` WI-10 節参照。
- **競合解決は `command_ids.h` の enum 宣言順で後勝ち、決定的。** 通知はDebugビルド限定の `OutputDebugStringW` ログのみ(トースト/ダイアログ基盤が本コードベースに無いため)。
- **`core::KeyBindings` は `ui::CommandId`/Win32 `VK_*` に非依存。** WI-09 の `theme_settings.h` と同じ「下位層は文字列、上位層で enum へブリッジ」パターンを踏襲(`ui::command_id_name.h`/`app::key_chord.h`)。
- **メニューバー表示の実行時更新はスコープ外。** `docs/issues/menu_bar_keybinding_label_stale.md` に起票済み。実際のキー入力自体はメニュー表示に関わらず正しく機能する。

### 8.6.3 サブフェーズ 8.6c — テーマ

`render_pipeline.cpp` にハードコードされている `D2D1_COLOR_F` 定数群 (背景 / テキスト / キャレット / 選択 / Keyword / Type / String / Number / Comment / Preprocessor / ミニマップ 3 種 / Breadcrumb / Indent guide / フォールドマーカー) を `render::Theme` 構造体経由へ移す。ダーク / ライト / ハイコントラストの 3 種を同梱 (要件定義書 §14 必須)。

**✅ 実装完了 (WI-09、2026-08-14)。実装後の確定事項:**

- 新規`render::ThemeKind`(enum、Dark/Light/HighContrast)+`render::Theme`(23フィールドの`D2D1_COLOR_F`構造体)+`themeForKind()`を新規`src/render/include/neomifes/render/theme.h`/`src/render/src/theme.cpp`へ実装。`render::RenderPipeline`(L4)は`core::Settings`(L5)に一切依存しない(CLAUDE.md §3の層分離)。文字列↔enum変換は新規`src/app/include/neomifes/app/theme_settings.h`(ヘッダオンリー)がアプリ層で担う — `syntax_language.h`の`detectLanguage()`と同じ役割。
- **キャレット専用のブラシ/色フィールドは無い** — `drawCaretOnLine()`は`m_textBrush`を再利用するため`Theme::text`が自動的にカバーする。roadmapスケッチが列挙する「キャレット」は実装時に不要と判明した。
- **`FrameState`修正が正しさに必須:** 粗粒度フレームスキップ(Phase 3c/ADR-011)は`setTheme()`単体呼び出し(他状態が無変化)の場合、`ThemeKind`を`FrameState`に含めないと実際の再描画がスキップされ画面が古い色のまま固まる。`m_leftColumn`/`m_imeComposition`と同じバグクラスとして`FrameState::themeKind`を追加し解消した。
- `recreateDevice()`の21ブラシ`.Reset()`ブロックを新規`resetThemeBrushes()`へ抽出し、`setTheme()`と共有(デバイス自体の再構築は`setTheme()`では行わない)。
- テーマ切替はコマンドパレット限定の3コマンド(`view.theme.dark`/`view.theme.light`/`view.theme.highContrast`)、メニューバー統合はスコープ外(`kViewMenuItems`のサブメニュー機構が無い)。
- OSハイコントラスト自動検出(`SPI_GETHIGHCONTRAST`)はスコープ外(build_plan.md原文で任意、要件定義書§14に記載無し)。
- 実機ドッグフーディングで3テーマの正しい配色・コマンドパレット経由のライブ切替(再起動不要)・再起動後の永続化を確認済み。詳細は`build_plan.md` WI-09節参照。

### 8.6.4 サブフェーズ 8.6d — 自動保存・バックアップ・クラッシュ復旧・最近開いたファイル

要件定義書 §6・§15 の必須項目。v2.0 は誤って Phase 12 (品質保証フェーズ) に配置していた。

- 自動保存: N 秒ごと / フォーカス喪失時に `%APPDATA%\NeoMIFES\autosave\` へ
- クラッシュ復旧: 起動時に autosave を検出したら復旧を提案
- 最近開いたファイル: メニュー + Jump List (roadmap §21.7)

> ✅ **実装後の確定事項 (WI-11 完了、2026-08-15、コミット `bf03ff0`):** 上記スケッチ通りに実装完了。`util::fnv1aHash64()` によるファイルパス→autosaveファイル名の決定的ハッシュ、`core::AutosaveIndex`(hash→元パス逆引き)、`core::RecentFiles`(MRU 20件)を新設し、既存3クラス(`Settings`/`SearchHistory`/`KeyBindings`)と同じ `loadFrom`/`saveTo` JSON パターンへ統一した。Jump List (`ICustomDestinationList`) は roadmap 原文が明示的に任意としている通り本WIではスコープ外(未実装)のまま。`document::saveFile()` に `keepBackup`/`markAsSaved` を追加し、自動保存が `doc.markSaved()` を誤って呼ばない(=タブの未保存マーカーを誤って消さない)ことを保証した。クラッシュ復旧は `Workspace::adoptSession()` で「通常起動 + 復旧セッションを追加タブとして復元」する方式を採用し、「復旧対象を初期タブとして使う」特別扱いはしなかった。

### 8.6.5 サブフェーズ 8.6e — 基本編集の穴埋め

`Ctrl+A` (全選択)、自動インデント (前行のインデントを継承)、行複製 (`Ctrl+D`) / 行移動 (`Alt+↑/↓`) / 行削除 (`Ctrl+Shift+K`)。

> ✅ **実装後の確定事項 (WI-12 完了、2026-08-15、🎉 M3):** 上記5機能を全て実装した。行複製/行移動/行削除は既存の2つのカーソル復元ポリシー(`MultiCursorEditCommand`/`ReplaceAllCommand`)のどちらにも合わなかったため、新規第3のポリシー `core::LineOperationCommand`(呼び出し側が `CursorEditMapping{editIndex, offsetIntoInsertedText}` を明示指定)を新設し、適用/Undo自体は既存の `cumulative_shift_edit.h` を共有した。複数行削除で「行末尾の `\n` を削るか」の判定を行ごとではなくラン(連続する行のまとまり)単位に統一する修正が必要だった(バックグラウンド検証エージェントが単体テストで発見)。自動インデントは `core::Settings` を一切参照せず「前行の実テキストをそのまま文字列コピーする」方式を採用し、タブ/スペース設定に自動的に追従する。5コマンドは意図的に `core::KeyBindings`(WI-10プリセットシステム)の対象外(既存の継続編集キーと同じハードコード扱い)とした — 秀丸/サクラ/VSCodeの複製・行移動・行削除キーは製品ごとの差が大きく、未確認の外部調査という新規スコープを避けるため。詳細は `build_plan.md` WI-12節参照。

---

## 9. Phase 9 — AI プラグイン (Claude 統合 + Copilot 型補完 + RAG + マルチモデル + ローカル LLM)

> 🧊 **凍結 (2026-08-23、build_plan.md §0「現在のゴール」参照)。** MVP(WI-13、2026-08-16)達成後の差別化機能追加が終わりの定義なく続いていたため、ユーザーとの合意でスコープを確定し本フェーズは着手しないことにした。本節以下は将来の再評価に備えた設計メモとして凍結保存する(削除しない)。
>
> ⚠️ **v2.1 でフェーズ順序を最後尾へ移動した(凍結前の経緯、参考として残す)。** 理由: CLAUDE.md が「AI 機能は完全プラグイン化。エディタ本体は AI 無しでも 100% 動作しなければならない」と定めているが、本体が 100% 動作していない段階で AI を積むのはこの原則と矛盾する。加えて AI 機能は外部 API 依存で陳腐化が速く、本体完成後に実装した方が製品価値が高い。**着手は Phase 8.5 / 8.6 / 12' / 10 / 11 の完了後。**

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

> ⚠️ **v2.1 でフェーズ順序を前倒しした (Phase 9 AI より先)。** 理由: 本章は §1.5 で「本ソフト最大の差別化点」と位置づけられており、かつ AI と異なり **外部サービスに依存せず自己完結し、陳腐化しない**。Phase 12' (MVP 出荷) 直後の最初の目玉機能として最適である。**着手は Phase 8.5 / 8.6 / 12' の完了後。**

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

#### 実装後の確定事項 (WI-14a、ヘッドレス基盤、2026-08-16)

`build_plan.md`の「1セッションに収まらない章はWIを切り直す」方針に従い、本節をWI-14a〜dへ4分割した(詳細はbuild_plan.md §5)。WI-14aで確定した、本節スケッチとの主な差分:

- **組込パターンはv2.0拡張の16種ではなく、公開・検証可能な標準4種 (RFC 5424/3164 syslog、Apache/Nginx Common+Combined Log Format、汎用ISO-8601+レベル行) のみをMVPスコープとした。** SAP/AWS CloudTrail/Azure Monitor/HANA/Tomcat/Docker/Kubernetes/OpenTelemetry/X-Ray/Loki/Fluentd等のベンダー固有パターンは、実データが手元に無い状態で書くとCLAUDE.mdルール3(推測実装をしない)に反するため、実データ入手まで先送りした(`docs/issues/phase_10_1_v2_extended_patterns.md`)。
- **`LogModel::attach(Document&, rule)` (mutate-in-place) ではなく `LogModel::build(const Document&, rule) -> std::expected<LogModel, LogPatternError>` (static、値返却) を採用した。** `Document*`を保持する設計は文書スワップ時の寿命管理問題を持ち込むため、`search::SearchService::findAll()`と同じ「呼び出しごとに完結」パターンを踏襲した。
- **`LogLine`はメッセージ本文/traceId/spanIdの文字列を持たない、`document::LineNumber`+`optional<Timestamp>`+`LogLevel`+`matched`のみの軽量構造体にした。** 数百万行規模のログで1行あたり複数の`std::u16string`コピーを保持するとWI-14bの10GB/60秒目標に対して構造的に不利になるため。
- **タイムスタンプ書式の自動推定(「先頭100行で最頻フォーマットを推定」)は行わず、各`LogPatternRule`が固定の`timestampFormat`を1つ持つ設計にした。** どのルールがマッチしたかで書式は既に決まっており、複数フォーマット混在を想定した自動推定は本段階では過剰(YAGNI)。
- **リアルタイムテール・分散トレースID対応・Structured Log (JSON/Logfmt)・統計ダッシュボードはWI-14a〜dのいずれにも含めず、`docs/issues/phase_10_1_v2_extended_patterns.md`にv2.0拡張候補としてまとめて起票した。**
- 非同期化(`LogIndexWorker`)・`EditorSession`統合・UIはWI-14b/cへ。

#### 実装後の確定事項 (WI-14b、非同期インデックス構築+ピース単位ストリーミング、2026-08-17)

- **`LogModel::build()`を`document::BufferSnapshot::pieces()`を1回だけ走査するピース単位ストリーミング実装へ書き換えた。** 旧実装(`Document::lineText()`を毎行呼ぶ)はO(行数×ピース数)のコストを持ち、10GB/60秒目標に対して構造的に不利だった。実測(Release): 50,000行=164ms、500,000行(10倍)=1550ms、items/sがほぼ一定(約302k〜325k/s)であり、複雑度クラスがO(document length)へ改善したことを確認した。実際に10GBファイルを生成する検証はWI-13の`tools/`スクリプト前例を踏襲せず、複雑度クラスの証明に留めた。
- **`LogIndexWorker`は`render::SyntaxWorker`(Phase 7c)を型としては踏襲したが、「保留中リクエストは最新の1件のみ・上書き」という設計は意図的に不採用とした。** 複数タブが同時にインデックス要求した場合、SyntaxWorker型だと一部のタブが永久に処理されない実害あるバグになるため、`std::deque`ベースのFIFOキュー(全リクエストを提出順に処理)を採用した。
- **完了メッセージのタブへのルーティングは`Workspace`への新規API追加なしで実現した。** `EditorSession`自身のポインタを不透明な`sessionToken`として往復させ、受信側が`&workspace.sessionAt(i)`とのポインタ値比較のみ(dereferenceしない)で対象タブを特定する。閉じられたタブへの結果は安全に破棄される。
- **WI-14bでは`beginLogIndexing()`/`applyLogIndexResult()`を実際に呼び出すUI/コマンドは一切配線しなかった(WI-14cへ)。** ただし完了メッセージの受信インフラ(`LogIndexWorker`の構築+`kMsgLogIndexReady`ハンドラ+`Workspace`線形走査ルーティング)はWI-14bで実装・統合テストで検証済み。
- 詳細は`build_plan.md` WI-14bセクション、`detailed_design.md` §11.3参照。

#### 実装後の確定事項 (WI-14c、UI モード MVP 🎉、2026-08-17、Phase 10.1 完結)

要件定義書§8の残り全項目(色分け/フィルタ/ERROR抽出/WARNING抽出/時系列ジャンプ)を実装し、Phase 10.1のMVPを達成した。

- **本節冒頭のUIスケッチ(左ペイン+右ペインの専用ツリー/統計ダッシュボード)は不採用とした。** `ui::CommandPalette`のみで全機能を提供する(WI-08〜WI-10で確立済みの`CommandId::None`パレット限定コマンドパターン)。新規ネイティブウィジェット追加のリスクとWI規模を避けるための判断。
- **要件定義書§8の5項目を、実質3機構(色分け/フィルタ/ジャンプ)で満たした。** ERROR抽出/WARNING抽出は「フィルタのプリセット(`logmode.filter.errorsOnly`/`warningsOnly`) + 既存のジャンプコマンド」の組み合わせで実現し、専用コマンドを新設していない — 時系列ジャンプ(`logmode.jump.next/previous`)がフィルタ状態に応じて自然にERROR/WARNING抽出ナビゲーションへ変化する設計。

| 要件定義書§8の項目 | 実装 |
|---|---|
| タイムスタンプ解析 | WI-14aで実装済み |
| 色分け | `RenderPipeline::setLogLineLevels()` + `drawLogLevelOnLine()` |
| フィルタ | `EditorSession::logLevelFilterMask()` + `isLineHidden()`拡張 |
| ERROR抽出/WARNING抽出 | フィルタのプリセット + `logmode.jump.next/previous` |
| 時系列ジャンプ | `logmode.jump.next/previous`(フィルタ無しなら全マッチ行を時系列順) |

- **`neomifes::render`が`neomifes::logmode::LogLevel`を仲介型なしで直接使う設計にした。** `RenderPipeline`が既に`syntax::Token`/`syntax::Language`を直接扱っているのと同じ理由(`neomifes::logmode`は`document::`のみに依存する自己完結モジュール)。`CursorVisual`/`MatchVisual`/`FoldVisual`が採用した「independent mirror struct」パターンは不要と判断した。
- **フィルタ(非表示行)は新規の隠蔽経路を作らず、既存の`RenderPipeline::isLineHidden()`(Phase 7iの折り畳み機構)へOR合流させた。** `drawVisibleLines()`/`hitTest()`等の既存可視行ロジックは無変更のままフィルタに対応させた。
- **`m_logLineLevels`(文書全体サイズになりうる)は`FrameState`の比較対象から除外し、`applyAsyncSyntaxTokens()`と同じ「到着時に強制再描画」パターンを踏襲した。** 軽量なフィルタマスクのみ`FrameState`へ直接含めた。
- **ログ編集追従(行番号ズレの自動補正)はスコープ外とした。** `core::BookmarkManager`の既知の制約と同じ理由。再インデックスで手動復旧する。
- **`buildCommandRegistry()`の認知的複雑度超過(43、閾値25)が発生し、`appendLogModeCommands()`への抽出で解消した。** WI-14bの`wireNormalMode()`と同種の問題が2WI連続で発生しており、5個を超えるコマンド群を1関数へ追加する際は着手前に抽出を前提とした設計を検討すべき教訓を得た。
- 詳細は`build_plan.md` WI-14cセクション、`detailed_design.md` §11.3参照。

#### 実装後の確定事項 (WI-14d、複数行グルーピング+ユーザー編集可能パターンファイル 🎉、2026-08-18、Phase 10.1 完結)

roadmap本節が元々見込んでいた「複数行エントリのグルーピング」「ユーザー編集可能パターンファイル」を実装し、Phase 10.1を完結させた。「パターン拡充」はWI-14a時点でCLAUDE.mdルール3(推測実装をしない)により見送り確定済みのため、ユーザー自身が検証済み正規表現を持ち込める手段として満たした(ベンダー固有組込パターンの追加は`docs/issues/phase_10_1_v2_extended_patterns.md`のP2のまま据え置き)。

- **実際の複数行グルーピングのバグは`nextVisibleLogLine()`/`previousVisibleLogLine()`ではなく`pushLogVisualsForSession()`にあった。** ジャンプ系は`matched==true`のみを対象にしており元々正しかったが、色分け/フィルタ用の`RenderPipeline::setLogLineLevels()`へ全行の`line.level`をそのまま渡していたため、継続行(既定`LogLevel::Unknown`)が親のERROR/WARNINGと独立してフィルタされ、「Errors onlyでフィルタしたのにJavaスタックトレース本体だけ残る」という実害があった。`neomifes::logmode::computeGroupedLogLevels(std::span<const LogLine>) -> std::vector<LogLevel>`という純粋関数へ集約して解消した。
- **ユーザー編集可能パターンファイルは「1ファイル=1`LogPatternRule`」のJSONを`%APPDATA%\NeoMIFES\log_patterns\`からディレクトリスキャンする方式にした。** 単一集約ファイル(Settings/KeyBindings型)ではなく、ユーザーが新規フォーマットを1つ追加する操作が常に「新規ファイル1つを置く」だけで完結するようにするための設計判断。不正ファイルはそのファイルのみ黒板消しし、id衝突はアルファベット順で先勝ち。
- **既存の組込パターンを`%APPDATA%`へ自動コピーする本節冒頭の原案スケッチは不採用とした。** コピーを作ると本体側の改善がユーザーのコピーに反映されないバージョニング問題を生むため。実際のギャップは「未対応フォーマットをユーザーが追加できること」であり「既存パターンを上書きできること」ではない。
- **`detectLogPatternRule()`に`std::span<const LogPatternRule> candidates = builtInLogPatterns()`を追加し、候補列を外部から差し替え可能にした。** `candidates`は候補列を置き換える(補うのではない)ため、`logmode.enable.auto`コマンド側で組込+ユーザー定義を結合してから渡す。
- **`buildCommandRegistry()`の認知的複雑度は3WI連続で閾値未超過を確認した。** WI-14b/cで2回連続超過した教訓を踏まえ`logmode.patterns.reload`コマンド追加直後に個別clang-tidy実行を計画に明記していたが、WI-14cで`appendLogModeCommands()`へ既に抽出済みだったため実際には超過しなかった — 「抽出しておけば次の追加が安全になる」という設計判断が機能した実例。
- 詳細は`build_plan.md` WI-14dセクション、`docs/design/detailed_design.md` §11.3参照。**Phase 10.1(ログ解析モード)完結。** 次はPhase 10.2(CSVモード)/10.3(JSON-XML Tree)、いずれも着手時にサブWIへ切り直す。

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

#### 実装後の確定事項/変更点 (2026-08-19、WI-16a完了 — ヘッドレス解析モデルのみ)

**`CsvModel`のデータ構造は上記スケッチから大きく逸脱した設計にした。** `logmode::LogModel`/`jsontree::JsonNode`(いずれもroadmap原案には存在しない先行実装)を実機で確認した上での判断:
- `document::Document* m_doc`は保持しない(`LogModel`が`attach()`を採用しなかったのと同じ理由 — 呼び出しごとに`Document&`/`BufferSnapshot&`を明示的に渡す設計に統一)
- `std::vector<std::vector<std::uint32_t>> m_columnOffsets`のネストvectorは不採用。平坦な`std::vector<CsvCell>` + 行オフセット`std::vector<std::uint32_t>`のCSR方式にした — 1000万行規模で行ごとの個別ヒープ確保を避けるため
- `std::vector<std::u16string> m_headers`は保持しない。`CsvCell`はテキストを複製せず位置(`startPos`/`endPos`)のみ保持する設計(`LogLine`の「テキストを複製しない」方針を踏襲)にしたため、ヘッダ行も他の行と同じ`CsvCell`表現で`headerRow()`から参照する
- `std::vector<std::size_t> m_visibleRows`(フィルタ後の順序)は本WIのスコープ外(フィルタ機能自体が未実装のため)

**列固定・セル単位クリック編集・TSV対応の実体・区切り文字自動判定・式列(v2.0)・グリッドUI(`WC_LISTVIEW`等)・`Mode::Csv`検出は全て本WIのスコープ外。** 区切り文字自動判定`detectCsvDelimiter()`のみ本WIで先行実装した(`,`/`\t`/`;`/`|`の4候補、`logmode::detectLogPatternRule()`のサンプリング構造を土台に「行ごとの出現回数の最頻値への一致度合い」でスコアリング)。ヘッダ自動判定は要件定義書・本節いずれにも記述がなく、`CsvParseOptions.hasHeader`は常に呼び出し側指定のまま(既定値`true`)とした。

詳細は`build_plan.md` WI-16aセクション参照。

#### 実装後の確定事項/変更点 (2026-08-19、WI-16b完了 — 非同期ワーカー+EditorSession配線のみ)

**新規`CsvModelWorker`(`neomifes::logmode::LogIndexWorker`を直接のテンプレート)を実装した。** `JsonTreeWorker`(WI-15b)ではなく`LogIndexWorker`型の設計を採用 — 理由は2点: ①`CsvModel::build()`が`CsvParseOptions{delimiter, hasHeader}`という呼び出し側設定を要する(`JsonTreeWorker`のリクエストは`snapshot`のみ)、②唯一の失敗契約`CsvParseError::InvalidDelimiter`が`LogPatternError::InvalidRegex`と同じ「呼び出し側の設定ミス」であり、`JsonTreeWorker`が扱う「コンテンツ依存の日常的な失敗」(nullopt)とは性質が異なる。そのため失敗リクエストは`LogIndexWorker`同様に投函せず握りつぶす設計にした(統合テストで直接証明)。

**WI-16a時点で`CsvModel::build()`の`BufferSnapshot`/`Document`両オーバーロードが既に揃っていたため、WI-14b/WI-15bが必要とした「非同期化の前提となるオーバーロード追加ステップ」が本WIには不要だった。** これにより本WIは3コミット(WI-14b/WI-15bの4コミットより1つ少ない)で完結した。

`EditorSession`へ`csvModel()`/`csvIndexInFlight()`/`beginCsvIndexing()`/`applyCsvIndexResult()`の4点を追加(`jsonTree()`系と同型)。`disableCsvMode()`相当・呼び出し元コマンド・グリッドUIは全て本WIのスコープ外(WI-16c以降)。

詳細は`build_plan.md` WI-16bセクション参照。

#### 実装後の確定事項/変更点 (2026-08-19、WI-16c完了 — グリッドUI MVP)

WI-16cで`EditorSession::csvModel()`系4点(WI-16b)を実際に消費する最初のUI/コマンドを実装した。`Ctrl+Shift+G`・表示メニュー・コマンドパレントの3経路からCSVグリッドをトグル表示できる。

- **`ui::CsvGridPane`は`LVS_REPORT | LVS_OWNERDATA`(仮想モード)の`WC_LISTVIEW`を採用した。** roadmapの原案スケッチが想定する`WC_LISTVIEW`自体は変わらないが、要件定義書の「1000万行CSV」規模を見据え、通常モード(`LVM_INSERTITEM`で全行を実データ保持)ではなく仮想モードを最初から採用した — 実装前のスタンドアロンprobeで`LVM_SETITEMCOUNT(10,000,000)`が0msで受理され破綻しないことを実機確認済み。
- **配置はroadmapのモックアップ通り「CSV Mode」的な全画面置き換えとした(ユーザーへAskUserQuestionで確認済み)。** `ui::OutlinePane`/`ui::JsonTreePane`(260dip右ドッキングストリップ)とは異なり、複数列を持つ表は狭い幅では実用にならないため。この配置ゆえ、タブ切替・文書スワップ時に自動的に閉じる新規ロジックが必須になった(OutlinePane/JsonTreePaneの「自動的に隠れない」既存ギャップをそのまま踏襲すると、全画面を覆うグリッドが別タブの中身を完全に隠し続けてしまうため)。
- **`syncViewForActiveSession()`/`resetViewAfterDocumentSwap()`(タブ切替・文書スワップの集約点)を拡張し、`CommandDispatchContext`構造体自体に`csvGridPane`/`csvGridPanePendingSessionToken`の2フィールドを追加した。** この2関数を`CommandDispatchContext`経由で呼ぶ5つの`dispatch*Command()`関数への個別のパラメータ追加を避けるための設計判断。
- **セルの活性化(ジャンプ)は`LVN_ITEMACTIVATE`(ダブルクリック/Enter)を使い、ジャンプと同時にグリッド自体を閉じる。** `OutlinePane`/`JsonTreePane`の「クリックでジャンプしてもパネルは開いたまま」とは意図的に異なる設計 — 全画面を覆うグリッドが開いたままだとジャンプ結果が見えないため。
- **配線作業中にWI-15c(`CommandId::JsonTreeToggle`)のコマンドパレット登録漏れを発見・是正した。** 計画・完了報告は「3経路全てに登録」と明記していたが、実装時に漏れていた。詳細は`build_plan.md` WI-15c節のDoD訂正注記参照。
- 列固定・フィルタ・ソート・セル編集・式列は全て本サブWIのスコープ外(WI-16d以降)。

詳細は`build_plan.md` WI-16cセクション参照。次はWI-16d以降(列固定・フィルタ・ソート・式列・セル編集)。

#### 実装後の確定事項/変更点 (2026-08-19、WI-16d完了 — フィルタ・ソート ヘッドレス計算基盤)

WI-16dで残りスコープ(列固定/フィルタ/ソート/検索/CSV編集)のうち、フィルタ・ソートのヘッドレス計算基盤(`computeCsvRowOrder()`)のみを実装した。WI-14/WI-15/WI-16a〜cが確立した「ヘッドレス基盤→非同期化+EditorSession配線(UIなし)→UI」の3段階パターンをそのまま踏襲し、EditorSession配線・UIは後続サブWI(WI-16e以降)へ送った。

- **要件定義書§9の「フィルタ」と「検索」を1機構(行内いずれかのセルへの部分一致・大文字小文字非区別)で統合した。** roadmap原案の`[Filter: City == Tokyo]`(列指定の等価フィルタ)は1000万行規模のグリッドに列選択UIまで持たせる過剰実装と判断し非スコープにした(要望が出れば別サブWIで追加可能な形にcomputeCsvRowOrder()のCsvFilterOptionsを設計してある)。
- **ソートは数値として両辺解釈できる場合のみ数値比較、それ以外は辞書式比較にフォールバックする設計にした。** 純粋な辞書式ソートだと"9"が"10"より後に来る罠があり、roadmapの`[Sort: Score desc]`モックアップが数値カラムを想定していることとも整合しない。
- **性能目標(フィルタ100万行≤1秒/ソート100万行≤3秒)を実測で達成した。** google/benchmark(`tests/bench/csvmode_row_order_bench.cpp`、`logmode_index_bench.cpp`を直接のテンプレート)で実測: フィルタ569ms、ソート1,214ms(いずれもRelease構成、1,000,000行)。同期呼び出しのままでも100万行規模までは許容範囲と実測で確認できたが、1000万行での外挙は未検証であり、非同期化の要否はEditorSession配線を行うWI-16eの設計判断として残した。
- `CsvModel`/`ui::CsvGridPane`/`EditorSession`は全て無変更のまま。

詳細は`build_plan.md` WI-16dセクション参照。次はWI-16e以降(EditorSession配線・UI)。

#### 実装後の確定事項/変更点 (2026-08-19、WI-16e完了 — フィルタ・ソート EditorSession配線+UI実装)

WI-16eで`computeCsvRowOrder()`(WI-16d)を実際に消費する最初のUI/配線を実装した。フィルタ編集欄+列ヘッダクリックソートで、Phase 10.2は列固定・セル編集・式列を除く主要機能が出揃った。

- **行順序を`EditorSession`側でキャッシュする設計にし、WI-16d完了記録が残した「非同期化の要否」を「同期のまま(追加ワーカー新設せず)」と確定した。** `CsvGridPane`の仮想モード`LVN_GETDISPINFOW`は可視セル1つにつき再描画のたびに発火するため、そのコールバック内で毎回`computeCsvRowOrder()`(WI-16d実測: 100万行で最大1.2秒)を呼ぶのは論外。フィルタ入力は150msデバウンス済み、ソートはクリックという離散イベントであり、いずれもこの実測値であれば同期呼び出しでも許容範囲と判断した(1000万行規模での外挿は引き続き未検証)。
- **`ui::CsvGridPane`のフィルタ編集欄は`ui::FindBar`のWC_EDIT+150msデバウンス+IME合成ガードを直接のテンプレートにした。** 新規のUIタイミング規約を発明せず、既存の「テキスト入力が段階的な結果を駆動する」制御パターンをそのまま再利用。
- **列ヘッダの並び替え状態はネイティブの`Header_SetItem`+`HDF_SORTUP`ではなくテキスト追記(▲/▼)で表現した。** `CsvGridPane`自体をcsvmode型非依存に保つため、矢印描画は`app::buildCsvGridColumnLabels()`(bridge層)の責務にした。
- **実機ドッグフーディングで、フィルタ・ソート(3段階サイクル・矢印表示・#列リセット)・フィルタ+ソート複合・ジャンプの行変換の正しさを実際の画面操作で確認した。** `Ctrl+Shift+G`の`SendInput`合成キーは今回不調だったため`WM_COMMAND`で代替、列ヘッダクリックは座標取得の試行錯誤の末(`HDM_GETITEMRECT`のクロスプロセス誤用で対象プロセスをクラッシュさせた事故が1件あったが、原因はドッグフーディング手法側であり本WIのコード欠陥ではない)`WM_LBUTTONDOWN`直接送信で確認、セルジャンプは合成マウスクリックがリスト選択状態を変えなかったためキーボード選択で代替 — いずれも本項目・詳細はbuild_plan.md WI-16e完了記録参照。
- **副産物として、末尾改行のあるCSVでグリッドの「#」列が実データ行数+1(暗黙の空行)を表示することを発見した。** WI-16aで既に文書化済みの仕様(Document全体の「末尾改行は空行1つ」規約の継承)がグリッドUIで初めて視覚的に露呈したものであり、WI-16eの実装ミスではないと判断。`docs/issues/csv_grid_shows_trailing_implicit_empty_row.md`(P2)として起票、対応方針は未確定。

詳細は`build_plan.md` WI-16eセクション参照。次はWI-16f以降(列固定・セル編集・式列)。

#### 実装後の確定事項/変更点 (2026-08-24、WI-16f完了 — セル単位クリック編集)

WI-16fで残りスコープ(列固定・セル編集・式列)のうちセル編集のみを実装した(列固定・式列は規模の大きさから後続サブWIへ先送り、`build_plan.md` WI-16fセクション参照)。新規`csvmode::escapeCsvCellText()`(`csvCellValue()`のエンコード側の対)、`CsvGridPane`への`WC_EDIT`セル編集オーバーレイ、app層`applyCsvCellEdit()`(`ReplaceRangeCommand`dispatch+`beginCsvIndexing()`再インデックス)。

**実機ドッグフーディングで、WI-16c(2026-08-19)以来の既存バグ(`LVS_EX_FULLROWSELECT`拡張スタイル未設定によりListViewの行ヒットテストが実質「#」列にしか反応しない)を発見・解消した。** WI-16c自身は「セルダブルクリックのみ自動化ハーネスの制約で未確認」と正直に記録しており、本物の人間の手によるマウスクリックでの検証は本WIが初めてだった。この発見過程で、比較検証用の一時`git worktree`をユーザーのホームディレクトリ直下へ無断作成してしまいユーザーから厳重注意を受け、CLAUDE.md 絶対ルール12(プロジェクト外への無断ファイル作成禁止)を新設する経緯もあった。

**追記(2026-08-25):** push前にユーザーが発見したCSVグリッドのフィルタ行付近の描画リーク(裏のDirect2D文書ビューがフィルタ行の意図的な余白から透けて見える既存バグ、WI-16c由来)を追加修正した(コミット`25f0414`)。新規`m_hwndFilterBackdrop`(無地`WC_STATIC`)でフィルタ行バンド全体を隙間なく覆う設計に変更。詳細は`build_plan.md` WI-16fセクションの追記参照。

詳細は`build_plan.md` WI-16fセクション参照。次はWI-16g以降(列固定・式列)。

#### 実装後の確定事項/変更点 (2026-08-25、WI-16g完了 — 「#」列固定、🎉Phase 10.2 列固定達成)

WI-16gで残りスコープ(列固定・式列)のうち列固定のみを実装した(式列はroadmapに具体的な文法・構文が一切無くこのまま実装すると推測実装になるため、着手前のAskUserQuestionで別WIへ切り分け)。

- **`ui::CsvGridPane`の単一`WC_LISTVIEW`を2つの同期`SysListView32`兄弟HWNDへ分割した。** `m_hwndFrozenList`(「#」列のみ、固定50dip幅、非水平スクロール)+`m_hwndDataList`(実CSV列のみ、シフト無しの列空間)。`NM_CUSTOMDRAW`単体では列固定を実現できない(ネイティブ水平スクロールが固定したい列のピクセルごと動かしてしまう)ため2HWND分割が必須、完全自前描画への転換はWI-16aで実証済みの`LVS_OWNERDATA`機構を丸ごと捨てることになり過大と判断した。
- **垂直スクロール・選択状態は行インデックス差分方式+`LVN_ITEMCHANGED`相互反映で同期。** 両リストへ新規`LVS_SINGLESEL`を付与(旧単一リストには無かった) — オーナーデータリストの範囲選択は`LVN_ITEMCHANGED`ではなく範囲指向の`LVN_ODSTATECHANGED`(本実装は非対応)を送る仕様のため、単一選択に限定することで単純化した。実装前に標準プローブ(`csv_freeze_scroll_probe.cpp`)で`ListView_Scroll`の境界クランプ挙動・`LVS_SINGLESEL`下での通知経路・`ListView_GetSubItemRect(subItem=0)`が列0ではなく行全体を返す挙動の3点を実機検証済み。
- **実機ドッグフーディングで、`showWith()`が2回目以降呼ばれると「#」列が画面上ずっと空白になる重大バグを発見・解消した。** `LVN_GETDISPINFOW`は正しい`mask`で発火し続けテキストも正しく書き込まれているにも関わらず画面に反映されないという不可解な状態で、`InvalidateRect`による強制再描画も無効だった。原因は「#」列特有の`LVM_DELETECOLUMN`+`LVM_INSERTCOLUMNW`の繰り返しに絞り込まれ、「#」列の内容は実CSV列と異なり常に不変(常に"#"という見出し、常に同じ50dip幅)であり再構築する理由が無いと気づいた結果、`createListViews()`で1回だけ挿入し`showWith()`では二度と触らない設計へ変更して解消した — comctl32のreport-view単一列delete+insertに関する未特定の内部挙動を、対症療法ではなく再構築自体をやめることで回避した形。
- **実機ドッグフーディング中に自動化ハーネス起因のプロセスクラッシュが1回発生した(`LVM_SETITEMSTATE`へ生ポインタをクロスプロセス送信、`COMCTL32.dll`内0xc0000005)。** ポインタはプロセス境界を越えて有効でないという既知のWin32制約が原因で、本実装のバグではないと判断し`SendInput`(`MOUSEEVENTF_VIRTUALDESK`付き、多モニタ相当の仮想デスクトップ環境での座標ずれも合わせて発見・解消)による実クリックへ切り替えて検証を継続した。

詳細は`build_plan.md` WI-16gセクション参照。次はWI-16h(式列、着手前に具体的な文法・構文をユーザーへ確認する必要あり)、WI-17f(Diffビュー)。

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
- JSON: `simdjson` 検討 (Phase 10 着手時 ADR) → 実装は `nlohmann::ordered_json` に転換 (WI-15a、位置復元APIの欠如が両ライブラリ共通の制約と判明したため独自`PositionScanner`方式を採用、詳細は上記実装後の確定事項参照)
- XML: ~~`pugixml` (MIT、軽量)~~ → **`tree-sitter-xml` を再利用 (WI-15f、位置復元API欠如の`pugixml`より有利と判明、新規ADR不要、詳細は上記実装後の確定事項参照)**
- 巨大 JSON (1GB+) は SAX 解析 + 部分ツリー展開
- XPath / JSONPath は自前実装

#### 性能目標
- 100MB JSON のツリー構築 (先頭部分のみ): ≤ 500ms
- ノード展開: ≤ 100ms
- 整形: ≤ 1 秒 (10MB JSON)

#### 影響ファイル
- **新規:** `src/tree/{json_parser.cpp, xml_parser.cpp, tree_model.cpp, xpath.cpp, jsonpath.cpp, formatter.cpp}`、`src/ui/tree_view_pane.{h,cpp}`、`tests/unit/tree_*_test.cpp`
- **変更:** `src/app/main.cpp` (JSON/XML モード検出)、`src/core/mode.h` (Mode::JsonTree / Mode::XmlTree)

#### 実装後の確定事項 (WI-15a、JSON ツリーモデル ヘッドレス基盤、2026-08-18)

WI-15a で JSON 側の最初のサブ WI(ヘッドレス基盤)に着手した。上記の原案スケッチと以下の点で異なる決定をした:

- **`simdjson` 検討は不要と判明、ADR-013 で既に採用済みの nlohmann/json を転用した。** `nlohmann::ordered_json`(同一ヘッダ内に既存)がキー順保持済みのDOMを提供し、位置情報は`nlohmann::json_sax`のコールバックに一切渡らない(実機ソース読解+スタンドアロンprobeで確認)ため、`ordered_json::parse()`による構文検証+DOM構築と、同じ検証済みテキストを独自の`PositionScanner`で並走させる位置復元、の二段構成を採用した。新規外部ライブラリ・新規ADRは不要だった。
- **`src/tree/`ではなく`src/jsontree/`という名前にした。** XML側が別ライブラリ選定(ADR待ち)で分離スコープになったため、モジュール名もJSON専用であることを明示。`neomifes::logmode`と同型の構成(`include/neomifes/jsontree/`、`src/`、独立STATIC ライブラリ)。
- **`src/core/mode.h`(`Mode::JsonTree`/`Mode::XmlTree`)は導入しなかった。** WI-14(ログモード)が`EditorSession`の機能ごと`std::optional<T>`方式(中央enumなし)で実装済みの前例に従う。
- **XMLは本サブWIのスコープから完全に除外した。** `pugixml`等の採否は未決定でADRが必要。
- **`巨大JSON対応(SAX解析+部分ツリー展開)`/`整形`/`バリデーション`/`XPath`/`JSONPath`/`ツリーUI(左右分割ペイン)`/`折り畳み統合`は全て後続サブWIへ。** 本サブWIは正しさ(構造/キー順序/位置精度/不正入力の黒板消し)を固めることのみに集中した(WI-14aが`LogModel::build()`をまず素朴実装にしてからWI-14bでストリーミング化した順序と同じ判断)。
- 詳細は`build_plan.md` WI-15aセクション参照。

#### 実装後の確定事項 (WI-15b、JSON ツリー 非同期インデックス化 + EditorSession配線、2026-08-18)

WI-15bでWI-14bに相当する非同期化を実施した(ヘッドレスモデル→非同期ワーカー+EditorSession配線→UIという順序をJSON側でも踏襲)。UIは本サブWIでも一切追加していない(WI-15cへ)。

- **`JsonTreeWorker`は`logmode::LogIndexWorker`を直接のテンプレートに、FIFO `std::deque`を採用した。** 複数タブがそれぞれ独立した結果を必要とするため(`SyntaxWorker`の「最新のみ保持」方式が安全な理由は単一の`RenderPipeline`にしか結果を返さない設計だからで、JSONツリーには当てはまらない)。
- **`parseJsonTree()`に`BufferSnapshot`オーバーロードを追加したのは、複雑度改善ではなく純粋なスレッド安全性リファクタと判明した。** `LogModel::build()`のBufferSnapshot化(WI-14b、O(lines×pieces)→O(document length))とは性質が異なる — JSONは`nlohmann`が全文一括読込を要求するため、複雑度クラスは元から変わらない。
- **`JsonTreeWorker`は`LogIndexWorker`と異なり、不正JSON(nulloptの解析結果)でも必ず結果をpostする。** `LogIndexWorker`は失敗結果を`continue`で握りつぶす設計だが(組込パターンでは到達不能な稀なエラーパスのため許容)、JSONでは「JSON以外のファイルに対して呼ばれた」がむしろ日常的な正常系であり、握りつぶすと`EditorSession::jsonTreeIndexInFlight()`が永久にtrueのまま固定される。
- **`EditorSession::clearJsonTree()`(`disableLogMode()`相当)は本サブWIに含めなかった。** `disableLogMode()`はWI-14cで「Log: Disable」コマンドとセットで追加されたものであり、呼び出し元(コマンド)を追加しないWI-15bには同じ理由で不要と判断した。
- **最終ゲート(ubsan/clang-cl構成)で、`nlohmann::ordered_json::parse()`自体(再帰下降パーサ、深度上限を設定するAPI無し)が病的に深いネストでスタックオーバーフローしうることを発見した。** `docs/issues/json_tree_worker_deep_nesting_stack_overflow.md`(P1)としてissue化、対応はWI-15c以降(実際にこの経路へ到達するコマンドが追加されるタイミング)へ先送り。
- 詳細は`build_plan.md` WI-15bセクション参照。

#### 実装後の確定事項 (WI-15c、JSON/XML Tree モード ツリーUI実装、2026-08-19)

WI-15cで`EditorSession::jsonTree()`系4点(WI-15b)を実際に消費する最初のUI/コマンドを実装した。`Ctrl+Shift+J`・表示メニュー・コマンドパレントの3経路からJSON構造ツリーパネルをトグル表示できる。

- **`ui::JsonTreePane`は`ui::OutlinePane`を直接のテンプレートに新設した(汎用化リファクタは不採用)。** `ui::OutlineItem`(`name`/`targetPos`/`children`)はJSON専用の新規データ構造体を作らずそのまま再利用 — 上記UI/UXモックアップの左ペインは「真の左右分割ペイン」を想定していたが、`OutlinePane`と同じ「右端オーバーレイ」方式のまま実装した(真の分割ペイン化はWI-15d以降へ)。
- **`app::buildJsonTreeItems()`(ブリッジ関数)は`app::buildOutlineItems()`と異なり明示スタックによる反復実装が必須と判明した。** `syntax::OutlineNode`の深さはシンボル定義の入れ子(生AST深さより浅い)に留まるため再帰で許容されていたが、`jsontree::JsonNode`の深さはJSON構造そのもの(`json_tree.cpp`の`kMaxJsonNestingDepth`ガードのみが上限)であり、同じ再帰実装を踏襲すると新たな`misc-no-recursion`リスクを持ち込むことになる。
- **非同期性の扱いとして、`main.cpp`ローカルの`const void* jsonTreePanePendingSessionToken`を新設した(`EditorSession`メンバ案は不採用)。** ペインはWorkspace全体で1枚しかないため、「どのセッションの非同期結果を待ってペインへ自動反映すべきか」はUI層の関心事であり、`freeCursorModeEnabled`/`isDraggingMinimap`と同じ配置とした。トグルOFF・Escape・非対象タブへの結果到着のいずれでもこのトークンを適切にクリアし、閉じた後に届く遅延結果でペインが勝手に再表示されるバグを防止した。
- **WI-15b最終ゲートで発見したP1 issue(`nlohmann::ordered_json::parse()`の深いネストによるスタックオーバーフロー)を本WIのコミット1として解消した。** `DepthLimitSax`(`nlohmann::json_sax<T>`の最小実装)による事前深度チェック(`kMaxJsonNestingDepth=200`)を`ordered_json::parse()`の前に追加。この設計の技術的前提(SAXコールバックの`false`が実際に再帰前に解析を打ち切ること)はスタンドアロンprobe+`nlohmann/detail/input/parser.hpp`のソース読解の両方で実装前に検証した。副産物として、`DepthLimitSax`のクラス派生がclang-tidyの`portability-template-virtual-member-function`を13件引き起こすことが判明し(NOLINTでは一次診断位置がサードパーティヘッダのため抑制不可)、`.clang-tidy`でこのチェックをプロジェクト全体で除外した(このプロジェクトが対象とする2コンパイラ=MSVC v143/clang-cl のいずれについても既に3構成の検証ゲートで実際にビルドしているため、チェックの前提=「コンパイラによる差異」という懸念自体が実質的に当てはまらないと判断)。
- **Format/Validate/JSONPath/XPath・XML対応・折り畳み状態の永続化・巨大JSON対応(プログレッシブ表示)は全て本サブWIのスコープ外(WI-15d以降)。** 折り畳み統合自体は行った(`buildJsonFoldRegions()`が`FoldingModel`へ統合、ガター折り畳みマーカーが機能する) — スコープ外なのはこれらより高度な機能群。
- 詳細は`build_plan.md` WI-15cセクション参照。

#### 実装後の確定事項 (WI-15d、JSON 整形(Format)・バリデーション(Validate)、2026-08-19)

WI-15dで要件定義書§10の残り6項目(XML対応/整形/バリデーション/XPath/JSONPath/真の左右分割ペイン化)のうち「整形」「バリデーション」の2つを実装した。コマンドパレット限定(`CommandId::None`)の「JSON: Format Document」「JSON: Validate」の2コマンドとして提供。

- **`formatJsonNode()`は`JsonNode`自身の生テキストをそのまま出力し、nlohmannの`.dump()`のような再シリアライズを行わない設計にした。** 数値`"1.50"`が`"1.5"`に化けない。Objectキーのみ`JsonNode::key`(デコード済み)を新規`escapeJsonString()`で再エンコードする必要があった。
- **`validateJson()`は新規パーシング経路を作らず、既存の`DepthLimitSax`(WI-15c)を拡張して実装した。** 構文エラーの位置はnlohmannの`parse_error`SAXコールバックの`position`引数(例外の`.byte`と同一の`chars_read_total`、vendoredソース読解で確認)を`parseJsonTree()`が既に構築済みの`byteToUtf16`テーブルでO(1)変換して取得する。ネスト超過は位置情報を得られないため固定メッセージ+位置0とした。
- **最終ゲートで`formatJsonNode()`がclang-tidyの`misc-no-recursion`に抵触した(相互再帰、`formatValue`⇄`formatChildren`)。** NOLINT抑制ではなく、`json_tree.cpp`の`buildTree()`が同じ理由で既に採用している「明示スタックによる反復実装」へ全面書き換えして解消した。書き換え前後で単体テスト8件全てがバイト単位で同一の出力を返すことを確認。
- **`core::ReplaceRangeCommand`が、このコードベースで初めて「文書全体を1回のUndo可能な編集として書き換える」実際の消費者になった。**
- **ダイアログは新規MessageBoxWではなく既存の`message_dialogs.h`(TaskDialogIndirectベース)を踏襲した。** 実装序盤の設計をMessageBoxWから訂正した経緯あり(詳細はbuild_plan.md WI-15dセクション参照)。
- **XML対応・XPath・JSONPath・真の左右分割ペイン化は全て本サブWIのスコープ外(WI-15e以降)。**
- 詳細は`build_plan.md` WI-15dセクション参照。

#### 実装後の確定事項 (WI-15e、JSONPath、2026-08-22)

WI-15dの残り4項目(XML対応/XPath/JSONPath/真の左右分割ペイン化)のうち「JSONPath」のみを実装した。新規外部ライブラリ・ADRが不要(既存`JsonNode`ツリーへの読み取り専用クエリとして完結)なことが、XML対応・XPath(XML用パーサのADRが前提)より先に着手した理由。

- **サポート構文を`$`/`.key`/`['key']`/`[0]`/`[*]`とその連鎖のサブセットに絞った自前実装(`neomifes::jsontree::json_path`)。** 再帰下降(`..`)・フィルタ式・スライスは非対応、将来の再評価事項として明記した。
- **`ui::JsonPathBar`は`ui::GotoLineBar`をほぼそのまま複製した新規オーバーレイ(単一WC_EDIT、デバウンス無し)。** ライブプレビューは追わず、Enterで初めて評価する設計にした(未完成の式でエラーダイアログが出続ける事態を避けるため)。
- **新規コマンド`json.jsonpath`は`CommandId::None`でパレット限定、`JsonPathBar`が開くだけの薄いaction+`onSubmit`から呼ばれる`dispatchJsonPathCommand()`という2段構成にした。** json.format/json.validateと異なり引数(式文字列)が必要なための設計上の違い。
- **最終ゲートでclang-tidyの`readability-function-cognitive-complexity`(evaluateJsonPath()、31/25)を3ヘルパー関数への抽出で解消、テストファイルの`bugprone-unchecked-optional-access`5件を参照束縛パターンへの変更で解消、clang-cl固有の`-Wmissing-designated-field-initializers`(MSVCでは無診断)を`JsonPathSegment::key`への明示デフォルト`= u""`付与で解消した。** いずれもDebug構成では検出されず、ubsan(clang-cl)構成の最終ゲートで初めて発覚 — 毎WIでubsanを走らせる運用の効果を改めて確認した事例。
- **実機ドッグフーディングで、TaskDialogIndirectのモーダル性が同期SendMessageベースの自動化ハーネスを最大120秒ブロックする、この種のダイアログ機能では初めての制約が見つかった。** `EnumWindows`での独立したダイアログHWND発見+非同期PostMessageへの切り替えで対処、NeoMIFES自体の欠陥ではない。
- XML対応・XPath・真の左右分割ペイン化は全て本サブWIのスコープ外(WI-15f以降)。
- 詳細は`build_plan.md` WI-15eセクション参照。

#### 実装後の確定事項 (WI-15f、XML ツリーモデル ヘッドレス基盤、2026-08-25)

WI-15fで要件定義書§10の残り3項目(XML対応/XPath/真の左右分割ペイン化)のうち「XML対応」のヘッドレス基盤のみを実装した。

- **本節冒頭「データ構造・アルゴリズム」の原案`pugixml`採用を覆し、Phase 7r以来ベンダリング済みの`tree-sitter-xml`を再利用する設計に転換した。** `pugixml`はノード単位の位置復元APIを一切公開しない(エラー時のオフセットのみ)ため、JSON側が`nlohmann`の同種の欠落に独自`PositionScanner`で対応した回避策を、XML側でより複雑な形で再実装する必要が生じる。一方`tree-sitter-xml`は新規依存・新規ADRが一切不要(ADR-014が既に承認済み)で、かつ既存のtree-sitter利用がUTF-16LEでパーサへ入力を渡しているため`ts_node_start_byte(node)/2`が直接`document::TextPos`になり、位置復元パスが実質無料で手に入ると判明した(WI-15aのsimdjson→nlohmann転換と同種の、着手前調査による原案の意図的な上書き)。
- **`XmlNode`/`XmlTree`の設計は`JsonNode`と意図的に異なる点が1つある: `parseXmlTree()`は`std::optional`を返さず常に`XmlTree`を返す。** nlohmannが厳格なfail-fastパーサであるためJSON側は`std::optional`が自然な契約だったのに対し、tree-sitterは本質的にエラー耐性パーサ(ADR-014の採用根拠そのもの)であり、この性質をXML側では活かす設計にした。文書のルート要素が解決できない場合(空文書、または不整合な閉じタグ名で文書全体が1つの`ERROR`ノードへ縮退する場合など)は`XmlNodeKind::Error`という不透明な葉ノードをルートとして返す。
- **実装完了後、単体テスト作成中に新たな限界を発見した: `tree-sitter-xml`自体がXMLタグのネスト深さ約505〜510階層を境に、整形式・バランス済み入力であっても`ts_node_has_error()`が`true`になる(誤検知する)。** クラッシュ・スタックオーバーフローではなく(5000階層まで安全と別途確認済み)、既存の「ルート要素解決不能→`XmlNodeKind::Error`センチネル」設計が安全に縮退するため対応不要と判断し、`docs/issues/xmltree_deep_nesting_misparse_limit.md`として起票した(P2)。
- **XPath・真の左右分割ペイン化・XMLツリーUI(`ui::JsonTreePane`が`ui::OutlineItem`のみに依存しJSON非依存と判明済みのため、`app::buildXmlTreeItems()`ブリッジ関数を書くだけで再利用できる見込み)は全て本サブWIのスコープ外(WI-15g以降)。**
- 詳細は`build_plan.md` WI-15fセクション参照。

#### 実装後の確定事項 (WI-15g、XML ツリー 非同期インデックス化+EditorSession配線、2026-08-25)

WI-15gでWI-15b(JSONツリーの非同期化+配線)を直テンプレートに、`XmlTreeWorker`+`EditorSession`4点(UIなし)を実装した。

- **`XmlTreeWorker`は`JsonTreeWorker`の機械的な型だが、`JsonTreeWorker`が抱えていた「失敗時に投函するかドロップするか」という判断自体が不要になった。** `parseXmlTree()`(WI-15f)が`std::optional`を返さない設計のため、常に実体のある`XmlTree`を投函するだけでよい。
- **`EditorSession::m_xmlTree`の型は`std::optional<xmltree::XmlTree>`とし、`jsonTree()`とは異なり`std::nullopt`は「未インデックス」のみを意味する設計にした。** JSON側はnlohmannのfail-fast契約により`std::nullopt`が「パース失敗」も兼ねるが、XML側はパース失敗という概念自体が無く(`XmlTree::hasErrors`が代わりにその情報を持つ)、こちらの方が`m_logModel`の元々の設計によりよく合致する。
- **配線はWI-15b当時(WI-15cのUI/pane機構が乗る前)の最も単純な形をそのまま踏襲した。** `beginXmlTreeIndexing()`を呼ぶコマンド/UIは一切追加していない。
- 詳細は`build_plan.md` WI-15gセクション参照。

#### 実装後の確定事項 (WI-15h、XML ツリーUI、2026-08-25)

WI-15hで`ui::JsonTreePane`をそのまま再利用したXMLツリーUIを実装し、**🎉 Phase 10.3(JSON/XML Treeモード)はJSON側・XML側とも構造ツリーUIまで完結した。**

- **`ui::JsonTreePane`自体がWI-15c以来「JSON/XML構造ツリーパネル」として両対応を想定した設計だったため、本WIは新規UIクラスを一切必要としなかった。** `ui::OutlineItem`のみに依存する汎用実装に、新規`app::buildXmlTreeItems()`/`app::buildXmlFoldRegions()`ブリッジ関数でXML由来のデータを流し込むだけで完結した。
- **`Ctrl+Shift+J`をJSON専用から「JSON/XML両対応の単一トグル」へ設計転換した(着手前にAskUserQuestionでユーザーへ確認済み)。** `EditorSession::language() == syntax::Language::Xml`の場合のみ新規`refreshXmlTreePane()`へ分岐し、それ以外(JSON含む全言語)は既存の`refreshJsonTreePane()`を無変更のまま通す。`jsonTreePanePendingSessionToken`は新設せずJSON/XML間で共用する設計にした — セッションの`language()`はトグル時点で固定されるため、1回のトグルONでどちらか一方のワーカーしか発火せず、JSON⇄JSON間の既存のトークン再利用と同じ安全性がJSON⇄XML間にもそのまま成立する。
- **ラベルテキストのみ汎用化し(「JSON構造ツリー」→「構造ツリー」)、内部識別子(`CommandId::JsonTreeToggle`本体等)は一切リネームしなかった。** ユーザー非可視かつ、既存のプリセット・テスト・ドキュメントへの影響範囲を最小化するため。
- **実機ドッグフーディングで一時的な診断ログ手法(`JsonTreePane::showWith()`が受け取った`OutlineItem`ツリーをファイルへダンプ)を新たに確立した。** `WM_COMMAND`をPowerShell経由で実際のNeoMIFES.exeへ送信し、XML文書・JSON文書の両方で非同期ワーカー経由の正しい構造ツリー表示(回帰なし)を確認した。
- 詳細は`build_plan.md` WI-15hセクション参照。

#### 実装後の確定事項 (WI-15i、XPath自前実装 + 真の左右分割ペイン化、2026-08-25)

WI-15iでWI-15hの残り2項目(XPath・真の左右分割ペイン化)を両方実装し、**🎉 Phase 10.3(JSON/XML Treeモード)が完結した。**

- **「真の左右分割ペイン化」は視覚バグ修正ではなく機能修正だった。** 着手前調査(Explore agent並行調査)で、ネイティブ子ウィンドウ(`OutlinePane`/`JsonTreePane`)は現状でもWin32の子ウィンドウZオーダーにより常にD2Dスワップチェーンの上に正しく重なっており、「テキストが透けて見える」視覚的バグは存在しないことを確認した。実際の不整合は`RenderPipeline::visibleColumnCount()`(水平スクロールバー範囲・折り返し判定に使用)がペイン分の幅を考慮しておらず、実際には見えない列までスクロール可能と計算してしまう点にあった。`gutterWidthDips()`(左側クリップ+`visibleColumnCount()`減算)を直接のテンプレートとして`setRightPaneWidthDips()`を新設し対称的に解消した。
- **`FrameState`へ`rightPaneWidthDips`を含めた判断は`m_leftColumn`の既存の教訓を踏襲した。** `m_tabBarHeightDips`/`m_statusBarHeightDips`(起動時1回だけ設定、二度と変わらないため`FrameState`比較対象外)とは異なり、この値はペインのトグルのたびに動的に変わるため、含めないと粗粒度フレームスキップによりペインを開いてもテキストが古い(狭まっていない)幅で表示され続けるバグになる。
- **XPathの位置述語`[N]`は独立したセグメント種別ではなく、`TagName`/`Wildcard`セグメントへの任意フィールドとして畳み込んだ。** 実装中に自己発見・訂正した設計判断 — 本物のXPathの`/tag[N]`は「そのステップ自身のタグ名/ワイルドカードフィルタに一致した中でN番目、親ごとに独立して計算」という意味であり、JSONPathの配列インデックス降下(`[N]`が単純に配列の要素へ降りる)とは演算の形が根本的に異なるため、当初のプラン案(独立した`Index`セグメント)のままでは`/book/*[1]`のような複数の親に跨るケースで誤った結果を返していた。専用テスト(`IndexPredicateIsComputedIndependentlyPerFannedOutParent`)で訂正後の挙動を検証した。
- **`XPathBar`は新設せず既存の`ui::JsonPathBar`を再利用し、コマンドは統一せず分離した。** WI-15h(`Ctrl+Shift+J`のパネルトグル)とは逆の判断 — クエリ構文自体(`$.key` vs `/tag[1]`)がパネルトグルより強くユーザーに見えるコマンドであるため、「JSON: Evaluate JSONPath」/「XML: Evaluate XPath」の2つの明確に分かれたパレットエントリの方が1つの自動判別エントリより発見しやすいと判断した(着手前にAskUserQuestionでユーザーへ確認済み)。JSON/XMLの判別は`main.cpp`ローカルの`bool jsonPathBarIsForXml`で行い、`onSubmit`時点(表示時点ではない)で読む設計にした。
- **実機ドッグフーディングで新しい自動化ハーネスの落とし穴を発見した。** コマンドパレットが開いている間にキー入力をメインウィンドウのHWNDへ直接`PostMessage`すると、パレットの入力欄ではなくドキュメント本文へ挿入されてしまう(パレットが最前面に見えていてもフォーカスベースの経路には乗らない)。パレット/バー自身のEditコントロールのHWNDを`EnumChildWindows`で見つけて直接ターゲットする必要がある。詳細は`build_plan.md` WI-15iセクションの「実機ドッグフーディング」参照。
- 詳細は`build_plan.md` WI-15iセクション参照。

---

## 11. Phase 11 — Git 統合 / LSP 完全実装 / マクロ (Lua + JS + 秀丸互換)

### 11.1 Git 統合 (要件定義書 §11)

> **実装状況 (2026-08-25、WI-17f完了):** 🎉 **Phase 11.1(Git統合)が完結した。** ヘッドレス基盤+非同期化+EditorSession/Workspace配線+左ガター差分マーカーUI(手動リフレッシュ+保存時自動トリガー)+Gitペイン(変更ファイル一覧)+Diffビュー(インライン統合diff、コマンドパレット限定)まで全て実装済み。2026-08-23合意の確定スコープはこれで達成。Blame・インラインBlame・Commit・Branch切替/3-Way Merge/Side-by-side表示は🧊凍結(対象外)。詳細は本節末尾の「実装後の確定事項」参照。

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

#### 実装後の確定事項 (WI-17a、2026-08-22)

- **ライブラリは`third_party/libgit2/`への直接バンドルではなく、既存の`Dependencies.cmake`のFetchContentパターン(RE2/nlohmann_json/Abseil等と同型)でvendoringした。** ADR-022参照。着手前にscratchpadでのCMake FetchContent実現性検証を実施し、Windows長パス問題(`git config --global core.longpaths true`必須)・`STATIC_CRT=OFF`必須・インクルードディレクトリ手動追加必須の3点を確認した上で着手した。
- **モジュール名は`src/git/{git_repo.cpp, diff_computer.cpp, blame_reader.cpp, ...}`という上記の原案分割ではなく、`neomifes::git`という単一STATICライブラリ(`logmode`/`jsontree`/`csvmode`と同型)にした。** `diff_computer`相当の機能のみ(`GitRepository::diffAgainstHead()`)を実装、`blame_reader`/`commit_dialog`/`inline_blame`は未着手のため対応するファイルもまだ存在しない — 実装が追いつき次第、原案のファイル分割案を見直す(1ファイル1責務を超える規模になった時点で分割する方針、CLAUDE.md「1クラス≤300行」に準拠)。
- **`diffAgainstHead()`はディスク上のファイル内容ではなく、`document::Document`のメモリ上テキスト(未保存の編集を含む)とHEADブロブを比較する設計にした。** 上記UI/UX節が想定する「左ガターに差分マーカー」機能は、保存前のリアルタイム編集にも反応する必要があるため。`git_diff_blob_to_buffer()`(HEADブロブ vs メモリ上バッファ)を採用し、ディスクへの再読込を経由しない。
- **1 hunk = 1 `LineDiffRegion`という粒度にした**(行単位ではない、上記UI/UX節の「差分マーカ」は連続領域として表示すれば足りるため)。`old_lines==0`→Added、`new_lines==0`→Deleted、それ以外→Modifiedで分類する。実装中、`git_diff_options`の既定`context_lines=3`が純粋な追加・削除でも前後3行を巻き込みModifiedへ誤分類させるバグを単体テストで発見、`context_lines=0`(ガター用途では変更行そのものだけが必要)に修正して解消した。
- 非同期化・`EditorSession`配線・UI全般(左ガター描画・Gitペイン・Diffビュー・Blame・Commit・Branch切替)は全て未着手のまま、WI-17b以降へ委ねる(WI-17bで非同期化+EditorSession配線を実施、下記参照)。

#### 実装後の確定事項 (WI-17b、2026-08-22)

WI-17bでWI-14b(LogIndexWorker)/WI-15b(JsonTreeWorker)/WI-16b(CsvModelWorker)に相当する「非同期化+EditorSession配線」を実施した(ヘッドレスモデル→非同期ワーカー+EditorSession配線→UIという順序をGit側でも踏襲)。UIは本サブWIでも一切追加していない(WI-17c以降へ)。

- **`GitRepository::diffAgainstHead()`にBufferSnapshotオーバーロードを追加した。** WI-17a時点の実装は`document::Document&`(UIスレッド専用、ADR-009)を直接取っており、バックグラウンドスレッドから安全に呼べなかった。`jsontree::parseJsonTree()`が確立した二重オーバーロード型(BufferSnapshot版が主エントリポイント、Document版はそれへ委譲)をそのまま踏襲し、機械的な置き換えで解消した。
- **新規`GitDiffWorker`は`CsvModelWorker`を構造テンプレートに、失敗時の扱いは`JsonTreeWorker`側を踏襲した。** リポジトリに属さない/未追跡のファイルは「日常的な正常系」(`CsvParseError::InvalidDelimiter`のような呼び出し側の設定ミスとは性質が異なる)であり、握りつぶすと`gitDiffIndexInFlight()`が永久にtrueで固定される。`nullopt`でも必ずpostする設計にした。
- **リポジトリのキャッシュはしない。** `discover()`はディレクトリ探索のみで軽量、`GitRepository`自体も`unique_ptr`1個だけで安価なため、複数リクエスト間で使い回すキャッシュ機構は今追加する必要性が無いと判断した(WI-16aの「まず素朴実装」という前例を踏襲)。
- **`EditorSession::beginGitDiffIndexing()`はUntitledバッファに対して無条件no-opにした。** Gitはファイルパスが無いと本質的に動作できないため — 既存4ワーカー中、この種の「無効化」ガードを持つ最初のasync worker配線になった。
- **`beginGitDiffIndexing()`を呼び出すコマンド/UIは本サブWIに含めなかった。** WI-14b/15b/16bの前例と同じ「配線のみ先行」の扱い。
- **最終ゲートで、新規テストコードにclang-tidyの複数指摘(`bugprone-unchecked-optional-access`/`misc-misplaced-const`/`cppcoreguidelines-special-member-functions`等)が見つかり全て解消した。** 2件(`cert-msc30-c`/`readability-function-cognitive-complexity`)はWI-17a由来の既存未修正パターンをそのまま複製したものであり、一貫性を優先し意図的に据え置いた。
- 詳細は`build_plan.md` WI-17bセクション参照。

#### 実装後の確定事項 (WI-17c、2026-08-23)

WI-17cで左ガター差分マーカーUI+コマンドパレット限定の手動リフレッシュコマンドを実装した(自動トリガー・Gitペイン・Diffビュー・Blameは全てWI-17d以降へ)。`render::GitDiffMarker`/`GitDiffKind`は`FoldVisual`と同じrender::-localミラー型、変換は新規`app::buildGitDiffMarkers()`ブリッジ関数がapp層で行う設計にし、`RenderPipeline`を`neomifes::git`に依存させない独立エンジン原則(CLAUDE.md §3)を維持した。

**実機ドッグフーディング(本サブWIが初めてUIを持つため必須)で重大バグを2件発見した。** どちらも単体テスト・ビルド確認では検出不可能で、実際にアプリを操作して初めて判明した。

1. **`RenderPipeline::drawGutterOnLine()`のブロック配置順序バグ。** 新規Git差分マーカー描画ループを既存の折り畳みマーカーブロック(2箇所の早期`return`を持つ)より後ろに置いてしまい、折り畳み領域を持たない行(=大半のファイルの事実上全ての行)で常に到達不能になっていた。ブックマークブロック直後・折り畳みブロックの早期returnより前に移動して解消。
2. **`neomifes::git::initializeLibgit2()`が`src/app/`のどこからも呼ばれていなかった。** WI-17aの実装以来、3件のテストフィクスチャの`SetUp()`内でのみ呼ばれており、実アプリの起動経路には一度も配線されていなかった。`GitRepository::discover()`が実アプリでは常に未初期化のlibgit2ランタイムに対して動作し、静かに失敗し続けていたことになる — **Git統合機能(WI-17a/b/c)はテストスイート以外の実際のNeoMIFES.exe実行では一度も正しく動作していなかった可能性が高い。** `main.cpp`の`wWinMain()`にRAII `Libgit2Guard`+`initializeLibgit2()`呼び出しを追加して解消した。

(1)を修正した直後の再ドッグフーディングでもマーカーが表示されず、そこから(2)を発見した。1つのバグの修正で満足せず再検証したことで、より深刻な2つ目のバグを発見できた点が教訓。

詳細は`build_plan.md` WI-17cセクション参照。

#### 実装後の確定事項 (WI-17d、2026-08-23)

WI-17dで保存時の自動再diffトリガーを実装した(Gitペイン・Diffビュー・Blame等は全てWI-17e以降へ)。`document::saveFile()`の呼び出し元は自動保存とユーザー起動保存(`performSave()`)の2箇所のみで、「ファイルを開く」の4箇所以上に分散した経路と異なり真に単一の合流点だったため、既存の`csvGridPane`フィールド(WI-16c)と同じパターンで`CommandDispatchContext`へ`gitDiffWorker`フィールドを追加し配線した。タブ/ウィンドウクローズ時の確認ダイアログ経由の保存(`confirmDiscardIfDirty()`)には意図的に配線しなかった — セッションが破棄/非表示になる直前で再diffが無意味なため。

実機ドッグフーディングで、追跡済みファイルの編集→保存→手動コマンド無しでのガター自動更新をピクセル単位(RGB(229,155,53)、WI-17cの`diffModified`テーマ色と一致)で確認した。Untitledバッファ→Save Asの経路も確認したが、保存先が未追跡ファイルのためマーカー無し(GitDiffWorkerの既存契約通りの正しい挙動、バグではない)。

詳細は`build_plan.md` WI-17dセクション参照。

#### 実装後の確定事項 (WI-17e、2026-08-25)

WI-17eでGitペイン(変更ファイル一覧)を実装した(Diffビューは別WIへ)。着手前のAskUserQuestionで2点確認: (1) `Ctrl+Shift+G`は既に`CsvGridToggle`が使用しており衝突するため「コマンドパレット限定」を選択、(2) 「Gitペイン」(変更ファイル一覧)と「Diffビュー」(差分描画サーフェース)は別機能と判明し「Gitペインのみ今回」を選択。

- **新規`GitRepository::statusList()`(libgit2の`git_status_list_new()`)+`GitStatusWorker`(`GitDiffWorker`の直接テンプレート、`WM_APP+8`)を追加した。**
- **Gitステータス状態を`EditorSession`ではなく`Workspace`に配置した。** `gitDiff()`/`csvModel()`/`jsonTree()`は全てper-タブだが、Gitステータスは「リポジトリ」に属する情報でありドキュメントに属さない。同一リポジトリの複数タブが独立に再フェッチするのは無駄で、フェッチタイミングのズレで食い違う表示をする実害もあるため。この結果、`beginGitStatusIndexing()`はアクティブセッションがUntitledのとき`m_gitStatus`を積極的にnulloptへクリアする(`EditorSession::beginGitDiffIndexing()`の単純なno-opとは異なる、Workspaceレベルキャッシュ特有の必要な差異)。
- **`ui::GitPane`は`ui::CsvGridPane`(WI-16g、仮想モード必須)ではなく`ui::OutlinePane`(260dip右ドッキング、実項目)を直接テンプレートにした。** 変更ファイル数は現実的な規模(数十〜低千)のため仮想モードは不要と判断。
- 新規コマンド「Git: Toggle Changed Files」(`git.togglePane`、`CommandId::None`、コマンドパレット限定)のみ追加。専用リフレッシュコマンドは無く、トグルOFF→ONが手動リフレッシュの経路。
- テスト中に2件の副次的なバグ/フレーキネスを発見・解消した: 既存の共有テストフィクスチャ`makeRepoWithCommit()`が`git_index_write()`を呼んでおらず(`git_index_write_tree()`のみではディスク上の`.git/index`へ永続化されない)`statusList()`系テストでのみ露呈した点、および`uniqueTempDir()`のunseeded `std::rand()`による失敗実行時の残置ディレクトリ衝突。
- **実機ドッグフーディングで、このリポジトリ自身(README.md追跡ファイル)を対象にGitペインをトグルし、`git status --short`と完全一致するM(4件)/U(3件)の変更一覧を確認した。** クリックで新規タブとしてファイルが開くこと、リポジトリ外ファイルでの「Not a Git repository」プレースホルダも確認済み。「変更0件」プレースホルダの実機確認は行わず、単体テスト+コードレビューでの確信度に留めた(正直に記録)。`Ctrl+Shift+P`の合成入力がこの環境では届かなかったため(既知の修飾キー合成入力の制約)、`onDeferredInit`への一時的な直接呼び出しフックで同じコード経路を検証し、確認後に除去した。
- 詳細は`build_plan.md` WI-17eセクション参照。

#### 実装後の確定事項 (WI-17f、2026-08-25、🎉 Phase 11.1 完結)

WI-17fでDiffビューを実装した(インライン統合diffのみ、Side-by-sideは対象外)。着手前調査で`RenderPipeline`が単一Document・単一Direct2D描画のみでside-by-side分割描画の前例がコードベース内に一切無いと判明、AskUserQuestionで「インライン統合diffのみ(推奨)」を選択。**これで2026-08-23合意の確定スコープにおけるGit統合部分が完結した。**

- **新規`GitRepository::unifiedDiffAgainstHead()`(libgit2の`git_diff_blob_to_buffer()`へ新規`line_cb`を渡す、既存`diffAgainstHead()`はhunk_cbのみ)を追加した。** context_linesは既定値3のまま(diffAgainstHead()の0とは意図的に異なる — Diffビューは周辺文脈が必要)。同期・UIスレッド専用、discreteなユーザー操作のため非同期ワーカーは作らなかった(JSON Format/Validateと同じ扱い)。
- **`render::DiffViewLineMarker`を`GitDiffMarker`の再利用ではなく完全に別の新規型にした。** 既存`drawGutterOnLine()`のDeleted分岐は点マーカー専用の特殊扱い(lineCountを無視)であり、Diffビューの削除行(合成ドキュメント内に実在する複数行範囲)には流用できない。出荷済みの既存コードを一切変更せず、独立した新規マーカー型・セッター・描画パス(全行背景の半透明塗り)を追加した。
- **色は`theme.diffAdded`/`diffDeleted`と同じRGB値を低アルファ(0.18)で複製した専用ブラシにした。** 既存の完全不透明ブラシをそのまま流用するとテキストが隠れてしまうため。
- **libgit2が完全一致するblob/bufferに対して1行もline_cbを呼ばないという事実を単体テストで発見した。** 空の結果を検出した場合にDocument全文を全行Contextとして分割するフォールバックを追加して解消。
- **`diffViewDocument`(合成ドキュメントの実体を所有する唯一の変数)以外は全て`RenderPipeline::isDiffViewActive()`経由で状態を判定する設計にした。** WI-17eの`gitPane`のような深いパラメータのリップル配線を避けられた。
- **着手前調査で`resetViewAfterDocumentSwap()`が元々`setDocument()`を一度も呼ばない実バグ相当のギャップを発見・修正した。** 「Documentのアドレスがスワップを跨いで不変」という既存の暗黙前提を`diffViewDocument`が初めて破るため。
- 入力ガードは`handleKeyDownEvent()`/`handleCharEvent()`に加えて`dispatchCommand()`(WM_COMMAND経路)にも追加し、Diffビュー表示中の実コマンド(Save/Undo/Redo等)は先に閉じてから実行する一貫した挙動にした。
- **実機ドッグフーディングで、実際に変更されたヘッダファイルを対象にDiffビューをトグルし、追加行(緑)・削除行(赤)の半透明背景+既存シンタックスハイライトの正しい表示、Escapeでのライブ文書復帰、表示中の打鍵(「ZZZINJECTIONTEST」)が実文書に一切到達しないこと(タイトルバーの未保存インジケータ無しで確認)を確認した。**
- 詳細は`build_plan.md` WI-17fセクション参照。

### 11.2 LSP 統合 — 完全実装 (v2.0 大幅拡張)

> 🧊 **凍結 (2026-08-23、build_plan.md §0「現在のゴール」参照)。** Phase 11.1(Git統合)のUI化を優先し、本節は着手しない。将来の再評価に備えた設計メモとして凍結保存する。

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

> 🧊 **凍結 (2026-08-23、build_plan.md §0「現在のゴール」参照)。** Phase 11.1(Git統合)のUI化を優先し、本節は着手しない。将来の再評価に備えた設計メモとして凍結保存する。

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

> 🧊 **§12.1〜§12.3(22項目フル版)は凍結 (2026-08-23、build_plan.md §0「現在のゴール」参照)。** NVDA/JAWS専門認証・本物のAuthenticode証明書配布・SBOM/CVE継続運用・自動更新機構(canary→stable)など、このワークフロー単独では完結できない項目を多く含み、商用配布を目指す段階になったら再評価する。**実際の出荷判定は新設した §12.5(軽量版)を使う。** 以下§12.1〜§12.3は将来の再評価に備えた設計メモとして凍結保存する(削除しない)。

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

**現時点 (2026-08-04) の達成状況: 22 項目中 達成 4 / 未着手 18。** 達成済みは「起動時間」(148ms 実測)・「60fps スクロール」(avgFrame 16.5ms)・「100 万 Undo」・「clang-tidy 新規指摘 0」。

### 12.4 Phase 12' — MVP 出荷判定 (v2.1 新設)

**新設の理由:** v2.0 は出荷判定を「全機能実装後の Phase 12」に 1 度だけ置いていた。これは **最初の出荷が最も遠い未来になる**という計画上の欠陥である。実ユーザーの反応を一度も得ないまま Phase 9〜11 の大規模機能を作り込むリスクは、商用製品として許容できない。

**Phase 12' の位置:** Phase 8.6 完了直後。この時点で NeoMIFES は「**秀丸・サクラの代替として実用に耐え、22 言語のシンタックスハイライト・10GB ファイル対応・ミニマップ・Breadcrumb・Sticky scroll・コマンドパレットで明確に上回る**」状態に到達する。

**Phase 12' 出荷判定チェックリスト (Phase 12 の実用サブセット):**
- [ ] ファイルを 開く / 編集 / 保存 / 別名保存 が全て動作する
- [ ] 日本語 IME でインライン変換が正しく表示される (**実機手動確認必須**)
- [ ] 未保存で終了しようとすると警告が出る
- [ ] 10 個のファイルをタブで開いて相互に切り替えられる
- [ ] 設定でフォント・タブ幅・テーマを変更でき、再起動後も保持される
- [ ] 長い行の右端まで横スクロールで到達できる
- [ ] クラッシュ 0 (8 時間ソーク。Phase 12 の 24 時間は正式出荷時)
- [ ] ASan/UBSan クラッシュ 0、clang-tidy 新規指摘 0
- [ ] 既存の全性能 DoD (起動 ≤300ms / 60fps / 10GB) を維持
- [ ] Authenticode 署名 + Portable Zip 配布 (MSIX は Phase 12 で)
- [ ] **ドッグフーディング: 開発者が日常的に NeoMIFES で NeoMIFES を開発している**
- [ ] ユーザーマニュアル (最低限のキーバインドリファレンス) を同梱

**Phase 12' で意図的にスコープ外とするもの (Phase 12 へ):** NVDA/JAWS 対応、WCAG 2.2 AA、中韓 IME、RTL、fuzz test 24 時間、MSIX、自動更新機構、SBOM、テレメトリ。

> ✅ **実装後の確定事項 (WI-13完了、2026-08-16、🎉 M4達成):** 上記チェックリストの最新の達成状況は `build_plan.md` §6 (同一内容、WI-13の実施記録として更新される正の情報源) を参照。ここでの重複更新は行わない — CLAUDE.md §11 が警告する「複数箇所の片方だけ更新し陳腐化する」パターンを避けるため。要点: 14項目中12項目達成 (起動時間29.3ms実測・60fps維持・10GB実ファイル開封・ASan/UBSan 1227/1227 green・8時間ソーク`SOAK_COMPLETE_NO_CRASH`・Portable Zipを含む)、残る2項目(本物のAuthenticode証明書未取得(自己署名で機構実証済み)・日常的ドッグフーディングは限定的)は当初からコードの正しさとは独立した出荷判断としてユーザーに委ねる設計だった。ユーザーへAskUserQuestionで確認の上、この状態で🎉 M4を正式達成として記録する承認を得た(2026-08-16)。

### 12.5 v1出荷判定チェックリスト (軽量版、2026-08-23策定) — 🎯 実際に使う出荷判定はこちら

**新設の理由:** MVP(Phase 12'、WI-13)達成後、差別化機能(Phase 10)とGit統合(Phase 11.1)の追加を続けていたが、「次に何をもって完成とするか」の定義が無いまま作業が続く状態になっていた。ユーザーへ現状の残作業量(§12.3フル版を目指す場合35〜50 WI規模)を提示し、AskUserQuestionで今後の方針を確認した結果、**「Phase 10残り+Git統合のUI化まで完成させたら、§12.3の22項目フル版ではなく本節の軽量版で出荷判定する」**という合意を得た(2026-08-23)。

**§12.3フル版との違い:** このワークフロー(Claude Codeエージェント+ユーザーのローカル開発機)単独では完結できない項目 — 専門機関によるNVDA/JAWSアクセシビリティ認証、本物のAuthenticode証明書での署名配布、SBOM/CVEスキャンの継続運用体制、自動更新機構(canary→stableのサーバインフラ)、中国語/韓国語IMEの実機確認(該当IME環境が無い可能性が高い) — を除外し、**ローカル環境だけで実際に検証・達成できる項目**に絞った。商用配布を将来検討する際は §12.3 を再評価すること。

**チェックリスト (§12.3から引き継ぎ・簡略化した項目):**
- [ ] 起動時間 ≤ 300ms (Release、実機実測) — WI-13時点で29.3ms実測済み、維持確認のみ
- [ ] 初期メモリ ≤ 20MB (Release、Working Set)
- [ ] 60fps スクロール (10GB ファイル、Release)
- [ ] 100 万 Undo (24 時間ソーク、メモリ膨張無し) — WI-13は8時間実施済み、24時間へ延長
- [ ] 10GB ファイル対応 (通常編集 + ログ解析モード + CSV モード + JSON/XML Tree モード)
- [ ] 数 GB Grep ≤ 30 秒
- [ ] クラッシュ 0 (24 時間ソーク)
- [ ] メモリリーク 0 (Application Verifier、ローカル実行)
- [ ] clang-tidy 新規指摘 0 (毎WIで既に実施中の運用を維持)
- [ ] ASan/UBSan クラッシュ 0 (毎WIで既に実施中の運用を維持)
- [ ] 全単体テスト pass
- [ ] AI プラグイン無効時に本体 100% 動作 (AIプラグイン自体を作らない選択のため設計上自明に真)
- [ ] fuzz test 24 時間クラッシュ 0 (対象は encoding パーサ・regex コンパイルのみ。LSP JSON パーサは§12.3のLSP実装が前提のため対象外)
- [ ] 基本アクセシビリティ (キーボード完結ナビゲーション・高コントラストモード動作・スクリーンリーダーでの基本疎通確認。専門認証は求めない)
- [ ] 日本語 IME でインライン変換が正しく表示される (実機手動確認、WI-06で実績あり)
- [ ] 動作確認した Windows バージョンを明記する (開発機で確認できた範囲のみ、未確認バージョンは「未確認」と正直に記載)
- [ ] Portable Zip 版配布 (WI-13で実証済みの自己署名を維持。本物のAuthenticode証明書取得は商用配布時の別課題として`docs/issues/`に残す)

**達成状況:** 未着手(Phase 10残り+Git統合UI化の完了後に着手)。着手時は各項目の実測値をこの節に追記する(CLAUDE.md絶対ルール10)。

---

## 13. UI/UX トップレベル方針 (v2.0 大幅拡張)

### 13.1 キーバインドプリセット

> ⚠️ **WI-10 (2026-08-15) 実装後の訂正:** 本節が列挙する5種のうち、実際に実装したのは **NeoMIFES 標準 (`neomifes`) / 秀丸互換 (`hidemaru`) / サクラ互換 (`sakura`) / VSCode (`vscode`、独立プリセットとして。「NeoMIFES標準のVSCodeベース」ではない) の4種のみ**。「MIFES 互換」は build_plan.md の WI-10 節が最初からスコープに含めておらず、`build_plan.md` を Plan-of-Record として本節より優先させた(CLAUDE.md 絶対ルール3)。Vim/Emacs モードは以下の記述通り Phase 8 プラグイン提供のまま未着手。詳細は §8.6.2 の「実装後の確定事項」参照。

- **NeoMIFES 標準** (VSCode ベース)
- **秀丸互換** (F5=マクロ実行、Alt+F=ファイルメニュー、Ctrl+G=grep 等)
- **サクラ互換** (Ctrl+Enter=改行挿入、Alt+↑↓=行移動 等)
- **MIFES 互換** (F9=保存、F10=閉じる 等、要件定義書と MIFES の実キー体系交差を Phase 4b8 で確定) — **WI-10 では対象外 (上記訂正参照)**
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

> ⚠️ **v2.1 での是正:** 本節は v2.0 で「章」としてのみ規定され、**フェーズが割り当てられていなかった**。その結果 Phase 8f 時点でもメインエディタは `WM_IME_*` を 1 つも処理しておらず、日本語の未確定文字列がインライン表示されない (`gap_analysis.md` §3.4)。**日本語対応は §8.5.7 (Phase 8.5e) として実フェーズ化した。**中国語・韓国語 IME の確認は Phase 12 に残す。
> ✅ **2026-08-12、WI-06として実装完了。** 実装後の確定事項は §8.5.7 末尾参照。

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

**レベル 1 (デフォルト):** ✅ Phase 8a完了(SEH隔離)+Phase 8d完了(ADR-018、権限)。同プロセス内 SEH 隔離 + `permissions`自己申告ビットフィールド
- クラッシュ隔離のみ、悪意あるプラグインは制限しきれない
- ~~権限マニフェスト検証~~ → マニフェストファイル(プラグイン発見・インストール機構が無い)ではなく、プラグイン自身の`NeoMifesPluginInfo::permissions`を自己申告として読むのみ。署名検証・未署名プラグインの確認ダイアログは未実装のまま(詳細はADR-018)

**レベル 2 (高危険度権限):** ✅ Phase 8c完了(ADR-017)。Job Object でリソース制限
- ~~Network 権限を要求するプラグイン全て~~ → 権限モデル(Phase 8d)が実装された後も、自己申告を信頼できないため引き続き全プラグインへ一律適用(ADR-018)
- ~~メモリ・CPU 時間・ハンドル数の上限~~ → 実装したのは`ActiveProcessLimit=1`のみ。メモリ/CPU時間はプロセス全体(ホスト含む)を巻き込むため意図的に見送り、ハンドル数上限は該当するWin32 APIが存在しないと判明(詳細はADR-017)

**レベル 3 (Phase 8g〜、将来検討):** Windows AppContainer で完全隔離
- Capability に基づく細粒度権限
- ファイルシステムアクセスは Broker 経由
- **Phase 8c着手前調査で判明: 既存の同一プロセス内`LoadLibraryW`アーキテクチャへ後付け不可能。別プロセス+IPC全面再設計(ADR-015が一度却下した規模)が前提となる。真に必要になった時点(マーケットプレース等で未検証サードパーティプラグインの実運用が具体化)で再評価する(ADR-015/016/017共通の再評価条件)。**

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
| **U#21** (v2.1 新規) | **縦編集 (縦書き入力) の要否** — v2.0 は「4b8 (研究)」としたが研究も実装も行われなかった。DirectWrite の縦書きレイアウト対応が必要で実装コストが大きい | Phase 12 前 | 対象市場・ユーザー要望 |
| **U#22** (v2.1 新規) | **保存後の Piece Table 再構築と Undo 履歴の整合性** — 保存で Add Buffer 断片を Original 単一ピースへ畳んだ後、既存 `UndoStack` の `TextRange` と `BufferSnapshot` の `shared_ptr` 参照が有効か | Phase 8.5a | 実機検証 (probe) |
| **U#23** (v2.1 新規) | **保存失敗時の一時ファイル処理** — `ReplaceFileW` が他プロセスのロックで失敗した場合、一時ファイルを残すか消すか。データ保全とゴミファイルのトレードオフ | Phase 8.5a | 実機検証 |
| **U#24** (v2.1 新規) | **`SyntaxWorker` をタブごとに持つか共有するか** — 共有時はタブ切替で `resetIncrementalState=true` が必要。メモリ量とレスポンスのトレードオフ | Phase 8.5d | 計測 |
| **U#25** (v2.1 新規) | **折返し表示 (word wrap) の実装要否** — 8.5g の横スクロールと排他的な表示モード。実装すると `RenderPipeline` の行↔画面行マッピング全体に波及 | Phase 12 前 | ユーザー要望 |

---

## 23. 更新履歴

| 日付 | 版 | 変更 |
|---|---|---|
| 2026-07-19 | v1.0 | 初版発行 (Phase 5b1 完了後、Phase 4b8/5b2/5b3/6-12 の実装詳細を一気通貫で規定、16 章構成、1183 行) |
| 2026-07-19 | v2.0 | **Google/MS 責任者視点の徹底レビュー反映**。18 項目の構造的欠陥を全て改善。**23 章構成に拡張**: (§1) ペルソナ・競合ポジショニング・60 機能継承マトリクス新設、(§3-11) 各 Phase に秀丸/サクラ/MIFES 固有機能 (フリーカーソル・マーカー・桁位置ジャンプ・キーマクロ・秀丸互換 grep 結果ペイン等) を追加、(§7) ミニマップ・Breadcrumb・Sticky scroll・Indent guides・Semantic highlighting を統合、(§9) Copilot 型補完・RAG・エージェント・マルチモデル並列・ローカル LLM 対応、(§10) リアルタイムテール・分散トレース・統計ダッシュボード、(§11) LSP を Semantic tokens/Code lens/Inlay hints/Rename 等 15 機能に拡張、(§13) UI/UX に Zen mode・分割ビュー・タブグループ・Mica/Acrylic・HDR/VRR・タッチ/ペン、(§15) 世界最高速の裏付け技術要素章 (SIMD/GPU/Direct Storage/Frame pacing) 新規、(§16) 国際化・アクセシビリティ章 (CJK IME・RTL・grapheme cluster・UI Automation・WCAG 2.2) 新規、(§17) セキュリティ章 (サンドボックス・SBOM・脆弱性開示・データ暗号化) 新規、(§18) リリース・配布・自動更新章 (MSIX/Portable/差分更新/カナリア) 新規、(§19) KPI/SLO/テレメトリ章 (opt-in・プライバシー原則) 新規、(§20) エコシステム戦略章 (マーケットプレース・テーマ・スニペット・コミュニティ・ライセンス) 新規、(§21) 開発品質基盤章 (テストピラミッド・回帰検出・フィーチャーフラグ・クラッシュ収集・bisect・ドキュメント・シェル統合) 新規、(§22) リスク・未決事項 12 → 20 に拡張 |
| 2026-08-04 | **v2.1** | **中間レビュー ([`gap_analysis.md`](gap_analysis.md)) 反映。** v2.0 が「アプリケーションシェル」(ファイル保存・複数文書・設定・IME・ウィンドウクローム) にフェーズを一度も割り当てていなかった構造的欠陥を是正: (§0.2) `gap_analysis.md` を Plan-of-Record 補遺として位置づけ、次フェーズ選定時の必読文書に指定、(§1.5) 60機能マトリクスの「対応 Phase」欄に章番号 (`§13.5` 等) を書いていた 8 行を実 Phase 番号へ是正し、三大エディタ全てが備えるため「差分」として認識されず列挙漏れしていた 12 機能 (ファイル保存/開く/新規/別名保存/未保存警告/クラッシュ復旧/横スクロール/メニューバー/コンテキストメニュー/IME インライン変換/全選択/行操作) を追加、(§2) 全フェーズ俯瞰表を製品価値順へ再編 — **Phase 8.5 (アプリケーションシェル、P0) / 8.6 (製品化基盤、P1) / 12' (MVP 出荷判定) を新設**、Phase 9 (AI) を最後尾へ移動、Phase 10 (ログ解析) を前倒し、8g (AppContainer) と 7z (大規模文書 DoD) を凍結、(§8.5/§8.6) 新章を実装詳細付きで新設 (mmap 中ファイルへの上書き設計・`EditorSession`/`Workspace` 型・IME 実装項目・`HACCEL` 集約)、(§12.3) 出荷判定 22 項目の現時点達成状況 (4/22) を明記、(§12.4) Phase 12' チェックリストを新設、(§22) U#21〜U#25 を追加 |
