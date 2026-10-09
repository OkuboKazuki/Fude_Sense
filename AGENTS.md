# AGENTS.md

このリポジトリは、Wacom ペンタブレット（Wintab32 API）と Windows Pointer API を用いた習字制作ワークスペース「**Fude Sense**」のプロジェクトです。

---

## 1. プロジェクト概要

- **言語・環境**: C++（`LanguageStandard` 未指定のため MSVC 既定の C++14）, Win32 API, GDI / Direct2D / Direct3D 11
- **プラットフォーム**: Windows (Win32 / x86 構成)
- **アーキテクチャ**: MVC + Input Adapter + InkEngine（デバイス非依存・モジュール設計）
- **主要機能**:
  - デバイス非依存のペン入力抽象化（Wintab / Windows Pointer API / マウス）
  - Direct3D 11 Compute Shader（使えない環境では CPU）によるリアルタイムな墨汁浸透・物理にじみシミュレーション（`InkEngine`）
  - 運筆に伴うインク消費とカスレ（かすれ）の物理連動シミュレーション
  - 筆（小筆・中筆・大筆、硬さ・感度調整、止め・払い・傾き連動）
  - 用紙（半紙）および各種下敷き格子ガイド（十字、2段、田の字、2x3、2x4）
  - お手本表示（入力した文字を升目ミニマップで配置、楷書・教科書体・行書）
  - 硯（すずり）での墨補充（ペンの筆圧に応じた量）および墨残量管理
  - 一画戻す / 一画復元
  - 紙を大きくする表示（F9、半紙を横向きにして画面いっぱいに表示）
  - 運筆の記録・リプレイ・解析（JSON / CSV 書き出し）
  - 作品の画像保存（PNG/BMP）およびクリップボードコピー

---

## 2. アーキテクチャ構成と設計方針

### 描画パイプラインのハイブリッド設計 (GDI + Direct2D / D3D11)
- **UI ウィジェット層 (GDI)**:
  - 左側フローティングメニュー、硯パネル、モーダル、文机背景などの UI 部品は Win32 GDI で実装。
  - 軽量・低レイテンシで確実な描画とフリッカーフリー（ダブルバッファリング）を実現し、Windows 10/11 の GDI Scaling により高 DPI 環境でもシャープに描画されます。
- **墨汁物理シミュレーション層 (Direct2D / Direct3D 11 Compute Shader)**:
  - 半紙上の墨汁浸透・水分拡散・セルラーオートマトン計算には Direct3D 11 Compute Shader を採用。
  - Direct3D 11 で生成した `B8G8R8A8_UNORM` ピクセルテクスチャを Direct2D 1.1 の `ID2D1Bitmap1`（DirectX Interop: `CreateBitmapFromDxgiSurface`）として VRAM 内で直接共有（ゼロコピー描画）。
  - 毎フレームの GPU ↔ CPU 往復転送（PCIe バス帯域の消費）を完全撤廃し、極めて低遅延なリアルタイム運筆レスポンスを実現しています。
  - 半紙への墨汁描画は `SRCAND` による乗算合成を行うことで、アンチエイリアス境界の白縁取りを解消し、和紙の地色に自然に墨が染み込む表現を可能にしています。

### 状態管理のファサード設計 (AppState)
- `AppState` は、Models (筆・紙・お手本・物理パラメータ), Controllers (運筆・UI制御), Views (各UIパネル) の間を仲介する統合ファサード（Facade）として設計されています。
- サブシステム間の疎結合を保ちつつ、運筆中の瞬時なパラメータ参照（筆圧、硬さ補正、インク消費量）をキャッシュミスなく O(1) で高速アクセス可能にしています。

```text
App/FudeCode/
├── Models/                          # [Model] アプリデータ・設定
│   ├── AppEnums.h                   # 共通列挙型 (Brush, PaperType, GridPattern 等)
│   ├── UIComponents.h               # UI ウィジェット（RECT 互換）
│   ├── TrajectoryModel.h / .cpp     # 運筆時系列アーカイブ・リプレイモデル
│   ├── UndoHistory.h / .cpp         # RLE圧縮による一画戻す/復元履歴
│   └── AppState.h / .cpp            # 筆・紙・お手本・UI状態統合ファサード
│
├── InkEngine/                       # ★ [Ink System] インク管理・物理にじみシミュレーション ★
│   ├── GpuInk.h / .cpp              # Direct2D/D3D11 リアルタイム墨汁浸透・物理エンジン
│   ├── GpuSimulator.h / .cpp        # Direct3D 11 Compute Shader 浸透シミュレータ
│   ├── ReplayInk.h / .cpp           # 運筆リプレイ墨汁再生エンジン
│   ├── InkSnapshot.h                # 墨バッファの控え（一画戻す・紙を大きくする表示の出入り）
│   └── InkModel.h                   # 墨残量・筆保水量・カスレ物理モデル
│
├── Views/                           # [View] 描画・プレゼンテーション (GDI + D2D)
│   ├── RenderUtils.h / .cpp         # GDI描画ヘルパー関数 (Box, Text, Font, Fill 等)
│   ├── TitleView.h / .cpp           # タイトル画面（ロゴ・「タップして硯へ向かう」）
│   ├── CanvasView.h / .cpp          # 半紙・下敷き・お手本・墨ストローク描画
│   ├── FloatingMenuView.h / .cpp    # 左側フローティングパネル (筆/紙/保存/お手本)
│   ├── InkStoneView.h / .cpp        # 右側 硯・墨残量・墨補充・全消し描画
│   ├── AnalysisView.h / .cpp        # リアルタイム運筆解析・3D筆姿勢モニタ描画
│   ├── ModalView.h / .cpp           # 全消し確認モーダル描画
│   └── MainView.h / .cpp            # ダブルバッファリングと描画統括
│
├── Controllers/                     # [Controller] 運筆・UI制御ロジック
│   ├── StrokeController.h / .cpp    # 運筆計算（筆圧平滑化・止め払い・線幅・インク消費/カスレ）
│   └── AppController.h / .cpp       # UI操作（パネル開閉・タブ切替・設定変更・モーダル）
│
├── Inputs/                          # [Input Adapter] デバイス固有入力の抽象化
│   ├── PenInputEvent.h              # デバイス非依存の正規化ペン入力構造体
│   ├── WintabAdapter.h / .cpp       # Wintab PACKET -> PenInputEvent 変換
│   ├── WindowsPointerAdapter.h      # Windows Ink / Pointer API 変換アダプタ
│   └── MouseAdapter.h               # マウス入力フォールバックアダプタ
│
├── Services/                        # [Service] OS・外部デバイス連携
│   ├── WintabManager.h / .cpp       # Wintabコンテキスト・デバイス管理
│   ├── ImageExporter.h / .cpp       # 画像エクスポート (WIC PNG / BMP, クリップボード)
│   └── WintabUtils.h / .cpp         # Wintab ユーティリティ
│
├── Wintab_SDK/                      # Wintab SDK ヘッダー（Wacom 配布物）
├── Wacom_Feel_SDK/                  # Wacom Feel Multi-Touch API（ビルドに含むが未使用）
├── title_logo.png                   # タイトル画面のロゴ
├── desk_texture.jpg                 # 文机の背景テクスチャ
├── Resource.h / FudeSense.rc        # Win32リソース
└── WacomMT_Scribble.cpp             # メインエントリ / イベントディスパッチ
```

---

## 3. ビルド & 実行方法

### ソリューション / プロジェクト

- ソリューションファイル: `App/FudeCode/FudeSense.sln`
- プロジェクトファイル: `App/FudeCode/FudeSense.vcxproj`

### MSBuild コマンドライン

```powershell
# Debug ビルド (Win32)
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' App\FudeCode\FudeSense.sln /p:Configuration=Debug /p:Platform=Win32

# Release ビルド (Win32)
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' App\FudeCode\FudeSense.sln /p:Configuration=Release /p:Platform=Win32
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
