# Issue: `ts_parser_parse()`自体が文書/保持木サイズに比例するコストを持つ (TSInputコールバックAPI化では解決しない)

- **起票日:** 2026-07-31 (Phase 7u 実装・検証・revert 時に判明)
- **対象:** [`src/syntax/src/incremental_parser.cpp`](../../src/syntax/src/incremental_parser.cpp) の `IncrementalParser::reparseRange()`
- **優先度:** 中 (roadmap §7.11 のDoD「1文字入力後の増分解析: ≤50ms」が大規模文書(50万行)で未達のまま残っている根本原因)
- **関連:** `docs/design/master_roadmap.md` §7.11、Phase 7q/7t の完了記録(いずれも同じDoD未達を記録)

## 背景

Phase 7t完了時点で、50万行文書の1文字編集増分再解析が narrow window(可視範囲のみ要求)でも155.95ms・full document(文書全体要求)でも155.45msとほぼ同一という実測が得られていた。この「ウォーク範囲を絞ってもコストが変わらない」という事実から、**ボトルネックは`detail::walkTree()`(ウォーク量に依存)ではなく`ts_parser_parse_string_encoding()`自体にある**、との仮説を立てた。

この関数は文字列ベースAPIであり、呼び出しごとに文書全体のテキストを1つの連続バッファとして事前に用意することを要求する(`SyntaxWorker::workerLoop()`の`BufferSnapshot::extract()`がこれを行う)。tree-sitterはこの制約を回避する**コールバックベースAPI**(`TSInput`+`ts_parser_parse()`)を公式に提供しており、`read(payload, byte_index, position, *bytes_read) -> const char*`という関数ポインタを渡すと、tree-sitterは実際に必要なバイト範囲だけを都度要求してくる。「増分再解析では、編集で無効化されていない大部分の部分木は`old_tree`からそのまま再利用され、その範囲のテキストは一度も`read()`されないはず」という理論に基づき、Phase 7uでこのAPIへの切り替えを実装した。

## 実施内容 (Phase 7u)

- `neomifes::syntax`に`TextChunk`/`TextSourceRead`/`TextSource`(関数ポインタ+payload、`std::function`不使用)を新設し、`reparseRange()`のシグネチャを`std::u16string_view text`から`TextSource source`へ置き換え
- `neomifes::render`に`BufferSnapshotTextSource`(`document::BufferSnapshot`の`pieceView()`を`std::ranges::upper_bound`による二分探索でチャンク単位(`kMaxChunkCodeUnits=4096`コード単位でキャップ)に切り出す実装)を新設
- `SyntaxWorker::workerLoop()`から`BufferSnapshot::extract()`(文書全体materialization)を削除し、`BufferSnapshotTextSource`経由の遅延読み込みへ置き換え
- 単体テスト5件・既存テスト・ベンチマーク一式を新契約に追従させ、Debug/Release/ubsanの870テスト全てgreenを確認

実装自体は正しく完成し、CLAUDE.mdの品質ゲート(ビルド警告0・clang-tidy新規警告0・全テストgreen)を満たしていた。

## 検証結果 — 仮説が誤りだったことが判明

一時的な診断計測(`std::chrono`によるタイミング計測、`read()`呼び出し回数/バイト数カウンタ)を追加し、50万行文書での1文字編集増分再解析を計測した。

**`BufferSnapshotTextSource::read()`の実際の呼び出し状況(narrow window・full document 双方):**

| 項目 | 実測値 |
|---|---|
| `read()`呼び出し回数 | **1回** |
| `read()`で読まれたバイト数 | **8,192バイト** |
| 文書全体のバイト数 | 115,621,560バイト |

→ **遅延読み込みメカニズム自体は設計通り完璧に動作していた**(文書全体のうち0.007%しか読んでいない)。

**にもかかわらず、`ts_parser_parse()`単体のコスト(`ts_tree_edit()`とは別に`std::chrono`で分離計測):**

| 項目 | 実測値 |
|---|---|
| `ts_tree_edit()`(全edits適用) | 0.02〜0.05ms(無視できる) |
| `ts_parser_parse()`本体 | **約300〜325ms** |

**公正な比較のため、Phase 7tが除外していた`BufferSnapshot::extract()`(文書全体実体化)のコストも別途計測した:**

| 項目 | 実測値 |
|---|---|
| `extract()`(50万行文書全体を`std::u16string`へ実体化) | **19.07ms** |
| Phase 7t実測(`ts_parser_parse_string_encoding()`、narrow window、extract()コスト抜き) | 155.95ms |
| Phase 7t実際のエンドツーエンド(`extract()` + `ts_parser_parse_string_encoding()`、公正な合計) | **約175ms** |
| Phase 7u実測(`ts_parser_parse()`、TSInput経由、narrow window) | **約300〜325ms** |

## 結論

1. **文書全体のテキスト事前実体化(`extract()`)は、真のボトルネックではなかった。** そのコストはわずか19msで、旧`ts_parser_parse_string_encoding()`の155msに対して約12%の追加コストに過ぎない。
2. **`ts_parser_parse()`(TSInputコールバックAPI経由)は、`read()`が実際にはごく一部しか呼ばれていないにもかかわらず、旧文字列一括API(`extract()`込みの公正な合計175ms)より約1.8倍遅い(300〜325ms)。** つまりPhase 7uの変更はDoD未達を解消しなかっただけでなく、**明確な性能後退(regression)** だった。
3. 保持木(`old_tree`)を使った増分再解析において、tree-sitterの`ts_parser_parse()`自身の内部処理(木の再利用可否を判定する走査、または増分diffアルゴリズムの一部)が、**実際にテキストを読み直す量とは無関係に、保持木(≒文書)のサイズに比例するコストを持つ**と強く示唆される。この内部コストはコールバックAPI・文字列一括APIのどちらを使っても発生し、後者よりも前者の方がむしろ(関数ポインタ経由の間接呼び出し・チャンク境界管理等のオーバーヘッドで)重くなる。

**対応: Phase 7uの実装は全面的にrevertし、`incremental_parser.h`/`.cpp`・`syntax_worker.cpp`はPhase 7t完了時点のコード(文字列ベース`ts_parser_parse_string_encoding()`)に戻した。** `BufferSnapshotTextSource`関連の新規ファイルも削除。

## 今後の検討候補 (未着手、優先度は要再検討)

- **`ts_parser_parse()`内部の保持木依存コストの正体を、tree-sitter自身のソースコード(`lib/src/parser.c`)を読んで特定する。** 現時点では「なぜ`old_tree`のサイズに比例するのか」の具体的なメカニズム(GLRスタックの再構築、subtreeのバイトオフセット再計算、エラー回復の探索範囲等)は未解明。
- **`ts_tree_edit()`自体は無視できるコスト(0.02〜0.05ms)と確認済みのため、「保持木を使わず毎回ゼロから全文書パースする」設計と実測比較する価値がある。** もし`old_tree`を渡さない完全な再パース(`old_tree=nullptr`)の方が速いとすれば、tree-sitterの増分再解析機能自体がこの利用パターン(巨大文書+単発小編集)には向いていない可能性がある。ただしこれは初回パースと同じO(文書サイズ)コストになるため、根本的な解決にはならない。
- **tree-sitter本体のバージョンアップ**(現行バージョンの内部実装に起因する既知の問題である可能性。上流での修正・改善を追跡する)。
- **「可視範囲のみを含む縮小されたドキュメントコピーに対して独立したパーサインスタンスでフルパースする」設計**(増分再解析自体を諦め、可視範囲外の構文コンテキストを一部犠牲にする代わりにO(可視範囲サイズ)を狙う、ネスト構造の誤判定リスクとのトレードオフを要検討)。

## 完了条件 (将来この Issue に着手する場合)

- [ ] `ts_parser_parse()`の保持木依存コストの原因をtree-sitter内部実装の読解で特定する
- [ ] 50万行文書の1文字編集増分再解析が実測 ≤50ms (roadmap §7.11 DoD) を達成する具体的な設計案を提示する
- [ ] 上記設計案をベンチマークで実証してから実装する(CLAUDE.mdルール10)
