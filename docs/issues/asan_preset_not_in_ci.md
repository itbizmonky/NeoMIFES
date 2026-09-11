# Issue: `asan` プリセットがCIに常設化されていない (P2 — 🟢 WI-43で解消)

- **起票日:** 2026-08-16 (WI-13、MVP出荷判定)
- **解決日:** 2026-09-11 (WI-43)
- **対象:** `.github/workflows/ci.yml`
- **優先度:** P2 (WI-13でローカル実行によりDoD「ASan/UBSanクラッシュ0」自体は満たしたが、以後の継続的な検証保証が無かった)
- **親文書:** WI-13 (`build_plan.md` §6)

## 事実(起票時点)

`CMakePresets.json`には`debug`/`release`/`asan`(MSVC AddressSanitizer)/`ubsan`(clang-cl UndefinedBehaviorSanitizer)の4プリセットが定義されているが、`.github/workflows/ci.yml`は`debug`/`release`/`ubsan`の3つのみを実行しており、`asan`プリセットはPhase 0.5でのプリセット新設以来、CIはおろかローカルの通常WI検証フロー(`build_plan.md` §4.3)でも一度も実行されていなかった。WI-13で初めてローカル実行し、build_plan.md §6のDoD「ASan/UBSanクラッシュ0」を文字通り満たした。

## 影響(起票時点)

- WI-13完了時点のスナップショットではASanクリーンだが、以後のWIでこのプリセットを継続実行する仕組みが無いため、将来のヒープ破損・use-after-free等のバグ(UBSanが検出する未定義動作とは異なるクラスの実行時エラー)がCIをすり抜ける可能性があった。
- `ubsan`プリセットは既に別のサニタイザ(未定義動作検出)を担っており、`asan`(メモリ破損検出)とは検出対象が異なるため、片方だけでは代替にならない。

## 解決内容(WI-43)

issueが挙げていた3択(①CI常設追加/②週次スケジュール実行/③手動運用の明文化)のうち、①の実施コストを再調査したところ、当初想定していた「トレードオフ」の大部分が実質的に存在しないと判明した。

- `gh repo view`でリポジトリがpublicと確認 — **GitHub-hosted runnerのActions実行時間はpublicリポジトリでは無料・無制限**であり、課金面のコストはゼロ。
- `.github/workflows/ci.yml`の`build-and-test`ジョブは既に`debug`/`release`を`matrix.preset`で**並列実行**している設計だった。`asan`を同じmatrixへ追加すれば並列実行されるため、CI全体のwall-clock時間への影響もほぼ無い(新規ジョブを直列追加するのではなく、既存の並列matrixへ1要素追加するだけで済んだ)。

これにより①を選ぶにあたって②・③との比較検討が不要になり、`.github/workflows/ci.yml`の`matrix.preset`を`[debug, release]`→`[debug, release, asan]`へ1行変更して常設化した。`CMakePresets.json`の`binaryDir`が全プリセット共通の`${sourceDir}/build/${presetName}`のため、既存の`build/${{ matrix.preset }}`パス規約と自動的に整合することも確認済み。「Startup PoC」「Frame PoC」「Upload compile_commands.json」の3ステップは`matrix.preset`の値で明示的にガードされているため、`asan`追加による影響は無い。

併せて`docs/design/build_plan.md` §2.1/§4.3のローカル検証規定を「フル3構成(Debug/Release/UBSan)」から「フル4構成(Debug/Release/ASan/UBSan)」へ改訂した——本セッションを含め実態としては既にASanも含めた4構成での検証が定着していたが、文書記述がそれに追従していなかった乖離も併せて解消した。

## 完了条件

- [x] `asan`プリセットの継続的検証方針を決定した(①CI常設追加、publicリポジトリ+並列matrix追加によりコストがほぼ無いと判明したため)
- [x] 決定した方針を`.github/workflows/ci.yml`(matrix追加)および`docs/design/build_plan.md` §2.1/§4.3(フル3構成→フル4構成への文言改訂)へ反映した

## 再検証コマンド

```bash
grep -n "preset:" .github/workflows/ci.yml
```
