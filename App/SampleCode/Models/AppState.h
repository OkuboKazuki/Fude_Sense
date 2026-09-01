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

// UI・レイアウト状態
struct UIState {
    LeftTab leftTab = LeftTab::Brush;
    bool isSubPanelOpen = true;
    bool showClearConfirm = false;
    int hoverClearModal = 0; // 1: すべて消す, 2: キャンセル

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
};

// アプリケーション全体の状態を統合する Model クラス
class AppState {
public:
    BrushModel brush;
    PaperModel paper;
    OtehonModel otehon;
    InkModel ink;
    TrajectorySession trajectory;
    UIState ui;

    // ウィンドウサイズに応じた全UI要素のレイアウト計算
    void Layout(int clientWidth, int clientHeight);

    void SetSaveFeedback(const std::wstring& message) {
        ui.saveFeedback = message;
        ui.saveFeedbackTime = GetTickCount();
    }
};

