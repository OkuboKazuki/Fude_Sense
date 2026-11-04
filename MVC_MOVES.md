# MVC 修正の移動一覧

MVC 修正で、どの関数や処理をどこからどこへ移したかをまとめた一覧です。修正した順に並べています。

- **パス:** `App/FudeCode/` からの相対パスです。
- **移動後:** 今のコードでの場所です。あとの修正でさらに移したときは、最新の場所に直します。
- **コミット:** その移動を行ったコミットです。`未コミット` は、まだコミットしていない変更です。
- **対象外:** 中身を変えずに呼び方だけを変えた箇所は、一覧に含めていません。

問題の内容と修正の方向は `MVC_ISSUES.md`、ペンでの確認は `PEN_TEST_CHECKLIST.md` にあります。

## M2. 共通の計算関数を移す

`RenderUtils.h` には using 宣言を残したので、View と Controller からは今も `RenderUtils` の名前で呼べます。

| 関数 | 移動前 | 移動後 | コミット |
|---|---|---|---|
| `Clamp` | `Views/RenderUtils.h`（`RenderUtils`） | `Models/MathUtils.h`（`MathUtils`） | `b4a27f2` |
| `RW` | `Views/RenderUtils.h`（`RenderUtils`） | `Models/MathUtils.h`（`MathUtils`） | `b4a27f2` |
| `RH` | `Views/RenderUtils.h`（`RenderUtils`） | `Models/MathUtils.h`（`MathUtils`） | `b4a27f2` |
| `PtIn` | `Views/RenderUtils.h`（`RenderUtils`） | `Models/MathUtils.h`（`MathUtils`） | `b4a27f2` |

## M4. ファイル選択ダイアログを移す

| 関数・型 | 移動前 | 移動後 | コミット |
|---|---|---|---|
| `PromptSaveArchiveJson` | `Models/TrajectoryModel`（`TrajectorySession`） | `Services/ArchiveDialogs` の `ArchiveDialogs::PromptSaveJson` | `8afc0b1` |
| `PromptSaveArchiveCsv` | `Models/TrajectoryModel`（`TrajectorySession`） | `Services/ArchiveDialogs` の `ArchiveDialogs::PromptSaveCsv` | `8afc0b1` |
| `PromptLoadArchive` | `Models/TrajectoryModel`（`TrajectorySession`） | `Services/ArchiveDialogs` の `ArchiveDialogs::PromptLoad` | `8afc0b1` |
| `SaveResult` | `Models/TrajectoryModel.h`（`TrajectorySession`） | `Services/ArchiveDialogs.h` の `ArchiveDialogs::Result` | `8afc0b1` |
| `DialogClosedResult` | `Models/TrajectoryModel.cpp` | `Services/ArchiveDialogs.cpp` | `8afc0b1` |
| `FileNameOf` | `Models/TrajectoryModel.cpp` | `Services/ArchiveDialogs.cpp` | `8afc0b1` |

## C2. グラフの余白の計算をまとめる

3 か所に同じ式があったので、1 か所で計算して `ui.rAnalysisGraphPlot` に持つようにしました。計算した場所は、そのあと M1 で `ScreenLayout` へ移しました。

| 処理 | 移動前 | 移動後 | コミット |
|---|---|---|---|
| 波形の描画領域の計算（描画） | `Views/AnalysisView.cpp` の `DrawWaveformGraph` | `Views/ScreenLayout.cpp` の `ScreenLayout::Layout` | `982a6aa`、`fb01639` |
| 波形の描画領域の計算（クリック判定） | `Controllers/AppController.cpp` の `OnLButtonDown` | `Views/ScreenLayout.cpp` の `ScreenLayout::Layout` | `982a6aa`、`fb01639` |
| 波形の描画領域の計算（ドラッグ判定） | `Controllers/AppController.cpp` の `OnMouseMove` | `Views/ScreenLayout.cpp` の `ScreenLayout::Layout` | `982a6aa`、`fb01639` |

## M5. 画面座標の解釈を移す

| 関数 | 移動前 | 移動後 | コミット |
|---|---|---|---|
| `ToClearModalSpace` | `Models/AppState`（`AppState`） | `Controllers/AppController.cpp` のファイル内の関数 | `aee2db0` |
| `SetInkAmountFromBadgeX` | `Models/AppState`（`AppState`） | `Controllers/AppController.cpp` のファイル内の関数 | `aee2db0` |
| `IsOnInkStoneLand` | `Models/AppState`（`AppState`） | `Controllers/AppController.cpp` のファイル内の関数 | `aee2db0` |
| `WipeInkTo` | `Models/AppState`（`AppState`） | `Controllers/AppController` の `AppController::WipeInkTo` | `aee2db0` |

## E1. ウィンドウプロシージャの処理を移す

| 処理 | 移動前 | 移動後 | コミット |
|---|---|---|---|
| タイトルからの遷移の進行 | `WacomMT_Scribble.cpp` の `WM_TIMER` | `Controllers/AppController` の `OnTimer` | `d44e40c` |
| タイトルのアニメーションの更新 | `WacomMT_Scribble.cpp` の `WM_TIMER` | `Controllers/AppController` の `OnTimer` | `d44e40c` |
| リプレイの時刻の進行 | `WacomMT_Scribble.cpp` の `WM_TIMER` | `Controllers/AppController` の `OnTimer` | `d44e40c` |
| `g_lastReplayTick` | `WacomMT_Scribble.cpp` | `Controllers/AppController.cpp` の `s_lastReplayTick` | `d44e40c` |
| `F9`、`Esc`、`Ctrl+Z`、`Ctrl+Shift+Z`、`Ctrl+Y` | `WacomMT_Scribble.cpp` の `WM_KEYDOWN` | `Controllers/AppController` の `OnKeyDown` | `d44e40c` |
| 入力ロックの解除（押下、接地解除、`WM_POINTERUP`） | `WacomMT_Scribble.cpp` | `Controllers/AppController` の `ReleasePenLock` | `d44e40c` |
| ボタンを離したときのフラグの解除 | `WacomMT_Scribble.cpp` の `WM_LBUTTONUP` | `Controllers/AppController` の `OnLButtonUp` | `d44e40c` |
| 圏外へ出たときのフラグの解除 | `WacomMT_Scribble.cpp` の `WT_PROXIMITY` | `Controllers/AppController` の `OnPenOutOfRange` | `d44e40c` |

## S1. 保存結果のメッセージを移す（第 1 段階）

`ExportCanvas` の戻り値は、`bool` から `ImageExportResult` に変えました。作品画像の合成は、まだ `ImageExporter` にあります。

| 処理 | 移動前 | 移動後 | コミット |
|---|---|---|---|
| 保存結果のメッセージの設定 | `Services/ImageExporter.cpp` の `ExportCanvas` | `Controllers/AppController.cpp` の `ExportCanvasWithFeedback` | `f3b0f27` |

## M7. 墨エンジンの操作を移す

`UndoHistory` の `Undo` と `Redo` は、書き戻す控えを返す関数と、履歴を更新する関数に分けました。

| 処理 | 移動前 | 移動後 | コミット |
|---|---|---|---|
| 墨の控えの取得と、メモリ不足の処理 | `Models/UndoHistory.cpp` の `CaptureCurrent` | `Controllers/AppController.cpp` の `CaptureInkForUndo` | `2bc3f5d` |
| 画の書き始めの控えの取得 | `Models/UndoHistory.cpp` の `PushBeforeStroke` | `Controllers/AppController` の `PushUndoBeforeStroke` | `2bc3f5d` |
| 戻すときの墨の書き戻し | `Models/UndoHistory.cpp` の `Undo` | `Controllers/AppController.cpp` の `UndoStroke` | `2bc3f5d` |
| 復元するときの墨の書き戻し | `Models/UndoHistory.cpp` の `Redo` | `Controllers/AppController.cpp` の `RedoStroke` | `2bc3f5d` |
| 墨残量と運筆記録の更新（戻す） | `Models/UndoHistory.cpp` の `Undo` | `Models/UndoHistory.cpp` の `CommitUndo` | `2bc3f5d` |
| 墨残量と運筆記録の更新（復元） | `Models/UndoHistory.cpp` の `Redo` | `Models/UndoHistory.cpp` の `CommitRedo` | `2bc3f5d` |
| `GpuInk::RotateSnapshot` の呼び出し | `Models/UndoHistory.cpp` の `RotateQuarter` | `Controllers/AppController.cpp` の `SetPaperOnly`（関数として渡す） | `2bc3f5d` |

## M1. レイアウト計算を移す

3 段階で移しました。`Models/AppState.cpp` は不要になったので削除しました。第 1 段階の `3c01b01` では、呼び出し元を `ScreenLayout` へ切り替えただけで、計算は移していません。

| 関数 | 移動前 | 移動後 | コミット |
|---|---|---|---|
| `AppState::Layout` | `Models/AppState.cpp` | `Views/ScreenLayout.cpp` の `ScreenLayout::Layout` | `fb01639`、`ca44652` |
| `AppState::UpdateReplaySeekThumb` | `Models/AppState.cpp` | `Views/ScreenLayout.cpp` の `ScreenLayout::UpdateReplaySeekThumb` | `ca44652` |
| `AppState::LayoutPaperOnly` | `Models/AppState.cpp` | `Views/ScreenLayout.cpp` のファイル内の関数 | `ca44652` |
| `AppState::LayoutGridGeometry` | `Models/AppState.cpp` | `Views/ScreenLayout.cpp` のファイル内の関数 | `ca44652` |
| `LayoutClearModal` | `Models/AppState.cpp` | `Views/ScreenLayout.cpp` のファイル内の関数 | `ca44652` |
| `GetGridDivision` | `Models/AppState.cpp` | `Views/ScreenLayout.cpp` のファイル内の関数 | `ca44652` |

## M3. UI の状態を移す

第 1 段階（`9f234b7`）: 関数や処理の移動なし。`Models/UIComponents.h` の `UIWidget` から、未使用の `isHovered`、`isPressed`、`isEnabled`、`Contains`、`Width`、`Height` を削除しました。

第 2 段階（`a6d4b27`）: `ExportCanvas` の引数を、`const AppState&` から `const RECT& rPaper` に変えました。

| 関数 | 移動前 | 移動後 | コミット |
|---|---|---|---|
| `SetSaveFeedback` | `Models/AppState.h`（`AppState`） | `Views/UIState.h`（`UIState`） | `a6d4b27`、`未コミット` |

第 3 段階（`173c78a`）: 関数や処理の移動なし。View の関数の引数に `const UIState& ui` を足しました。`AppState` を使わなくなった `CanvasView::DrawBackground`、`CanvasView::RenderInk`、`CanvasView::Draw3DBrushPose`、`ModalView::DrawClearConfirm`、`ModalView::DrawClearConfirmBody` は、引数の `const AppState&` を `const UIState&` に置き換えました。

第 4 段階（`66896c1`）: 関数や処理の移動なし。`AppController` と `StrokeController` の関数の引数に `UIState& ui` を足しました。`AppState` を使わなくなった `AppController::ReleasePenLock`、`AppController::OnPenOutOfRange`、`ExportCanvasWithFeedback` は、引数の `AppState&` を `UIState&` に置き換えました。

第 5 段階（`未コミット`）: 型の定義を移し、`AppState` の `ui` メンバーを削除して、実体をエントリポイントに置きました。

| 型・変数 | 移動前 | 移動後 | コミット |
|---|---|---|---|
| `UIState` | `Models/AppState.h` | `Views/UIState.h` | `未コミット` |
| `UIWidget` | `Models/UIComponents.h` | `Views/UIComponents.h` | `未コミット` |
| `UIState` の実体 | `Models/AppState.h` の `AppState::ui` | `WacomMT_Scribble.cpp` の `g_uiState` | `未コミット` |
