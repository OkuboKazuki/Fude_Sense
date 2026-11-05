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

// 筆設定モデル
struct BrushModel {
    Brush type = Brush::Medium;
    double hardness = 0.5; // 0.1: 超極軟 ~ 2.0: 非常に硬い

    // 傾き非対応タブレットからの移行にともなう線幅の補正。
    // 旧環境（Wacom One Creative Pen Display）は傾きを返さないので pkOrientation が
    // 常に 0 になり、StrokeController の tiltFactor が 1.0 へ張り付いていた。
    // その結果、線幅には常に最大の 1.52 倍が乗った状態で下の基準幅が追い込まれている。
    // Cintiq 16 は傾きを返すため、実測したペン角（仰角の中央値 41 度 =
    // tiltFactor 0.54）では同じ係数が 1.27 まで落ち、線が 16% 細くなる。
    // 1.52 / 1.27 ぶんを基準幅側で打ち消し、旧環境の手応えへ揃える。
    // 係数 0.6 側ではなく基準幅で補正するのは、係数を上げると傾きによる
    // 線幅の振れ幅まで一緒に広がってしまうため。
    static constexpr double TILT_WIDTH_COMPENSATION = 1.20;

    double GetBaseMaxWidth() const {
        double base = 36.0;
        switch (type) {
        case Brush::Small:  base = 16.0; break;
        case Brush::Medium: base = 36.0; break;
        case Brush::Large:  base = 56.0; break;
        }
        return base * TILT_WIDTH_COMPENSATION;
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
        }
    }

    // キャンバスの内部固定論理解像度（長辺 1200px 基準）
    // ウィンドウサイズに関わらず固定サイズで保持することで、リサイズ時の劣化を防ぐ
    void GetCanvasSize(int& outW, int& outH) const {
        double ratioW = 242.0, ratioH = 333.0;
        GetAspectRatio(ratioW, ratioH);
        constexpr int BASE_LONG_EDGE = 1200;
        if (ratioH >= ratioW) {
            outH = BASE_LONG_EDGE;
            outW = static_cast<int>(std::round(BASE_LONG_EDGE * (ratioW / ratioH)));
        } else {
            outW = BASE_LONG_EDGE;
            outH = static_cast<int>(std::round(BASE_LONG_EDGE * (ratioH / ratioW)));
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

    // 書きたい文字の入力（IME 変換確定後の文字を WM_CHAR で受け取る）。
    // 入力があればその文字が候補タイルになり、空なら既定の8字に戻る。
    std::wstring inputText;
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
#include "UndoHistory.h"

// アプリケーション全体の状態を統合する Model クラス
class AppState {
public:
    AppScreen currentScreen = AppScreen::Title;  // 起動時はタイトル画面
    bool isTransitioning = false;                // 遷移アニメーション中
    DWORD transitionStartTime = 0;
    DWORD transitionDurationMs = 400;            // 400ms の滑らかな遷移
    float transitionProgress = 0.0f;             // 0.0 (Title) -> 1.0 (Studio)

    BrushModel brush;
    PaperModel paper;
    OtehonModel otehon;
    InkModel ink;
    TrajectorySession trajectory;
    // 解析タブで読み込んだ他人の運筆アーカイブ。見るだけで、書き足しや「一画戻す」の
    // 対象にはならない。自分の記録（trajectory）と半紙の墨には手を付けない。
    TrajectorySession importedTrajectory;
    bool viewingImport = false;
    std::wstring importedName;   // 読み込んだファイル名（解析タブに出す）
    ReplayModel replay;
    UndoHistory undo;   // 「一画戻す」用に、画を書き始める直前の状態を控える


    // 解析タブのリプレイ・グラフが見ている記録。読み込んだ記録を表示中ならそちら
    TrajectorySession& AnalysisSession() { return viewingImport ? importedTrajectory : trajectory; }
    const TrajectorySession& AnalysisSession() const { return viewingImport ? importedTrajectory : trajectory; }
};

