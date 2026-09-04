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

// お手本の候補文字数（入力欄から取り込む上限＝タイルの数）
constexpr int OTEHON_PALETTE_MAX = 8;

// お手本モデル
struct OtehonModel {
    bool isVisible = false;
    int selectedIndex = 0; // palette 内の選択位置
    double opacity = 0.35; // 0.05 ～ 1.0
    bool isDraggingOpacity = false;

    // 書きたい文字の入力（IME 変換確定後の文字を WM_CHAR で受け取る）。
    // 入力があればその文字が候補タイルになり、空なら既定の8字に戻る。
    std::wstring inputText;
    bool isTyping = false;
    std::vector<std::wstring> palette;

    // 手本の書体。楷書・教科書体・行書を練習内容で切り替える。
    OtehonFontStyle fontStyle = OtehonFontStyle::Seikaisho;

    // マスへ置いたお手本文字。空文字は未配置。
    // 候補の番号ではなく文字そのものを持つ。番号だと、入力を変えて候補が
    // 入れ替わったときに、置いてある字まで別の字に化けてしまう。
    std::wstring cellText[MAX_GRID_CELLS];

    OtehonModel() {
        RebuildPalette();
    }

    // 入力文字列から候補を作り直す。サロゲートペアは2要素で1文字として扱う。
    void RebuildPalette() {
        palette.clear();
        for (size_t i = 0; i < inputText.size() && (int)palette.size() < OTEHON_PALETTE_MAX; ) {
            size_t len = 1;
            if (IS_HIGH_SURROGATE(inputText[i]) && i + 1 < inputText.size()
                && IS_LOW_SURROGATE(inputText[i + 1])) {
                len = 2;
            }
            palette.push_back(inputText.substr(i, len));
            i += len;
        }
        if (palette.empty()) {
            static const wchar_t* const kDefault[] = { L"永", L"夢", L"和", L"心", L"道", L"光", L"美", L"桜" };
            for (const wchar_t* d : kDefault) palette.push_back(d);
        }
        if (selectedIndex < 0 || selectedIndex >= (int)palette.size()) selectedIndex = 0;
    }

    const std::wstring& GetCellText(int cellIndex) const {
        static const std::wstring empty;
        return (cellIndex >= 0 && cellIndex < MAX_GRID_CELLS) ? cellText[cellIndex] : empty;
    }

    // マスへお手本を配置する。配置先はミニマップから選ぶ。
    // 既に同じ字が置かれているマスを指した場合は取り消す。
    void PlaceAtCell(int cellIndex, const std::wstring& text) {
        if (cellIndex < 0 || cellIndex >= MAX_GRID_CELLS) return;
        cellText[cellIndex] = (cellText[cellIndex] == text) ? std::wstring() : text;
    }

    void ClearCellChars() {
        for (std::wstring& t : cellText) t.clear();
    }

    bool HasPlacedChar() const {
        for (const std::wstring& t : cellText) if (!t.empty()) return true;
        return false;
    }

    int GetPaletteCount() const { return (int)palette.size(); }

    const std::wstring& GetCharacter(int index) const {
        static const std::wstring empty;
        if (index >= 0 && index < (int)palette.size()) return palette[index];
        return empty;
    }

    const std::wstring& GetCurrentCharacter() const {
        return GetCharacter(selectedIndex);
    }

    // 入力欄への1文字追加。制御文字・空白は候補にならないので受け付けない。
    void AppendInputChar(wchar_t ch) {
        if (ch < 0x20 || ch == 0x7F || ch == L' ' || ch == L'　') return;
        // サロゲートペアを保持できるよう、上限は文字数ではなく要素数で見る
        if (inputText.size() >= OTEHON_PALETTE_MAX * 2) return;
        inputText.push_back(ch);
        RebuildPalette();
    }

    void BackspaceInput() {
        if (inputText.empty()) return;
        size_t n = inputText.size();
        if (n >= 2 && IS_LOW_SURROGATE(inputText[n - 1]) && IS_HIGH_SURROGATE(inputText[n - 2])) {
            inputText.erase(n - 2);
        } else {
            inputText.erase(n - 1);
        }
        RebuildPalette();
    }

    void ClearInput() {
        inputText.clear();
        RebuildPalette();
    }
};

#include "InkModel.h"
#include "TrajectoryModel.h"
#include "CalibrationModel.h"
#include "UndoHistory.h"

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
    RECT rOtehonInputBox{};                  // 書きたい文字の入力欄
    RECT rOtehonCellMapBox{};                // 配置先を選ぶ升目ミニマップの配置枠
    RECT rOtehonFontBtn[OTEHON_FONT_COUNT]{}; // 書体の切り替え（楷書 / 教科書体 / 行書）
    RECT rOtehonCellBtn[MAX_GRID_CELLS]{};   // ミニマップ上の各マス（rGridCell と同じ並び）
    RECT rSaveBtnPng{}, rSaveBtnClip{}, rSaveBtnJson{}, rSaveBtnCsv{};
    RECT rAnalysisCompassBox{}, rAnalysisGraphBox{}, rAnalysisMetricsBox{};
    RECT rInkStoneLarge{}, rInkRefillBtn{}, rUndoBtn{}, rRedoBtn{}, rClearAllBtn{};
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
    UndoHistory undo;   // 「一画戻す」用に、画を書き始める直前の状態を控える
    UIState ui;

    // ウィンドウサイズに応じた全UI要素のレイアウト計算
    void Layout(int clientWidth, int clientHeight);

    void SetSaveFeedback(const std::wstring& message) {
        ui.saveFeedback = message;
        ui.saveFeedbackTime = GetTickCount();
    }
};

