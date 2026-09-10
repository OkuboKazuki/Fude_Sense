# AGENTS.md

このリポジトリは、Wacomペンタブレット / Multi-Touch API および Wintab32 API を用いた習字制作ワークスペース「**SHUJI STUDIO (Fudesence)**」のプロジェクトです。

---

## 1. プロジェクト概要

- **言語・環境**: C++ (C++17 / C++20), Win32 API, GDI / Direct2D / Direct3D 11
- **プラットフォーム**: Windows (Win32 / x86 構成)
- **アーキテクチャ**: MVC + Input Adapter + InkEngine（デバイス非依存・モジュール設計）
- **主要機能**:
  - デバイス非依存のペン入力抽象化（Wintab / Windows Ink / Pointer API 対応可能）
  - Direct2D / CPU ハイブリッドによるリアルタイムな墨汁浸透・物理にじみシミュレーション（`InkEngine`）
  - 運筆に伴うインク消費とカスレ（かすれ）の物理連動シミュレーション
  - 筆（小筆・中筆・大筆、硬さ・感度調整、止め・払い・傾き連動）
  - 用紙（半紙・条幅・色紙・短冊）および各種下敷き格子ガイド（十字、田の字、米字格等）
  - お手本オーバーレイ表示
  - 硯（すずり）での墨補充および墨残量管理
  - 作品の画像保存（PNG/BMP）およびクリップボードコピー

---

## 2. アーキテクチャ構成

```text
App/SampleCode/
├── Models/                          # [Model] アプリデータ・設定
│   ├── AppEnums.h                   # 共通列挙型 (Brush, PaperType, GridPattern 等)
│   └── AppState.h / .cpp            # 筆・紙・お手本・UI状態統合
│
├── InkEngine/                       # ★ [Ink System] インク管理・物理にじみシミュレーション ★
│   ├── GpuInk.h / .cpp              # Direct2D/D3D11 リアルタイム墨汁浸透・物理エンジン
│   └── InkModel.h                   # 墨残量・筆保水量・カスレ物理モデル
│
├── Views/                           # [View] 描画・プレゼンテーション
│   ├── RenderUtils.h / .cpp         # GDI描画ヘルパー関数 (Box, Text, Font, Fill 等)
│   ├── CanvasView.h / .cpp          # 半紙・下敷き・お手本・墨ストローク描画
│   ├── FloatingMenuView.h / .cpp    # 左側フローティングパネル (筆/紙/保存/お手本)
│   ├── InkStoneView.h / .cpp        # 右側 硯・墨残量・墨補充・全消し描画
│   ├── ModalView.h / .cpp           # 全消し確認モーダル描画
│   └── MainView.h / .cpp            # ダブルバッファリングと描画統括
│
├── Controllers/                     # [Controller] 運筆・UI制御ロジック
│   ├── StrokeController.h / .cpp    # 運筆計算（筆圧平滑化・止め払い・線幅・インク消費/カスレ）
│   └── AppController.h / .cpp       # UI操作（パネル開閉・タブ切替・設定変更・モーダル）
│
├── Inputs/                          # [Input Adapter] デバイス固有入力の抽象化
│   ├── PenInputEvent.h              # デバイス非依存の正規化ペン入力構造体
│   └── WintabAdapter.h / .cpp       # Wintab PACKET -> PenInputEvent 変換
│
├── Services/                        # [Service] OS・外部デバイス連携
│   ├── WintabManager.h / .cpp       # Wintabコンテキスト・デバイス管理
│   ├── ImageExporter.h / .cpp       # 画像エクスポート (BMP保存、クリップボード)
│   └── WintabUtils.h / .cpp         # Wintab ユーティリティ
│
├── Resource.h / Fudesence.rc        # Win32リソース
└── WacomMT_Scribble.cpp             # メインエントリ / イベントディスパッチ
```

---

## 3. ビルド & 実行方法

### ソリューション / プロジェクト

- ソリューションファイル: `App/SampleCode/Fudesence.sln`
- プロジェクトファイル: `App/SampleCode/Fudesence.vcxproj`

### MSBuild コマンドライン

```powershell
# Debug ビルド (Win32)
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' App\SampleCode\Fudesence.sln /p:Configuration=Debug /p:Platform=Win32

# Release ビルド (Win32)
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' App\SampleCode\Fudesence.sln /p:Configuration=Release /p:Platform=Win32
```

---

## 4. コーディングルール

1. **命名規則**:
   - **クラス / 構造体 / 列挙型**: PascalCase (`BrushModel`, `StrokeController`, `WintabAdapter`, `GpuInk`)
   - **関数 / メソッド**: PascalCase (`DrawSegment`, `ProcessPenEvent`, `ConvertPacket`, `ConsumeInk`)
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
