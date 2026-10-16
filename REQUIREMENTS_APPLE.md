# Fude Sense iPad / Mac 版 開発要件

> 現行の Windows 版（Win32 + Direct3D 11 + Wintab）と同じ書き味・機能を、iPad（Apple Pencil）と Mac（液タブ / ペンタブ）で提供するための要件をまとめる。
> 「要検討」と書いた項目は、まだ決まっていないもの。

---

## 1. 目的と範囲

- **目的**: 1 つのリポジトリで Windows 版・iPad 版・Mac 版を開発し、筆の物理・墨のにじみ・運筆記録といった中心部分を共通にする。
- **対象**:
  - iPad 版（Apple Pencil）
  - Mac 版（Wacom などの液タブ / ペンタブ、マウス、トラックパッド）
- **前提**:
  - **Windows 版は壊さない。** 共通化の作業中も、Visual Studio 2022 でのビルドと動作を常に保つ。
  - 実行ファイル（アプリ）は機種ごとに分かれる。ソースコードの中心部分は共通にする。

---

## 2. 全体の構成方針

```text
        ┌──────────────── 共通コア（C++17）────────────────┐
        │ Models / Controllers / InkEngine の計算部分          │
        │ 筆の物理・墨の消費・カスレ・にじみ（CPU 版）・運筆記録・一画戻す │
        └───────────────────────┬──────────────────────────┘
          ┌─────────────────────┼─────────────────────┐
     Windows 版               iPad 版                 Mac 版
     Win32 GDI / D2D        SwiftUI                 SwiftUI
     Direct3D 11            Metal                   Metal
     Wintab / Pointer       UITouch（Apple Pencil）   NSEvent（タブレットイベント）
     → FudeSense.exe        → iPad アプリ             → Mac アプリ
```

- iPad 版と Mac 版は **SwiftUI のマルチプラットフォームアプリ** として 1 つの Xcode プロジェクトにまとめ、画面・Metal の処理を共有する。
- 違いはペン入力（UIKit / AppKit）と、一部の画面配置だけにする。
- Swift から共通コア（C++）を呼ぶ方法は、Objective-C++ のラッパー、または Swift の C++ 相互運用のどちらかにする（要検討）。
- Mac 版は、iPad アプリを Apple Silicon Mac でそのまま動かす方法（Designed for iPad）では作らない。この方法では液タブの筆圧・傾き（NSEvent のタブレットイベント）を受け取れないため。

---

## 3. 現行コードの評価（共通化の見込み）

| モジュール | 行数（概算） | Windows 依存 | 方針 |
|---|---|---|---|
| Models | 約 2,500 | 少 | 共通コアへ。Windows の型を置き換える |
| Controllers（StrokeController） | 約 1,400 | 少 | 共通コアへ |
| Controllers（AppController） | 同上に含む | 中 | UI 操作の部分は機種ごとに作るか、状態操作だけ共通にする（要検討） |
| InkEngine（InkModel, にじみ計算） | 約 2,900 | 中 | 計算部分は共通コアへ。GPU 部分は機種ごとに作る |
| Inputs（PenInputEvent） | 約 170 | 少 | `PenInputEvent` を共通の入力形式にする |
| Views | 約 3,500 | **大** | 機種ごとに作り直す（SwiftUI + Metal） |
| Services（Wintab, WIC） | 約 600 | 大 | 機種ごとに作り直す |

---

## 4. 機能要件（Windows 版との対応）

| 機能 | Windows 版 | iPad 版 | Mac 版 |
|---|---|---|---|
| タイトル画面 | あり | 同等 | 同等 |
| 筆（小筆・中筆・大筆、硬さ 0.1〜2.0） | あり | 同等 | 同等 |
| 半紙（242×333mm）・下敷きガイド 6 種・罫線の配色 3 種 | あり | 同等 | 同等 |
| お手本の重ね表示（3 書体、文字入力、升目ミニマップ、不透明度） | あり | 同等（日本語入力に対応） | 同等 |
| 硯での墨補充（筆圧に応じた量）・墨残量 | あり | 同等（Apple Pencil の筆圧で補充） | 同等 |
| にじみ・カスレの物理シミュレーション | D3D11 / CPU | Metal / CPU | Metal / CPU |
| 一画戻す / 一画復元 | Ctrl+Z / Ctrl+Y | ボタン、2 本指タップ / 3 本指タップ（要検討）、外付けキーボードなら ⌘Z / ⇧⌘Z | ⌘Z / ⇧⌘Z |
| 全消し（確認つき） | Esc | ボタン | Esc / ボタン |
| 紙を大きくする（横向き） | F9 | iPad を横にしたとき、またはボタン | ボタン / ショートカット |
| 全画面表示 | F11 | 常に全画面 | ⌃⌘F |
| リアルタイム解析（筆圧・角度・速度・画数、筆の傾きレーダー、波形） | あり | 同等 | 同等 |
| リプレイ（再生・一時停止、コマ送り、0.5x / 1x / 2x、シーク） | あり | 同等 | 同等 |
| JSON / CSV の書き出し・読み込み | あり | ファイル App / 共有シート | 保存・開くダイアログ |
| 作品を PNG で保存 | WIC | 写真 App に保存 / 共有シート | 保存ダイアログ |
| クリップボードへコピー | あり | UIPasteboard | NSPasteboard |

---

## 5. ペン入力の要件

### 5.1 共通の入力形式

`Inputs/PenInputEvent.h` の形式を共通コアに移し、すべての機種の入力をこの形に変換する。

| `PenInputEvent` の項目 | iPad（Apple Pencil / UITouch） | Mac（NSEvent タブレットイベント） |
|---|---|---|
| `x`, `y` | `location(in:)` | `locationInWindow` を画面の座標に変換 |
| `pressure`（0〜1） | `force / maximumPossibleForce` | `pressure` |
| `altitudeDegrees` | `altitudeAngle`（ラジアン → 度） | `tilt`（x, y）から計算 |
| `azimuthRad` | `azimuthAngle(in:)` | `tilt`（x, y）から計算 |
| `z`, `inRange`（ホバー） | `UIHoverGestureRecognizer`（ホバー対応の iPad と Pencil の組み合わせのみ） | 近接イベント（`tabletProximity`） |
| `isEraser` | なし（Pencil のダブルタップやスクイーズで切り替える案。要検討） | `pointingDeviceType == .eraser` |
| `time` | `timestamp` | `timestamp` |

### 5.2 iPad の要件

- **遅延を小さくする**: `coalescedTouches`（間引かれた点をすべて取得する）と `predictedTouches`（次の点の予測）を使う。Apple Pencil は最大 240Hz で点を送る。
- **パームリジェクション**: 半紙の上では Apple Pencil の入力だけを筆として扱い、指や手のひらは無視する。指は UI 操作とジェスチャーに使う。
- **筆圧の補正**: Apple Pencil と Wacom では筆圧の感じ方が違う。機種ごとに筆圧カーブの初期値を持たせ、筆の硬さの補正と組み合わせる。
- **ホバー**: ホバーに対応していない iPad と Pencil の組み合わせでも、正しく動くようにする（ホバーがないときは `inRange` を接触中だけ true にする）。

### 5.3 Mac の要件

- Wacom の macOS 用ドライバを入れた液タブ / ペンタブで、筆圧・傾き・消しゴムを扱えること。
- マウス・トラックパッドでも代わりに操作できること（現行の MouseAdapter と同じ考え方）。
- 液タブの画面とペンの位置関係（マッピング）は、OS とドライバに任せる。

---

## 6. 描画・GPU の要件

- **にじみの計算**: `InkEngine/GpuSimulator.cpp` の HLSL コンピュートシェーダー（`kPropagationCS`、16×16 スレッド）を Metal Shading Language に書き直す。
- **段階的に移す**: まず共通コアの CPU 版のにじみ計算で動かし、Metal 版の結果が CPU 版と同じになることを確かめてから切り替える。
- **メモリの共有**: Apple Silicon は CPU と GPU がメモリを共有しているので、`MTLStorageMode.shared` のバッファを使い、Windows 版のような読み戻し（リードバック）をなくす。
- **合成**: 墨は和紙の色に乗算で合成する（Windows 版の `SRCAND` と同じ見た目にする）。
- **フレームレート**: ProMotion（120Hz）の機種では 120fps、それ以外は 60fps を目標にする。
- **解像度**: Retina 画面で、半紙の内部の解像度をどうするか（何 px にするか）を決める。保存する画像の解像度も決める（要検討）。

---

## 7. 共通コアの要件

- **Windows の型をなくす**: 共通コアでは `windows.h` を読み込まない。
  - `DWORD` → `uint32_t` / `uint64_t`
  - `RECT` / `POINT` → 自前の構造体
  - `BOOL` → `bool`
  - `wchar_t` / `WCHAR` の文字列 → UTF-8 の `std::string`（Windows 側で変換する）
- **言語の規格**: C++17 にそろえる（現在は MSVC の既定の C++14）。
- **ビルド**: Windows（MSVC）と Apple（Xcode / Clang）の両方でビルドできること。CMake を使うかどうかは要検討。
- **テスト**: 筆圧の平滑化・墨の消費・カスレの判定・アーカイブの読み書きは、画面がなくても試せる単体テストを用意する。同じ入力を与えたら、Windows でも Apple でも同じ結果になることを確かめる。
- **作業の順番**: 共通化はまず Windows 上で進め、Windows 版が動くことを確かめてから Apple 版に取りかかる。

---

## 8. データの要件

- **アーカイブ形式**: 現在の `FudeSenseStrokeArchive`（JSON / CSV）をそのまま使い、どの機種で記録したものでも、どの機種でも読み込んでリプレイできるようにする。
  - 座標はすでに正規化された値（`nx`, `ny`）を持っているので、機種の画面サイズが違っても再生できる。
- **追加する項目（案）**: 記録した機種（Windows / iPad / Mac）、入力機器（Wacom / Apple Pencil / マウス）、使った筆圧カーブ。
- **互換性**: 項目を追加しても、古いバージョンのアーカイブを読み込めること。また、Windows 版の古いアプリでも新しいアーカイブを読み込めるようにする（知らない項目は無視する）。

---

## 9. 書体（お手本）の要件

- 現在の HG 正楷書体-PRO・HG 教科書体・HG 行書体は Microsoft Office に付いてくるフォントで、**iPad / Mac には入っておらず、アプリに入れて配布することもできない**。
- **アプリへの組み込みが許されたライセンス** の楷書体・教科書体・行書体を選び、アプリに同梱する。
- 書体を選んだら、ライセンスの表示を `App/THIRD-PARTY.md` に追記する。
- できれば Windows 版も同じ書体にそろえて、お手本の見た目を統一する（要検討）。

---

## 10. 非機能要件

| 項目 | 要件（目標） |
|---|---|
| 書き味の遅延 | ペンが触れてから墨が表示されるまでを、Windows 版と同じかそれより短くする（目標値は要検討） |
| フレームレート | 書いている間に 60fps（ProMotion の機種では 120fps）を下回らない |
| 発熱・電池 | iPad で 30 分続けて書いても、大きく遅くならない |
| メモリ | 一画戻すの履歴（RLE 圧縮）を含め、iPad のメモリ制限内に収める |
| 個人情報 | 運筆データは端末の中だけに保存し、外部へ送らない |
| アクセシビリティ | ボタンが VoiceOver で読み上げられる。Dynamic Type で文字を大きくしても崩れない（できる範囲で） |

---

## 11. 開発環境と必要なもの

| もの | 用途 | 状況 |
|---|---|---|
| Mac（Apple Silicon） | iPad / Mac 版の開発 | あり |
| Xcode（無料） | ビルド、シミュレーター、Metal のデバッグ | 未インストール（Command Line Tools のみ） |
| iPad + Apple Pencil | 実機での確認（筆圧・傾きはシミュレーターでは試せない） | 要確認 |
| Wacom 液タブ / ペンタブ（macOS 用ドライバ） | Mac 版の確認 | 要確認 |
| Apple Developer Program（有料・年会費） | TestFlight や App Store での配布 | 自分の iPad で試すだけなら無料のアカウントでも可 |
| Windows 環境（Parallels など） | Windows 版が壊れていないかの確認 | 要準備 |

### リポジトリの構成（決定）

Windows 版・iPad 版・Mac 版は **1 つのリポジトリ** で開発し、フォルダで分ける。

- 共通コアが 1 か所だけになるので、機種によって書き味やアーカイブの形式がずれない。
- 共通コアの変更と、それに合わせた各アプリの修正を 1 回のコミットにまとめられる。
- リポジトリを分ける（共通コアを別のリポジトリにしてサブモジュールで読み込む）のは、共通コアの形が固まり、担当や公開の方法を分ける必要が出てから改めて考える。

```text
Fude_Sense/
├── Core/          # 共通コア（C++17）
├── Tests/         # 共通コアの単体テスト
├── App/FudeCode/  # Windows 版（今の場所のまま）
└── Apple/         # iPad / Mac 版（Xcode プロジェクト、Swift、Metal）
```

---

## 12. 配布の要件

- テスト配布は TestFlight、公開は App Store で行う。
- App Store の審査に向けて、プライバシーの説明（データを集めない）、同梱フォントのライセンス表示、Wacom SDK など同梱ライブラリのライセンス確認を準備する。
- Windows 専用の SDK（Wintab SDK、Wacom Feel SDK）は Apple 版に入れない。

---

## 13. 段階的な計画

| 段階 | 内容 | 終わりの目安 |
|---|---|---|
| 0. 準備 | Xcode を入れる、Windows の確認環境を用意する、書体を探し始める | 両方の環境でビルドできる |
| 1. 共通コアの切り出し | Models / Controllers / InkEngine の計算部分から Windows の型をなくし、`Core/` に移す。単体テストを作る | Windows 版が今までどおり動き、テストが通る |
| 2. iPad で最小限の試作 | SwiftUI + CPU 版のにじみ計算で、Apple Pencil で半紙に書ける | 筆圧・傾きで線の太さとカスレが変わる |
| 3. Metal 化 | にじみの計算を Metal に移す | CPU 版と同じ結果で、目標のフレームレートが出る |
| 4. 機能をそろえる | お手本・硯・解析・リプレイ・保存などを移す | 4 章の表がすべて「同等」になる |
| 5. Mac 版 | NSEvent の入力と Mac 向けの画面配置を追加する | 液タブで iPad 版と同じように書ける |
| 6. 配布 | TestFlight、App Store に申請する | 公開 |

---

## 14. 未決事項

- [ ] 対応する iPadOS / macOS の最低バージョン
- [ ] Swift と C++ をつなぐ方法（Objective-C++ のラッパーか、Swift の C++ 相互運用か）
- [ ] ビルドシステム（CMake を入れるか、Visual Studio と Xcode をそれぞれ使うか）
- [ ] 同梱する書体（楷書・教科書体・行書）とそのライセンス
- [ ] iPad で消しゴムと一画戻すをどう操作するか（ジェスチャー、Pencil のダブルタップやスクイーズ）
- [ ] 書き味の遅延とフレームレートの具体的な目標値
- [ ] 半紙の内部解像度と、保存する画像の解像度
- [ ] AppController を共通コアに入れるか、機種ごとに作るか
- [ ] 有料で配布するか、無料で配布するか
