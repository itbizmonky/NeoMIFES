# Issue: `MSVC_RUNTIME_LIBRARY` 修正が事後上書きで脆弱

- **起票日:** 2026-07-19 (Phase 5a コードレビューで指摘、`/code-review` high effort)
- **対象:** [`cmake/Dependencies.cmake`](../../cmake/Dependencies.cmake) の `neomifes_collect_targets_recursive()` および直後の `foreach` ループ
- **優先度:** 低 (現状ローカル Debug/Release/ubsan の3プリセット・CI全4ジョブで実際に動作確認済み。将来の Abseil バージョン更新時にのみ顕在化しうる潜在リスク)

## 背景

Phase 5a で RE2/Abseil を FetchContent 導入した際、ubsan(clang-cl) プリセットでのみ `_ITERATOR_DEBUG_LEVEL` 不一致によるリンクエラーが発生した。原因は Abseil 自身の `CMakeLists.txt` が `ABSL_MSVC_STATIC_RUNTIME` オプション(既定 OFF)経由で `CMAKE_MSVC_RUNTIME_LIBRARY` を無条件に再 `set()` しており、この上書きが Abseil の `add_subdirectory()` ツリー配下の各ターゲットにだけ適用され、ubsan プリセットが最上位で指定した値を無視してしまうことだった。

現在の修正は、新規 `neomifes_collect_targets_recursive()`(`SUBDIRECTORIES` プロパティを再帰的に辿る自作関数)で Abseil の全ターゲットを収集し、`FetchContent_MakeAvailable(abseil-cpp)` 完了**後**に `set_target_properties(... MSVC_RUNTIME_LIBRARY ...)` で強制的に上書きし直す、という事後対症療法になっている。

## リスク

- `neomifes_collect_targets_recursive()` は Abseil の現在の `add_subdirectory()` 階層構造に依存している。将来 Abseil がディレクトリ構成を変更したり、この再帰探索では捕捉できない方法(例: 独自のターゲット生成関数)でターゲットを作るようになった場合、収集結果が黙って不完全になりうる(空リストを返しても検出する assertion は無い)
- その場合、今回発見・修正した `_ITERATOR_DEBUG_LEVEL` 不一致リンクエラーが**コンパイル時の手がかり無しに**再発する可能性がある

## より根本的な修正案 (未着手)

Abseil は本来この用途向けの `ABSL_MSVC_STATIC_RUNTIME` オプションを公開している。`FetchContent_MakeAvailable(abseil-cpp)` を呼ぶ**前**に、プロジェクトが要求するランタイムライブラリ設定に応じて

```cmake
set(ABSL_MSVC_STATIC_RUNTIME <ON/OFF> CACHE BOOL "" FORCE)
```

を明示的に設定しておけば、Abseil 自身が最初から一貫した値で各ターゲットを構成するため、事後上書きが不要になる。ただし、この方法にも制約がある:

- Abseil 側のオプションは `set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>[DLL]")` という **CONFIG 依存の式**を生成するのに対し、現状の ubsan プリセットは `"MultiThreaded"`(CONFIG 非依存、常に非デバッグ)という**リリース CRT 固定**の値を指定している(`cmake/Sanitizers.cmake` のコメント参照: clang-cl 同梱の UBSan ランタイムはリリース CRT 限定でビルドされているため)。両者を単純に一致させると、ubsan プリセットが `CMAKE_BUILD_TYPE=Debug` を使っている現状の構成では `MultiThreadedDebug` に評価され、UBSan ランタイムとの不整合を再発させる可能性がある
- そのため、単純に `ABSL_MSVC_STATIC_RUNTIME` を設定するだけでは解決せず、ubsan プリセット側の CONFIG 依存性の扱いも合わせて見直す必要がある

## 完了条件 (将来この Issue に着手する場合)

- [ ] `ABSL_MSVC_STATIC_RUNTIME`(または同等の事前設定)を使い、事後の `set_target_properties` 上書きを撤去する
- [ ] ubsan プリセットの `CMAKE_MSVC_RUNTIME_LIBRARY` 設定と Abseil 側の解決値が一致することを、実際に `compile_commands.json` の `-MT`/`-MTd`/`-MD`/`-MDd` フラグを比較して確認する
- [ ] ローカル ubsan プリセットで再度ビルド・全テスト green を確認
- [ ] Abseil/RE2 のバージョンを更新するたびにこの Issue の前提(ディレクトリ構造・オプション名)が変わっていないか確認する運用を `docs/handoff/RESUME_HERE.md` に明記する
