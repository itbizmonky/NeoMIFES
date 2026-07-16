# Issue: TextLayoutCache のサイズ無制限成長

- **起票日:** 2026-07-16 (Phase 3c 実装中)
- **対象:** [`src/render/include/neomifes/render/text_layout_cache.h`](../../src/render/include/neomifes/render/text_layout_cache.h)
- **優先度:** 低(現時点では顕在化しうる駆動源が存在しない)

## 背景

Phase 3c で実装した `TextLayoutCache` は `LineNumber` をキーとした `std::unordered_map` で、サイズ上限や LRU 追い出しを一切持たない。無効化は `Document::version()` が変化した時の `clear()`(全消去)のみ。

## なぜ無制限のままにしたか

- 可視行は常に40〜80行程度で、スクロールで訪れた行のみエントリが増える
- `neomifes::util` には LRU キャッシュ相当の実装が現状存在しない(新設するとしたら本 issue のためだけの新規実装になる)
- 対話的スクロール入力(Phase 4 Editor Core)が未実装のため、「ユーザーが巨大ファイルを延々スクロールし続ける」というシナリオ自体が今のコードベースでは発生し得ない — つまり無制限成長の実害を**測定する手段自体が今存在しない**

CLAUDE.md 絶対ルール3(推測実装をしない)・ルール10(性能改善はベンチマーク根拠必須)に照らし、測定できない段階で LRU 化を先回りして実装するのは時期尚早と判断した([ADR-011](../decisions/ADR-011-phase3c-render-cache-scope.md) 参照)。

## リスクシナリオ

Phase 4 で対話的スクロールが実装された後、ユーザーが 10GB 級ファイルを一度のセッションで最初から最後まで連続スクロールした場合、`TextLayoutCache` のエントリ数はドキュメントの総行数に比例して増え続け、セッション終了(または編集による `clear()`)まで解放されない。

## 対応方針

**今回は対応しない。** Phase 4 で対話的スクロールが実装された後、実際のスクロールセッションでのメモリ増分を実測してから、必要であれば LRU 化(または他の追い出し戦略)を検討する。

## 完了条件(将来この Issue に着手する場合)

- [ ] Phase 4 の対話的スクロールで、実ファイル(1GB/10GB 級)を連続スクロールした際のメモリ増分を実測(`neomifes::platform::currentProcessMemory()` 等の既存計測ユーティリティを流用)
- [ ] 実測結果が問題になる規模であれば、LRU 化の設計(`neomifes::util` への LRU 実装の追加を含む)を別 ADR として起票
- [ ] `TextLayoutCache` の単体テスト(`tests/unit/render_text_layout_cache_test.cpp`)に、上限到達時の追い出し挙動のテストを追加
