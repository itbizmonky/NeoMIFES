# NeoMIFES

Windows 向け純粋ネイティブテキストエディタ。C++23 + Win32 API + Direct2D/DirectWrite で実装し、「Windows 最速・最軽量・AI 親和」を目指す。

## 現在の状態

**Phase 0.5 — ビルド基盤整備中。** アプリケーションはまだ本体機能を持ちません。

## ドキュメント

- [要件定義書](NeoMIFES_要件定義書.md)
- [プロジェクト運用ガイド (CLAUDE.md)](CLAUDE.md)
- [基本設計書](docs/design/basic_design.md)
- [詳細設計書](docs/design/detailed_design.md)
- [マスターロードマップ (未着手フェーズの実装詳細計画)](docs/design/master_roadmap.md)
- [自己レビュー](docs/design/self_review.md)
- [Architecture Decision Records](docs/decisions/README.md)
- [フェーズレポート](docs/phase_reports/)

## ビルド

**前提:** Visual Studio 2022 17.13 以上 + CMake 3.28 以上 + Ninja。

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

Release 版 / AddressSanitizer 版のプリセットも同名で用意しています。

## ライセンス

[MIT License](LICENSE) — 商用/改変/再配布いずれも自由。詳細は `LICENSE` ファイル参照。
