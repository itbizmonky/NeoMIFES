# Issue: `--measure-frame`(`FrameMeasureTest.ProducesValidProfile`)が`ubsan`(clang-cl)ビルドでのみハングする (P2 — 未修正)

- **起票日:** 2026-09-05(WI-29のRelease/ASan/UBSan検証中にサブエージェントが発見)
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

## 対応案(未実施)

1. `ubsan`ビルドの`NeoMIFES.exe --measure-frame`を実機でアタッチ/デバッグし、実際にどこでブロックしているか特定する。
2. 原因判明後、修正するか、`FrameMeasureTest`を`ubsan`プリセットでは実行しない(CI側でスキップ)という回避策を検討する。

## 完了条件

- [ ] ハング箇所を特定する
- [ ] 修正するか、恒久的な回避策(該当プリセットでのテストスキップ等)を実施する

## 再検証コマンド

```powershell
# ubsanプリセットをビルド済みの状態で
ctest --preset ubsan -R FrameMeasureTest
```
