# 制御ロジックレイヤー (Controllers)

本レイヤーは、入力抽象化レイヤーから受け取った正規化ペンイベントやユーザーのGUI操作メッセージを解析し、書道の運筆力学計算・インク消費・状態更新・画面遷移・再描画要求といったビジネスロジックを統括する責務を持ちます。

---

## 1. ファイル構成と関係性

制御層は、運筆の筆跡・物理計算を司る [`StrokeController`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Controllers/StrokeController.h) と、UI操作・画面遷移全般（タイトル遷移、パネル操作、文字入力、Undo/Redo、紙だけ表示、アーカイブ外部読込など）を司る [`AppController`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Controllers/AppController.h) の2つで構成されています。

```mermaid
graph TD
    subgraph InputsLayer ["Inputs レイヤー"]
        PIE["PenInputEvent"]
    end

    subgraph ControllersLayer ["Controllers レイヤー"]
        SC["StrokeController<br>(StrokeController.h / .cpp)"]
        AC["AppController<br>(AppController.h / .cpp)"]
    end

    subgraph StateLayer ["Models レイヤー (AppState)"]
        BrushMod["筆設定 (BrushModel)"]
        InkMod["墨・水分モデル (InkModel)"]
        Traj["運筆アーカイブ (TrajectorySession)"]
        Undo["一画戻す/復元 (UndoHistory)"]
        UI["UI状態・配置 (UIState)"]
        Screen["画面状態 (AppScreen)"]
    end

    subgraph EngineLayer ["InkEngine レイヤー"]
        GPU["GpuInk (リアルタイム墨汁描画)"]
    end

    subgraph OSLayer ["OS / Win32"]
        WinMsg["WM_PAINT / InvalidateRect"]
    end

    PIE -->|運筆パケット| SC
    SC -->|筆設定参照| BrushMod
    SC -->|墨・水分の消費 (1.02倍スケール)| InkMod
    SC -->|時系列データ追記| Traj
    SC -->|画開始時のスナップショット| Undo
    SC -->|セグメント描画命令| GPU
    SC -->|約120Hz 局所Dirty Rect更新| WinMsg

    AC -->|タイトル遷移・ホームボタン| Screen
    AC -->|UIクリック・IME文字入力| UI
    AC -->|全消し・サイズ変更・用紙回転| GPU
    AC -->|一画戻す/復元・90度回転指示| Undo
    AC -->|外部アーカイブ読込・切替| Traj
    AC -->|画面全体の再描画| WinMsg
```

---

## 2. 各プログラムファイルの詳細

### 2.1 [StrokeController.h](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Controllers/StrokeController.h) / [StrokeController.cpp](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Controllers/StrokeController.cpp)

- **責務 (Responsibility)**:
  - デバイス非依存の [`PenInputEvent`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Inputs/PenInputEvent.h) を受け取り、書道特有の運筆力学計算を実行します。
  - **運筆可否判定 (`canDrawInk`)**:
    - スタジオ画面（`AppScreen::Studio`）かつ遷移中・全消しモーダル中でなく、半紙領域内かつ開いたサブパネルに重なっていない場合にのみ描画を許可します。
    - 解析タブ表示中はリプレイ墨が半紙へ描画されるため、通常運筆の透過・記録ずれを防ぐため運筆を受け付けません（「紙を大きくする」表示中は解析パネルが無いため受け付けます）。
  - **誤運筆ロックの保護 (`suppressPenUntilLift`)**:
    - 全消しモーダルや全消しボタン等の操作直後、ペンが接地したままの場合にペンが一度離れるまで運筆を遮断します。
  - **筆圧平滑化**:
    - 加圧時は `0.35`、抜圧時は `0.60` の係数でスムージングし、滑らかさと抜けの良さを両立。
  - **量子化ノイズと角度ジッターの平滑化**:
    - ピクセル整数座標の量子化振動を `m_smoothedDist = m_smoothedDist * 0.7 + dist * 0.3` で吸収。
    - 進行方向ベクトル `(m_smoothedDirX, m_smoothedDirY)` を平滑化係数（`dist > 4.0 ? 0.35 : 0.20`）で平滑化し、線の波打ちやコブの発生を防止。
  - **止め・払いの動的線幅幾何計算**:
    - 払い減衰補正指数: `angleHaraiPower = 1.3 + (0.2 + 0.3 * absAngleDiff) * (std::min)(dist, 10.0)`
    - 離筆に向かう際の穂先一点収束: `tipConvergence = std::pow(pressureFactor, 0.4)`
    - 腹方向角度補正: `angleFactor = 1.0 + 0.3 * absAngleDiff * tipConvergence`
    - 実効傾き補正: `effectiveTiltFactor = tiltFactor * tipConvergence`
    - 止め補正係数: `tomeFactor = 1.0 + 0.08 * (1.0 - (std::min)(m_smoothedDist / 4.0, 1.0)) * std::pow(pressureFactor, 0.8)`
    - 線幅追従平滑化: 通常 `0.25`、急激な線幅減少（払い・跳ね）時 `0.45`、高速移動時 `0.35`。
  - **物理インク消費 (USABLE_INK_SCALE = 1.02)**:
    - 満タン（1.0 = 100%）のまま書ける距離を1.02倍にするため、消費を 1.02 で除算。
    - かすれ（`dryness > 0`）時は消費を最大45%低減（`drynessFactor = 1.0 - dryness * 0.45`）し、かすれが続く距離を自然に伸長。
    - `consumeAmount = stepDist * widthRatio * (0.00020 / USABLE_INK_SCALE) * drynessFactor`
  - **描画ディスパッチとレート制御**:
    - 画の開始時に [`UndoHistory::PushBeforeStroke`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Models/UndoHistory.h) を実行。
    - [`GpuInk::DrawSegmentLinear`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/InkEngine/GpuInk.h) へ線分を投入。
    - 再描画要求（`InvalidateRect`）の頻度を約120Hz（`kMinInvalidateIntervalMs = 8`ms間隔）にレート制御し、過剰な描画呼び出しを抑制。
    - 硯パネル（液面・残量テキスト）の再描画は約30Hz（33ms間隔）で同期。
- **入力 (Input)**:
  - `HWND hWnd`: 描画先メインウィンドウ
  - `const PenInputEvent& event`: 正規化されたペン入力情報
  - `AppState& state`: アプリケーション状態
  - `GpuInk& gpuInk`: 墨汁描画エンジン
- **出力 (Output)**:
  - `gpuInk.DrawSegmentLinear(seg)`: 運筆線分の描画実行
  - `state.ink.Consume(...)`: 墨残量および水分量の減少
  - `state.trajectory.AddPoint(...)`: 運筆アーカイブの更新
  - `InvalidateRect(hWnd, &m_accumDirty, FALSE)`: 局所更新領域の通知
- **使用されている定数の名前 (Constants used)**:
  - `USABLE_INK_SCALE = 1.02`: 使える墨の量を伸ばすスケール定数。
  - `kMinInvalidateIntervalMs = 8`: 再描画間隔（約120Hz）。
  - `alphaPrs`: 筆圧平滑化係数（加圧 `0.35`、抜圧 `0.60`）。
  - 最大飛び判定閾値: `dist > 300.0`。
  - Dirty Rect パディング: `pad = static_cast<int>(std::ceil(maxWidth * 1.5 + 24.0))`。

---

### 2.2 [AppController.h](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Controllers/AppController.h) / [AppController.cpp](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Controllers/AppController.cpp)

- **責務 (Responsibility)**:
  - マウスクリック、ドラッグ、キー入力、ウィンドウサイズ変更など、UI全般のイベントをハンドリングします。
  - **画面遷移制御**:
    - タイトル画面でのタップ/クリック検知によりスタジオ画面への遷移アニメーション（`isTransitioning = true`）を開始。
    - 左上ホームボタン押下によるタイトル画面への復帰。
  - **UIインタラクション**:
    - 左側フローティングパネルの開閉・タブ切り替え（筆・紙・解析・保存・お手本）、筆の硬さスライダー調整、半紙選択、升目選択、カラーテーマ、保存ボタン群。
    - お手本文字の配置・消去（トグル動作）、書体変更。
    - **IME 変換ウィンドウ追従 (`SetOtehonImePosition`)**: お手本入力欄の位置へ IME 変換候補ウィンドウを配置（`ImmSetCompositionWindow`）。
    - 硯パネルでの墨補充（`RefillByPress`: ペンの筆圧に応じた補充量、マウス時は満タン）。
  - **一画戻す (Undo) / 一画復元 (Redo)**:
    - [`UndoHistory`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Models/UndoHistory.h) を操作し、墨テクスチャ・墨残量・運筆時系列データを過去の状態へ安全に巻き戻し/復元。
    - 運筆記録の変更に合わせて `SyncReplayTimeline` を呼び出し、リプレイタイムラインを同期。
  - **外部運筆アーカイブのインポート解析**:
    - `ImportArchive`: 他者の運筆アーカイブ（JSON / CSV）を読み込み、現在の半紙寸法に合わせてリプレイ解析の対象に設定（自分の墨・記録は保持）。
    - `CloseImportedArchive`: 読み込んだ記録を閉じ、自分の運筆記録の解析へ復帰。
  - **紙を大きくする（横向き）表示の切り替え (`SetPaperOnly`)**:
    - 半紙の縦横回転、書いた墨と運筆記録の90度回転引き継ぎ、升目ジオメトリの再配置。
    - `UndoHistory::RotateQuarter` により、Undo 履歴の墨スナップショットおよび復元用ストロークも90度回転させて保持。
  - **全消し (`ClearAllInk`)**:
    - 墨・運筆アーカイブ・Undo履歴・リプレイをリセット。消去直後の誤運筆を防ぐため `suppressPenUntilLift` を設定。
- **入力 (Input)**:
  - `HWND hWnd`: ウィンドウハンドル
  - `POINT pt`: マウスクリック・移動座標
  - `double penPressure`: ペンで押したときの筆圧（負値はマウス入力）
  - `WPARAM wParam`: 修飾キーまたはメッセージパラメータ
  - `wchar_t ch`: WM_CHAR で受け取る入力文字（IME確定文字）
  - `int width, int height`: クライアント領域の幅と高さ
  - `AppState& state`: 状態モデル
  - `GpuInk& gpuInk`: 墨描画エンジン
- **出力 (Output)**:
  - 戻り値 `bool`: 各操作がUIによって消費されたか（運筆等への透過を防ぐフラグ）
  - `state.ui` / `state.brush` / `state.paper` / `state.otehon` / `state.replay` の各フィールド更新
  - `InvalidateRect(hWnd, NULL, FALSE)`: UI変更に伴う画面全体の再描画要求
- **使用されている定数の名前 (Constants used)**:
  - `LeftTab`: 各タブ識別列挙子。
  - `REPLAY_TIMER_ID`: 運筆リプレイ再生用の Win32 タイマ ID (`101`)。
  - `MAX_GRID_CELLS`: お手本配置マスの最大数 (`8`)。
  - `OTEHON_PALETTE_MAX`: お手本候補タイルの最大数 (`8`)。
