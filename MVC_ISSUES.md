# MVC アーキテクチャの問題点

Fude Sense のコードが MVC から外れている箇所をまとめた資料です。

- **確認時点:** コミット `8b4acde`（`Debuc04` ブランチ）
- **確認方法:** キーワード検索と主要部分の読み取り。全行は読んでいないため、記載のない問題が残っている可能性があります。
- **再確認:** コミット `de9cc72` の時点で同じ方法で確認し直し、C3〜C6、M10、M11 を追加しました。
- **パス:** 特に記載がなければ `App/FudeCode/` からの相対パスです。

---

## 1. まとめ

最も大きな問題は、**Model（`AppState`）が画面の情報を抱えていること**でした。レイアウトの計算は M1、`UIState` は M3 で Model の外へ移しました。

| 層 | 本来の役割 | 現状の問題 |
|---|---|---|
| Model | データとアプリのルール | `UIState` は M3、操作中の一時状態は M6、画面の種類と UI 用の定数は M8 で解消。運筆の記録の画面への依存は M10 で意図して残す |
| View | 表示と、そのためのレイアウト | レイアウトの計算は M1 で解消 |
| Controller | 入力の受け取りと、Model・View への指示 | 運筆や墨消費のルールは C1 で意図して残す。設定値の範囲は C3 で解消。表示の数値と描画用ヘッダーへの依存が残っている |
| エントリポイント | 起動と Controller への受け渡し | 状態の直接更新は E1 で解消 |

判断の目安は「画面デザインを変えたら、その値や処理も変わるか」です。変わるものは View 側に置きます。

| 優先度 | 件数 | 項目 |
|---|---|---|
| 高 | 3 | M1 レイアウト計算（完了）、M2 View への依存（完了）、M3 UI の状態（完了） |
| 中 | 7 | M4 ダイアログ（完了）、M5 画面座標の解釈（完了）、M10 運筆の記録の画面への依存（意図して残す）、C1 ドメインルール（意図して残す）、C2 余白の重複（完了）、C3 設定値の範囲の重複（完了）、E1 状態の直接更新（完了） |
| 低 | 9 | M6 操作中の一時状態（完了）、M7 描画エンジンへの依存（完了）、M8 画面の種類と UI 用の定数（完了）、M9 升目の分割数（完了）、M11 Model の小さなずれ（意図して残す）、C4 Controller の表示の数値、C5 ホバーの番号、C6 描画用ヘッダーへの依存、S1 Service の責務（第 1 段階まで完了） |

---

## 2. Model の問題

### M1. Model が画面レイアウトを計算している（優先度: 高）（完了）

レイアウトの計算を `Views/ScreenLayout` へ移し、`Models/AppState.cpp` を削除しました。`ScreenLayout` は `UIState&` と読み取り専用の `PaperModel`、`viewingImport`、`ReplayModel` を受け取るので、書き換えられるのは `UIState` だけです。計算式と順序は変えていません。Windows でのビルドは未確認です。

以下は修正前の記録です。

**場所:** `LayoutClearModal`、`UpdateReplaySeekThumb`、`AppState::Layout`、`AppState::LayoutPaperOnly`、`AppState::LayoutGridGeometry`（いずれも `Models/AppState.cpp`）

**問題:** ボタンの座標や大きさは「どう見せるか」の情報です。`AppState.cpp` のほぼすべてを、これらの処理が占めていました。ボタンの大きさを変えるだけでも Model を編集することになります。

**修正の方向:** `Views/` 側にレイアウト専用のクラスを作って移します。Controller はクリック判定のために、その計算結果を読み取ります。

### M2. Model が View のヘッダーに依存している（優先度: 高）（完了）

`Clamp`、`RW`、`RH`、`PtIn` を新しい `Models/MathUtils.h` へ移し、`Models/` と `Services/ImageExporter.cpp` から `RenderUtils` への依存をなくしました。View と Controller の呼び出しは、`RenderUtils.h` の using 宣言でそのまま使えます。Windows でのビルドは未確認です。

以下は修正前の記録です。

**場所**

- `Models/AppState.h:13`
- `Models/TrajectoryModel.cpp:3`

どちらも `Views/RenderUtils.h` を読み込んでいます。

**問題:** Model は View を知らないのが原則で、依存の向きが逆です。`RenderUtils.h` には `Clamp` などの計算関数のほか、`HDC` や `HFONT` を使う描画関数も宣言されているため、Model から描画関数が見える状態です。Model 側で使っているのは `Clamp`、`RW`、`RH`、`PtIn` で、描画関数の呼び出しは見つかっていません。

**修正の方向:** これらの4関数を描画と無関係な共通ヘッダーへ移し、Model からは `RenderUtils.h` を読み込まないようにします。

### M3. Model が UI の状態を持っている（優先度: 高）（完了）

`UIState` を `Views/UIState.h`、`UIWidget` を `Views/UIComponents.h` へ移し、`AppState::ui` を削除しました。実体は `WacomMT_Scribble.cpp` の `g_uiState` で、View と Controller へは引数で渡します。`Models/` から `UIState` と `UIWidget` への参照はなくなりました。

**段階**

- **第 1 段階:** `UIWidget` の未使用メンバー（`isHovered`、`isPressed`、`isEnabled`、`Contains`、`Width`、`Height`）を削除しました。3 つのコンストラクタは代入で使うので残しています。Windows でのビルドは未確認です。
- **第 2 段階:** `SetSaveFeedback` を `AppState` から `UIState` へ移しました。`ExportCanvas` は `AppState` の代わりに半紙の矩形を受け取るようにし、`ImageExporter.h` から `AppState.h` への依存をなくしました。Windows でのビルドは未確認です。
- **第 3 段階:** View の関数が `state.ui` を読まず、`const UIState& ui` を引数で受け取るようにしました。`AppState` を使わなくなった `DrawBackground`、`RenderInk`、`Draw3DBrushPose`、`ModalView` の 2 つの関数は、`UIState` だけを受け取ります。呼び出し元の `WacomMT_Scribble.cpp` は、今は `g_appState.ui` を渡します。Windows でのビルドは未確認です。
- **第 4 段階:** `AppController` と `StrokeController` の関数が `state.ui` を読まず、`UIState& ui` を引数で受け取るようにしました。`AppState` を使わなくなった `ReleasePenLock`、`OnPenOutOfRange`、`ExportCanvasWithFeedback` は、`AppState` の代わりに `UIState` を受け取ります。呼び出し元の `WacomMT_Scribble.cpp` は、今は `g_appState.ui` を渡します。Windows でのビルドは未確認です。

- **第 5 段階:** 定義を `Views/` へ移し、`AppState::ui` を削除して、`g_uiState` を `WacomMT_Scribble.cpp` に置きました。Windows でのビルドは未確認です。

以下は修正前の記録です。

**場所**

- `Models/AppState.h` の `UIState`（ホバー状態、ボタン矩形、モーダルの開閉など）
- `Models/UIComponents.h` の `UIWidget`

**問題:** ホバー状態やボタン矩形は、作品や設定のデータではなく画面の状態です。

**修正の方向:** `UIState` と `UIWidget` を Model の外に移します。選択中のタブなど、一部を画面用の状態として `AppState` に残す判断はありえます。

### M4. Model がファイル選択ダイアログを開いている（優先度: 中）（完了）

3 つのダイアログと結果の列挙型を新しい `Services/ArchiveDialogs` へ移し、`AppController` からはそちらを呼ぶようにしました。Model に残したのは、ファイルパスを受け取る `ExportToJson`、`ExportToCsv`、`ImportFromFile` だけで、`HWND` への依存もなくなりました。Windows でのビルドは未確認です。

以下は修正前の記録です。

**場所**

- `Models/TrajectoryModel.cpp:353`（`PromptSaveArchiveJson`）
- `Models/TrajectoryModel.cpp:378`（`PromptSaveArchiveCsv`）
- `Models/TrajectoryModel.cpp:919`（`PromptLoadArchive`）

**問題:** ダイアログの表示は UI 操作です。Model が `HWND` を受け取っているため、画面なしでテストできず、他の OS にも移しにくくなっています。

**修正の方向:** ダイアログの表示は Controller か Services に移します。Model にはファイルパスを受け取る読み書き処理（`ExportToJson`、`ImportFromFile` など）だけを残します。

### M5. Model が画面座標を解釈している（優先度: 中）（完了）

4 つの関数を `AppState` から `Controllers/AppController.cpp` へ移しました。`WipeInkTo` は `StrokeController` からも呼ぶので `AppController` の公開関数にし、残りの 3 つはファイル内の関数にしました。計算の内容は変えていません。Windows でのビルドは未確認です。

以下は修正前の記録です。

**場所**

| 関数 | 場所 | 内容 |
|---|---|---|
| `ToClearModalSpace` | `Models/AppState.cpp:43` | 画面上の点をモーダルの座標系へ変換 |
| `SetInkAmountFromBadgeX` | `Models/AppState.cpp:335` | ゲージの X 座標から墨の残量を決定 |
| `IsOnInkStoneLand` | `Models/AppState.cpp:345` | 点が硯の下の部分にあるかを判定 |
| `WipeInkTo` | `Models/AppState.cpp:353` | 筆の移動距離から墨を落とす |

**問題:** 「画面のこの位置を押した」を「残量を何％にする」に変換するのは、Controller の役割です。

**修正の方向:** 座標の解釈は Controller に移します。Model には `ink.amount` の変更や `ink.Wipe` のように、座標を含まない操作だけを残します。

### M6. 操作中の一時状態や表示用メッセージが Model にある（優先度: 低）（完了）

Model のフラグと `UIState` の中の操作の状態を、`Views/UIState.h` の新しい `InteractionState`（`ui.interaction`）へまとめました。`ReplayModel::Reset` と `LayoutPaperOnly` にあったフラグの解除は、`ClearAllInk` と `SetPaperOnly` へ移しました。Windows でのビルドは未確認です。

以下は修正前の記録です。表示用メッセージの `SetSaveFeedback` は、M3 で `UIState` ごと `Views/UIState.h` へ移しました。

**場所**

- `Models/AppState.h:18`（`BrushModel::isDraggingHardness`）
- `Models/AppState.h:78`（`OtehonModel::isDraggingOpacity`）
- `Models/AppState.h:83`（`OtehonModel::isTyping`）
- `Models/TrajectoryModel.h:85-86`（`isDraggingSeekBar`、`isDraggingWaveform`）

**問題:** ドラッグ中かどうかは、筆やお手本のデータではなく操作の状態です。

`Views/ScreenLayout.cpp:141-142` の `LayoutPaperOnly` も、`ui.isDraggingInkAmount` と `ui.isWiping` を解除しています。これもレイアウトではなく操作の状態です。

**修正の方向:** M3 では `UIState` を分けずに移したので、`suppressPenUntilLift` や `isWiping` など、`UIState` の中の操作の状態もここで整理します。Model に残るフラグは、`UIState` か操作の状態を持つ専用の構造体へ移します。

### M8. 画面の種類と UI 用の定数が Model に残っている（優先度: 低）（完了）

遷移の状態と `AppScreen`、`TbButton`、`LeftTab` を `Views/UIState.h` へ、タイマの ID を `Controllers/AppController.h` へ移しました。値は変えていません。Windows でのビルドは未確認です。

以下は修正前の記録です。

**場所**

- `Models/AppState.h:180-184`（`currentScreen`、`isTransitioning`、`transitionStartTime`、`transitionDurationMs`、`transitionProgress`）
- `Models/AppEnums.h:54`、`:60`、`:74`（`AppScreen`、`TbButton`、`LeftTab`）
- `Models/AppEnums.h:88-90`（タイマの ID）

**問題:** どの画面を出しているかや遷移の進み具合、ボタンの種類、タイマの ID は、作品や設定のデータではなく画面の情報です。M3 の修正で、`UIState` を移したあとも残っていることが分かりました。`Models/` のほかの処理からは、`TbButton` と `LeftTab` は使われていません。

**修正の方向:** 遷移の状態は `UIState` へ、UI 用の列挙型とタイマの ID は `Views/` か `Controllers/` のヘッダーへ移します。

### M9. 升目の分割数の計算が View にある（優先度: 低）（完了）

`GetGridDivision` を `Views/ScreenLayout.cpp` から `PaperModel` のメンバー関数へ移しました。分割数の値は変えていません。Windows でのビルドは未確認です。

以下は修正前の記録です。

**場所:** `Views/ScreenLayout.cpp:10`（`GetGridDivision`）

**問題:** 升目の種類から列数と行数を決める計算で、座標も画面の情報も使っていません。M1 でレイアウトの計算と一緒に `ScreenLayout` へ移しましたが、見せ方の計算ではなく、半紙の設定のルールです。

**修正の方向:** `PaperModel` のメンバー関数にして、`ScreenLayout` からはそれを呼びます。

### M10. 運筆の記録が画面のピクセルに依存している（優先度: 中）（意図して残す）

**判断:** 不具合は確認しておらず、修正は JSON と CSV の形式やリプレイにも及ぶので、今は直しません。記録の形式を変えるときや、同じ記録を別の画面サイズで扱う機能を足すときに見直します。

**場所**

- `Models/TrajectoryModel.h:107`（`AddPoint`。`PenInputEvent` と画面上の半紙の矩形を受け取る）
- `Models/TrajectoryModel.h:122`、`:124`（`RotateQuarter`、`RotateStrokeQuarter`）
- `Models/TrajectoryModel.h:141`（`GetReplaySample`）
- `Models/TrajectoryModel.h:156`（`ImportFromFile`）
- `Models/TrajectoryModel.h:18-19`、`:23-24`（`paperX`、`paperY`、`speedPxPerSec`、`width`）

**問題:** 座標は正規化もしていますが、線幅と速度、`paperX`、`paperY` は画面のピクセルを基準に持っています。紙だけ表示に切り替えると、`RotateQuarter` で記録も書き換わるので、表示の切り替えが作品のデータを変える形です。`AddPoint` は入力層の `PenInputEvent` にも依存しています。

**修正するときの方向:** 記録は半紙に対する相対値で持ち、画面のピクセルへの変換は View と Controller で行います。古いアーカイブの読み込みを崩さないようにします。

### M11. Model の置き場所と中身に小さなずれがある（優先度: 低）（意図して残す）

**判断:** 動作に影響がなく、設計を歪めるほどでもないので、今は直しません。近くを変更するときに合わせて直します。

**場所**

- `InkEngine/InkModel.h`（Model なのに `Models/` にない）
- `Models/AppEnums.h:13`（表示名を返す `BrushName`。どこからも呼ばれていない）
- `Models/TrajectoryModel.cpp` の `ImportFromFile` とその内部の処理（画面に出す日本語のエラーメッセージを作る）

**修正するときの方向:** `InkModel.h` は `Models/` へ移し、`FudeSense.vcxproj` も更新します。`BrushName` は削除します。エラーメッセージは、エラーの種類を返す形にして、Controller か View で文言にします。

### M7. Model が描画エンジンに依存している（優先度: 低）（完了）

墨の控えの取得と書き戻しを `AppController` へ移し、`UndoHistory` は `InkSnapshot` の保管と、墨残量・運筆アーカイブをまとめた更新だけを行うようにしました。控えの回転は関数として受け取るので、`Models/` から `GpuInk` への依存はなくなっています。Windows でのビルドは未確認です。

以下は修正前の記録です。

**場所**

- `Models/UndoHistory.h:43`、`:46`、`:49`（`GpuInk&` を受け取る）
- `Models/UndoHistory.cpp:3`（`GpuInk.h` を読み込む）

**問題:** View の混入ではありませんが、Model が Direct2D / Direct3D 11 を使う墨エンジンを直接操作しています。

**修正の方向:** 墨の控えの取得と復元は Controller が行い、`UndoHistory` は `InkSnapshot` の保管に専念します。

---

## 3. Controller の問題

### C1. ドメインルールが Controller にある（優先度: 中）（意図して残す）

**判断:** MVC の観点では指摘は妥当ですが、Controller に残します。計算を使うのは `StrokeController` だけで、Controller が持つ平滑化の状態とも結び付いています。単体テストも今は予定がなく、移す利点より、書き味が変わるリスクとペンでの確認の負担のほうが大きいからです。同じ計算を別の場所でも使うときや、数値のテストを用意するときに見直します。

以下は指摘の記録です。

**場所:** `Controllers/StrokeController.cpp`

| 行 | 内容 |
|---|---|
| `:143` | 筆の硬さを筆圧へ反映 |
| `:151-188` | 傾き、方位角、止め、払いによる線幅の計算 |
| `:199-207` | 線幅の平滑化 |
| `:210-220` | 墨の消費量の計算 |

**問題:** 筆の太り方や墨の減り方は、入力の扱いではなくアプリのルールです。Controller にあると、ペン入力を介さずに単体で確かめにくくなります。

**修正の方向:** 計算を Model 側（`BrushModel` や `InkModel`、または運筆計算の専用クラス）へ移し、Controller は入力を渡して結果を受け取るだけにします。

### C2. View の余白の数値が Controller に重複している（優先度: 中）（完了）

`AppState::Layout` で波形の描画領域 `ui.rAnalysisGraphPlot` を一度だけ計算し、描画とクリック、ドラッグの判定はその値を読むようにしました。余白の数値は変えていません。余白は `Layout` にあるので、M1 で `Layout` と一緒に `Views/` 側へ移します。Windows でのビルドは未確認です。

以下は修正前の記録です。

**場所:** 同じ `+38 / +32 / -14 / -20` の余白が3か所にあります。

- `Views/AnalysisView.cpp:584`（描画）
- `Controllers/AppController.cpp:534`（クリック判定）
- `Controllers/AppController.cpp:874`（ドラッグ判定）

**問題:** グラフの余白は View の知識です。描画側だけを変更すると、見た目とクリック位置がずれます。今回の中で、不具合に直結しやすい問題です。

**修正の方向:** レイアウトの計算でグラフの描画領域を一度だけ求め、View と Controller の両方がそれを参照します。

### C3. 設定値の範囲が Controller と View に重複している（優先度: 中）（完了）

硬さと透過度の範囲の定数と、スライダーの割合との変換を `BrushModel` と `OtehonModel` へ移し、Controller と View はそれを使うようにしました。値は変えていません。Windows でのビルドは未確認です。

以下は修正前の記録です。

**場所**

- `Controllers/AppController.cpp:670`、`:1087`（硬さの 0.1〜2.0 への変換）
- `Controllers/AppController.cpp:808`、`:1101`（透過度の 0.05〜1.0 への変換）
- `Views/FloatingMenuView.cpp:36`（硬さからつまみの位置への変換）
- `Models/AppState.h:17`（硬さの範囲はコメントだけ）

**問題:** 設定値の範囲は Model のルールですが、数値が Controller と View に書かれています。範囲を変えると複数の箇所の修正が必要で、1 か所でも漏れると、つまみの位置と実際の値がずれます。

**修正の方向:** 範囲の定数と、割合と値の変換を `BrushModel` と `OtehonModel` に置き、Controller と View はそれを使います。

### C4. Controller に表示の数値がある（優先度: 低）

**場所**

- `Controllers/StrokeController.cpp:284`、`:311`（硯と墨残量の再描画の範囲。`44 * ui.inkStoneScale`）
- `Controllers/AppController.cpp:23-24`（IME の候補の位置。入力欄から `+12`、`+6`）

**問題:** 再描画の範囲は、`Views/ScreenLayout.cpp:437-441` のゲージの配置（高さ 34、間隔 8）に依存していて、同じ式が 2 か所にあります。ゲージの配置を変えると、再描画の範囲からはみ出すおそれがあります。IME の位置も入力欄の見た目に依存します。入力欄の文字の位置（`Views/FloatingMenuView.cpp` の `+14`）とは値が違いますが、意図した差かは分かっていません。

**修正の方向:** 再描画の範囲は同じ式のまま `ScreenLayout` で計算して `UIState` に持ち、Controller はそれを読みます。IME の位置も値を変えずに `ScreenLayout` へ移します。

### C5. ホバーの番号を Controller と View が数値で共有している（優先度: 低）

**場所:** `Views/UIState.h:82`、`:89`、`:91-92`、`:95`（`hoverClearModal`、`hoverPaperOnly`、`hoverSub`、`hoverInkStone`、`hoverReplayBtn`）

**問題:** たとえば `hoverSub == 10 + i` は升目のタイルを表します。Controller が番号を書き、View が同じ番号を読むので、片方だけ変えると表示が崩れます。意味はコメントかコードを読まないと分かりません。

**修正の方向:** 番号に名前を付けます。変更する箇所が多いので、変数ごとに段階を分けます。

### C6. Controller が描画用のヘッダーに依存している（優先度: 低）

**場所**

- `Controllers/AppController.cpp:3`、`:14`
- `Controllers/StrokeController.cpp:4`、`:229-230`
- `WacomMT_Scribble.cpp:33`、`:354`、`:373`

**問題:** `Views/RenderUtils.h` には描画関数が宣言されていますが、ここで使っているのは `Clamp`、`RW`、`RH`、`PtIn` だけで、どれも `Models/MathUtils.h` にあります。描画しない層から描画関数が見える状態です。

**修正の方向:** `MathUtils.h` に置き換えます。`RenderUtils.h` を通して間接的に読み込んでいたヘッダーがないかも確かめます。

---

## 4. エントリポイントの問題

### E1. ウィンドウプロシージャが状態を直接更新している（優先度: 中）（完了）

タイマとキーの処理、入力ロックなどのフラグの解除を `AppController` の `OnTimer`、`OnKeyDown`、`ReleasePenLock`、`OnPenOutOfRange`、`OnLButtonUp` へ移し、`g_appState` への直接の代入をなくしました。ウィンドウ操作の `F11` と、ストロークの開始や終了、入力の経路の振り分けは `WacomMT_Scribble.cpp` に残しています。Windows でのビルドは未確認です。

以下は修正前の記録です。

**場所:** `WacomMT_Scribble.cpp`

| 行 | 内容 |
|---|---|
| `:255-268` 付近 | タイトル画面から制作画面への遷移の進行 |
| `:299-311` 付近 | リプレイの時刻を進める |
| `:368`、`:375-376` | Esc キーで全消し確認モーダルを開閉 |
| `:481`、`:520-521`、`:561`、`:602`、`:627-629` | 入力ロックなどのフラグの解除 |

**問題:** メッセージの受け渡しに加えて、Controller が担当すべき判断と状態更新が入っています。同じ操作の処理が `AppController` と分かれるため、追いにくくなります。

**修正の方向:** `OnTimer` や `OnKeyDown` のような関数を Controller に作り、ウィンドウプロシージャは呼び出すだけにします。

---

## 5. Service の問題

### S1. `ImageExporter` の責務が広い（優先度: 低）

**終わった段階（第 1 段階）:** `ExportCanvas` は `ImageExportResult` で結果を返すだけにし、メッセージの設定は `AppController` の `ExportCanvasWithFeedback` へ移しました。`AppState` は `const` で受け取るので、Service から書き込めません。Windows でのビルドは未確認です。

**残り（第 2 段階）:** 作品画像の合成を、書き出しから分けます。

**場所**

- `Services/ImageExporter.cpp:147`（半紙の背景色を直接指定）
- `Services/ImageExporter.cpp:154`（墨を描画）
- `Controllers/AppController.cpp:46`（半紙の大きさを `ui.rPaper` から渡す。`ImageExporter` が直接読むのは M3 の第 2 段階で解消）

**問題:** 作品画像の合成は描画の処理で、書き出しとは別の責務です。

**修正の方向:** 合成した `HBITMAP` を Service へ渡す形が候補です。クリップボードへ渡したときはビットマップの所有権がシステムへ移るので、解放の扱いを崩さないように分けます。

---

## 6. 問題ない点

- **View から Model への書き込み:** 描画を行う `Views/` の関数は `const AppState&` と `const UIState&` を受け取っていて、`const_cast` や `mutable` も見つかりませんでした。レイアウトを計算する `ScreenLayout` だけは例外で、`UIState&` へ書き込みます。`UIState` は M3 で `Views/` へ移したので、Model への書き込みではありません。
- **Model 内の描画処理:** `Models/` に描画処理の呼び出しは見つかりませんでした。
- **運筆の記録の座標:** 正規化座標（`normX`、`normY`）を持っています。ただし、線幅や速度などは画面のピクセルに依存しています（M10）。
- **Controller のカーソル変更や入力位置の指定:** `SetCursor` や IME の呼び出しは、Win32 では Controller にあっても大きな問題ではありません。位置の数値は C4 で扱います。
- **`GpuInk`:** 墨の画素データと描画の両方を持っていて、純粋な MVC には収まりません。分けるには墨エンジンの作り直しが必要なので、構造上の例外として扱います。
- **エントリポイントの入力の振り分け:** マウスとペンの判別や運筆の開始は、E1 で意図して `WacomMT_Scribble.cpp` に残しました。

---

## 7. 当初の設計との関係

`AGENTS.md:51` や `learn/models.md:158` では、`AppState` を UI 状態とレイアウト計算を含むファサードとして説明しています。座標を一か所で計算すれば、描画と判定、升目とお手本の位置が揃うという狙いがあり、書き漏れではなく意図した設計です。

そのため、「当初の設計判断だが、厳密な MVC からは外れている」というのが正確な評価です。この狙いは、レイアウト専用のクラスを `Views/` 側に置いても保てます。

修正時には、`AGENTS.md` と `learn/` の説明も合わせて更新する必要があります。

---

## 8. 修正の順番（案）

変更が小さく、影響範囲が狭いものから進める案です。

1. **M2（完了）:** 共通の計算関数を移し、Model から `RenderUtils.h` への依存をなくす
2. **M4（完了）:** ダイアログを Model の外へ移す
3. **C2（完了）:** グラフの余白の重複をなくす
4. **M5（完了）、E1（完了）:** 座標の解釈と状態の更新を Controller に集める
5. **C1（意図して残す）、S1（第 1 段階まで完了）、M7（完了）:** ドメインルールと Service、墨エンジンの依存を整理する
6. **M1（完了）、M3（完了）、M6（完了）:** レイアウトと `UIState` を分離する
7. **M8（完了）:** 画面の種類と UI 用の定数を Model の外へ移す
8. **M9（完了）:** 升目の分割数の計算を Model へ戻す
9. **C3（完了）:** 硬さと透過度の範囲を Model にまとめる
10. **C6:** Controller の描画用ヘッダーへの依存をなくす
11. **S1（第 2 段階）:** 作品画像の合成を書き出しから分ける
12. **C4:** 表示の数値を `ScreenLayout` へ移す
13. **C5:** ホバーの番号に名前を付ける

残りは C6、S1 の第 2 段階、C4、C5 です。C1、M10、M11 は意図して残します。
