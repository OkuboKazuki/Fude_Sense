#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include "AppEnums.h"
#include "UIComponents.h"

// 画面状態（タイトル画面 / スタジオ画面）
enum class AppScreen {
    Title,      // タイトル・モード選択画面
    Studio      // 習字制作ワークスペース
};

// ツールバー・ナビゲーションボタン
enum class TbButton {
    None,
    Home,           // タイトル画面へ戻る
    NavToggle,
    Brush,
    Paper,
    Analysis,
    Save,
    Otehon,
    InkRefill,
    ClearAll
};

// 左側フローティングパネルのタブ
enum class LeftTab {
    Brush,
    Paper,
    Analysis,
    Save,
    Otehon
};

// 操作の状態。押してから離すまでのドラッグや入力中など、今進行している操作を表す。
struct InteractionState {
    // UI操作直後にペンが接地したままでも運筆を開始させないためのロック。
    // 全消しモーダル等のボタンは半紙の上に重なるため、押した直後にペンが
    // 紙へ触れたままだと、そのまま墨が落ちてしまう。ペンが紙から離れる
    // （筆圧0 / 圏外）まで運筆入力を無視する。
    bool suppressPenUntilLift = false;

    // ペンで硯・「墨を補充」を押している最中。離すまでの筆圧に応じて墨を含ませる。
    bool isPenRefilling = false;

    // 硯の上の墨残量ゲージをつかんで、残量を直接動かしている最中
    bool isDraggingInkAmount = false;

    // 硯の下の部分（磨り面）を押してから離すまでの間。そこで動かした長さと筆圧に応じて墨を落とす。
    // マウスは筆圧が無く、動きも WM_MOUSEMOVE から拾うので区別しておく。
    bool isWiping = false;
    bool wipeByMouse = false;
    POINT wipeLastPt{};

    // スライダーやシークバー、波形をつかんでいる最中
    bool isDraggingHardness = false;
    bool isDraggingOpacity = false;
    bool isDraggingSeekBar = false;
    bool isDraggingWaveform = false;

    // お手本の入力欄で文字を入力している最中
    bool isTyping = false;
};

// UI・レイアウト状態
struct UIState {
    AppScreen currentScreen = AppScreen::Title;  // 起動時はタイトル画面
    bool isTransitioning = false;                // 遷移アニメーション中
    DWORD transitionStartTime = 0;
    DWORD transitionDurationMs = 400;            // 400ms の滑らかな遷移
    float transitionProgress = 0.0f;             // 0.0 (Title) -> 1.0 (Studio)

    LeftTab leftTab = LeftTab::Brush;
    bool isSubPanelOpen = true;
    bool showClearConfirm = false;
    int hoverClearModal = 0; // 1: すべて消す, 2: キャンセル

    InteractionState interaction;

    // 紙だけ表示。左メニューと硯パネルを畳み、横向きの半紙を画面いっぱいに出す。
    // 硯が無いので、墨の補充は半紙の右のボタンから行う。
    bool paperOnly = false;
    int hoverPaperOnly = 0;  // 1: 通常表示に戻る, 2: 墨を補充, 3: 筆跡を消す
    TbButton hoverTb = TbButton::None;
    int hoverSub = 0;
    int hoverInkStone = 0;
    // 右の硯パネル（硯・ボタン・文字）の拡大率。Layout で空きに収まる範囲で決める。
    double inkStoneScale = 1.0;
    int hoverReplayBtn = 0; // 1: Reset, 2: Prev, 3: Play/Pause, 4: Next, 5: Speed0.5, 6: Speed1.0, 7: Speed2.0, 8: SeekTrack, 9: Import, 10: Back

    std::wstring saveFeedback = L"";
    DWORD saveFeedbackTime = 0;
    bool saveFeedbackIsError = false; // 保存に失敗したときは赤で出す

    void SetSaveFeedback(const std::wstring& message, bool isError = false) {
        saveFeedback = message;
        saveFeedbackTime = GetTickCount();
        saveFeedbackIsError = isError;
    }

    // 各種UIウィジェット（コンポーネント指向・RECT互換）
    UIWidget rSub{};
    UIWidget rCanvasArea{};
    UIWidget rRight{};
    UIWidget rTbHomeBtn{}; // タイトル画面へ戻るホームボタン
    UIWidget rTbNavToggle{}, rTbBrush{}, rTbPaper{}, rTbAnalysis{}, rTbSave{}, rTbOtehon{};
    UIWidget rPaper{};
    UIWidget rSubSmall{}, rSubMedium{}, rSubLarge{};
    UIWidget rGridTile[GRID_PATTERN_COUNT]{};
    UIWidget rColorBtn[3]{};
    UIWidget rOtehonTile[8]{};
    UIWidget rOtehonToggleBtn{}, rOtehonOpacityTrack{};
    UIWidget rOtehonInputBox{};                  // 書きたい文字の入力欄
    UIWidget rOtehonCellMapBox{};                // 配置先を選ぶ升目ミニマップの配置枠
    UIWidget rOtehonFontBtn[OTEHON_FONT_COUNT]{}; // 書体の切り替え（楷書 / 教科書体 / 行書）
    UIWidget rOtehonCellBtn[MAX_GRID_CELLS]{};   // ミニマップ上の各マス（rGridCell と同じ並び）
    UIWidget rSaveBtnPng{}, rSaveBtnClip{}, rSaveBtnJson{}, rSaveBtnCsv{};
    UIWidget rAnalysisCompassBox{}, rAnalysisGraphBox{}, rAnalysisMetricsBox{}, rAnalysisReplayBox{};
    UIWidget rAnalysisGraphPlot{};   // 波形の描画領域（描画とシークの判定で共通）
    UIWidget rReplayPlayBtn{}, rReplayPrevBtn{}, rReplayNextBtn{}, rReplayResetBtn{};
    UIWidget rReplaySeekTrack{}, rReplaySeekThumb{};
    UIWidget rReplaySpeedBtn[3]{};
    UIWidget rAnalysisImportBtn{};   // 運筆アーカイブを読み込む
    UIWidget rAnalysisBackBtn{};     // 読み込んだ記録を閉じて自分の記録に戻る（表示中だけ）
    UIWidget rInkStoneLarge{}, rInkRefillBtn{}, rUndoBtn{}, rRedoBtn{}, rClearAllBtn{};
    UIWidget rInkPool{}, rInkLand{}; // 硯の内側: 墨溜まり（上、墨を含ませる）と磨り面（下、墨を整える）
    UIWidget rInkBadge{};         // 右パネル: 硯の上の墨残量ゲージ（左右に動かして残量を決める）
    UIWidget rPaperOnlyBtn{};      // 右パネル: 紙だけ表示に入る
    UIWidget rPaperOnlyExitBtn{};  // 紙だけ表示中: 通常表示に戻る（墨の補充は rInkRefillBtn を半紙の右へ置き直す）
    UIWidget rPaperOnlyClearBtn{}; // 紙だけ表示中: 筆跡を消す（確認なしで消去）
    UIWidget rPaperOnlyBar{};      // 紙だけ表示中: 半紙の右のボタンの帯
    UIWidget rHardnessTrack{};
    UIWidget rClearModalBox{}, rModalClearBtn{}, rModalCancelBtn{};

    // 下敷き升目のジオメトリ。罫線描画（CanvasView::DrawGrid）とお手本の配置が
    // 同じセルを参照できるよう、ScreenLayout::Layout で一元的に算出する。
    // ここを分けて計算すると、お手本がマスからずれる。
    RECT rGridBorder{};                 // 升目の外枠（半紙の内側マージン適用後）
    RECT rGridCell[MAX_GRID_CELLS]{};   // 升目セル。並びは縦書きの書字順（右列を上から下、次に左列）
    int gridCellCount = 0;              // 有効なセル数
    int gridCols = 1;                   // 列数（縦書きの「行（ぎょう）」に相当）
    int gridRows = 1;                   // 1列あたりの文字数
};
