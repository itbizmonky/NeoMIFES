# Issue: 検索・Grepが「数GB ≤ 30秒」目標を実測で満たさない (P1 — 未対応)

- **起票日:** 2026-08-31 (v1出荷判定、`master_roadmap.md` §12.5「数GB Grep ≤ 30秒」項目の実測中に発見)
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

## 対応方針 (未着手・複数の選択肢を検討)

1. **`detailed_design.md` sec.7.2が既に示唆するBoyer-Moore-Horspool+SIMD経路を実装する。** リテラル検索(正規表現を使わない大多数のケース)に限定すれば、RE2を経由せず高速化しやすい。
2. **ピース単位の並列スキャン(`detailed_design.md` sec.7.3)を実装する。** マルチスレッド化により論理コア数に応じた高速化が見込める(要件定義書は「論理コア数-1並列」を目標値として掲げている)。
3. **GrepService側のファイルあたり固定オーバーヘッドを削減する。** 現状1ファイルごとに`loadUtf8File()`(mmap+スキャン)を個別に行っており、多数の小ファイルでこのオーバーヘッドが支配的になっている可能性がある。並列ファイル処理(スレッドプール)も候補。
4. **何もしない(現状維持)、目標値を実測ベースへ改訂する。** 要件定義書・roadmapの「数GB ≤ 30秒」自体を「実装コストとのトレードオフとして受け入れる」と明示的に再定義する。

現時点では対応方針を確定せず、ユーザーへ選択肢を提示した上で次フェーズ候補として検討する。

## 完了条件

- [ ] 上記4方針のいずれかを採用するかを決定する(ユーザー確認)
- [ ] (方針1〜3採用の場合) 実装し、同じ`grep_probe.cpp`相当の手法で3GB/30秒以内を再実測する
- [ ] `tests/bench/search_find_all_bench.cpp`/`GrepService`用の新規ベンチマークを`tests/bench/`へ追加し、継続的な計測を可能にする(現状は200,000行規模のベンチのみでGB規模の計測がCIに存在しない)

## 再検証コマンド

標準プローブ(`grep_probe.cpp`相当、`SearchService::findAll()`/`GrepService::findAll()`を直接呼ぶスタンドアロンプログラム)を再構築し、3GB以上の実ファイル/複数ファイルで再実測する。プローブ自体は本セッションのスクラッチパッドにのみ存在し、リポジトリにはコミットされていない。
