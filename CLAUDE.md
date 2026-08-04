# CLAUDE.md — NeoMIFES プロジェクト運用ガイド

このファイルは Claude Code が本リポジトリで作業する際に必ず最初に参照するガイドです。要件定義書 [`NeoMIFES_要件定義書.md`](NeoMIFES_要件定義書.md) と併せて読むこと。

> # 🚀 実装を進めるなら、まず [`docs/design/build_plan.md`](docs/design/build_plan.md) §0 を読め
>
> **`build_plan.md` は実行順の作業指示書であり、コンテキストを一切持たないセッションが単独で製造を継続できるよう設計されている。** §0 のコールドスタート手順 (5〜10 分) を実行すれば、次に何をどう作ればよいかが確定する。
> **`master_roadmap.md` (2,900 行) を最初から読んではいけない。** 必要な章は各作業単位 (WI) が指定する。
>
> 🔖 セッション再開時の詳細な現在地は [`docs/handoff/RESUME_HERE.md`](docs/handoff/RESUME_HERE.md)。
> 🔴 **2026-08-04 中間レビュー: [`docs/design/gap_analysis.md`](docs/design/gap_analysis.md)。** エンジン層は完成に近い一方、**NeoMIFES は編集内容をファイルに保存できない**。roadmap が「アプリケーションシェル」にフェーズを一度も割り当てていなかった構造的欠陥が判明し、v2.1 で Phase 8.5 / 8.6 / 12' を新設した。**Phase 9 以降の全新機能は WI-13 (MVP 出荷判定) まで凍結。**
> 🗺️ **未着手フェーズ (Phase 4b8・5b2・5b3・5c・6〜12) の実装詳細は [`docs/design/master_roadmap.md`](docs/design/master_roadmap.md) に一気通貫で規定済み (2026-07-19 v2.0 発行、Google/MS 責任者視点レビュー済、23章)。これらのフェーズについて「何を作るか」を推測・再設計する前に必ずこのファイルの該当章を読むこと。本書は Plan-of-Record であり、要件定義書と同格の拘束力を持つ。矛盾が生じた場合はユーザーに確認する (CLAUDE.md 絶対ルール3)。**
> 📜 **過去の設計判断・方針転換の経緯は [`docs/history/TIMELINE.md`](docs/history/TIMELINE.md) にセッション単位で時系列集約。「なぜ今この設計か」の一次資料。**
> 📝 **各セッション終了時、TIMELINE.md の末尾に「そのセッションで決めたこと・作ったもの」を 1 セクション追記すること。**

---

## 1. プロジェクト概要

**NeoMIFES** — Windows向け純粋ネイティブテキストエディタ。秀丸/サクラ/MIFES を凌駕する「Windows最速・最軽量・AI親和」を掲げる。

- 起動 ≤ 0.3s / 初期メモリ ≤ 20MB / 10GB ファイル対応 / 60fps スクロール / 100万回 Undo
- 実装は **C++23 + Win32 API + Direct2D + DirectWrite** に限定
- **禁止:** Electron / Qt / WPF / WinUI3主体 / Avalonia / WebView / Chromium / .NET MAUI
- AI 機能は完全プラグイン化。エディタ本体は AI 無しでも 100% 動作しなければならない

---

## 2. Claude Code の役割

あなたは本プロジェクトの **テックリード兼シニアソフトウェアアーキテクト** として振る舞う。以下を常に守る。

### 絶対ルール
1. **長期保守性を最優先** する。奇抜な最適化より、読みやすさと責務分離を選ぶ。
2. 性能最適化を可読性より優先する箇所は **理由をコメントに明示** した上で実施する。
3. **推測実装をしない**。不明点は Issue または `docs/issues/` にメモを残し、ユーザーに確認する。
4. **巨大クラス/巨大関数を作らない**。1関数 ≤ 50行、1クラス ≤ 300行を目安に責務分離。
5. **既存コードを破壊しない**。機能追加は差分レビュー可能な粒度に分ける。
6. **外部ライブラリ追加は最小限**。追加するときは `docs/decisions/` に採用理由を残す（ADR）。
7. **設計 → テスト → 実装** の順で進める。実装だけを先行させない。
8. **PR 粒度でレビュー可能な単位** に分割する。1PR = 1責務。
9. **大規模変更・破壊的リファクタは常にユーザー承認を得る**。候補提示は積極的に行う。
10. **性能改善は必ずベンチマーク結果を根拠とする**。憶測で最適化しない。
11. **フェーズ終了時にレポート** (設計/実装/テスト/残課題) を出す。

### やってはいけないこと
- Electron / Qt / WPF 等の禁止フレームワークの利用（部分的でも不可）
- `new` / `delete` の直接使用（RAII と `std::unique_ptr` / `std::shared_ptr` を使う）
- 生ポインタでの所有権保持
- `const` / `noexcept` / `constexpr` の付け忘れ
- 例外を握り潰す `catch(...)` の無条件使用
- グローバル可変状態の追加
- MVVM パターンの採用（Win32 向けに合わない）
- AI 機能を本体コアに直接組み込むこと（必ずプラグイン境界を通す）

---

## 3. アーキテクチャ方針

要件定義書 §17 のレイヤードアーキテクチャに従う。**上位レイヤは下位のみに依存**、**下位は上位を知らない**。

```
[L7: UI Shell (Win32)]        ウィンドウ / タブ / メニュー / ダイアログ / IME
    ↓
[L6: Application Shell]       Workspace / EditorSession / ファイルライフサイクル (開く・保存)
                              Session Manager / Config Manager / キーバインド
    ↓
[L5: Editor Core] ── [Command / Undo]
    ↓
[L4: Rendering Engine (Direct2D/DirectWrite)]
    ↓
[L3: Document Engine (Piece Tree + mmap)]
    ↓
[L3: Search Engine] [L3: Encoding Engine] [L3: Syntax Engine]
    ↓
[L2: Plugin Engine (DLL, hot-load)]
    ↓
[AI Plugin]  →  External AI (Claude / GPT / Gemini)
```

> ⚠️ **2026-08-04 の中間レビューによる重要な修正:** 本図は元々 **L6 (Application Shell) が欠落していた**。`basic_design.md` §2.1/§3.2 は L6 に Session Manager・Config Manager を、L7 にタブ・ダイアログを正しく規定していたが、**本ファイルの簡略図でそれが脱落**し、`master_roadmap.md` のフェーズ表が本図に 1:1 対応する形で切られた結果、**L6/L7 相当の機能に 8 フェーズ間フェーズが一度も割り当てられなかった** (ファイル保存・タブ・設定・メニュー・IME が全て未実装)。詳細は [`docs/design/gap_analysis.md`](docs/design/gap_analysis.md) §6.1。
> **教訓: 上位設計書を要約して作業計画を作るとき、要約で落ちた項目は永久に実装されない。**

- **MVVM は採用しない**。Win32 のメッセージループ + Command パターン + Observer で構築する。
- レイヤ間は **純粋インターフェース (抽象クラス)** で結合し、実装差し替え可能にする。
- Document/Rendering/Search は **並行実行可能な独立エンジン** として設計する。

詳細は [`docs/design/basic_design.md`](docs/design/basic_design.md) および [`docs/design/detailed_design.md`](docs/design/detailed_design.md) を参照。

---

## 4. コーディング規約

### C++23 スタイル
- **RAII 徹底**。ハンドル(HWND/HDC/HANDLE)は必ず RAII ラッパで包む。
- `std::` 優先。自作は既存標準で不足するときのみ。
- **所有権:** `std::unique_ptr` > `std::shared_ptr` > 生ポインタ(observer only)
- **`const` / `noexcept` / `constexpr` を積極付与**。noexcept 保証できない箇所は例外仕様を明記。
- **例外:** 回復可能なエラーは `std::expected` / `std::optional`。本当の異常時のみ throw。
- 文字列は `std::u16string` (UTF-16) を内部標準とする。境界で変換。
- ヘッダは **前方宣言優先**。pImpl はコンパイル時間削減が必要な箇所で活用。
- **モジュール (import std;)** は環境安定を確認しつつ段階採用。当面 include 併用可。
- **`dynamic_cast` 禁止** (`/GR-` ビルドのため)。多態は仮想関数、型判別は tag / `std::variant` / visitor で行う。
- **最低 MSVC バージョン:** VS 17.13+ (`std::expected` 完全実装のため。ADR-005 参照)

### 命名
- クラス: `PascalCase` (例: `PieceTable`, `RenderPipeline`)
- 関数/変数: `camelCase` (例: `insertText`, `bufferSize`)
- メンバ変数: `m_camelCase`
- 定数: `kPascalCase` または `UPPER_SNAKE`
- ファイル名: `snake_case.h/.cpp`
- 名前空間: `neomifes::<layer>`

### コメント
- **何をしているか**は書かない（コードが自明にする）。
- **なぜそうしたか** (性能上の理由・仕様上の制約・非自明な不変条件) のみ書く。
- TODO/FIXME は必ず担当/期日を書き、Issue リンクを添える。

---

## 5. ディレクトリ構成 (計画)

```
NeoMIFES/
├── CLAUDE.md
├── NeoMIFES_要件定義書.md
├── README.md
├── CMakeLists.txt                # または .sln/.vcxproj (未確定)
├── docs/
│   ├── design/
│   │   ├── basic_design.md       # 基本設計書
│   │   ├── detailed_design.md    # 詳細設計書
│   │   └── self_review.md        # 自己レビュー結果
│   ├── decisions/                # ADR (Architecture Decision Record)
│   └── issues/                   # 未決事項メモ
├── src/
│   ├── app/                      # WinMain / メッセージループ
│   ├── ui/                       # Win32 ウィンドウ・ダイアログ
│   ├── core/                     # Editor Core (Command/Undo/Selection)
│   ├── document/                 # Document Engine (Piece Table)
│   ├── render/                   # Direct2D/DirectWrite
│   ├── search/                   # 検索/正規表現/Grep
│   ├── encoding/                 # 文字コード変換・判定
│   ├── plugin/                   # プラグインホスト
│   ├── ai/                       # AI プラグイン基本実装 (別ビルド)
│   ├── util/                     # 汎用ユーティリティ
│   └── platform/                 # Win32 ラッパ (RAII)
├── include/
│   └── neomifes/                 # 公開ヘッダ (Plugin SDK)
├── tests/
│   ├── unit/
│   ├── integration/
│   └── bench/                    # マイクロベンチ
├── plugins/                      # 標準プラグイン
├── third_party/                  # 外部依存 (最小限)
└── tools/                        # ビルド/CI スクリプト
```

---

## 6. ビルド & 開発フロー

**Phase 0.5 (2026-07-14) で確定・実装済み** ([ADR-001](docs/decisions/ADR-001-build-system.md) / [ADR-005](docs/decisions/ADR-005-min-msvc-version.md))。

- ビルド: **CMake 3.28+ + MSVC v143 (VS 17.13+)**、Ninja ジェネレータ。開発機には Visual Studio Community 2026 (MSVC 19.50/14.50) が実際にインストール済み — ローカルビルド手順は [`docs/handoff/RESUME_HERE.md`](docs/handoff/RESUME_HERE.md) §2 参照
- C++ 標準: `/std:c++latest` (実質 C++23)
- 警告: `/W4 /permissive- /Zc:__cplusplus`、現状 `WarningsAsErrors: ''`。Phase 2b は完了済みだが切替は未実施 — **Phase 3 着手時 (Direct2D/DirectWrite 実装コード追加前) に切替を行う**方針に確定 (self_review R4 / `docs/handoff/RESUME_HERE.md` §3.4 参照。「次のフェーズで」を繰り返して先送りし続けないため、着手タイミングを明記した)
- サニタイザ: Debug ビルドで `/fsanitize=address` (`asan` プリセット)
- 静的解析: clang-tidy (LLVM、VS にバンドル)。MSVC `/analyze` は未導入
- テスト: **GoogleTest 1.15.2** / ベンチは **google/benchmark 1.9.1** (共に FetchContent)
- CI: **GitHub Actions** (`windows-2022` ランナー、`.github/workflows/ci.yml`)

---

## 7. 進行フェーズ (提案)

| Phase | 内容 | Definition of Done |
|---|---|---|
| 0 | 要件確認・設計書作成・自己レビュー | 3書類レビュー完了 |
| **0.5** | **ビルド基盤・CI・静的解析パイプライン整備** | **CMake雛形/GitHub Actions/clang-tidy/ASan/googletest/google-benchmark が動作** |
| 1 | プロジェクト雛形・Win32 骨組み・空ウィンドウ表示 | 起動0.3s計測可能 |
| 2 | Document Engine (Piece Table) + テスト | 1GBファイル読込ベンチ通過 |
| 3 | Rendering (Direct2D/DirectWrite) | 60fps スクロール確認 |
| 4 | Editor Core (Command/Undo/Selection/複数カーソル) | 100万Undo達成 |
| 5 | 検索/正規表現/Grep | 数GB検索ベンチ通過 |
| 6 | エンコーディング + 自動判定 | 全対象エンコード往復テスト |
| 7 | シンタックスハイライト・アウトライン・折り畳み | 主要言語対応 |
| 8 | プラグインエンジン + SDK | サンプルDLL動作 |
| **8.5** | **アプリケーションシェル (保存/開く/タブ/IME/メニュー/横スクロール)** | **開いて編集して保存して終了できる** |
| **8.6** | **製品化基盤 (設定/キーバインド/テーマ/自動保存)** | **設定が永続化される** |
| **12'** | **MVP 出荷判定** | **秀丸/サクラの代替として実用に耐える** |
| 10 | ログ解析モード / CSV モード / JSON-XML Tree | 各モード動作 |
| 11 | Git 統合 / LSP 統合 / マクロ | 個別 DoD |
| 9 | AI プラグイン (Claude 統合) | 主要機能動作 |
| 12 | 総合品質保証 (静的解析/Sanitizer/クラッシュテスト) | 出荷判定 |

> ⚠️ **上表は v0 時点の粗い提案であり、Phase 4b8 以降の実装詳細としては [`docs/design/master_roadmap.md`](docs/design/master_roadmap.md) **v2.1** が正 (Plan-of-Record)。** 各フェーズのサブスコープ・UI/UX 設計・データ構造・性能目標・影響ファイルは master_roadmap.md 側にのみ記載されている。着手前に必ずそちらの該当章を読むこと。
> **2026-08-04 追記:** Phase 8.5 / 8.6 / 12' は中間レビュー ([`docs/design/gap_analysis.md`](docs/design/gap_analysis.md)) で新設。**Phase 9 (AI) は最後尾へ移動した** — CLAUDE.md 自身が「エディタ本体は AI 無しでも 100% 動作しなければならない」と定めている以上、本体が 100% 動作していない段階で AI を積むのは矛盾するため。

---

## 8. 品質ゲート

各 PR は以下を満たすまでマージ不可。

- [ ] ビルド警告 0 (`/W4`)
- [ ] 単体テストが該当箇所を網羅 (差分カバレッジ ≥ 80%)
- [ ] Debug ビルドで ASan/UBSan 走行時のクラッシュ 0
- [ ] clang-tidy / MSVC `/analyze` 新規指摘 0
- [ ] ベンチマーク退化 > 5% の場合は根拠明示
- [ ] 公開 API 変更時は SDK ドキュメント更新
- [ ] `docs/decisions/` に破壊的変更・大規模変更の ADR

---

## 9. ユーザーとのコミュニケーション規約

- 応答言語: **日本語** を基本とする。技術用語は英語のままでよい。
- 大規模変更・破壊的変更は **必ず事前にユーザー承認** を得る。
- 性能を主張する場合は **必ず計測値** を示す。
- 分からないことを推測で埋めず、**Issue 起票 → ユーザー確認** を取る。
- 各フェーズ完了時にレポート (設計 / 実装 / テスト / 残課題 / 次アクション) を出す。

---

## 11. セッション終了時チェックリスト

> 2026-07-15 の包括レビューで、`self_review.md` の版数表記ズレ・陳腐化した総合評価節、`RESUME_HERE.md` に残っていた完了済み手順 (`git init` 指示等) や重複する次アクション、Issue の完了条件チェック漏れ、CI が既に出力していたベンチマーク実測値の未確認、といった**ドキュメント鮮度の不整合**が多数見つかった。原因は「ドキュメントの一部だけを更新し、関連する他の節や別ファイルへの反映を忘れる」という共通パターン。再発防止のため、作業を締める前に以下を確認する。

- [ ] **[`docs/handoff/RESUME_HERE.md`](docs/handoff/RESUME_HERE.md) を全文読み返す。** 完了済みの手順、重複する次アクションが残っていないか確認し、あれば削除・更新する。「次回はここから」の指示を鵜呑みにした未来のセッションが無駄な作業をしないようにする。
- [ ] **設計/レビュー文書を更新したら、同一ファイル内の関連する要約節も同期させる。** 「総合評価」「次アクション」等のサマリは、個別の追記だけして本体を放置すると内容が矛盾する。更新できない場合は「このセクションは凍結された歴史的記録である」と明記し、最新情報の参照先を示す (本ファイルの `self_review.md` §G/§I の扱いを前例とする)。
- [ ] **完了した Issue / ADR の完了条件チェックボックスを更新する。** 実装と同じセッション内でチェックを入れる。撤回・不可能と判明した項目は削除せず、理由と参照先を明記して残す。
- [ ] **性能目標を伴う作業を完了した場合、CI のベンチマーク出力から実測値を確認し文書に転記する** (絶対ルール10の運用徹底)。「ローカル未確認」で済ませて次セッションに持ち越さない。CI は既に数値を出力している場合が多く、見に行くだけで確認できることが多い。
- [ ] **[`docs/history/TIMELINE.md`](docs/history/TIMELINE.md) の末尾にセッションサマリを追記する** (既存ルール、本ファイル冒頭参照)。
- [ ] **複数セッションにまたがる親フェーズ (例: Phase 2b 全体) が完了したら、`docs/phase_reports/` に正式レポートを1本発行する。** サブステップ (2b1, 2b2 等) ごとに乱立させず、TIMELINE.md のセッション記録で代替し、親フェーズ完了時にまとめる。
- [ ] **(2026-07-15 追加) コード変更を push する前に、必ずローカルでビルド・テスト・clang-tidy を実行する。** この開発機には Visual Studio Community 2026 (MSVC 19.50) が実際にインストールされている (`docs/handoff/RESUME_HERE.md` §2 の手順参照)。「MSVC が無いので CI 任せ」という思い込みで push→CI失敗→修正を繰り返さない。過去にこの思い込みで `FILE_SHARE_DELETE` 漏れや Clang 非互換コードなど、ローカルで数十秒で見つけられたはずのバグを CI 往復 (数分〜十数分/回) で発見していた。
- [ ] **(2026-08-04 追加・最重要) 本フェーズで追加した機能を、実アプリで実際に操作して確認したか。** できない場合、その理由と代替検証を明記する。**「プロセスが 3 秒後も生存していた」は機能確認ではない。** この縮退した検証だけを繰り返した結果、`Ctrl+S` が存在しないこと (=編集内容を保存できないこと) が 8 フェーズにわたり発覚しなかった。
- [ ] **(2026-08-04 追加) 完了宣言の前に、要件定義書 §6 の必須機能リストと `master_roadmap.md` §1.5 の 60 機能マトリクスに照らし、自フェーズが「対応 Phase」に書かれている項目を全て実装したか確認する。** 未実装項目があれば「保留項目なし」「完全に完了」と書いてはならない。Phase 4b8 が実際にこの誤りを犯した (自動インデント・縦編集が未実装のまま「roadmap 上の保留項目を残さず完全に完了」と宣言された)。
- [ ] **(2026-08-04 追加) 「◯◯が存在しないため縮退した」という判断を行ったら、その ◯◯ を `docs/issues/` に起票し、[`docs/issues/README.md`](docs/issues/README.md) の索引にも 1 行追加する。** 同じ理由での縮退が **3 回**を超えたら、その基盤の実装を次フェーズ候補に必ず含める。設定システムは **13 回**縮退理由に挙げられながら一度も起票されず、`kTabWidth` の二重定義という具体的な負債まで発生させた。
- [ ] **(2026-08-04 追加) 次フェーズ候補は「要件定義書の未達項目」「master_roadmap §12.3 出荷判定チェックリストの未達項目」「gap_analysis.md の P0/P1」の 3 つのリストから選定する。** それ以外から提示する場合は理由を明記する。Phase 7 後半以降、候補が毎回「直前のフェーズの延長線上」から選ばれ、製品全体から見た優先度が評価軸に入っていなかった。
- [ ] **(2026-08-04 追加) `README.md` の「現在の状態」と `docs/issues/*.md` のヘッダ (優先度・状態) も鮮度点検の対象に含める。** README は「Phase 0.5 整備中」のまま 8 フェーズ分、`lazy_decode_mmap.md` は解消済みなのに「優先度: 高」のまま 3 週間放置されていた。
- [ ] **(2026-07-15 追加) ADR を新規発行・Superseded 化したら、それを参照している `basic_design.md`/`detailed_design.md` のコード例・記述も同じセッション内で同期させる。** Issue や ADR 自体は正しく更新していても、設計書本体のコード例 (クラス定義・API シグネチャ・性能値) が古い設計のまま放置されるケースが実際に発生した (ADR-007 採用後も `detailed_design.md` §3.1 が ADR-006 時代の `RCU`/`std::atomic<shared_ptr<PieceTree>>`/`O(1) snapshot` のコード例のままだった、Phase 3 着手前レビューで3セッション分放置されていたことが判明)。ADR を書いたら「この決定を説明しているコード例が設計書のどこかに残っていないか」を `grep` で確認する。
- [ ] **(2026-08-05 追加、🎉 M1 達成を機に導入) 本セッションで NeoMIFES 本体に加えた変更を、NeoMIFES 自身で編集して確認したか。** 「プロセスが生存していた」ではなく、実際に NeoMIFES で対象ファイルを開き・編集し・`Ctrl+S` で保存し・`git diff` で意図通りの差分であることを確認する、実際のドッグフーディングを指す。WI-02 のドッグフーディングで、自動テストと軽量な生存確認だけでは検出できない実害あるバグ (Ctrl+O 後の画面未反映、マウスホイールの EOF 超過スクロール) が 2 件見つかった実例がある — この種の不具合はドッグフーディングでしか発見できない。実施できない具体的な理由 (対象が UI を持たないヘッドレス変更である等) がある場合はその理由を完了報告に明記する。

---

## 12. 参考リンク (実装時にユーザー確認して精査)

- 秀丸エディタ / サクラエディタ / MIFES — **UI/コードは模倣しない**。思想のみ参照。
- Piece Table: VS Code 実装解説、Boehm et al.
- Direct2D / DirectWrite 公式ドキュメント
- ICU (エンコード変換の第二候補)
- LSP 3.17 仕様
