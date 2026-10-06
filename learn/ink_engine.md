# 墨汁物理・描画エンジンレイヤー (InkEngine)

本レイヤーは、Direct2D および Direct3D 11 Compute Shader を駆使し、和紙（半紙）特有の毛細管現象による墨汁の浸透、水分拡散、繊維に沿ったにじみ、ならびに高速運筆時のかすれ（渇筆）をリアルタイムに物理シミュレーションして描画するコアエンジンです。

---

## 1. ファイル構成と関係性

中心となる [`GpuInk`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuInk.h) は、CPU側の粒子描画・線分ラスタライズと、[`GpuSimulator`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuSimulator.h) によるGPUコンピュートシェーダー浸透計算を統合しています。また、運筆リプレイ専用の再現エンジンとして [`ReplayInk`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/ReplayInk.h) が提供されています。

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
    SC -->|インク消費 (Consume)| IM
    GI -->|GPUテクスチャ転送・シェーダー実行| GS
    GS -->|Ping-Pong拡散計算結果| GI
    GI -->|スナップショット保存・復元| IS
    AC -->|Undo/Redo制御| IS

    RI -->|内部GpuInkインスタンスを制御| GI
    RI -->|チェックポイント復元| IS

    GI -->|墨テクスチャ (ARGB)| CV
    RI -->|リプレイ墨テクスチャ| CV
```

---

## 2. 各プログラムファイルの詳細

### 2.1 [GpuInk.h](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuInk.h) / [GpuInk.cpp](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuInk.cpp)

- **責務 (Responsibility)**:
  - 墨汁描画システムの中核クラスです。
  - **ストローク描画**: 始点と終点の線幅・運筆方向・乾き具合（`StrokeSegment`）を受け取り、毛筆の繊維束や粒子をCPUバッファ上に高密度にラスタライズします。
  - **物理にじみ進行**: バックグラウンドスレッドまたはフレーム更新時に、和紙の水分フィールド（`m_wetField`）と墨量バッファ（`m_ink`）を用いて墨汁の浸透・拡散（セルラーオートマトン計算）を進めます。
  - **GPU連携**: [`GpuSimulator`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuSimulator.h) が利用可能な場合は Direct3D 11 Compute Shader へ処理を委譲し、PCIeバス帯域を節約するため Dirty Rect のみを局所ダウンロードします。
- **入力 (Input)**:
  - `DrawSegmentLinear(const StrokeSegment& seg)`: 線分パラメータ（座標、線幅、方向、乾き具合、濃度）
  - `UpdatePen(int z, double alt, double azim, bool isUp)`: ペン姿勢情報
  - `Initialize(HWND hWnd, int width, int height)`: 初期化寸法
- **出力 (Output)**:
  - `Render(HDC dc, int x, int y)`: 描画された墨汁テクスチャのGDI転送
  - `CaptureSnapshot(InkSnapshot& out)`: 現在の墨量・水分のスナップショット出力
- **使用されている定数の名前 (Constants used)**:
  - 墨汁粒子アルファ基準値: `255`。
  - 打刻刻み幅 (`step`): `std::max(0.5, std::min(maxRadius * 0.06, 1.0))`（スタンプ間隔を最大1.0px以内に抑え、線の外周波打ち・数珠つなぎアーティファクトを排除）。
  - 浸透しきい値: 後述の `SimConstantBuffer` パラメータ群と連動。

---

### 2.2 [GpuSimulator.h](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuSimulator.h) / [GpuSimulator.cpp](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuSimulator.cpp)

- **責務 (Responsibility)**:
  - Direct3D 11 のコンピュートシェーダー（Compute Shader CS 5.0）を用いて、GPU上で並列に墨汁の水分拡散・浸透シミュレーションを実行します。
  - Ping-Pong テクスチャ構造（墨量テクスチャ2面、水分テクスチャ2面）により、毎フレームの競合なし更新を実現します。
- **入力 (Input)**:
  - `UploadFromCpu(const int* inkData, const uint8_t* wetData, ...)`: CPU側で描画された墨・水分
  - 定数バッファ `SimConstantBuffer`: 幅・高さ・物理シミュレーション係数
- **出力 (Output)**:
  - `DownloadToPixelsRegion(...)`: 画面表示用のARGBピクセルバッファ（局所矩形のみ転送可能）
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
  - 残量がしきい値（`KASURE_START_LEVEL = 0.60`）を下回ると、非線形カーブによって筆先の渇き度合い（`dryness`）を算出し、運筆に伴うかすれ（渇筆）をリアルタイムに駆動します。
- **入力 (Input)**:
  - `Consume(double delta)`: 運筆に応じたインク消費量
  - `Refill()`: 硯での墨補充（満タン）
  - `PressRefill(double pressure)`: ペンの筆圧に応じた継ぎ足し補充
- **出力 (Output)**:
  - `amount`: 現在の墨残量 ($0.0 \sim 1.0$)
  - `GetDryness()`: 筆の乾き具合（渇筆度合い $0.0$: 潤沢 $\sim 1.0$: 完全かすれ）
- **使用されている定数の名前 (Constants used)**:
  - `KASURE_START_LEVEL = 0.60`: かすれ（渇筆）が立ち上がり始めるインク残量のしきい値。
  - `REFILL_MIN_AMOUNT = 0.10`: 筆圧補充時の最小付与量。
  - `REFILL_FULL_PRESSURE = 0.8`: 満タン補充となる筆圧基準。
  - `INK_MAX_VALUE = 1.0`: 墨の満タン値。

---

### 2.4 [InkSnapshot.h](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/InkSnapshot.h)

- **責務 (Responsibility)**:
  - 「一画戻す」や「リプレイのシーク」のために、ある瞬間の墨量バッファ（`m_ink`）と紙の水分バッファ（`m_wetField`）を保持する軽量スナップショット構造体です。
  - 和紙の大部分が白紙（ゼロ）であることを活かしたランレングス圧縮（RLE）に対応しています。
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
  - 単なる線の補間ではなく、本番描画と全く同じ [`GpuInk`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuInk.h) インスタンスへ時系列アーカイブの記録パラメータ（線幅、硬さ補正値、乾き具合、運筆方向）を流し込むため、にじみ・かすれの再現度が完全一致します。
  - シークバー操作による巻き戻しに対応するため、画の節目でチェックポイント（`Checkpoint`）を保持し、高速なシーク・再描画を実現しています。
- **入力 (Input)**:
  - `const TrajectorySession& session`: 運筆時系列アーカイブ
  - `DWORD timeMs`: 再生ターゲット時刻
  - `bool scrubbing`: シークバーをドラッグ中かどうかのフラグ
- **出力 (Output)**:
  - `Render(HDC dc, int destX, int destY)`: 半紙領域への再生墨テクスチャ転送
- **使用されている定数の名前 (Constants used)**:
  - `Checkpoint`: 時刻、投入済み点数、および `InkSnapshot` を含むチェックポイント構造体。
