# Issue: `parseJsonTree()`が病的に深いネストでスタックオーバーフローしうる (P1 — 未対応)

- **起票日:** 2026-08-18 (WI-15b、JSON構造ツリー 非同期インデックス化)
- **対象:** `neomifes::jsontree::parseJsonTree()`(`src/jsontree/src/json_tree.cpp`)、およびそれを呼ぶ`JsonTreeWorker`(`src/jsontree/src/json_tree_worker.cpp`)
- **優先度:** P1 (ファイルを開く/コマンドを実行するだけでプロセス全体がクラッシュしうる)
- **対応 Phase:** 未定 (WI-15c以降、JSONツリーの実UI配線時に再評価)
- **親文書:** WI-15b (`build_plan.md` §5)

## 事実

WI-15bの最終ゲート検証(ubsan/clang-cl構成)で、深さ2000階層のネストした配列(`[[[...]]]`)を`JsonTreeWorker`のバックグラウンドスレッドへ渡す統合テスト(`RequestIndexOnDeeplyNestedJsonDoesNotCrashWorkerThread`、当初の意図は「ワーカースレッドがクラッシュしないことの安全側の保険」)が、実際に`STATUS_STACK_OVERFLOW`(`0xC00000FD`)でプロセスごとクラッシュすることが判明した。

原因を切り分けたところ、`neomifes::jsontree::buildTree()`自体(WI-15a、明示スタックによる反復実装)は無関係で、`json_tree.cpp`が呼んでいる`nlohmann::ordered_json::parse()`自体が再帰下降パーサであり、ネスト1階層につきC++呼び出しスタックを1段消費することが原因と判明した。**nlohmann/json (v3.11.3、ADR-013で採用) には解析深度の上限を設定する公式APIが存在しない**(ライブラリ側の既知の設計上の制約)。

MSVC Debug/Release構成では深さ2000でもクラッシュしなかったが、clang-cl+UBSanの計装ビルド(1フレームあたりのスタック消費が大きい)では`std::thread`の既定スタックサイズ(通常1MB)を使い切ってクラッシュした。**「MSVC構成でクラッシュしなかった」は安全性の証明にならない** — スタック消費量はビルド設定・最適化レベルに強く依存するため、本番のRelease構成でも十分深いネストを与えれば同様にクラッシュしうる。

このテスト自体は`RequestIndexOnDeeplyNestedJsonDoesNotCrashWorkerThread`という名前のまま深さを50程度まで下げて残した(WI-15bのコミット内)。これは「buildTree()自体の反復実装が安全であること」の回帰保険としては機能するが、**nlohmann自身の再帰下降パーサが十分深いネストでクラッシュしうるという根本問題は未解決のまま**である。

## 影響

- ユーザーが(意図的か偶然かを問わず)数千階層以上ネストしたJSON/配列を含むファイルに対してJSONツリー機能を使うと、`JsonTreeWorker`のバックグラウンドスレッドがスタックオーバーフローし、**アプリケーションプロセス全体がクラッシュする**(Windowsではスタックオーバーフローは通常回復不能で、SEHで捕捉してもプロセス継続は保証されない)。
- WI-15b時点では`beginJsonTreeIndexing()`を呼ぶコマンド/UIが一切存在しないため(意図的にWI-15cへ先送り)、**現時点でこの経路に到達するユーザー操作は存在しない**。実害が顕在化するのはWI-15c(UI配線)以降。
- `json_tree.cpp`の`Document`版オーバーロード(UIスレッドで直接呼ばれる可能性がある)も同じ問題を抱える。

## 対応方針 (未着手、複数の選択肢)

1. **事前の深度チェック(SAXベースの軽量バリデーションパス)。** `nlohmann::json_sax`の`start_object()`/`end_object()`/`start_array()`/`end_array()`コールバックでネスト深度をカウントし、閾値超過時に`false`を返して解析を中断する専用の検証パスを`ordered_json::parse()`の前に挟む。WI-15aの調査で確認済みの通りSAXコールバックは位置情報を運ばないため位置復元には使えないが、深度カウントだけなら問題なく使える。実装コストは小さいが、「安全な最大深度」の閾値をどう決めるか(スレッドのスタックサイズ・ビルド設定に依存)は要検討。
2. **`JsonTreeWorker`専用スレッドのスタックサイズを明示的に拡大する。** `std::thread`はOS既定のスタックサイズしか指定できないため、`CreateThread`のWin32 API+`dwStackSize`引数への置き換えが必要(現状の`std::thread`ベース実装からの設計変更)。深いネストを許容できる代わりに、根本的な上限が無くなるわけではない(どこまで深くても安全とは言えない)。
3. **JSON構造ツリーモード自体の対象外とする。** 「病的に深いネストのJSONはツリー表示の対象外」として、WI-15c以降のUIで解析前にファイルサイズ/簡易深度スキャンによる警告ダイアログを出す運用回避。

いずれもWI-15bのスコープ外(非同期ワーカー配線のみが本WIの責務)であり、着手はWI-15c(UI配線、実際にユーザーがJSONツリー機能を使い始めるタイミング)以降に先送りする。

## 完了条件

- [ ] 上記いずれかの対応方針を選択し実装した
- [ ] 深いネストのJSONファイルに対してクラッシュせず(グレースフルな失敗として)処理できることを統合テストで確認した

## 再検証コマンド

```powershell
cmake --build --preset ubsan
ctest --preset ubsan -R jsontree_json_tree_worker --output-on-failure
```
