# 墨汁物理・描画エンジンレイヤー (InkEngine)

本レイヤーは、Direct2D 1.1 および Direct3D 11 Compute Shader を駆使し、和紙（半紙）特有の毛細管現象による墨汁の浸透、水分拡散、繊維に沿ったにじみ、ならびに高速運筆時のかすれ（渇筆）をリアルタイムに物理シミュレーションして描画するコアエンジンです。

---

## 1. ファイル構成と関係性

中心となる [`GpuInk`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuInk.h) は、CPU側の粒子描画・線分ラスタライズと、[`GpuSimulator`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuSimulator.h) による GPU コンピュートシェーダー浸透計算を統合し、DirectX Interop（`CreateBitmapFromDxgiSurface`）によって VRAM 内ゼロコピー描画を実現しています。また、運筆リプレイ専用の再現エンジンとして [`ReplayInk`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/ReplayInk.h) が提供されています。

```mermaid
graph TD
    subgraph ControllerLayer ["Controllers レイヤー"]
        SC["StrokeController"]
        AC["AppController"]
    end

    subgraph InkEngineLayer ["InkEngine レイヤー"]
        IM["InkModel<br>(InkModel.h)"]
        IS["InkSnapshot<br>(InkSnapshot.h)"]
        GI["GpuInk<br>(GpuInk.h / .cpp)"]
        GS["GpuSimulator<br>(GpuSimulator.h / .cpp)"]
        RI["ReplayInk<br>(ReplayInk.h / .cpp)"]
    end

    subgraph ViewLayer ["Views レイヤー"]
        CV["CanvasView (半紙への転送)"]
    end

    SC -->|セグメント描画 (DrawSegmentLinear)| GI
    SC -->|インク・水分消費 (Consume)| IM
    GI -->|スタンプ領域テクスチャ転送 (UploadPixelsRegion)| GS
    GS -->|Ping-Pong拡散計算 (Compute Shader)| GS
    GS -.->|DirectX Interop 共有テクスチャ| GI
    GI -->|スナップショット保存・復元 (終筆同期保護)| IS
    AC -->|Undo/Redo制御・90度回転| IS

    RI -->|内部GpuInkインスタンスを制御| GI
    RI -->|チェックポイント復元| IS

    GI -->|SRCAND 乗算合成転送| CV
    RI -->|リプレイ墨テクスチャ| CV
```

---

## 2. 各プログラムファイルの詳細

### 2.1 [GpuInk.h](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuInk.h) / [GpuInk.cpp](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuInk.cpp)

- **責務 (Responsibility)**:
  - 墨汁描画システムの中核クラスです。
  - **DirectX Interop ゼロコピー描画基盤**:
    - Direct3D 11 で生成・計算した `B8G8R8A8_UNORM` ピクセルテクスチャ（`m_texPixel`）を、Direct2D 1.1 の `ID2D1Bitmap1`（`m_d2dInkBitmap`）として `CreateBitmapFromDxgiSurface` で共有。
    - GDI 互換テクスチャ（`m_gdiTargetTex`, `m_gdiSurface1`）へ `DrawBitmap` し、`m_gdiSurface1->GetDC(FALSE, &hD2DDC)` を介してウィンドウの HDC へ転送します。
    - 毎フレームの GPU ↔ CPU 往復リードバック（PCIe バス帯域の消費）を完全撤廃し、極めて低遅延なリアルタイム描画を実現しています。
  - **`SRCAND` による和紙乗算合成**:
    - 半紙への描画時に `BitBlt(..., SRCAND)` を使用します。これにより、白地（`0xFFFFFFFF`）部分は和紙の地色を100%保持し、ストローク外側の不自然な白色の輪郭・縁取りが完全に消滅します。
  - **ストローク描画**: 始点と終点の線幅・運筆方向・乾き具合（`StrokeSegment`）を受け取り、毛筆の繊維束や粒子をCPUバッファ上に高密度にラスタライズします。スタンプ刻み幅は最大1.0px以内に抑え、数珠つなぎアーティファクトを排除しています。
  - **終筆消失防止（スナップショット同期順序）**:
    - `CaptureSnapshot` 実行時、未アップロードの墨・水分がある場合（ストローク終筆直後）は、GPU からダウンロードする前にまず GPU へアップロード（`UploadFromCpu`）することで、最新ストロークの終筆が失われる問題を防止しています。
  - **紙を大きくする表示への回転追従**:
    - `RotateSnapshot`（静的関数）および `RestoreSnapshotRotated` により、控えのスナップショットを90度回転させて保持・書き戻すことが可能です。
- **入力 (Input)**:
  - `DrawSegmentLinear(const StrokeSegment& seg)`: 線分パラメータ（座標、線幅、方向、乾き具合、濃度）
  - `UpdatePen(int z, double altitudeDegrees, double azimuthRad, bool hovering)`: ペン姿勢情報
  - `Initialize(int width, int height, bool runPropagationThread)`: 初期化寸法
- **出力 (Output)**:
  - `Render(HDC hdc, int destX, int destY, int dispW, int dispH)`: 描画された墨汁テクスチャの転送
  - `CaptureSnapshot(InkSnapshot& out)`: 現在の墨量・水分のスナップショット出力
- **使用されている定数の名前 (Constants used)**:
  - 墨汁粒子アルファ基準値: `255`。
  - 打刻刻み幅 (`step`): `std::max(0.5, std::min(maxRadius * 0.06, 1.0))`。
  - 拡散収束最大ステップ数 (`maxSteps`): `200`（`SettleDiffusion`）。

---

### 2.2 [GpuSimulator.h](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuSimulator.h) / [GpuSimulator.cpp](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuSimulator.cpp)

- **責務 (Responsibility)**:
  - Direct3D 11 のコンピュートシェーダー（Compute Shader CS 5.0）を用いて、GPU上で並列に墨汁の水分拡散・浸透シミュレーションを実行します。
  - Ping-Pong テクスチャ構造（墨量テクスチャ2面、水分テクスチャ2面）により、毎フレームの競合なし更新を実現します。
- **入力 (Input)**:
  - `UploadFromCpu(const int* inkData, const uint8_t* wetData, ...)`: CPU側で描画された墨・水分
  - `UploadPixelsRegion(...)`: 描画されたスタンプ画素の局所矩形アップロード
  - 定数バッファ `SimConstantBuffer`: 幅・高さ・物理シミュレーション係数
- **出力 (Output)**:
  - `GetPixelTexture()`: 描画用の `ID3D11Texture2D`（Direct2D ゼロコピー共有元）
  - `DownloadInkAndWet(...)`: Undo/Redo保存用の墨量・水分バッファ
- **使用されている定数の名前 (Constants used)**:
  - `SimConstantBuffer` 内の物理パラメータ:
    - `threshold = 350`: 墨が周囲へ拡散を開始する濃度のしきい値
    - `amount = 40`: 1ステップあたりの拡散墨量
    - `wetLoss = 40`: 拡散に伴う水分の消費量
    - `wetDryRate = 1`: 水分の自然乾燥速度
    - `wetThreshold = 8`: 拡散が持続する最小水分量
    - `maxInk = 450`: ピクセルあたりの最大墨量上限

---

### 2.3 [InkModel.h](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/InkModel.h)

- **責務 (Responsibility)**:
  - 硯および筆の墨残量（`amount`）を単一の物理パラメータ ($0.0 \sim 1.0$) で一元管理します。
  - ペンで硯を押した際の筆圧に応じた補充（`BeginPressRefill`, `PressRefill`）を実装。軽く触れただけで最小量、筆圧を強めるほど多く継ぎ足されます。
  - 残量がしきい値（`KASURE_START_LEVEL = 0.60`）を下回ると、非線形指数カーブ（指数 `1.65`）によって筆先の渇き度合い（`dryness`）を算出し、運筆に伴うかすれ（渇筆）をリアルタイムに駆動します。
- **入力 (Input)**:
  - `Consume(double delta)`: 運筆に応じたインク消費量
  - `Refill()`: 硯での墨補充（満タン `INK_MAX_VALUE = 1.0`）
  - `BeginPressRefill()`: 押し始めの残量（`refillBaseAmount`）を基準に設定
  - `PressRefill(double pressure)`: ペンの筆圧に応じた継ぎ足し補充
- **出力 (Output)**:
  - `amount`: 現在の墨残量 ($0.0 \sim 1.0$)
  - `GetDryness()`: 筆の乾き具合（渇筆度合い $0.0$: 潤沢 $\sim 1.0$: 完全かすれ）
- **使用されている定数の名前 (Constants used)**:
  - `REFILL_MIN_AMOUNT = 0.05`: 筆圧補充時の最小付与量。
  - `REFILL_MAX_AMOUNT = 0.60`: 筆圧補充時の最大付与量（1回では満タンにならず、何度か押して含ませる）。
  - `REFILL_FULL_PRESSURE = 0.8`: 最大補充となる筆圧基準（出しにくい最大筆圧の手前で最大化）。
  - `KASURE_START_LEVEL = 0.60`: dryness が 0 から立ち上がり始める残量のしきい値（実際に白い筋が見え始めるのは残量 25～30% 付近）。
  - `INK_MAX_VALUE = 1.0`: 墨の満タン値。

---

### 2.4 [InkSnapshot.h](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/InkSnapshot.h)

- **責務 (Responsibility)**:
  - 「一画戻す」や「リプレイのシーク」のために、ある瞬間の墨量バッファ（`m_ink`）と紙の水分バッファ（`m_wetField`）を保持する軽量スナップショット構造体です。
  - 和紙の大部分が白紙（ゼロ）であることを活かしたランレングス圧縮（`RleEncode`, `RleDecode`）により、メモリ消費を大幅に削減しています。
- **入力 (Input)**:
  - 墨量および水分の生配列データ
- **出力 (Output)**:
  - 復元先への配列データ
  - `ByteSize()`: 占有メモリサイズ
- **使用されている定数の名前 (Constants used)**:
  - なし（純粋なデータ構造体）

---

### 2.5 [ReplayInk.h](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/ReplayInk.h) / [ReplayInk.cpp](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/ReplayInk.cpp)

- **責務 (Responsibility)**:
  - 運筆解析タブにおいて、過去に書いた書道作品を時系列に沿ってリアルタイムに再生する専用の墨エンジンです。
  - 本番描画と全く同じ [`GpuInk`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuInk.h) インスタンスへ時系列アーカイブの記録パラメータ（線幅、硬さ補正値、乾き具合、運筆方向角）を流し込むため、にじみ・かすれの再現度が完全一致します。
  - 画の節目でチェックポイント（`Checkpoint`）を保持し、シークバーや波形ドラッグによる巻き戻し時も高速なシーク・再描画を実現しています。
- **入力 (Input)**:
  - `const TrajectorySession& session`: 運筆時系列アーカイブ
  - `DWORD timeMs`: 再生ターゲット時刻
  - `bool scrubbing`: シークバー等をドラッグ中かどうかのフラグ
- **出力 (Output)**:
  - `Render(HDC dc, int destX, int destY, int dispW, int dispH)`: 半紙領域への再生墨テクスチャ転送
- **使用されている定数の名前 (Constants used)**:
  - `Checkpoint`: 時刻、投入済み点数、および `InkSnapshot` を含むチェックポイント構造体。
