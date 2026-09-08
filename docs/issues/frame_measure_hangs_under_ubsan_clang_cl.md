# Issue: `--measure-frame`(`FrameMeasureTest.ProducesValidProfile`)が`ubsan`(clang-cl)ビルドでのみハングする (P2 — 🟡 WI-39で再現調査、再現できず監視継続)

- **起票日:** 2026-09-05(WI-29のRelease/ASan/UBSan検証中にサブエージェントが発見)
- **調査日:** 2026-09-09(WI-39)、11回連続で再現せず。副次的に関連issue([`measure_frame_hangs_forever_on_hidden_window.md`](measure_frame_hangs_forever_on_hidden_window.md))を発見・起票
- **対象:** `src/app/main.cpp`の`--measure-frame`パス(実際のハング箇所は未特定)、`tests/integration/frame_measure_test.cpp`
- **優先度:** P2(`release`/`asan`ビルドでは再現しない、`--measure-frame`はベンチマーク計測専用のコマンドラインフラグでエンドユーザー機能ではない)

## 事実

`ctest --preset ubsan`実行時、`FrameMeasureTest.ProducesValidProfile`(`tests/integration/frame_measure_test.cpp:74`)が唯一失敗する。このテストは`NeoMIFES.exe --measure-frame <file>`を子プロセスとして起動し(`spawnAndWait()`, `frame_measure_test.cpp:35`)、30秒のタイムアウト内に終了することを期待する(`ASSERT_EQ(exitCode, 0u)`)。実際には子プロセスが終了せず、`exitCode`がタイムアウト時のセンチネル値`0xFFFFFFFF`のまま返る。

**`release`/`asan`プリセットでは同一テストが問題なくpassする——`ubsan`(clang-cl)プリセットでのみ再現する。** サブエージェントが手動で`NeoMIFES.exe --measure-frame <file>`(`build\ubsan`配下のバイナリ)を実行したところ、60秒経過してもプロセスが終了せず出力JSONも生成されなかった。

## 前提条件の切り分け(WI-29とは無関係と確認済み)

WI-29(`render_pipeline.cpp`のプローブ文字列拡張+`SetLineSpacing()`追加)の作業中に発見されたが、**WI-29が原因ではない**ことを確認済み: `git stash`でWI-29の変更を退避し、直前のコミット(`93468ad`、WI-28)に対して`ubsan`を再ビルド、同じ手動再現手順を実行したところ**同一のハングが再現した**(45秒経過後も未終了)。したがって本issueはWI-29より前から存在する潜在バグである。

サニタイザ診断は一切検出されていない(`runtime error:`/`AddressSanitizer`/`heap-buffer-overflow`/`stack-buffer-overflow`/`use-after-free`/`^SUMMARY:`のいずれもctest全文ログに一致なし)——UBSanが実際の未定義動作を検出してabortしているのではなく、単純なハング(デッドロック・無限ループ・イベント待ちの取りこぼし等)である可能性が高い。

## 影響

- CIの`UBSan (clang-cl)`ジョブがこのテストにヒットした場合、そのジョブ自体がタイムアウトするまでブロックされる可能性がある(現状のCI設定でこのテストが実際に実行されているか、タイムアウト設定がどうなっているかは未確認)。
- `--measure-frame`はエンドユーザー向け機能ではなく、フレーム性能計測用の開発者向けコマンドラインフラグのため、実際の製品機能への影響は無い。

## 原因(未調査)

`release`/`asan`(いずれもMSVCコンパイル)では問題なく、`ubsan`(clang-cl特有)でのみ再現するため、clang-clでコンパイルされた場合にのみ顕在化するタイミング依存の問題(スレッド同期・メッセージポンプの何らかの差異等)が疑われるが、未調査。`main.cpp`の`--measure-frame`実装のどこでブロックしているか(メッセージループ・レンダリング完了待ち・ファイルI/O等)の特定はこれから行う必要がある。

## 調査結果(2026-09-09、WI-39、再現できず)

このセッションでの再現を試みたが、**一貫して再現しなかった**。実施内容:

1. `ctest --preset ubsan -R "^frame_measure$"`を6回連続実行 → **6/6回とも正常終了**(約5.27〜5.32秒)。
2. 実際のテストが使う`CreateProcessW(CREATE_NO_WINDOW)`呼び出しの直接再現も試みたが、P/Invoke側の実装課題(`ERROR_INVALID_NAME`)で完走できず、代わりに①のctest直接実行で代替した。
3. 本セッション自体でも、WI-35〜WI-38の各検証(いずれもRelease/ASan/UBSan3構成のフル検証)で本テストは**5回連続で正常終了**しており、一度もこのハングを観測していない。

**合計11回連続で再現せず。** この結果は、当初の報告(2026-09-05、WI-29の検証時)が**CIランナー固有の一時的な要因による一度きりの事象だった可能性が高い**ことを示唆する。ただし「絶対に再発しない」ことの証明ではなく、非決定的な性質上、今後も低頻度で再発しうる。

**副次的発見(関連するが別issueとして起票):** 調査中、`--measure-frame`をウィンドウ非表示(`SW_HIDE`)状態で起動すると、Release/UBSanいずれのプリセットでも**100%再現してハングする**ことを発見した。`Present1(1, 0, ...)`(vsync同期Present)がウィンドウの合成状態に依存してブロックしうるという、本issueと類似のメカニズムだが、`CREATE_NO_WINDOW`(実際のテストが使う機構)は`SW_HIDE`とは異なりウィンドウ表示状態に影響しないため、両者を同一の根本原因と断定する証拠は無い。詳細は[`measure_frame_hangs_forever_on_hidden_window.md`](measure_frame_hangs_forever_on_hidden_window.md)(新規、P2)として別途起票した。

## 対応案

1. ~~`ubsan`ビルドの`NeoMIFES.exe --measure-frame`を実機でアタッチ/デバッグし、実際にどこでブロックしているか特定する。~~ → 再現しないため実施不可能、監視継続へ切り替え
2. **現時点の判断: 積極的な追加調査は行わず、監視継続とする。** `--measure-frame`は開発者向けフラグで製品機能への影響が無く、11回連続で再現しない非決定的事象に対しさらに時間を投じる費用対効果は低いと判断。再発した場合(CI・ローカルいずれでも)は、その時点でのビルドログ・環境情報を残した上で改めて調査する。

## 完了条件

- [~] ハング箇所を特定する — **未達、正直に記録。** 11回の再現試行(このセッションのみで)がいずれも成功したため特定不能。原因調査ではなく再発時の証拠保全へ方針転換
- [ ] 修正するか、恒久的な回避策(該当プリセットでのテストスキップ等)を実施する — 再現しない限り対応不要と判断、監視継続

## 再検証コマンド

```powershell
# ubsanプリセットをビルド済みの状態で
ctest --preset ubsan -R FrameMeasureTest
```
