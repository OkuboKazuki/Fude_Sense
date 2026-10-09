# 基盤サービス & アプリケーションエントリ (Services & Entry)

本レイヤーは、Windows OS との対話、Win32 メインウィンドウの生成とメッセージループ、Wintab デバイスドライバとのコンテキスト接続・管理、ならびに完成した作品の画像保存（BMP / WIC PNG）やクリップボード連携を担うインフラストラクチャ層です。

---

## 1. ファイル構成と関係性

アプリケーションのエントリポイントである [`WacomMT_Scribble.cpp`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/WacomMT_Scribble.cpp) がOSのメッセージループを駆動し、デバイス管理を [`WintabManager`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Services/WintabManager.h) に、画像エクスポートを [`ImageExporter`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Services/ImageExporter.h) に委譲します。

```mermaid
graph TD
    subgraph OS ["Windows OS"]
        WinMain["WinMain / メッセージループ"]
        MsgWT["WT_PACKET"]
        MsgPtr["WM_POINTER*"]
        MsgMouse["WM_LBUTTONDOWN 等"]
        MsgKey["WM_KEYDOWN (F11, F9, Ctrl+Z, Ctrl+Y, Esc)"]
        MsgPaint["WM_PAINT"]
        MsgTimer["WM_TIMER<br>(Replay: 101, Transition: 102, TitleAnim: 103)"]
    end

    subgraph EntryLayer ["アプリケーションエントリ"]
        MainApp["WacomMT_Scribble.cpp<br>(WndProc / メインウィンドウ / ショートカット)"]
    end

    subgraph ServiceLayer ["Services レイヤー"]
        WM["WintabManager<br>(WintabManager.h / .cpp)"]
        WU["WintabUtils<br>(WintabUtils.h / .cpp)"]
        IE["ImageExporter<br>(ImageExporter.h / .cpp)"]
    end

    subgraph OtherLayers ["他レイヤーへのディスパッチ"]
        Adapters["Inputs (Wintab / Pointer / Mouse)"]
        Controllers["Controllers (Stroke / App)"]
        Views["Views (MainView::Render)"]
    end

    WinMain --> MainApp
    MsgWT --> MainApp
    MsgPtr --> MainApp
    MsgMouse --> MainApp
    MsgKey --> MainApp
    MsgPaint --> MainApp
    MsgTimer --> MainApp

    MainApp -->|タブレット初期化・解放| WM
    WM -->|DLL関数呼び出し| WU
    MainApp -->|画像保存・クリップボード発行| IE

    MainApp -->|入力パケット| Adapters
    Adapters --> Controllers
    MainApp -->|ショートカットキー・UI操作| Controllers
    MainApp -->|描画トリガー| Views
```

---

## 2. 各プログラムファイルの詳細

### 2.1 [WacomMT_Scribble.cpp](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/WacomMT_Scribble.cpp) / [WacomMT_Scribble.h](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/WacomMT_Scribble.h)

- **責務 (Responsibility)**:
  - アプリケーションのエントリポイント（`_tWinMain` / `WinMain`）とメインウィンドウプロシージャ（`WndProc`）を含みます。
  - **メッセージディスパッチ**:
    - `WT_PACKET`: Wintab アダプタ経由で [`StrokeController`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Controllers/StrokeController.h) へ転送。
    - `WM_POINTERDOWN` / `WM_POINTERUPDATE` / `WM_POINTERUP`: Windows Pointer アダプタ経由で [`StrokeController`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Controllers/StrokeController.h) へ転送。
    - `WM_LBUTTONDOWN` / `WM_MOUSEMOVE` / `WM_LBUTTONUP`: [`AppController`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Controllers/AppController.h) またはマウスアダプタへ転送。
    - `WM_CHAR`: お手本文字の入力ハンドリング（IME確定文字）。
    - `WM_PAINT`: [`MainView::Render`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Views/MainView.h) の実行。
    - `WM_SIZE`: レイアウト再計算と描画エンジンのリサイズ。
    - `WM_TIMER`:
      - `REPLAY_TIMER_ID` (`101`): 運筆リプレイ再生時刻の進行。
      - `TRANSITION_TIMER_ID` (`102`): タイトル画面からスタジオ画面へのアルファブレンド遷移演出の進行。
      - `TITLE_ANIM_TIMER_ID` (`103`): タイトル画面でのロゴ・テキストのブレスアニメーション更新。
  - **キーボードショートカット制御**:
    - `F11`: `ToggleFullscreen` によるボーダーレス全画面表示切替（マルチモニタ対応、元のウィンドウスタイルと配置を完全退避・復元。お手本入力中も常時有効）。
    - `F9`: 紙を大きくする（横向き）表示の切り替え（`AppController::SetPaperOnly`）。
    - `Esc`: 全消し確認モーダルの呼び出し / モーダル表示中のキャンセル。
    - `Ctrl + Z`: 一画戻す（`AppController::UndoStroke`）。
    - `Ctrl + Shift + Z` / `Ctrl + Y`: 一画復元（`AppController::RedoStroke`）。
    - `Alt + /` / `Alt + ?`: バージョン情報ダイアログ（About）。
  - **入力保護 ([`IsStudioAcceptingKeys`](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/WacomMT_Scribble.cpp#L81))**:
    - タイトル画面および画面遷移中は、F11 以外のすべてのショートカットを遮断し、裏でスタジオ状態が変わることを防止。
    - お手本文字の入力中（`g_appState.otehon.isTyping`）は、文字入力以外のショートカットを一時遮断。
- **入力 (Input)**:
  - Windows メッセージ（`HWND`, `UINT message`, `WPARAM wParam`, `LPARAM lParam`）
- **出力 (Output)**:
  - `LRESULT`: メッセージ処理結果
- **使用されている定数の名前 (Constants used)**:
  - ウィンドウタイトル: `szTitle = L"Fude Sense"`
  - ウィンドウクラス名: `szWindowClass = L"FUDESENSE"`
  - `REPLAY_TIMER_ID`: リプレイ進行用タイマ識別子 (`101`)。
  - `TRANSITION_TIMER_ID`: 画面遷移用タイマ識別子 (`102`)。
  - `TITLE_ANIM_TIMER_ID`: タイトルブレス用タイマ識別子 (`103`)。

---

### 2.2 [WintabManager.h](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Services/WintabManager.h) / [WintabManager.cpp](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Services/WintabManager.cpp)

- **責務 (Responsibility)**:
  - `wintab32.dll` との接続を確立し、ペンタブレットの論理コンテキスト（`HCTX`）のオープン・クローズおよびデバイス特性情報の取得を管理します。
  - デバイスごとの最大筆圧（`maxPressure`）、タブレット物理解像度（`tabletXExt`, `tabletYExt`）、液晶タブレットか板タブレットかの判定を行い、コンテキストマップ（`m_contextMap`）で保持します。
- **入力 (Input)**:
  - `HWND hWnd`: 接続先メインウィンドウハンドル
- **出力 (Output)**:
  - `GetMaxPressure(HCTX hCtx)`: 対象コンテキストの最大筆圧値（フォールバック時 `1024.0`）
  - `GetPrimaryContext()`: プライマリコンテキストハンドル
- **使用されている定数の名前 (Constants used)**:
  - `PACKETDATA`: パケット取得フラグ（`PK_X | PK_Y | PK_Z | PK_BUTTONS | PK_NORMAL_PRESSURE | PK_TANGENT_PRESSURE | PK_TIME | PK_ORIENTATION`）。
  - `PACKETMODE`: ボタンモードフラグ (`PK_BUTTONS`)。

---

### 2.3 [WintabUtils.h](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Services/WintabUtils.h) / [WintabUtils.cpp](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Services/WintabUtils.cpp)

- **責務 (Responsibility)**:
  - `wintab32.dll` を動的ロード（`LoadLibraryA`）し、Wintab API の関数ポインタ（`WTInfoA`, `WTOpenA`, `WTPacket`, `WTClose` など）を解決・エクスポートする低レベルユーティリティモジュールです。
  - タブレットドライバがインストールされていない環境でも安全に初期化エラーを検出し、クラッシュを防ぎます。
- **入力 (Input)**:
  - なし（DLLロード関数）
- **出力 (Output)**:
  - 各種グローバル関数ポインタ（`gpWTInfoA`, `gpWTOpenA`, `gpWTPacket` 等）
- **使用されている定数の名前 (Constants used)**:
  - DLL名: `"wintab32.dll"`。

---

### 2.4 [ImageExporter.h](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Services/ImageExporter.h) / [ImageExporter.cpp](file:///c:/Users/kazuk/デスクトップ/Fudesence/App/FudeCode/Services/ImageExporter.cpp)

- **責務 (Responsibility)**:
  - 半紙上に描かれた墨汁作品を、デスクトップへの高画質画像ファイル保存（WIC PNG / BMP 形式）またはクリップボードへの転送（`CF_BITMAP` 形式）を行う画像エクスポートサービスです。
  - **出力内容**:
    - お手本や下敷き罫線は揮毫の下敷きであって作品の一部ではないため書き出しには含めず、純粋に和紙の地色の上に墨汁ストローク（`gpuInk.Render`）のみを高解像度でレンダリングして出力します。
  - **ファイル命名規則**:
    - デスクトップ上に `習字作品_YYYYMMDD_HHMMSS.png`（WIC失敗時は `.bmp`）として保存。
  - **破損ファイルのクリーンアップ**:
    - 保存途中で容量不足や書き込み失敗が発生した場合、壊れたファイルを残さないよう直ちに削除（`DeleteFileW`）を実行。
  - **フィードバック通知**:
    - `AppState::SetSaveFeedback` を介して、成功時は緑（`✓ デスクトップにPNG保存しました` など）、失敗時は赤のエラーメッセージをポップアップ表示します。
- **入力 (Input)**:
  - `HWND hWnd`: 親ウィンドウ
  - `GpuInk& gpuInk`: 墨テクスチャ
  - `AppState& state`: 半紙の寸法・UI状態
  - `bool toClipboard`: クリップボードにコピーするか、ファイル保存するか
- **出力 (Output)**:
  - ファイル出力（デスクトップ上に `習字作品_YYYYMMDD_HHMMSS.png` または `.bmp`）
  - クリップボードへのビットマップデータ格納 (`CF_BITMAP`)
  - 戻り値 `bool`: エクスポート成否
- **使用されている定数の名前 (Constants used)**:
  - `CF_BITMAP`: Win32 クリップボード形式。
  - `GUID_ContainerFormatPng`: WIC (Windows Imaging Component) の PNG エンコーダー GUID。
  - `GUID_WICPixelFormat24bppBGR`: WIC 24bpp BGR ピクセルフォーマット。
