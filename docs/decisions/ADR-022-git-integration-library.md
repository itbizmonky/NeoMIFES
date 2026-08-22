# ADR-022: Git 統合ライブラリに libgit2 を採用する

- **ステータス:** Accepted
- **決定日:** 2026-08-22

## コンテキスト

要件定義書 §11(Diff/3-Way Merge/Blame/Commit/Branch 切替)、master_roadmap.md §11.1(GitLens 相当のインライン Blame 表示を含む)が Git 統合を明示的に要求している。roadmap §11.1 は Plan-of-Record として **libgit2** を既に名指ししており(CLAUDE.md 冒頭の運用ルール、矛盾が生じた場合はユーザーに確認する方針)、本 ADR は「採用するか」を一から検討するのではなく、**roadmap の指定通り libgit2 を採用し、着手前に実機検証した結果と実務上の注意点を記録する**という性質のものである。

CLAUDE.md ルール3(推測実装をしない)に従い、libgit2 を実際に FetchContent で vendoring できるか、このプロジェクトの正確なツールチェーン(MSVC v143 / VS Community 2026 19.50、CMake 3.28+、Ninja、`/std:c++latest`、動的 CRT 方針)で configure・build・リンクまで通るかを、スタンドアロンの検証用 CMake プロジェクト(実リポジトリを一切変更しない scratchpad 環境)で WI-17a 着手前に実際に確認した。

## 選択肢

1. **libgit2 (FetchContent、静的リンク)**
2. システム `git.exe` へのシェルアウト(サブプロセス起動+出力テキストパース)
3. 自作 Git オブジェクトモデル実装(pack ファイル・index 直接パース)

## 決定

**libgit2 (v1.9.7) を FetchContent 経由で静的リンク採用する。**

## 根拠

| 項目 | libgit2 | git.exe シェルアウト | 自作実装 |
|---|---|---|---|
| ネイティブ統合度 | ○ (プロセス内 API 呼び出し) | ✗ (サブプロセス起動+テキストパース) | ○ |
| git.exe の実行環境依存 | 無し | 有り (PATH 上に git.exe が必須) | 無し |
| Diff/Blame/Commit の網羅性 | ○ (libgit2 自身がフル機能) | ○ (git コマンド自体はフル機能だが出力形式がバージョン間で変わりうる) | ✗ (保守コストが割に合わない) |
| バイナリサイズ | 中 (Release: リンク後の実行ファイル増分 ~1.5MB、Debug 単体アーカイブは大きいがリンク後に刈られる) | ゼロ (追加バイナリなし) | 中〜大 |
| ライセンス | GPLv2 + Linking Exception (実質 LGPL 相当、静的リンクしても配布側に GPL 伝播しない) | N/A (外部プロセス、リンクしない) | N/A |
| 実装・保守コスト | 中 (C API のラップが必要) | 低いが壊れやすい (git のバージョンで出力形式が変わりうる、エラーハンドリングが文字列マッチ依存) | 高 (Git オブジェクトモデル・pack format を再実装) |

- **roadmap 自身が libgit2 を明示的に指定している** — 独自の再検討ではなく、指定されたライブラリが実際にこのツールチェーンで機能するかの確認が本 ADR の主目的
- **git.exe シェルアウトは不採用。** 要件定義書の「Windows最速・最軽量」という製品方針上、外部プロセスの起動オーバーヘッド+テキスト出力のパース(git のバージョン間で微妙に形式が変わりうる)は、GitLens 相当の頻繁な差分計算(左ガターのリアルタイムマーカー等)には不向き。また、git.exe が PATH 上に存在しない環境(最小構成の Windows、企業配布物など)では機能しなくなり、「エディタ本体は依存無しで動作する」という製品方針とも整合しない
- **自作実装は不採用。** Git オブジェクトモデル(pack ファイル形式、delta 圧縮、index 形式)の再実装は保守コストが要件の価値と釣り合わない

## 実機検証で判明した実務上の注意点

着手前に scratchpad 環境で libgit2 v1.9.7 を実際に FetchContent し、configure・build・簡易リンクテスト(`git_libgit2_init()`呼び出し)まで成功することを確認した。以下 3 点は本番の `Dependencies.cmake` 統合時に必ず反映する:

1. **Windows の長パス問題。** libgit2 自身のテストフィクスチャ(病的なファイル名を含む)を `git clone` する際、ビルドディレクトリのパスが深いと `Filename too long` で失敗することがある。Windows のレジストリ `LongPathsEnabled=1` だけでは不十分で、**`git config --global core.longpaths true`** を開発機のビルド前提条件として明記する必要がある(`BUILD_TESTS=OFF` で libgit2 自身のテストは無効化するため、通常のビルドではこの問題を回避できるはずだが、`FETCHCONTENT_QUIET OFF` での初回 clone 自体がこのフィクスチャを含む全体を取得することに変わりはなく、環境によっては影響しうる)。
2. **`STATIC_CRT=OFF` が必須。** libgit2 の CMake は既定で `CMAKE_C_FLAGS` へ文字列を直接注入して `/MT`/`/MTd` を強制する(`MSVC_RUNTIME_LIBRARY` プロパティ経由ではない)。このプロジェクトの動的 CRT 方針(`/MD`/`/MDd`)と衝突し、`Dependencies.cmake` が Abseil で既に踏んだ `_ITERATOR_DEBUG_LEVEL` 不一致と同じ種類のリンクエラーを起こす。`STATIC_CRT OFF` を明示的に `CACHE BOOL "" FORCE` で設定する。
3. **インクルードディレクトリの手動追加が必要。** libgit2 の CMake ターゲット(`libgit2package`)はヘッダを `INSTALL_INTERFACE` のみで公開しており、RE2/nlohmann_json のような `PUBLIC` 経由の自動伝播が効かない。消費側で `${libgit2_SOURCE_DIR}/include` と `${libgit2_BINARY_DIR}/include`(生成ヘッダを含む)の両方を明示的に `target_include_directories` する必要がある。

## 影響

- `BUILD_SHARED_LIBS=OFF`/`BUILD_TESTS=OFF`/`BUILD_CLI=OFF`/`USE_SSH=OFF`/`USE_HTTPS=OFF`/`USE_GSSAPI=OFF`/`USE_HTTP_PARSER=builtin`/`REGEX_BACKEND=builtin`/`USE_BUNDLED_ZLIB=ON` を指定し、ネットワーク機能(clone/push/pull/fetch over SSH・HTTPS)を全て無効化する — 本プロジェクトが必要とするのはローカルリポジトリに対する Diff/Blame/Commit/Branch 切替のみであり、リモート通信機能は要件外
- libgit2 は `zlib`/`pcre2`/`llhttp`/`xdiff` を `add_subdirectory()` でネスト vendoring する。現時点でこのプロジェクトに同名ターゲットの衝突は無いが、`Dependencies.cmake` の `neomifes_collect_targets_recursive()`(CRT 強制ループ)を libgit2 のツリーにも拡張する必要がある(Abseil と同じ理由)
- GPLv2 + Linking Exception ライセンスは、動的・静的いずれのリンクでも自プロジェクトに GPL を伝播させない(Linking Exception 条項)ため、このプロジェクトの配布方針に影響しない

## 却下理由

- **git.exe シェルアウト:** 「根拠」表参照。開発初期の暫定実装としては魅力的だが、GitLens 相当のリアルタイム性・git.exe 非依存という2つの要件と本質的に相性が悪い
- **自作実装:** Git オブジェクトモデルの再実装は保守コストが見合わない

## 将来の再評価

- libgit2 の GPLv2 + Linking Exception ライセンスは通常問題にならないが、将来 NeoMIFES 自体のライセンス方針が変わる場合は再確認すること
- ネットワーク機能(clone/push/pull/fetch)が要件に加わった場合、`USE_SSH`/`USE_HTTPS` の再有効化と、それに伴う libssh2/OpenSSL 等の追加依存を再評価する
