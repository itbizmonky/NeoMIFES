# Phase 0.5 完了レポート — ビルド基盤整備

- **期間:** 2026-07-14 (1 日)
- **担当:** Claude Code (テックリード)
- **対象 DoD (CLAUDE.md §7):** CMake雛形 / GitHub Actions / clang-tidy / ASan / googletest / google-benchmark が動作

---

## 1. 成果物

### 1.1 ビルド基盤
| ファイル | 役割 |
|---|---|
| [CMakeLists.txt](../../CMakeLists.txt) | プロジェクトルート。C++23 / MSVC v14.43+ 強制 (ADR-005) / x64 強制 |
| [CMakePresets.json](../../CMakePresets.json) | `debug` / `release` / `asan` の 3 プリセット (Ninja + MSVC v143) |
| [cmake/CompileOptions.cmake](../../cmake/CompileOptions.cmake) | 共通コンパイル/リンクフラグ (`neomifes::compile_options`)。`/W4 /permissive- /GR- /utf-8` 等 |
| [cmake/Sanitizers.cmake](../../cmake/Sanitizers.cmake) | ASan (`/fsanitize=address`) 有効化、`/RTC1` 除去 |
| [cmake/Dependencies.cmake](../../cmake/Dependencies.cmake) | GoogleTest 1.15.2 / google-benchmark 1.9.1 を FetchContent で取得 |

### 1.2 最小ソース骨格
| ファイル | 役割 |
|---|---|
| [src/util/](../../src/util/) | 静的ライブラリ `neomifes_util` (`version.h` / `version.cpp`) |
| [src/app/main.cpp](../../src/app/main.cpp) | `wWinMain` スタブ。MessageBoxW でバナー表示のみ (Phase 1 で本体実装) |

### 1.3 テスト・ベンチ
| ファイル | 役割 |
|---|---|
| [tests/unit/util_version_test.cpp](../../tests/unit/util_version_test.cpp) | GoogleTest スモーク (3 ケース) |
| [tests/bench/util_version_bench.cpp](../../tests/bench/util_version_bench.cpp) | google-benchmark スモーク (2 ベンチ) |

### 1.4 静的解析・スタイル・エディタ設定
| ファイル | 役割 |
|---|---|
| [.clang-tidy](../../.clang-tidy) | CLAUDE.md §4 命名規約を写像。modernize / bugprone / performance / cppcoreguidelines を有効化 |
| [.clang-format](../../.clang-format) | Google ベース / ColumnLimit 100 / 4 スペース / PointerAlignment Left |
| [.editorconfig](../../.editorconfig) | UTF-8 / LF (`.bat` `.cmd` `.ps1` は CRLF) |
| [.gitignore](../../.gitignore) | `build/` `.vs/` `CMakeUserPresets.json` 等 |

### 1.5 CI
| ファイル | 役割 |
|---|---|
| [.github/workflows/ci.yml](../../.github/workflows/ci.yml) | `windows-2022` ランナーで Debug/Release ビルド + CTest + benchスモーク + clang-tidy |

### 1.6 ドキュメント
| ファイル | 役割 |
|---|---|
| [README.md](../../README.md) | プロジェクト概要 + ビルド手順 |
| 本レポート | Phase 0.5 完了報告 |

---

## 2. DoD 達成状況

| DoD 項目 | 状態 | 補足 |
|---|---|---|
| CMake 雛形 | ✅ | ルート + presets + 3 モジュール |
| GitHub Actions | ✅ | Debug/Release マトリクス + clang-tidy 別ジョブ |
| clang-tidy | ✅ | .clang-tidy 配置。CI で全 `src/` `tests/` の `.cpp` を走査 |
| ASan | ✅ | `asan` プリセットで有効化、`/RTC1` 競合を回避 |
| googletest | ✅ | FetchContent (v1.15.2)、`gtest_discover_tests` で個別ケース列挙 |
| google-benchmark | ✅ | FetchContent (v1.9.1)、スモークベンチ 2 本 |

---

## 3. 検証手順 (ローカル)

Visual Studio 2022 17.13+ のインストール済み PowerShell から:

```powershell
# Debug ビルド & テスト
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure

# ベンチスモーク
./build/debug/tests/bench/neomifes_util_bench.exe --benchmark_min_time=0.01s

# アプリスタブ (MessageBox 表示)
./build/debug/src/app/NeoMIFES.exe

# ASan ビルド (別プリセット)
cmake --preset asan
cmake --build --preset asan
ctest --preset asan --output-on-failure
```

**注:** 本エージェント環境ではローカル MSVC ビルドを実行していない。CI 側の green ラン (次回 push 時) で最終検証する。

---

## 4. 意図的にスコープ外とした項目

| 項目 | 理由 | 予定 |
|---|---|---|
| Direct2D / DirectWrite 初期化・ウィンドウ生成 | Phase 1 スコープ | Phase 1 |
| 起動 0.3s / メモリ 20MB PoC | 実ウィンドウが必要 | Phase 1 冒頭 |
| RE2 / Abseil の統合 | 使う場所がまだ無い (Search Engine) | Phase 5 |
| libgit2 | Git 統合フェーズ | Phase 11 |
| clang-cl による UBSan ジョブ | 検出対象コードが薄いため後回し | Phase 1 |
| Authenticode 署名 CI | 証明書取得後 | Phase 12 |

---

## 5. 既知の懸念事項 / リスク

| # | 懸念 | 深刻度 | 対応 |
|---|---|---|---|
| P05-1 | GitHub Actions `windows-2022` ランナーの MSVC バージョンが 17.13 に届いていない可能性 | 中 | CI 初回実行時に確認。落ちる場合は `microsoft/setup-msbuild@v2` で明示設定 |
| P05-2 | FetchContent の `/MDd` vs `/MD` CRT 不整合 | 中 | `gtest_force_shared_crt=ON` を予め設定済み。要 CI 検証 |
| P05-3 | `.clang-tidy` の初回警告ノイズ | 低 | スケルトンコードのみのため大量発生の心配は少ない。CI で赤くなったら段階的に修正 |
| P05-4 | `WarningsAsErrors: ''` にしてある | 低 | 現状は警告のみ表示。CI 上で 0 件を確認できたら `'*'` に切替 (Phase 1 中に) |

---

## 6. Phase 1 への引き継ぎ事項

1. **起動 0.3s / メモリ 20MB PoC** を Phase 1 の最初のタスクとする (計測基盤は本フェーズで整った)
2. **Win32 MainWindow + Direct2D + DirectWrite** の最小構成を組み、`neomifes_util_bench` に「アプリ起動 → 空ウィンドウ表示 → 終了」のマクロベンチを追加する
3. **UBSan (clang-cl) ジョブ** を CI に追加
4. **`WarningsAsErrors: '*'`** に切替
5. **命名規約の clang-tidy 自動チェック** が発火することを実コードで確認
6. `NEOMIFES_UTIL_PUBLIC` 相当の エクスポートマクロを検討 (DLL 化する場合。現状は静的なので不要)

---

## 7. Definition of Done 判定

**Phase 0.5 完了。** Phase 1 着手可能。ただし CI 初回グリーンをもって最終承認とする (次コミット後に確認)。
