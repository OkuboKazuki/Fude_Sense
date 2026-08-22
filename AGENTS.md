# AGENTS.md

このリポジトリは、Wacomペンタブレット / Multi-Touch API および Wintab32 API を用いた習字制作ワークスペース「**SHUJI STUDIO (Fudesence)**」のプロジェクトです。

---

## 1. プロジェクト概要

- **言語・環境**: C++ (C++17 / C++20), Win32 API, GDI / Direct2D / Direct3D 11
- **プラットフォーム**: Windows (Win32 / x86 構成)
- **アーキテクチャ**: MVC (Model-View-Controller) 設計
- **主要機能**:
  - Wintab API による高精度な筆圧・筆先傾き・速度・方位角の取得
  - Direct2D / CPU ハイブリッドによるリアルタイムな墨汁浸透・物理にじみシミュレーション（`GpuInk`）
  - 筆（小筆・中筆・大筆、硬さ・感度調整）
  - 用紙（半紙・条幅・色紙・短冊）および各種下敷き格子ガイド（十字、田の字、米字格等）
  - お手本オーバーレイ表示
  - 硯（すずり）での墨補充および墨残量管理
  - 作品の画像保存（PNG/BMP）およびクリップボードコピー

---

## 2. アーキテクチャ構成 (MVC)

```text
App/SampleCode/
├── Models/                          # [Model] 状態・ビジネスロジック
│   ├── AppEnums.h                   # 共通列挙型 (Brush, PaperType, GridPattern 等)
│   └── AppState.h / .cpp            # アプリ全体の状態統合 (Brush, Paper, Otehon, Ink, UI)
│
├── Views/                           # [View] 描画・プレゼンテーション
│   ├── Common/RenderUtils.h / .cpp  # GDI描画ヘルパー関数 (Box, Text, Font, Fill 等)
│   ├── CanvasView.h / .cpp          # 半紙・下敷き・お手本・墨ストローク描画
│   ├── FloatingMenuView.h / .cpp    # 左側フローティングパネル (筆/紙/保存/お手本)
│   ├── InkStoneView.h / .cpp        # 右側 硯・墨残量・墨補充・全消し描画
│   ├── ModalView.h / .cpp           # 全消し確認モーダル描画
│   ├── StatusBarView.h / .cpp       # 下部ステータスバー描画
│   └── MainView.h / .cpp            # ダブルバッファリングと描画統括
│
├── Services/                        # [Service / Infra] OS・外部デバイス連携
│   ├── WintabManager.h / .cpp       # Wintabコンテキスト・デバイス管理
│   ├── ImageExporter.h / .cpp       # 画像エクスポート (BMP保存、クリップボード)
│   └── GpuInk/GpuInk.h / .cpp       # Direct2D/D3D物理にじみエンジン
│
├── Resource.h / WacomMT_Scribble.rc # Win32リソース
└── WacomMT_Scribble.cpp             # メインエントリ / イベントハンドリング
```

---

## 3. ビルド & 実行方法

### ソリューション / プロジェクト
- ソリューションファイル: `App/SampleCode/WacomMT_Scribble.sln`
- プロジェクトファイル: `App/SampleCode/WacomMT_Scribble.vcxproj`

### MSBuild コマンドライン
```powershell
# Debug ビルド (Win32)
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' App\SampleCode\WacomMT_Scribble.sln /p:Configuration=Debug /p:Platform=Win32

# Release ビルド (Win32)
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' App\SampleCode\WacomMT_Scribble.sln /p:Configuration=Release /p:Platform=Win32
```

---

## 4. コーディングルール

1. **命名規則**:
   - **クラス / 構造体 / 列挙型**: PascalCase (`BrushModel`, `PaperType`, `RenderUtils`)
   - **関数 / メソッド**: PascalCase (`DrawSegment`, `Initialize`, `FillBox`)
   - **メンバー変数**: `m_` プレフィックス + camelCase (`m_width`, `m_pD2DFactory`) または 構造体では camelCase
   - **定数 / constexpr**: SCREAMING_SNAKE_CASE または PascalCase (`INK_MAX_VALUE`, `DEFAULT_DPI`)

2. **C++ モダン化 & 安全性**:
   - 生ポインタの手動管理を避け、リソースの寿命は RAII（スマートポインタ、スマートハンドル、Win32 GDI オブジェクト解放ラッパー）で行う。
   - スレッドセーフな変数には `std::atomic` や `std::mutex` / `std::lock_guard` を適切に使用する。
   - 文字列・文字セットは `wchar_t` / `std::wstring` (Unicode / UTF-16) およびソースコードの UTF-8 (`/utf-8`) を標準とする。

3. **Win32 / GDI リソースの解放**:
   - `CreateSolidBrush` / `CreatePen` / `CreateFontW` 等で作成した GDI オブジェクトは、描画終了時に確実に `DeleteObject` または RAII ラッパーで破棄し、GDI リークを防ぐ。
   - デバイスコンテキストへの `SelectObject` 時は必ず元のオブジェクトを退避し、破棄前に復元する。

4. **ヘッダーの依存関係とインクルードガード**:
   - すべてのヘッダーファイルの先頭に `#pragma once` を記述する。
   - 不要な `<windows.h>` の巨大インクルードの連鎖を避け、必要最小限の前方宣言を活用する。
