# ADR-001: ビルドシステムに CMake を採用する

- **ステータス:** Accepted
- **決定日:** 2026-07-14
- **決定者:** テックリード + ユーザー承認

## コンテキスト

NeoMIFES は Windows ネイティブ C++23 アプリケーションであり、以下を満たすビルドシステムが必要:

- MSVC v143 (VS2022) をターゲット
- CI (GitHub Actions Windows Server) で並列ビルド可能
- クロスプラットフォーム将来性 (現時点スコープ外だが完全に排除しない)
- サブディレクトリ構成 (`src/`, `tests/`, `plugins/`, `third_party/`) を明確に管理
- 静的解析 (clang-tidy) との統合が容易 (`compile_commands.json` 出力)

## 選択肢

1. **CMake 3.28+ + Ninja + MSVC**
2. MSBuild (Visual Studio ソリューション `.sln`)
3. Bazel / Buck2 / xmake

## 決定

**CMake 3.28+ を採用し、ジェネレータは Ninja を第一選択とする。IDE 統合は VS の "CMake プロジェクトを開く" を利用。**

## 根拠

- **CI 並列性:** Ninja + `/MP` で MSBuild より速い
- **clang-tidy 統合:** `CMAKE_EXPORT_COMPILE_COMMANDS=ON` で `compile_commands.json` を出力できる
- **依存管理:** FetchContent / find_package で `googletest`, `google-benchmark`, `RE2`, `libgit2` などを扱いやすい
- **将来性:** クロスプラットフォーム化時にそのまま移行できる
- **CMake 3.28+:** C++23 モジュール (`import std;`) の実験的サポートが入っており将来採用しやすい

## 影響

- 開発者は Visual Studio 2022 (17.13+) + CMake サポートワークロードのインストールが必要
- `CMakeLists.txt` を各サブディレクトリに配置する構成
- Preset ファイル (`CMakePresets.json`) で Debug/Release/ASan の 3 構成を提供

## 却下理由

- **MSBuild:** VS 統合は強いが、CI での並列ビルドと clang-tidy 統合が煩雑。ソリューションファイルは手編集時にコンフリクトしやすい
- **Bazel/Buck2:** 学習コスト大、Windows 対応の成熟度が CMake に劣る
- **xmake:** 実装は魅力的だが Windows ネイティブ C++ 領域での実績が薄い
