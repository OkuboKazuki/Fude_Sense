#include "stdafx.h"
#include "StatusBarView.h"
#include "RenderUtils.h"

const wchar_t* StatusBarView::GetGridPatternName(GridPattern pattern) {
    switch (pattern) {
    case GridPattern::None:     return L"なし";
    case GridPattern::Cross1:   return L"1字(十字)";
    case GridPattern::Div2:     return L"2文字(2段)";
    case GridPattern::Grid4:    return L"4文字(田)";
    case GridPattern::Grid6:    return L"6文字";
    case GridPattern::Grid8:    return L"8文字";
    case GridPattern::Lines3:   return L"3行罫線";
    case GridPattern::Lines4:   return L"4行罫線";
    case GridPattern::StarGrid: return L"米字格";
    }
    return L"なし";
}

void StatusBarView::Draw(HDC dc, int width, int height, const AppState& state) {
    using namespace RenderUtils;
    const UIState& ui = state.ui;

    Fill(dc, ui.rStatus, RGB(18, 19, 23)); 
    HFONT f = CreateCustomFont(12);
    RECT t = { 12, height - 22, width - 12, height - 4 };
    wchar_t buf[192]; 
    wsprintfW(buf, L"状態: 準備完了 | 筆: %s | 下敷き: %s | お手本: %s | 墨量: %d%%", 
        BrushName(state.brush.type), 
        GetGridPatternName(state.paper.gridPattern),
        state.otehon.isVisible ? state.otehon.GetCurrentCharacter().c_str() : L"なし",
        (int)(state.ink.stoneAmount * 100.0));
    DrawTextCustom(dc, t, buf, f, RGB(150, 155, 165));
    DeleteObject(f);
}
