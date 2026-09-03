#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include "AppEnums.h"
#include "RenderUtils.h"

// 筆設定モデル
struct BrushModel {
    Brush type = Brush::Medium;
    double hardness = 0.5; // 0.1: 超極軟 ~ 2.0: 非常に硬い
    bool isDraggingHardness = false;

    double GetBaseMaxWidth() const {
        switch (type) {
        case Brush::Small:  return 16.0;
        case Brush::Medium: return 36.0;
        case Brush::Large:  return 56.0;
        }
        return 36.0;
    }
};

// 用紙・下敷きモデル
struct PaperModel {
    PaperType type = PaperType::Hanshi;
    GridPattern gridPattern = GridPattern::Grid4;
    GridColorTheme gridColor = GridColorTheme::RedLine;

    void GetAspectRatio(double& outW, double& outH) const {
        switch (type) {
        case PaperType::Hanshi:   outW = 242.0; outH = 333.0; break;
        case PaperType::Jofuku:   outW = 350.0; outH = 680.0; break;
        case PaperType::Shikishi: outW = 242.0; outH = 272.0; break;
        case PaperType::Tanzaku:  outW = 60.0;  outH = 180.0; break;
        }
    }
};

// お手本モデル
struct OtehonModel {
    bool isVisible = false;
    int selectedIndex = 0; // 0: 永, 1: 夢, 2: 和, 3: 心, 4: 道, 5: 光, 6: 美, 7: 桜
    double opacity = 0.35; // 0.05 ～ 1.0
    bool isDraggingOpacity = false;

    // 「書いている升目」への追従状態
    int activeCell = 0;       // UIState::rGridCell のインデックス（書字順なので初期値0は右上のマス）
    bool cellLatched = false; // 運筆中は追従を止める。ペンが紙から離れると解除される

    // 書字順の先頭マス（右上）へ戻す。表示を始めるときや、全消し・升目変更で
    // 書き始めの位置が変わったときに使う。
    void ResetActiveCell() {
        activeCell = 0;
        cellLatched = false;
    }

    static const wchar_t* GetCharacter(int index) {
        static const wchar_t* chars[] = { L"永", L"夢", L"和", L"心", L"道", L"光", L"美", L"桜" };
        if (index >= 0 && index < 8) return chars[index];
        return L"永";
    }

    const wchar_t* GetCurrentCharacter() const {
        return GetCharacter(selectedIndex);
    }
};

#include "InkModel.h"
#include "TrajectoryModel.h"
#include "CalibrationModel.h"

// UI・レイアウト状態
struct UIState {
    LeftTab leftTab = LeftTab::Brush;
    bool isSubPanelOpen = true;
    bool showClearConfirm = false;
    int hoverClearModal = 0; // 1: すべて消す, 2: キャンセル
    int hoverCalib = 0;      // 1: 適用, 2: 再計測, 3: キャンセル/閉じる

    // UI操作直後にペンが接地したままでも運筆を開始させないためのロック。
    // 全消しモーダル等のボタンは半紙の上に重なるため、押した直後にペンが
    // 紙へ触れたままだと、そのまま墨が落ちてしまう。ペンが紙から離れる
    // （筆圧0 / 圏外）まで運筆入力を無視する。
    bool suppressPenUntilLift = false;

    TbButton hoverTb = TbButton::None;
    int hoverSub = 0;
    int hoverInkStone = 0;

    std::wstring saveFeedback = L"";
    DWORD saveFeedbackTime = 0;

    // 各種UI矩形
    RECT rSub{};
    RECT rCanvasArea{};
    RECT rRight{};
    RECT rStatus{};
    RECT rTbNavToggle{}, rTbBrush{}, rTbPaper{}, rTbAnalysis{}, rTbSave{}, rTbOtehon{};
    RECT rPaper{};
    RECT rSubSmall{}, rSubMedium{}, rSubLarge{};
    RECT rSubCalibBtn{}; // 筆圧キャリブレーション起動ボタン
    RECT rGridTile[9]{};
    RECT rColorBtn[3]{};
    RECT rPaperTile[4]{};
    RECT rOtehonTile[8]{};
    RECT rOtehonToggleBtn{}, rOtehonOpacityTrack{};
    RECT rSaveBtnPng{}, rSaveBtnClip{}, rSaveBtnJson{}, rSaveBtnCsv{};
    RECT rAnalysisCompassBox{}, rAnalysisGraphBox{}, rAnalysisMetricsBox{};
    RECT rInkStoneLarge{}, rInkRefillBtn{}, rClearAllBtn{};
    RECT rHardnessTrack{};
    RECT rClearModalBox{}, rModalClearBtn{}, rModalCancelBtn{};
    RECT rCalibModalBox{}, rCalibApplyBtn{}, rCalibRetryBtn{}, rCalibCloseBtn{};

    // 下敷き升目のジオメトリ。罫線描画（CanvasView::DrawGrid）とお手本の配置が
    // 同じセルを参照できるよう、AppState::Layout で一元的に算出する。
    // ここを分けて計算すると、お手本がマスからずれる。
    RECT rGridBorder{};                 // 升目の外枠（半紙の内側マージン適用後）
    RECT rGridCell[MAX_GRID_CELLS]{};   // 升目セル。並びは縦書きの書字順（右列を上から下、次に左列）
    int gridCellCount = 0;              // 有効なセル数
    int gridCols = 1;                   // 列数（縦書きの「行（ぎょう）」に相当）
    int gridRows = 1;                   // 1列あたりの文字数

    // 指定座標を含む升目セルの番号を返す。どのセルにも含まれなければ -1
    int FindGridCell(POINT pt) const {
        for (int i = 0; i < gridCellCount; ++i) {
            if (RenderUtils::PtIn(rGridCell[i], pt)) return i;
        }
        return -1;
    }
};

// アプリケーション全体の状態を統合する Model クラス
class AppState {
public:
    BrushModel brush;
    PaperModel paper;
    OtehonModel otehon;
    InkModel ink;
    TrajectorySession trajectory;
    CalibrationModel calibration;
    UIState ui;

    // ウィンドウサイズに応じた全UI要素のレイアウト計算
    void Layout(int clientWidth, int clientHeight);

    void SetSaveFeedback(const std::wstring& message) {
        ui.saveFeedback = message;
        ui.saveFeedbackTime = GetTickCount();
    }
};

