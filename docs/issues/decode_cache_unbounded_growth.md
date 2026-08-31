# Issue: OriginalBuffer のデコードキャッシュ無制限蓄積による OOM (🟢 解決済み)

- **起票日:** 2026-08-30 (v1出荷判定、10GBファイルでのログ解析モード実機検証中に発見)
- **解決日:** 2026-08-31
- **対象:** `src/document/src/original_buffer.cpp`(`OriginalBuffer`)/ `src/document/src/buffer_snapshot.cpp`(`BufferSnapshot`)、および文書全体を走査する全消費者
- **優先度:** 🔴 高 → 🟢 解決済み (実機でシステムメモリを枯渇させた実績があったため)
- **対応 Phase:** v1出荷判定 (`master_roadmap.md` §12.5)

## 事実

### 発見の経緯

v1出荷判定の「10GBファイル対応(ログ解析モード)」検証で、10GB(約1.23億行)のログファイルを開き「Log: Enable (Auto-Detect)」を実行したところ、30秒でプロセスのWorking Setが25.3GBまで増大し、開発機(物理メモリ31.8GB)の空きメモリが0.3GBまで低下、UIがハングした。同様の症状はJSON Treeモード(100MB/145万要素で3分以上UIハング、原因は別、[json_tree_ui_population_hang.md](json_tree_ui_population_hang.md)参照)でも観測された。

### 根本原因

`OriginalBuffer`(mmap + Lazy Decode、Phase 2b3実装)の`m_decodeCache`は、デコード結果を`(offset, length)`キーで**永久にキャッシュし、一度も追い出さない**設計だった。これは`docs/issues/lazy_decode_mmap.md`が当初想定していた「mmapビュー自体のLRU」とは別の、より狭い懸念で、当時は「実際にメモリ圧迫が計測されるまで先送り」と意図的に保留されていた(`original_buffer.h`のクラスコメント参照)。

通常のスクロール編集では画面に見えている範囲しかデコード要求が発生しないためキャッシュは肥大化しない。しかし以下の消費者は**文書全体を一度に**(`pieceView()`または`extract()`経由で)走査するため、デコード結果が事実上ファイル全体分キャッシュされ、UTF-8→UTF-16変換で理論上約2倍に膨張したまま解放されなかった:

- `document::LineIndex::build()` — **あらゆるファイルを開いた際に初回の行番号参照時に必ず実行される**、最も影響範囲の広い呼び出し元
- `logmode::LogModel::build()` (ログ解析モード)
- `csvmode::CsvModel::build()` (CSVモード)
- `jsontree`/`xmltree`の`bufferFromSnapshot()` (JSON/XML Treeモード)
- `search::SearchService::scanDocument()` (検索・Grep)
- `document::file_saver.cpp`の`writeChunks()` (**ファイル保存、あらゆる編集で発生**)
- `git::GitRepository::diffAgainstHead()`/`unifiedDiffAgainstHead()` (Git差分、保存時自動トリガー含む)
- `render::syntax_worker.cpp`の全文抽出 (**構文ハイライトの再パース、編集のたびに発生**。文書長が編集ごとに変わるためキャッシュキーも毎回変わり、**古いエントリが二度と参照されないまま無制限に積み上がる**、最も深刻なケース)
- `document::PieceTable::ensureBoundary()` (巨大な未分割ピース内での初回編集)

### 第二の発見: 一括デコード自体のスケーラビリティの壁

上記の「非キャッシュ化」だけを適用した第一次修正では、10GBファイルで依然として問題が残った。`LineIndex::build()`等が文書全体(1ピース)を**1個の`std::u16string`として一括デコード**する設計のままだったため、10GBファイルでは約20GBの一時バッファを一括確保・書き込みすることになり、1GBファイルでは1.83秒で完了する処理が10GBでは50秒以上経っても完了せず(単純な10倍ではない超線形の劣化。システムの物理メモリ上限に近づくにつれ悪化したと推測される)、システム空きメモリが0.5GBまで低下した。

## 対応

### 修正1: 非キャッシュAPI追加 (`viewNoCache()`/`extractNoCache()`/`pieceTextNoCache()`)

`OriginalBuffer`/`BufferSnapshot`に、デコード結果を`m_decodeCache`へ格納しない非キャッシュ版のAPIを追加し、上記「一度だけ訪問して使い捨てる」消費者を全てこちらへ切り替えた。既存の`view()`/`extract()`/`pieceView()`(スクロール等の反復・ランダムアクセス向け、キャッシュが有効に機能する)は無変更。

### 修正2: ストリーミングAPI追加 (`viewStreamed()`/`pieceTextStreamed()`)

`LineIndex::build()`/`LogModel::build()`/`CsvModel::build()`(文字を1つずつ見て改行・区切り文字を判定するだけで、文書全体を同時にメモリ上に持つ必要が無い消費者)向けに、固定チャンク単位(`OriginalBuffer::kStreamChunkCodeUnits` = 1,048,576 CU ≒ 2MB)でコールバックへ渡すストリーミングAPIを追加し、これら3箇所を切り替えた。ピーク メモリを文書サイズに依存しない一定値へ抑える。

**実測 (Release、10GBプレーンテキストファイル、`--measure-frame`):**

| | 修正前 | 非キャッシュ化のみ | ストリーミング化後 |
|---|---|---|---|
| 初回インデックス構築の所要時間 | 113.7秒 | 50秒超(未完走、強制終了) | **26.99秒** |
| Private メモリ ピーク | 測定時に強制終了(20GB超で継続増加) | 20.23GBで頭打ち(一時バッファのサイズ) | **1.22GB** |
| 定常スクロール性能 | p50=16.67ms/p95=16.82ms | (未計測) | p50=16.66ms/p95=16.78ms (無劣化) |

ログ解析モード(10GB、1.23億行)は非キャッシュ化+ストリーミング化後、Private 4.54GBで完走・応答維持を実機確認。CSVモード(10GB)は非キャッシュ化+ストリーミング化後、一時バッファの問題は解消したが、恒常的な per-cell インデックスのメモリコストという**別の**課題が残ることが判明した([csv_per_cell_index_memory_scaling.md](csv_per_cell_index_memory_scaling.md)参照)。

## 対応不能・未対応のまま残る範囲 (意図的にスコープ外)

- **`SearchService::scanDocument()`/JSON-XML Treeの`bufferFromSnapshot()`は非キャッシュ化のみでストリーミング化していない。** RE2の複数行マッチや nlohmann/tree-sitter のDOM/AST構築は、文書全体を1個の連続バッファとして必要とするアルゴリズムであり、真のストリーミング化(regexのチャンク跨ぎマッチ処理、SAXベースの逐次JSON構築、tree-sitterのreadコールバックAPI活用等)は本issueのスコープを超える、別途の大規模な再設計が必要。**非キャッシュ化により「永久保持」は解消したが、「一時的に文書サイズ分のメモリを使う」という制約は残る。**
- **CSVモードの永続的なper-cellインデックスのメモリコスト**は別issue([csv_per_cell_index_memory_scaling.md](csv_per_cell_index_memory_scaling.md))として起票。

## 完了条件

- [x] `OriginalBuffer::viewNoCache()`/`decodeUtf8NoCache()`等、非キャッシュ版デコードAPIを追加
- [x] `BufferSnapshot::pieceTextNoCache()`/`extractNoCache()`を追加
- [x] `OriginalBuffer::viewStreamed()`/`streamUtf8()`等、ストリーミング版デコードAPIを追加
- [x] `BufferSnapshot::pieceTextStreamed()`を追加
- [x] 全消費者(9箇所)を適切なAPIへ切り替え
- [x] `LineIndex::build()`/`LogModel::build()`/`CsvModel::build()`をストリーミング化
- [x] 単体テスト追加 (`tests/unit/document_buffer_snapshot_test.cpp`、非キャッシュ/ストリーミング双方の内容一致・チャンク境界・エラー処理を検証)
- [x] 10GBファイルでの実機再検証 (プレーンテキスト/ログ解析モード、Private メモリが数GB以内に収まり応答性を維持することを確認)
- [x] Debug/Release/ubsan全green、clang-tidy新規警告0

## 再検証コマンド

```bash
grep -rn "pieceTextNoCache\|pieceTextStreamed\|viewNoCache\|viewStreamed" src/document/include/neomifes/document/
```
