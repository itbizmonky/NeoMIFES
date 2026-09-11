# NeoMIFES

Windows 向け純粋ネイティブテキストエディタ。C++23 + Win32 API + Direct2D/DirectWrite で実装し、「Windows 最速・最軽量・AI 親和」を目指す。

秀丸エディタ / サクラエディタ / MIFES の思想を継承しつつ、10GB ファイル対応・22 言語のシンタックスハイライト・ミニマップ・コマンドパレットなど、モダンエディタの体験を純ネイティブで実現する。

---

## 🧊 プロジェクト凍結中 (2026-09-11)

**本プロジェクトはユーザーの判断により開発を凍結している。完全終了ではなく、条件が整えば再開する一時停止。**

当初の差別化構想だった「AI 親和」(Phase 9) は、GitHub Copilot / Cursor / Windsurf / Continue.dev 等の専門企業によるコモディティ化で個人開発が勝負できる領域ではないと判断した。AI 機能抜きで秀丸エディタ / MIFES という既存の枯れたエディタと同じ土俵(通常のテキストエディタとしての完成度)で比較すると、凍結時点で明確な優位性は無い。

**経緯・凍結理由・2ヶ月間 (400コミット) の到達点・再開条件・再開時の技術的手順の全ては [`docs/phase_reports/project_freeze_2026-09-11.md`](docs/phase_reports/project_freeze_2026-09-11.md) に集約されている。** コード・ドキュメント・issue は全て凍結時点のまま保存されており、削除の予定は無い。

---

## 現在の状態 (2026-09-11 凍結時点)

**🎉 M1〜M5 達成、v1 出荷判定完了。** 2026-07-15 の開発着手から約2ヶ月・400コミットで、エンジン層からアプリケーションシェル・差別化モードまでの一通りの機能を実装した。詳細な機能追加の経緯は [`docs/history/TIMELINE.md`](docs/history/TIMELINE.md) を参照。

| 領域 | 状態 |
|---|---|
| **エンジン層** (Document / Rendering / Search / Encoding / Syntax / Plugin) | ✅ 完成 — 起動 29.3ms / 60fps スクロール / 10GB mmap 対応 (Private 1.22GB) / 100万 Undo / 22 言語ハイライト / RE2 検索 を実測達成 |
| **アプリケーションシェル** (保存 / タブ / メニュー / 設定 / IME / 複数ウィンドウ) | ✅ 完成 — Ctrl+S/O/N・タブ UI・メニューバー・設定 (`settings.json`)・IME 完全対応・テーマ 3 種・キーバインド 4 プリセット・複数ウィンドウ |
| **差別化モード** (ログ解析 / CSV / JSON-XML Tree / Git 統合) | ✅ 完成 — CSV グリッド編集・列固定、JSON/XML Tree + XPath、Git ステータス/diff ペイン |
| **AI プラグイン** (Phase 9) | 🧊 意図的凍結 — 詳細設計は `docs/design/master_roadmap.md` §9 に保存済み。プラグインホスト (L2層) 自体は実装・テスト済み |
| **LSP 完全実装 / マクロ** | 🧊 意図的凍結 (2026-08-23 合意によるスコープ確定) |

### 現時点で動くもの

- ファイルを開く (起動時 `--open <path>` / `Ctrl+O` / ドラッグ&ドロップ)、閲覧、編集、Undo/Redo (100万件)
- ファイルの保存 (`Ctrl+S`) / 別名保存 (`Ctrl+Shift+S`) / 新規文書 (`Ctrl+N`) / 新規ウィンドウ (`Ctrl+Shift+N`)、未保存時の警告ダイアログ、自動保存・クラッシュ復旧
- タブ UI (複数ファイル)・メニューバー・ステータスバー・行番号ガター・右クリックメニュー・複数ウィンドウ
- 日本語 IME 完全対応 (インライン変換・候補ウィンドウ追従・下線描画)
- 設定 (`%APPDATA%\NeoMIFES\settings.json`)・テーマ (ダーク/ライト/ハイコントラスト)・キーバインド 4 プリセット (neomifes/hidemaru/sakura/vscode)
- シンタックスハイライト **22 言語** (C / C++ / Python / JavaScript / TypeScript / TSX / Java / Go / Rust / JSON / HTML / CSS / Shell / YAML / TOML / XML / PHP / Markdown / PowerShell / INI / Batch / SQL)
- 検索 (`Ctrl+F`) / 置換 (`Ctrl+H`) / Grep (`Ctrl+Shift+F`) / タグジャンプ (`F12`) / 桁ジャンプ (`Ctrl+G`)、CRLF 対応
- コマンドパレット (`Ctrl+Shift+P`) / アウトラインパネル (`Ctrl+Shift+O`)
- 複数カーソル / 矩形選択 (列編集) / ブックマーク / 折り畳み / ミニマップ / Breadcrumb / Sticky scroll / Indent guides / 折り返し (word wrap)
- 縦横スクロール (ネイティブスクロールバー・マウスホイール、設定で ON/OFF 可)
- 文字コード自動判定 (UTF-8/16/32, Shift-JIS, EUC-JP, ISO-2022-JP) と行末コード判定
- ログ解析モード / CSV モード (グリッド編集・フィルタ・列固定) / JSON・XML Tree モード (XPath)
- Git 統合 (ステータス・Git ペイン・Diff ビュー)
- プラグインエンジン (C ABI DLL、SEH クラッシュ隔離、権限モデル、Job Object サンドボックス)
- 簡易アクセシビリティ (UI Automation 経由の読み上げ対応)
- 10GB ファイル対応 (mmap + Lazy Decode、全モード共通)

### 現時点で動かないもの・意図的に対象外としたもの

- **AI 機能全般** (Copilot 型補完・チャット・エージェント・RAG) — Phase 9、意図的凍結中(本プロジェクト自体の凍結理由)
- LSP 完全実装・マクロ (Lua/JS/秀丸互換)・Git の高度機能 (Blame/Commit/Branch/3-Way Merge) — 意図的凍結中
- 本物の Authenticode 証明書による署名配布 — 自己署名証明書までは実装済み、証明書購入はユーザー判断待ち
- CSV モードの 10GB 規模での根本的なメモリ最適化 — 対象外確定(部分対応まで実施済み)

---

## ドキュメント

**まずここから:**
- 🧊 [**プロジェクト凍結レポート (`project_freeze_2026-09-11.md`)**](docs/phase_reports/project_freeze_2026-09-11.md) — 凍結の経緯・到達点・再開条件・再開手順
- 🚀 [**製造全体計画 (`build_plan.md`)**](docs/design/build_plan.md) — 実行順の作業指示書、全 WI (作業単位) の詳細記録
- [セッション再開ガイド (`RESUME_HERE.md`)](docs/handoff/RESUME_HERE.md) — これまでの経緯の詳細記録

**計画:**
- [要件定義書](NeoMIFES_要件定義書.md) — 何を作るか (v1.0 凍結)
- [マスターロードマップ v2.1](docs/design/master_roadmap.md) — 各フェーズで何をどう作るか (Plan-of-Record、23 章。Phase 9 の AI プラグイン詳細設計を含む)
- [商用化ギャップ分析 (`gap_analysis.md`)](docs/design/gap_analysis.md) — 2026-08-04 中間レビュー結果、P0/P1 ギャップ
- [プロジェクト運用ガイド (CLAUDE.md)](CLAUDE.md) — 開発規約・絶対ルール

**設計:**
- [基本設計書](docs/design/basic_design.md) — レイヤ構成
- [詳細設計書](docs/design/detailed_design.md) — 実装済み機能のリファレンス
- [Architecture Decision Records](docs/decisions/README.md) — 22 本の技術判断記録
- [Issue 索引](docs/issues/README.md) — 未解決の技術的負債・解決済み issue の経緯
- [開発タイムライン](docs/history/TIMELINE.md) — セッション単位の時系列記録 (一次資料)
- [フェーズレポート](docs/phase_reports/)

---

## ビルド

**前提:** Visual Studio 2022 17.13 以上 (MSVC v143) + CMake 3.28 以上 + Ninja。

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

`release` / `asan` / `ubsan` (clang-cl + UBSan) プリセットも用意している。凍結時点で単体+統合テスト **1621 件**が全 4 構成 (Debug/Release/ASan/UBSan) で green。CI (GitHub Actions) はこの 4 構成 + clang-tidy の計 5 ジョブを並列実行する。

外部依存 (tree-sitter 22 文法 / RE2 / Abseil / GoogleTest / google-benchmark / nlohmann-json) は CMake FetchContent で自動取得される。

---

## アーキテクチャ

```
[L7: UI Shell (Win32)]        ウィンドウ / タブ / メニュー / ダイアログ / IME
    ↓
[L6: Application Shell]       Workspace / EditorSession / ファイルライフサイクル
                              Session Manager / Config Manager / キーバインド
    ↓
[L5: Editor Core] ── [Command / Undo]
    ↓
[L4: Rendering Engine (Direct2D/DirectWrite)]
    ↓
[L3: Document Engine (Piece Tree + mmap + Lazy Decode)]
    ↓
[L3: Search Engine (RE2)] [L3: Encoding Engine] [L3: Syntax Engine (tree-sitter)]
    ↓
[L2: Plugin Engine (C ABI DLL, SEH 隔離)]
    ↓
[AI Plugin]  →  External AI    (Phase 9、意図的凍結中)
```

**禁止事項:** Electron / Qt / WPF / WinUI3 主体 / Avalonia / WebView / Chromium / .NET MAUI。実装は C++23 + Win32 + Direct2D/DirectWrite に限定する。
AI 機能は完全プラグイン化し、**エディタ本体は AI 無しでも 100% 動作する。**

---

## ライセンス

[MIT License](LICENSE) — 商用/改変/再配布いずれも自由。詳細は `LICENSE` ファイル参照。

同梱する第三者コードのライセンスは各 `third_party/` サブディレクトリの `LICENSE` / `NOTICE.md` を参照してください。
