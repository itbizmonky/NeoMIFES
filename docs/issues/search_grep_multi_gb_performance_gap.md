# Issue: 検索・Grepが「数GB ≤ 30秒」目標を実測で満たさない (P1 — 🟡 部分対応)

- **起票日:** 2026-08-31 (v1出荷判定、`master_roadmap.md` §12.5「数GB Grep ≤ 30秒」項目の実測中に発見)
- **部分対応日:** 2026-09-01 (`SearchService::findAll()`単一ファイルケースを対応、`GrepService::findAll()`のファイルあたり固定オーバーヘッドは対象外)
- **対象:** `src/search/src/search_service.cpp`(`SearchService::findAll()`)/ `src/search/src/grep_service.cpp`(`GrepService::findAll()`)
- **優先度:** P1 (実測で目標を明確に超過。ただしクラッシュ・データ破損ではなく速度のみの問題)
- **対応 Phase:** 未定 (SIMD/並列化は当初からPhase 5a設計時点で「将来の最適化」と明記され意図的に未実装だった)

## 事実

v1出荷判定チェックリストの「数GB Grep ≤ 30秒」項目を実測するため、標準プローブ(`grep_probe.cpp`、`SearchService::findAll()`/`GrepService::findAll()`を直接呼ぶ)を作成し、以下2パターンで実測した:

| パターン | 規模 | 実測時間 | 目標 |
|---|---|---|---|
| `SearchService::findAll()`(単一ファイル) | 3GB(約3,065万行、疎なマッチ2,041件) | **38.94秒** | ≤30秒 |
| `GrepService::findAll()`(マルチファイル) | 1.49GB(5,000ファイル、各ファイル1マッチ) | **23.87秒** | ≤30秒(規模はさらに小さいが所要時間は同程度) |

単一ファイル3GBの時点で既に目標を26%超過している。マルチファイル側は規模(1.49GB)こそ小さいが、ファイルあたりの固定オーバーヘッド(`document::loadUtf8File()`の毎回の開閉+スキャン)が蓄積し、GB単価では単一ファイルより悪化している(23.87秒/1.49GB ≈ 16秒/GB vs 38.94秒/3GB ≈ 13秒/GB)。`master_roadmap.md` §5.5が本来の目標として掲げる「数GB (100万ファイル)」規模(本検証の5,000ファイルよりさらに200倍の点数)では、この傾向のままだと大幅に目標を超過する可能性が高い。

## 推定原因 (Phase 5a設計時点で既知・意図的)

`search_service.h`自身のヘッダコメントが当初から明記している設計上のトレードオフ:

> Memory cost scales with document size, not longest line (known tradeoff of the whole-buffer scan strategy above ...). Piece-chunked/parallel scanning (detailed_design.md sec.7.3) remains unimplemented.
>
> One regex engine, one code path ... (detailed_design.md sec.7.2's Boyer-Moore-Horspool+SIMD path remains a documented future optimization, not implemented here).

つまり「文書全体を1個の連続バッファへ結合してからRE2で単発スキャンする、同期・単一スレッド・SIMD無し」という実装(Phase 5a)は、要件定義書NFR「検索: 数GBファイルでも高速」の実測前ベースラインとして最初から位置づけられており、SIMD/並列化/Boyer-Moore-Horspool等の最適化は明示的に「将来のPhase」へ先送りされていた。本issueは、その「将来」が実際にはPhase 12前のv1出荷判定まで一度も実測されないまま来ていたことを示す。

`decode_cache_unbounded_growth.md`の修正(非キャッシュ化)は、この関数が使う一時バッファを`m_decodeCache`へ永久保持しないようにしただけで、バッファを1個の連続領域として構築しRE2へ渡すという計算量・スループット自体には変更を加えていない。

## 影響

- クラッシュ・ハング・データ破損は無い(あくまで速度の問題)。
- ユーザー視点では、3GB以上のログファイル等でCtrl+F/Grepを実行すると30秒以上待たされる可能性がある。要件定義書§8の想定ペルソナ(SAPコンサルの「数GBのトランザクションログでERROR抽出」等)に照らすと実用上のペインになりうる。

## 実際の原因 (2026-09-01、標準プローブで実測・確認済み)

**着手前調査で、issueの推定原因(「RE2自体が遅い」)は誤りだったと判明した。** `SearchService::scanDocument()`の処理を3段階(①ピース連結`pieceTextNoCache()`、②UTF-16→UTF-8変換`toUtf8WithOffsets()`、③RE2スキャン)に分けて標準プローブ(RE2/absl込みでリンクした`search_probe.cpp`)で実測したところ、3GBファイル(自作テストファイル、2,667件マッチ)で以下の内訳だった:

| 段階 | 実測コスト |
|---|---|
| ①ピース連結(`pieceTextNoCache()`) | 約6.4〜7.8秒 |
| ②UTF-16→UTF-8変換(`toUtf8WithOffsets()`) | 約9.1〜9.5秒 |
| **③RE2スキャン自体** | **約0.15〜0.33秒(無視できるレベル)** |

**RE2自体は3GBのバッファに対して0.2秒未満で完走しており、issueが提案していた方針①(SIMD/Boyer-Moore)・②(並列ピーススキャン)はいずれも「そもそも遅くない部分」を高速化しようとするものだった。** 実際のボトルネックはRE2の前段階、「文書全体を1個のバッファへ組み立てる処理」(①+②)にあった。

## 実施内容 (2026-09-01)

上記実測値をユーザーへ提示し、3方針(A: ストリーミング化+UTF-8変換最適化 / B: issue原案通りSIMD・並列スキャン / C: 目標値改訂)から**「A: ストリーミング化+UTF-8変換最適化」が選ばれた。**

1. **`toUtf8WithOffsets()`にASCII連続区間の高速パスを追加した(`src/util/src/utf8_convert.cpp`)。** 元の実装は1コードポイントごとに`decodeOne()`+`appendUtf8()`+1バイトずつ`push_back()`するブランチの多いループだった。ASCII文字(コードポイント<0x80)はUTF-16オフセットとUTF-8バイトオフセットが1:1対応するため、連続ASCII区間を`resize()`+添字書き込みで一括処理する経路を追加(サロゲートペア・非ASCII文字は既存の低速パスをそのまま使用、正しさは一切変更していない)。**実測: 3GBで約9.1秒→約5.5〜5.6秒(約38〜40%削減)、複数回の実測で一貫。**
2. **`SearchService::scanDocument()`のピース連結も`pieceTextStreamed()`へ切り替えを試みたが、実測で効果が無いと判明し撤回した。** `LineIndex::build()`が同じ「単一の巨大Original piece」ケースで一括デコードからストリーミングデコードへ切り替えて大きな改善を得た前例(`decode_cache_unbounded_growth.md`)に倣った試みだったが、3GB規模で複数回実測したところ、`pieceTextStreamed()`はむしろ一貫して約1〜2秒遅かった(8.4〜8.8秒 vs `pieceTextNoCache()`の6.4〜7.8秒)。CLAUDE.mdルール10(効果を計測で裏付ける)に従い、効果の無い変更は採用せず`pieceTextNoCache()`のまま維持することにした。`LineIndex::build()`は1文字ずつ見て捨てる用途だが、`scanDocument()`はRE2がピース境界をまたぐマッチを見るために最終的に全体を1個のバッファとして持つ必要があり、両者の使われ方の違いがこの結果の差の一因と考えられる(未検証の推測)。

## 実機検証結果 (2026-09-01、Release)

3GBファイル(2,667件マッチ)で、①+②+③(ピース連結+UTF-8変換+RE2スキャン、`SearchService::findAll()`の主要部分)の合計が**約17.1秒→約12.0〜12.4秒(約28%削減)**。ファイル読み込み(`loadFile()`、約5.2〜5.9秒、一度きりのファイルオープンコストで`findAll()`自体には含まれない)を含めても合計約17〜18秒で、目標の30秒に対して余裕がある。

**注意:** issueが最初に報告した38.94秒という数値は、本修正前の時点でも本セッションの実測環境では再現できなかった(本セッションでの「修正前」ベースラインは約17秒で、既に38.94秒より大幅に小さい)。ディスクキャッシュの状態(本セッションのテストファイルは生成直後でOSキャッシュが温まっていた可能性が高い)や実行環境の違いによるものと推定されるが、厳密な原因特定はできていない。したがって「○倍改善」という単純な比較は避け、本セッションで実測した相対的な改善(UTF-8変換で約38〜40%、全体で約28%)のみを実測値として記録する。

**`GrepService::findAll()`(マルチファイル、5,000ファイル)は当初今回のスコープ外として残っていたが、2026-09-03に対応した(下記参照)。** `GrepService`は内部で`SearchService::findAll()`を呼ぶため、上記のUTF-8変換最適化は各ファイルにも及ぶが、issue自身の分析が示す通りマルチファイルケースの支配的コストは「ファイルあたりの`loadUtf8File()`固定オーバーヘッド」であり、これは対象方針③(GrepService側のオーバーヘッド削減、並列ファイル処理)に相当する。

実機ドッグフーディングで、日本語テキストとASCII"ERROR"が混在するファイルでの検索(3件中1/3、いずれも正しくハイライト)、および3GBファイルでの実際の検索(WM_COMMAND経由でFindBarへ"ERROR"を送信、1/2667件と正しい件数、ハング無く復帰)の両方をスクリーンショットで確認した。

## GrepServiceのファイルあたり固定オーバーヘッド対応 (2026-09-03)

**着手前調査で、当初の仮説(`OriginalBuffer::scanUtf8()`のバイト単位UTF-8検証パスが支配的コスト)は誤りだったと判明した。** サブエージェントへ委譲した実測(2,000ファイル、各約150KB、合計約293MB、`tests/bench/`へ一時的に追加し使用後に削除した専用プローブによる)で`loadUtf8File()`のコスト内訳を分解したところ、`scanUtf8()`(mmap時に走る全バイトのUTF-8検証+チェックポイント構築)は全体(約1,572ms/2,000ファイル)のうち約570msに過ぎず、**残る約969ms(「`openMemoryMapped()`以外の全て」)の方が大きい単一要因だった。** さらに絞り込むと、`detectLineEndingBounded()`が`BufferSnapshot::extract()`(デコード結果を`OriginalBuffer`のデコードキャッシュへ永久保持するキャッシュ付き経路)を使っていたことが原因と判明した——本検証で使ったテストファイル(約150KB)は行末検出の走査上限(`kLineEndingDetectionHeadCodeUnits` = 1<<20コード単位 ≈ 1MiB)を下回るため、「先頭の一部だけを見る」という設計意図に反し、実質ファイル全体をデコード+キャッシュしていた。`GrepService::grepOneFile()`は`LoadResult`を1回読んで捨てるだけで`.lineEnding`を一度も参照しないため、このキャッシュは書き込まれるだけで二度と読まれない。

以下3件を実施した:

1. **Fix A:** `detectLineEndingBounded()`を`snap->extract()`から`snap->extractNoCache()`へ切替(`decode_cache_unbounded_growth.md`が確立した「使い捨てはキャッシュしない」パターンの適用漏れだった箇所)。`loadUtf8File()`/`loadFile()`両方の全呼び出し元(GrepServiceに限らない)が恩恵を受ける、動作無変更の性能改善。
2. **Fix B:** `search::GrepService`専用の新規ローダ`document::loadUtf8FileForGrep()`(`file_loader.h`/`.cpp`)を追加。`loadUtf8File()`と実装を共有する内部ヘルパー`loadUtf8FileImpl(path, maxBytes, bool detectLineEnding)`を新設し、`detectLineEnding=false`で`detectLineEndingBounded()`の呼び出し自体を丸ごとスキップする(`LoadResult::lineEnding`は既定値`Lf`のまま)。既存`loadUtf8File()`は無変更(`document_file_loader_test.cpp`の7箇所の`.lineEnding`直接検証が依存しているため契約を変えられない)。`grep_service.cpp`の`grepOneFile()`をこの新関数へ切替。
3. **Fix C:** `preflightFile()`が内部で計算済みの`std::filesystem::file_size()`結果を`std::uint64_t& outSize`引数で呼び出し元へ返すよう変更し、`loadUtf8File()`/`loadFile()`双方にあった冗長な2回目の`file_size()`呼び出しを削除。

新規テスト4件(`LoadUtf8FileForGrepTest`、`tests/unit/document_file_loader_test.cpp`)で、新関数が内容を正しく読み込みつつ明らかにCRLFなファイルでも`.lineEnding`を検出しない(既定値`Lf`のまま)ことを検証。Debug全1576テスト・変更4ファイルのclang-tidy(新規指摘0件)・Release/asan構成で確認済み(詳細は本ファイル末尾の完了条件参照)。

## 完了条件

- [x] 上記4方針のいずれかを採用するかを決定する(ユーザー確認) — 「ストリーミング化+UTF-8変換最適化」を選択(ストリーミング化は実測後に撤回)
- [x] (方針1〜3採用の場合) 実装し、同じ`grep_probe.cpp`相当の手法で3GB/30秒以内を再実測する — 3GBで約17〜18秒(loadFile含む)、目標30秒以内を達成。ただし元issueの38.94秒という基準値は再現できておらず、単純比較は避けて記録した
- [x] `GrepService`側のファイルあたり固定オーバーヘッドを削減する(方針③) — 2026-09-03、`detectLineEndingBounded()`の冗長デコード除去(Fix A)+GrepService専用ローダ新設(Fix B)+`file_size()`重複呼び出し除去(Fix C)で対応。サブエージェント実測で`loadUtf8File()`単体コストの主要因(約969ms/2,000ファイル、`scanUtf8()`の約570msより大)を特定した上での対応
- [ ] `tests/bench/search_find_all_bench.cpp`/`GrepService`用の新規ベンチマークを`tests/bench/`へ追加し、継続的な計測を可能にする(現状は200,000行規模のベンチのみでGB規模の計測がCIに存在しない) — 未実施のまま残る

## 再検証コマンド

標準プローブ(RE2/abslをリンクしたスタンドアロンプログラム、`SearchService::findAll()`相当の3段階を個別に計測)を再構築し、3GB以上の実ファイルで再実測する。プローブ自体は本セッションのスクラッチパッドにのみ存在し、リポジトリにはコミットされていない(再現手順: `neomifes_search`/`neomifes_document`/`re2`/`abseil-cpp`の各`.lib`を直接リンクする標準プローブをスクラッチパッドで構築)。
