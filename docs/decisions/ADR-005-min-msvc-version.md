# ADR-005: 最低 MSVC バージョンを Visual Studio 2022 17.13 以上とする

- **ステータス:** Accepted
- **決定日:** 2026-07-14

## コンテキスト

CLAUDE.md および詳細設計書で以下の C++23 機能を利用する方針:

- `std::expected<T, E>` (エラー処理)
- `std::print` / `std::println`
- `std::span<T>` (C++20、既に安定)
- 名前空間フォーマット (`std::formatter` 特殊化)
- `import std;` (将来採用検討)

これらの完全実装状況を踏まえ、CI と開発環境の最低バージョンを確定する必要がある。

## 決定

**最低 Visual Studio 2022 17.13 以上 (MSVC v143 の最新点)**

## 根拠

- **`std::expected` 完全実装:** Visual Studio 17.13 で完了 (それ以前は不完全 / 空実装だった時期あり)
- **`std::print` / `std::println`:** 17.9+ で利用可
- **C++23 `if consteval`, deducing `this` など:** 17.8+
- **`import std;`:** 17.5+ で experimental、17.13+ で実用レベル (将来的採用時に問題ない)
- **CI (`windows-2022` ランナー):** 定期的に更新されており 17.13+ の追随に問題なし
- **利用者影響なし:** 本ソフトはコンパイル済みバイナリ配布なので、エンドユーザーには影響しない (開発者のみが対象)

## 影響

- 開発者は VS 2022 17.13 以上を導入する必要がある (17.10 以下は不可)
- CI ワークフローで `msvc-toolset` バージョンを固定する
- `_MSC_VER` を CMake で検査し、下回るバージョンでビルド前エラー

```cmake
if (MSVC AND MSVC_VERSION LESS 1943)   # 17.13 = 1943
  message(FATAL_ERROR "NeoMIFES requires MSVC v14.43 (VS 17.13) or later.")
endif()
```

## 将来の再評価

- 17.14+ で新機能 (パターンマッチング等) が入れば、必要に応じて底上げする
- C++26 移行のタイミングで再検討
