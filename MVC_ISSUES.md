# MVC アーキテクチャの問題点

Fude Sense のコードが MVC から外れている箇所をまとめた資料です。

- **確認時点:** コミット `8b4acde`（`Debuc04` ブランチ）
- **確認方法:** キーワード検索と主要部分の読み取り。全行は読んでいないため、記載のない問題が残っている可能性があります。
- **パス:** 特に記載がなければ `App/FudeCode/` からの相対パスです。

---

## 1. まとめ

最も大きな問題は、**Model（`AppState`）が画面の情報を抱えていること**です。

| 層 | 本来の役割 | 現状の問題 |
|---|---|---|
| Model | データとアプリのルール | 画面レイアウト、UI の状態、ダイアログを持っている |
| View | 表示と、そのためのレイアウト | レイアウトの計算を Model に任せている |
| Controller | 入力の受け取りと、Model・View への指示 | 運筆や墨消費のルール、View の余白の数値を持っている |
| エントリポイント | 起動と Controller への受け渡し | 状態の更新を直接行っている |

判断の目安は「画面デザインを変えたら、その値や処理も変わるか」です。変わるものは View 側に置きます。

| 優先度 | 件数 | 項目 |
|---|---|---|
| 高 | 3 | M1 レイアウト計算、M2 View への依存（完了）、M3 UI の状態 |
| 中 | 5 | M4 ダイアログ、M5 画面座標の解釈、C1 ドメインルール、C2 余白の重複、E1 状態の直接更新 |
| 低 | 3 | M6 操作中の一時状態、M7 描画エンジンへの依存、S1 Service の責務 |

---

## 2. Model の問題

### M1. Model が画面レイアウトを計算している（優先度: 高）

**場所**

| 関数 | 場所 | 内容 |
|---|---|---|
| `LayoutClearModal` | `Models/AppState.cpp:27` | 全消し確認モーダルの配置 |
| `UpdateReplaySeekThumb` | `Models/AppState.cpp:51` | シークバーのツマミの配置 |
| `AppState::Layout` | `Models/AppState.cpp:61` | 全 UI 要素の配置 |
| `AppState::LayoutPaperOnly` | `Models/AppState.cpp:373` | 紙だけ表示の配置 |
| `AppState::LayoutGridGeometry` | `Models/AppState.cpp:431` | 升目のピクセル矩形 |

**問題:** ボタンの座標や大きさは「どう見せるか」の情報です。`AppState.cpp` 全513行のうち、約8割をこれらの処理が占めています。ボタンの大きさを変えるだけでも Model を編集することになります。

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

### M3. Model が UI の状態を持っている（優先度: 高）

**場所**

- `Models/AppState.h:182-262` の `UIState`（ホバー状態、ボタン矩形、モーダルの開閉など）
- `Models/UIComponents.h` の `UIWidget`

**問題:** ホバー状態やボタン矩形は、作品や設定のデータではなく画面の状態です。また、`UIWidget` の `isHovered`、`isPressed`、`isEnabled` はどこからも使われていません。

**修正の方向:** `UIState` と `UIWidget` を Model の外に移します。選択中のタブなど、一部を画面用の状態として `AppState` に残す判断はありえます。未使用のメンバーは削除できます。

### M4. Model がファイル選択ダイアログを開いている（優先度: 中）

**場所**

- `Models/TrajectoryModel.cpp:353`（`PromptSaveArchiveJson`）
- `Models/TrajectoryModel.cpp:378`（`PromptSaveArchiveCsv`）
- `Models/TrajectoryModel.cpp:919`（`PromptLoadArchive`）

**問題:** ダイアログの表示は UI 操作です。Model が `HWND` を受け取っているため、画面なしでテストできず、他の OS にも移しにくくなっています。

**修正の方向:** ダイアログの表示は Controller か Services に移します。Model にはファイルパスを受け取る読み書き処理（`ExportToJson`、`ImportFromFile` など）だけを残します。

### M5. Model が画面座標を解釈している（優先度: 中）

**場所**

| 関数 | 場所 | 内容 |
|---|---|---|
| `ToClearModalSpace` | `Models/AppState.cpp:43` | 画面上の点をモーダルの座標系へ変換 |
| `SetInkAmountFromBadgeX` | `Models/AppState.cpp:334` | ゲージの X 座標から墨の残量を決定 |
| `IsOnInkStoneLand` | `Models/AppState.cpp:344` | 点が硯の下の部分にあるかを判定 |
| `WipeInkTo` | `Models/AppState.cpp:352` | 筆の移動距離から墨を落とす |

**問題:** 「画面のこの位置を押した」を「残量を何％にする」に変換するのは、Controller の役割です。

**修正の方向:** 座標の解釈は Controller に移します。Model には `ink.amount` の変更や `ink.Wipe` のように、座標を含まない操作だけを残します。

### M6. 操作中の一時状態や表示用メッセージが Model にある（優先度: 低）

**場所**

- `Models/AppState.h:18`（`BrushModel::isDraggingHardness`）
- `Models/AppState.h:78`（`OtehonModel::isDraggingOpacity`）
- `Models/AppState.h:83`（`OtehonModel::isTyping`）
- `Models/AppState.h:309`（`SetSaveFeedback`）
- `Models/TrajectoryModel.h:85-86`（`isDraggingSeekBar`、`isDraggingWaveform`）

**問題:** ドラッグ中かどうかは、筆やお手本のデータではなく操作の状態です。

**修正の方向:** M3 で UI の状態を移すときに一緒に移します。

### M7. Model が描画エンジンに依存している（優先度: 低）

**場所**

- `Models/UndoHistory.h:43`、`:46`、`:49`（`GpuInk&` を受け取る）
- `Models/UndoHistory.cpp:3`（`GpuInk.h` を読み込む）

**問題:** View の混入ではありませんが、Model が Direct2D / Direct3D 11 を使う墨エンジンを直接操作しています。

**修正の方向:** 墨の控えの取得と復元は Controller が行い、`UndoHistory` は `InkSnapshot` の保管に専念します。

---

## 3. Controller の問題

### C1. ドメインルールが Controller にある（優先度: 中）

**場所:** `Controllers/StrokeController.cpp`

| 行 | 内容 |
|---|---|
| `:141` | 筆の硬さを筆圧へ反映 |
| `:149-187` | 傾き、方位角、止め、払いによる線幅の計算 |
| `:198-207` 付近 | 線幅の平滑化 |
| `:209-219` | 墨の消費量の計算 |

**問題:** 筆の太り方や墨の減り方は、入力の扱いではなくアプリのルールです。Controller にあると、ペン入力を介さずに単体で確かめにくくなります。

**修正の方向:** 計算を Model 側（`BrushModel` や `InkModel`、または運筆計算の専用クラス）へ移し、Controller は入力を渡して結果を受け取るだけにします。

### C2. View の余白の数値が Controller に重複している（優先度: 中）

**場所:** 同じ `+38 / +32 / -14 / -20` の余白が3か所にあります。

- `Views/AnalysisView.cpp:584`（描画）
- `Controllers/AppController.cpp:533`（クリック判定）
- `Controllers/AppController.cpp:873`（ドラッグ判定）

**問題:** グラフの余白は View の知識です。描画側だけを変更すると、見た目とクリック位置がずれます。今回の中で、不具合に直結しやすい問題です。

**修正の方向:** レイアウトの計算でグラフの描画領域を一度だけ求め、View と Controller の両方がそれを参照します。

---

## 4. エントリポイントの問題

### E1. ウィンドウプロシージャが状態を直接更新している（優先度: 中）

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

**場所**

- `Services/ImageExporter.cpp:147`（半紙の背景色を直接指定）
- `Services/ImageExporter.cpp:154`（墨を描画）
- `Services/ImageExporter.cpp:162`、`:178`、`:185`、`:194`（`AppState` へ画面用メッセージを直接設定）

**問題:** 作品画像の合成は描画の処理で、書き出しとは別の責務です。また、結果の通知まで Service が UI の状態に直接書き込んでいます。

**修正の方向:** Service は成否を返すだけにして、メッセージの設定は Controller で行います。

---

## 6. 問題ない点

- **View から Model への書き込み:** `Views/` の関数は `const AppState&` を受け取っていて、`const_cast` や `mutable` も見つかりませんでした。
- **Model 内の描画処理:** `Models/` に描画処理の呼び出しは見つかりませんでした。
- **運筆の記録:** 正規化座標（`normX`、`normY`）を持っていて、画面サイズに依存しない形にできています。
- **Controller のカーソル変更や入力位置の指定:** `SetCursor` や IME の位置指定は、Win32 では Controller にあっても大きな問題ではありません。

---

## 7. 当初の設計との関係

`AGENTS.md:50` や `learn/models.md:158` では、`AppState` を UI 状態とレイアウト計算を含むファサードとして説明しています。座標を一か所で計算すれば、描画と判定、升目とお手本の位置が揃うという狙いがあり、書き漏れではなく意図した設計です。

そのため、「当初の設計判断だが、厳密な MVC からは外れている」というのが正確な評価です。この狙いは、レイアウト専用のクラスを `Views/` 側に置いても保てます。

修正時には、`AGENTS.md` と `learn/` の説明も合わせて更新する必要があります。

---

## 8. 修正の順番（案）

変更が小さく、影響範囲が狭いものから進める案です。

1. **M2（完了）:** 共通の計算関数を移し、Model から `RenderUtils.h` への依存をなくす
2. **M4:** ダイアログを Model の外へ移す
3. **C2:** グラフの余白の重複をなくす
4. **M5、E1:** 座標の解釈と状態の更新を Controller に集める
5. **C1、S1、M7:** ドメインルールと Service、墨エンジンの依存を整理する
6. **M1、M3、M6:** レイアウトと `UIState` を分離する

6 は効果が最も大きい一方で、変更範囲も最大です。`Layout` の呼び出しが8か所あり、運筆処理は `rPaper` を、お手本の配置は升目の計算結果を使っています。
