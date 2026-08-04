# NeoMIFES

Windows 向け純粋ネイティブテキストエディタ。C++23 + Win32 API + Direct2D/DirectWrite で実装し、「Windows 最速・最軽量・AI 親和」を目指す。

秀丸エディタ / サクラエディタ / MIFES の思想を継承しつつ、10GB ファイル対応・22 言語のシンタックスハイライト・ミニマップ・コマンドパレットなど、モダンエディタの体験を純ネイティブで実現する。

---

## ⚠️ 現在の状態 (2026-08-05)

**開発中。まだ実用に耐えません — 保存基盤は実装済みだが、ドッグフーディングで発覚した2件のバグ修正がコミット待ちで、🎉 M1 (NeoMIFES を NeoMIFES で編集できる) は未達成です。**

| 領域 | 状態 |
|---|---|
| **エンジン層** (Document / Rendering / Search / Encoding / Syntax / Plugin) | ✅ **ほぼ完成** — 起動 148ms / 60fps / 10GB mmap / 100万 Undo / 22 言語ハイライト / RE2 検索 を実測達成 |
| **アプリケーションシェル** (保存 / タブ / メニュー / 設定 / IME) | 🟡 **着手中** — 保存基盤・ファイルライフサイクル UI (Ctrl+S/O/N/D&D) は実装済み (コミット待ち)。タブ/メニュー/設定/IME は未着手 |

2026-08-04 の中間レビューにより、ロードマップが「アプリケーションシェル」にフェーズを一度も割り当てていなかったことが判明しました。
経緯・是正計画: **[商用化ギャップ分析 (`gap_analysis.md`)](docs/design/gap_analysis.md)**

**次のマイルストーン: 🎉 M1 (NeoMIFES で NeoMIFES を編集できる)** — 実装は完了、ドッグフーディングで発覚した2件のバグ ([`RESUME_HERE.md` §3.69](docs/handoff/RESUME_HERE.md)) を修正済み、ユーザーによる再確認待ち。

### 現時点で動くもの

- ファイルを開く (起動時 `--open <path>` / `Ctrl+O` / ドラッグ&ドロップ)、閲覧、編集、Undo/Redo (100万件)
- ファイルの保存 (`Ctrl+S`) / 別名保存 (`Ctrl+Shift+S`) / 新規文書 (`Ctrl+N`)、未保存時の警告ダイアログ
- シンタックスハイライト **22 言語** (C / C++ / Python / JavaScript / TypeScript / TSX / Java / Go / Rust / JSON / HTML / CSS / Shell / YAML / TOML / XML / PHP / Markdown / PowerShell / INI / Batch / SQL)
- 検索 (`Ctrl+F`) / 置換 (`Ctrl+H`) / Grep (`Ctrl+Shift+F`) / タグジャンプ (`F12`) / 桁ジャンプ (`Ctrl+G`)
- コマンドパレット (`Ctrl+Shift+P`) / アウトラインパネル (`Ctrl+Shift+O`)
- 複数カーソル / 矩形選択 / ブックマーク / 折り畳み / ミニマップ / Breadcrumb / Sticky scroll / Indent guides
- 文字コード自動判定 (UTF-8/16/32, Shift-JIS, EUC-JP, ISO-2022-JP) と行末コード判定
- プラグインエンジン (C ABI DLL、SEH クラッシュ隔離、権限モデル、Job Object サンドボックス)

### 現時点で動かないもの (主なもの)

**タブ (複数ファイル)** / メニューバー / ステータスバー / 行番号表示 / **横スクロール** / **日本語 IME のインライン変換** / 設定 / テーマ切替 / 全選択 (`Ctrl+A`) / 自動インデント / 自動保存

---

## ドキュメント

**まずここから:**
- 🚀 [**製造全体計画 (`build_plan.md`)**](docs/design/build_plan.md) — **実行順の作業指示書。開発を進めるならここから**
- [**商用化ギャップ分析 (`gap_analysis.md`)**](docs/design/gap_analysis.md) — 中間レビュー結果、P0/P1 ギャップ
- [セッション再開ガイド (`RESUME_HERE.md`)](docs/handoff/RESUME_HERE.md) — これまでの経緯の詳細記録

**計画:**
- [要件定義書](NeoMIFES_要件定義書.md) — 何を作るか (v1.0 凍結)
- [マスターロードマップ v2.1](docs/design/master_roadmap.md) — 各フェーズで何をどう作るか (Plan-of-Record、23 章)
- [プロジェクト運用ガイド (CLAUDE.md)](CLAUDE.md) — 開発規約・絶対ルール

**設計:**
- [基本設計書](docs/design/basic_design.md) — レイヤ構成
- [詳細設計書](docs/design/detailed_design.md) — 実装済み機能のリファレンス
- [Architecture Decision Records](docs/decisions/README.md) — 21 本の技術判断記録
- [Issue 索引](docs/issues/README.md) — 未解決の技術的負債
- [開発タイムライン](docs/history/TIMELINE.md) — セッション単位の時系列記録
- [フェーズレポート](docs/phase_reports/)

---

## ビルド

**前提:** Visual Studio 2022 17.13 以上 (MSVC v143) + CMake 3.28 以上 + Ninja。

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

`release` / `ubsan` (clang-cl + UBSan) / `asan` プリセットも用意しています。全 966 テストが 3 構成で green であることが変更のマージ条件です。

外部依存 (tree-sitter 22 文法 / RE2 / Abseil / GoogleTest / google-benchmark / nlohmann-json) は CMake FetchContent で自動取得されます。

---

## アーキテクチャ

```
[UI Shell (Win32)]
    ↓
[Editor Core] ── [Command / Undo]
    ↓
[Rendering Engine (Direct2D/DirectWrite)]
    ↓
[Document Engine (Piece Tree + mmap + Lazy Decode)]
    ↓
[Search Engine (RE2)] [Encoding Engine]
    ↓
[Plugin Engine (C ABI DLL, SEH 隔離)]
    ↓
[AI Plugin]  →  External AI
```

**禁止事項:** Electron / Qt / WPF / WinUI3 主体 / Avalonia / WebView / Chromium / .NET MAUI。実装は C++23 + Win32 + Direct2D/DirectWrite に限定します。
AI 機能は完全プラグイン化し、**エディタ本体は AI 無しでも 100% 動作します。**

---

## ライセンス

[MIT License](LICENSE) — 商用/改変/再配布いずれも自由。詳細は `LICENSE` ファイル参照。

同梱する第三者コードのライセンスは各 `third_party/` サブディレクトリの `LICENSE` / `NOTICE.md` を参照してください。
