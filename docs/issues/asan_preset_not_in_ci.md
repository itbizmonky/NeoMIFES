# Issue: `asan` プリセットがCIに常設化されていない (P2 — 待機)

- **起票日:** 2026-08-16 (WI-13、MVP出荷判定)
- **対象:** `.github/workflows/ci.yml`
- **優先度:** P2 (WI-13でローカル実行によりDoD「ASan/UBSanクラッシュ0」自体は満たしたが、以後の全WIで継続的に検証される保証がない)
- **対応 Phase:** 未定 (実行時間増加とのトレードオフ検討が必要)
- **親文書:** WI-13 (`build_plan.md` §6)

## 事実

`CMakePresets.json`には`debug`/`release`/`asan`(MSVC AddressSanitizer)/`ubsan`(clang-cl UndefinedBehaviorSanitizer)の4プリセットが定義されているが、`.github/workflows/ci.yml`は`debug`/`release`/`ubsan`の3つのみを実行しており、**`asan`プリセットはPhase 0.5でのプリセット新設以来、CIはおろかローカルの通常WI検証フロー(`build_plan.md` §4.3)でも一度も実行されていなかった。** WI-13で初めてローカル実行し、build_plan.md §6のDoD「ASan/UBSanクラッシュ0」を文字通り満たした。

## 影響

- WI-13完了時点のスナップショットではASanクリーンだが、以後のWIでこのプリセットを継続実行する仕組みが無いため、将来のヒープ破損・use-after-free等のバグ(UBSanが検出する未定義動作とは異なるクラスの実行時エラー)がCIをすり抜ける可能性がある。
- `ubsan`プリセット既に別のサニタイザ(未定義動作検出)を担っており、`asan`(メモリ破損検出)とは検出対象が異なるため、片方だけでは代替にならない。

## 対応方針 (未着手)

CI実行時間の増加(4番目のフルビルド+テストジョブ追加)とのトレードオフを検討した上で判断する。選択肢:
1. `ci.yml`へ`asan`ジョブを恒久追加(実行時間増、確実性向上)
2. 定期的(週次等)なスケジュール実行のみに留める
3. 大きめの変更(WI完了時等)のみ手動実行する運用を明文化する(現状の実態に近いが、明文化されていない)

## 完了条件

- [ ] `asan`プリセットの継続的検証方針(上記1〜3のいずれか、または別案)を決定した
- [ ] 決定した方針を`build_plan.md` §4.3または`.github/workflows/ci.yml`へ反映した

## 再検証コマンド

```bash
grep -n "asan" .github/workflows/ci.yml
```
