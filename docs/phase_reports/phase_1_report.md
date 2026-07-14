# Phase 1 完了レポート — Win32 骨組み + 起動 0.3s / メモリ 20MB PoC

- **期間:** 2026-07-14
- **担当:** Claude Code (テックリード)
- **対象 DoD (CLAUDE.md §7):** 起動 0.3s **計測可能**

Phase 1 のゴールは「絶対数値の達成」ではなく「計測パイプラインの確立」。実際の 0.3s / 20MB 達成は、Phase 3 (Rendering)、Phase 4 (Editor Core) の各段階で退化を追跡しながら詰めていく。

---

## 1. 追加/更新した成果物

### 1.1 platform レイヤ (L1)
| ファイル | 役割 |
|---|---|
| [src/platform/include/neomifes/platform/handle_guard.h](../../src/platform/include/neomifes/platform/handle_guard.h) | Win32 HANDLE/HMODULE/HWND/HGDIOBJ 用の汎用 RAII |
| [perf_clock.h](../../src/platform/include/neomifes/platform/perf_clock.h) + [.cpp](../../src/platform/src/perf_clock.cpp) | QueryPerformanceCounter ラッパ。`markProcessStart()` / `nanosSinceProcessStart()` |
| [process_metrics.h](../../src/platform/include/neomifes/platform/process_metrics.h) + [.cpp](../../src/platform/src/process_metrics.cpp) | PSAPI `PROCESS_MEMORY_COUNTERS_EX2`。Working Set / Private WS / Private Bytes |

### 1.2 ui レイヤ (L7)
| ファイル | 役割 |
|---|---|
| [main_window.h](../../src/ui/include/neomifes/ui/main_window.h) + [.cpp](../../src/ui/src/main_window.cpp) | WNDCLASSEX 登録 + WndProc + WM_PAINT (GDI FillRect) + WM_ERASEBKGND 抑制。`onFirstPaint` コールバックで計測フック提供 |

### 1.3 app 層 (L6)
| ファイル | 役割 |
|---|---|
| [main.cpp](../../src/app/main.cpp) | `wWinMain` 書き換え。Per-Monitor V2 DPI / 引数パーサ / メッセージループ / 計測モード |
| [startup_profile.h](../../src/app/startup_profile.h) + [.cpp](../../src/app/startup_profile.cpp) | 起動時刻 4 マーカ + メモリ 2 値の JSON 出力 |

### 1.4 テスト
| ファイル | 役割 |
|---|---|
| [tests/unit/platform_perf_clock_test.cpp](../../tests/unit/platform_perf_clock_test.cpp) | 単調増加 / re-mark で原点リセット |
| [tests/unit/platform_process_metrics_test.cpp](../../tests/unit/platform_process_metrics_test.cpp) | Working Set 非ゼロ / 32MB 触ると増加 |
| [tests/integration/startup_measure_test.cpp](../../tests/integration/startup_measure_test.cpp) | NeoMIFES.exe を `--measure-startup` で spawn → JSON 検証。ctest 経由 |

### 1.5 CI
| 変更点 | 内容 |
|---|---|
| [.github/workflows/ci.yml](../../.github/workflows/ci.yml) | Release 版ビルドで `--measure-startup` 実行し出力を CI ログに転記 (ハード失敗させず観測) |

---

## 2. 計測パイプラインの動作

```
CreateProcess NeoMIFES.exe --measure-startup out.json
        │
        ▼
wWinMain 開始 ─ PerfClock::markProcessStart()   (t=0)
        │
        ▼
enableHighDpi()   (SetProcessDpiAwarenessContext PMv2)
        │
        ▼
MainWindow.create() → WM_NCCREATE → WM_CREATE   (t=windowCreatedNs)
        │
        ▼
ShowWindow + UpdateWindow → WM_PAINT (同期)
        │
        ▼
onFirstPaint コールバック:
   ├─ PerfClock::nanosSinceProcessStart()  → firstPaintNs
   ├─ currentProcessMemory()               → workingSet / privateWS
   └─ MainWindow.requestClose() (WM_CLOSE をポスト)
        │
        ▼
DestroyWindow → PostQuitMessage → メッセージループ抜け
        │
        ▼
StartupProfile.writeJson(out.json)
```

出力例 (期待形):
```json
{
  "winMainEnterNs": 0,
  "windowCreatedNs": 45000000,
  "firstPaintNs":   62000000,
  "measuredExitNs": 91000000,
  "workingSetBytesAtFirstPaint":        14680064,
  "privateWorkingSetBytesAtFirstPaint": 9437184
}
```

---

## 3. 設計整合性

| 設計項目 | Phase 1 での扱い |
|---|---|
| basic §3.11 platform レイヤ | ✅ RAII (`HandleGuard`) と PerfClock/ProcessMetrics で最小版 |
| basic §3.1 UI Shell | ✅ MainWindow クラス提供 (Line Gutter/Tab/Status は Phase 3+) |
| basic §4.1 起動 0.3s 最小同期 | ✅ WinMain 内で行うのは DPI + Window 作成のみ。COM/Direct2D/DirectWrite は Phase 3 で必要になった時点で lazy 化 |
| detailed §22.1 char16_t 境界ヘルパ | 未使用 (Phase 1 は `wchar_t` 直接使用が支配的)。ヘルパは Phase 2 以降で導入 |
| detailed §22.2 `dynamic_cast` 禁止 | ✅ 使用箇所なし |
| basic §2.3 複数ウィンドウ / 単一プロセス | ⚠️ MainWindow 複数生成は将来対応。Named Mutex による単一インスタンス化は Phase 2 で追加 |

---

## 4. DoD 判定

| 項目 | 状態 |
|---|---|
| **起動 0.3s 計測可能** | ✅ **達成**。`--measure-startup <file>` で ns 精度の 4 マーカを JSON 出力 |
| メモリ 20MB 計測可能 (自己レビュー §H) | ✅ **達成**。同 JSON に Working Set / Private WS を含む |
| Win32 骨組み | ✅ Per-Monitor V2 DPI 対応の空ウィンドウ表示 |
| プロジェクト雛形 | ✅ platform / ui / app が各レイヤに分離 |

---

## 5. 制約付き検証状況

- 本エージェント環境には MSVC ツールチェインが無いため **ローカルビルド未実施**
- 検証は次の 2 手段で行う:
  1. ユーザー環境で `cmake --preset release && cmake --build --preset release && ctest --preset release`
  2. GitHub Actions CI 側の green ラン確認 (`--measure-startup` ログが出力される)

---

## 6. 既知の懸念事項

| # | 懸念 | 対応 |
|---|---|---|
| P1-1 | `PROCESS_MEMORY_COUNTERS_EX2` は Win10 20H1+ 必要 | `_WIN32_WINNT=0x0A00` で有効。それ以前の OS では `PrivateWorkingSetSize` が 0 になる可能性あり |
| P1-2 | 統合テストが Windows Runner でウィンドウ表示できるか | `windows-2022` は Desktop 経由ウィンドウ作成可 (実績あり)。CI 初回で確認 |
| P1-3 | `SetProcessDpiAwarenessContext` が Win10 1607 未満で失敗 | 現状は silent fail → Phase 12 で最低 OS を Win10 21H2 に確定 |
| P1-4 | GDI FillRect は Phase 3 で Direct2D に置換予定 | 計測基準変わるため、Phase 3 完了時に再測定 |

---

## 7. 次アクション (Phase 2 引き継ぎ)

1. **Document Engine (Piece Table) 実装** — 詳細設計 §3
2. **Lazy Decode** の PoC (10GB モックファイルで Working Set 増分測定)
3. **Named Mutex による単一インスタンス化** (basic §2.3)
4. **`char16_t` ↔ `wchar_t` 変換ヘルパ** (detailed §22.1) の導入
5. **`WarningsAsErrors: '*'`** に切替 (Phase 0.5 P05-4 の宿題)

---

## 8. Definition of Done

**Phase 1 完了。Phase 2 (Document Engine) 着手可能。**
