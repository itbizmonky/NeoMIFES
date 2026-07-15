# CLAUDE.md — NeoMIFES プロジェクト運用ガイド

このファイルは Claude Code が本リポジトリで作業する際に必ず最初に参照するガイドです。要件定義書 [`NeoMIFES_要件定義書.md`](NeoMIFES_要件定義書.md) と併せて読むこと。

> 🔖 **セッション再開時は先に [`docs/handoff/RESUME_HERE.md`](docs/handoff/RESUME_HERE.md) を読み、現在のフェーズと未検証の宿題を把握すること。**
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
[UI Shell (Win32)]
    ↓
[Editor Core] ── [Command / Undo]
    ↓
[Rendering Engine (Direct2D/DirectWrite)]
    ↓
[Document Engine (Piece Table / Rope)]
    ↓
[Search Engine] [Encoding Engine]
    ↓
[Plugin Engine (DLL, hot-load)]
    ↓
[AI Plugin]  →  External AI (Claude / GPT / Gemini)
```

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

未確定事項が多いため、現時点では以下を **想定** として明記。着手時にユーザー確認する。

- ビルド: **CMake + MSVC (v143)** を第一候補。Ninja ジェネレータで高速化。
- C++ 標準: `/std:c++latest` (実質 C++23)
- 警告: `/W4 /permissive- /Zc:__cplusplus`
- サニタイザ: Debug ビルドで `/fsanitize=address`
- 静的解析: MSVC `/analyze` + clang-tidy
- テスト: **GoogleTest** (第一候補) / ベンチは **google/benchmark**
- CI: **GitHub Actions** (Windows Server ランナー)

> ⚠ ビルドシステム最終決定は **Phase 1 着手前にユーザー承認** を得ること。

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
| 9 | AI プラグイン (Claude 統合) | 主要機能動作 |
| 10 | ログ解析モード / CSV モード / JSON-XML Tree | 各モード動作 |
| 11 | Git 統合 / LSP 統合 / マクロ | 個別 DoD |
| 12 | 総合品質保証 (静的解析/Sanitizer/クラッシュテスト) | 出荷判定 |

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

## 10. 参考リンク (実装時にユーザー確認して精査)

- 秀丸エディタ / サクラエディタ / MIFES — **UI/コードは模倣しない**。思想のみ参照。
- Piece Table: VS Code 実装解説、Boehm et al.
- Direct2D / DirectWrite 公式ドキュメント
- ICU (エンコード変換の第二候補)
- LSP 3.17 仕様
