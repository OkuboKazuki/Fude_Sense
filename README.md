# Fude Sense

> **物理にじみ・カスレと筆圧生体情報を科学する、本格書道・習字制作ワークスペース**

---

## 1. プロジェクト概要

**Fude Sense** は、Windows および Wacom ペンタブレット（Wintab API / Windows Pointer API）の高度なセンシング性能を最大限に引き出した、デジタル書道制作・運筆解析プラットフォームです。

単なるペイントソフトとは一線を画し、毛筆特有の「穂先の広がり」「毛束のかすれ」「紙への墨汁浸透」「毛管流による物理にじみ」を Direct3D 11 Compute Shader と数理・物理モデルによってリアルタイムにシミュレート。さらに揮毫中の筆圧・姿勢・速度をリアルタイム解析し、書道学習・臨書・運筆の科学的分析とリプレイ再生を可能にします。

---

## 2. コア機能と技術的特徴

### ① Direct3D 11 Compute Shader によるリアルタイム墨汁浸透・物理にじみ
- **二層物理シミュレーション**: 顔料（墨）と水分（キャリア）を独立した物理量としてセルラー・オートマトンでモデル化。
- **GPU 高速演算 & 局所リードバック**: Direct3D 11 Compute Shader で並列計算を行い、運筆領域（Dirty Rect）のみを局所転送することで、極めて高いフレームレートと低遅延な筆追従性を両立。
- **渇筆（かすれ）の物理再現**: 運筆速度・残水分量・紙目テクスチャから毛束の離散接地を幾何学的に判定し、本物の毛筆のような渇筆筋を生成。

### ② 運筆時系列データのアーカイブ & 3D姿勢リプレイ
- **リアルタイム運筆解析**: 筆圧（%）、高度角（°）、運筆速度（px/s）、総画数をリアルタイム計測。
- **筆姿勢・傾きレーダー**: 筆の3D傾き（方位角・高度角）を直感的なコンパスUIで可視化。
- **運筆時系列波形グラフ**: 筆圧・速度の変動グラフをリアルタイムプロット。
- **タイムライン・リプレイ**: 過去の運筆セッションをタイムラインで巻き戻し、筆の空中ホバーを含む3D姿勢アニメーション付きでリアルタイム再生（再生/一時停止、コマ送り/コマ戻し、0.5x / 1.0x / 2.0x 速度変更、シークバー・グラフクリックによるシーク操作）。
- **研究・教育用データ出力**: **JSON アーカイブ** および **CSV 時系列データ** にエクスポート可能（筆圧・姿勢・速度に加え、各点の墨残量と画ごとの時刻を含む）。
- **運筆アーカイブの読み込み**: 解析タブから他人の JSON / CSV を読み込み、リプレイ・波形・筆姿勢で解析可能。自分の記録と半紙の墨はそのまま残り、「自分の記録に戻る」で切り替えられる。

### ③ 実践的な書道支援システム
- **タイトル画面（Title）**: 和モダンな手書き筆文字ロゴとブレスアニメーションによる優美なエントランス画面。タップ/クリックでスタジオ画面へシームレスに遷移。
- **筆設定**: 小筆・中筆・大筆の太さ切り替え、および筆の硬さ（0.1: 超極軟 ～ 2.0: 非常に硬い）の感度補正スライダー。
- **用紙・下敷きガイド**:
  - 用紙: 半紙（実寸比率 242×333mm）
  - 下敷きガイド線: なし（無地）、1字（十字）、2文字（上下2段）、4文字（田の字）、6文字（2x3）、8文字（2x4）
  - 罫線配色テーマ: 朱赤線、高級毛氈白線、薄墨点線
- **お手本オーバーレイ**:
  - 楷書（HG正楷書体-PRO）・教科書体（HG教科書体）・行書（HG行書体）の3書体対応。
  - 任意の文字入力（IME対応）により、入力した文字を候補タイルとして半紙上の指定マス（ミニマップで配置先を選択）へ自由に薄墨配置。
  - 不透明度（薄墨の濃さ）スライダー調整。
- **硯（すずり）と墨汁残量管理**:
  - 運筆に伴い墨と水分がリアルタイム消費。
  - 硯をクリックまたはペンで押し込むことで、筆圧に応じた墨・水分の補給が可能。
- **「紙を大きくする（横向き）」モード（F9キー）**:
  - メニュー類を隠し、横向き大画面で半紙を最大化表示して揮毫に集中可能。
- **一画戻す / 一画復元 (Undo / Redo)**:
  - RLE圧縮スナップショットによる低メモリ・高速な一画単位の復元（履歴残数バッジ付き）。
- **作品画像保存 & コピー**:
  - WIC (Windows Imaging Component) による高解像度 PNG 保存、およびクリップボード転送に対応。

---

## 3. アーキテクチャ

本プロジェクトは、低遅延描画と物理シミュレーションを両立するため、MVC + Input Adapter + InkEngine パターンを採用しています。

```text
[ Wintab API / Windows Pointer API / Mouse ]
                      │ (PenInputEvent)
                      ▼
          [ StrokeController ] ──── 運筆平滑化・インク物理消費計算
                      │
           ┌──────────┴──────────┐
           ▼                     ▼
      [ GpuInk Engine ]   [ TrajectorySession ]
      (D3D11 CS / D2D)     (運筆アーカイブ / リプレイ)
           │
           ▼
      [ CanvasView / MainView ] (ダブルバッファリング描画)
```

- **UI層 (Win32 GDI)**: 左側メニュー、硯パネル、モーダル、文机背景などの UI 部品は Win32 GDI で実装。軽量・低レイテンシでフリッカーフリー（ダブルバッファリング）を実現し、GDI Scaling により高 DPI 環境でもシャープに描画。
- **描画・物理層 (Direct2D & Direct3D 11 Compute Shader)**: 半紙上の墨汁浸透・水分拡散・セルラーオートマトン計算に GPU を活用し、Dirty Rect の局所リードバックで低遅延なリアルタイム運筆レスポンスを実現。
- **状態管理ファサード (`AppState`)**: 筆・紙・お手本・物理パラメータを O(1) で高速仲介。

### ディレクトリ構成

```text
App/FudeCode/
├── Models/                          # [Model] アプリデータ・設定
│   ├── AppEnums.h                   # 共通列挙型 (Brush, PaperType, GridPattern 等)
│   ├── AppState.h / .cpp            # 筆・紙・お手本・UI状態統合ファサード
│   ├── TrajectoryModel.h / .cpp     # 運筆時系列アーカイブ・リプレイモデル
│   ├── UndoHistory.h / .cpp         # RLE圧縮による一画戻す/復元履歴
│   └── UIComponents.h               # UIコンポーネント構造体
│
├── InkEngine/                       # [Ink System] インク管理・物理にじみシミュレーション
│   ├── GpuInk.h / .cpp              # Direct2D/D3D11 リアルタイム墨汁浸透・物理エンジン
│   ├── GpuSimulator.h / .cpp        # Direct3D 11 Compute Shader 浸透シミュレータ
│   ├── ReplayInk.h / .cpp           # 運筆リプレイ墨汁再生エンジン
│   ├── InkModel.h                   # 墨残量・筆保水量・カスレ物理モデル
│   └── InkSnapshot.h                # 一画アンドゥ用 RLE 圧縮スナップショット
│
├── Views/                           # [View] 描画・プレゼンテーション (GDI + Direct2D)
│   ├── RenderUtils.h / .cpp         # GDI描画ヘルパー関数 (Box, Text, Font, Fill 等)
│   ├── CanvasView.h / .cpp          # 半紙・下敷き・お手本・墨ストローク描画
│   ├── FloatingMenuView.h / .cpp    # 左側フローティングパネル (筆/紙/解析/保存/お手本)
│   ├── InkStoneView.h / .cpp        # 右側 硯・墨残量・墨補充・一画操作描画
│   ├── AnalysisView.h / .cpp        # リアルタイム運筆解析・3D筆姿勢モニタ描画
│   ├── ModalView.h / .cpp           # 全消し確認モーダル描画
│   ├── TitleView.h / .cpp           # タイトル画面・ロゴ・ブレス描画
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
├── Wacom_Feel_SDK/                  # Wacom Feel Multi-Touch SDK（同梱）
├── Wintab_SDK/                      # Wintab SDK（同梱）
├── Resource.h / FudeSense.rc        # Win32リソース定義
└── WacomMT_Scribble.cpp             # メインエントリ / イベントディスパッチ
```

---

## 4. 動作環境

- **OS**: Windows 10 / Windows 11（アプリは 32 ビット版としてビルド。64 ビット版 Windows でも動作）
- **推奨入力機器**: 
  - Wacom ペンタブレット / 液晶ペンタブレット（Wintab ドライバ対応機種）
  - Windows Ink / Microsoft Pen Protocol (MPP) 対応デジタイザーペン（Surface等）
  - マウス操作（フォールバック動作に対応）
- **グラフィックス**: DirectX 11 (Feature Level 11_0 以上) 対応 GPU

---

## 5. ビルド手順

### 開発環境
- Visual Studio 2022 (C++ によるデスクトップ開発ワークロード)
- Windows 10/11 SDK
※ Wacom Feel SDK および Wintab SDK はリポジトリ内に同梱されています。

### MSBuild によるコマンドラインビルド
```powershell
# リポジトリ直下から Release ビルドを実行
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' App\FudeCode\FudeSense.sln /p:Configuration=Release /p:Platform=Win32
```
ビルド完了後、`App/FudeCode/Release/FudeSense.exe` が生成されます。

---

## 6. キーボードショートカット

| キー | 操作 |
|---|---|
| **F11** | 全画面表示の切り替え |
| **F9** | 紙を大きくする（横向き）表示の切り替え |
| **Ctrl + Z** | 一画戻す (Undo - RLE圧縮スナップショット復元) |
| **Ctrl + Shift + Z** / **Ctrl + Y** | 一画復元 (Redo) |
| **Esc** | 筆跡をすべて消す (確認モーダル表示) |

