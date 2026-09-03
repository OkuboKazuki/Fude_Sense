#include "stdafx.h"
#include "CanvasView.h"
#include "RenderUtils.h"
#include <algorithm>

void CanvasView::DrawBackground(HDC dc, const AppState& state) {
    using namespace RenderUtils;
    const RECT& rPaper = state.ui.rPaper;
    if (RW(rPaper) <= 0 || RH(rPaper) <= 0) return;

    // 1. 本物の書道用下敷き（毛氈：縦長フェルト布マット）
    int marginX = (std::max)(22, RW(rPaper) / 18);
    int marginTop = (std::max)(26, RH(rPaper) / 16);
    int marginBottom = (std::max)(32, RH(rPaper) / 13);

    RECT rMat = {
        rPaper.left - marginX,
        rPaper.top - marginTop,
        rPaper.right + marginX,
        rPaper.bottom + marginBottom
    };

    // 毛氈本体（品格のある濃紺インディゴフェルト布地・影なしでスッキリ描画）
    Box(dc, rMat, RGB(20, 26, 40), RGB(46, 54, 76), 1, 6);

    // 2. 和紙（半紙）本体
    HBRUSH pb = CreateSolidBrush(RGB(252, 251, 248));
    HPEN pp = CreatePen(PS_SOLID, 1, RGB(220, 216, 206));
    HBRUSH ob = (HBRUSH)SelectObject(dc, pb);
    HPEN op = (HPEN)SelectObject(dc, pp);
    Rectangle(dc, rPaper.left, rPaper.top, rPaper.right, rPaper.bottom);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pp);
    DeleteObject(pb);
}

void CanvasView::DrawOtehon(HDC dc, const AppState& state) {
    if (!state.otehon.isVisible) return;
    if (state.otehon.selectedIndex < 0 || state.otehon.selectedIndex >= 8) return;

    using namespace RenderUtils;
    const RECT& rPaper = state.ui.rPaper;
    const wchar_t* ch = state.otehon.GetCurrentCharacter();
    int size = (int)((std::min)(RW(rPaper), RH(rPaper)) * 0.72);
    if (size <= 0) return;

    HFONT fOtehon = CreateFontW(size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Yu Mincho");

    int grayVal = (int)(248 - state.otehon.opacity * 105.0);
    grayVal = Clamp(grayVal, 80, 245);
    COLORREF oColor = RGB(grayVal, (int)(grayVal * 0.98), (int)(grayVal * 0.95));

    HFONT old = (HFONT)SelectObject(dc, fOtehon);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, oColor);
    RECT drawRect = rPaper;
    DrawTextW(dc, ch, 1, &drawRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
    DeleteObject(fOtehon);
}

void CanvasView::DrawCross(HDC dc, int x, int y, int s) {
    MoveToEx(dc, x - s, y, nullptr); LineTo(dc, x + s, y);
    MoveToEx(dc, x, y - s, nullptr); LineTo(dc, x, y + s);
}

void CanvasView::DrawGrid(HDC dc, const AppState& state) {
    if (state.paper.gridPattern == GridPattern::None) return;

    using namespace RenderUtils;
    const RECT& rPaper = state.ui.rPaper;

    COLORREF lineColor = RGB(228, 90, 90);
    if (state.paper.gridColor == GridColorTheme::WhiteLine) lineColor = RGB(220, 222, 230);
    else if (state.paper.gridColor == GridColorTheme::InkGray) lineColor = RGB(160, 165, 175);

    HPEN gp = CreatePen(PS_SOLID, 1, lineColor);
    HPEN gpDash = CreatePen(PS_DOT, 1, lineColor);
    HPEN op = (HPEN)SelectObject(dc, gp);

    int left = rPaper.left;
    int top = rPaper.top;
    int right = rPaper.right;
    int bottom = rPaper.bottom;
    int w = RW(rPaper);
    int h = RH(rPaper);

    int m = (std::max)(6, w / 48);
    RECT rBorder = { left + m, top + m, right - m, bottom - m };
    MoveToEx(dc, rBorder.left, rBorder.top, nullptr);
    LineTo(dc, rBorder.right, rBorder.top);
    LineTo(dc, rBorder.right, rBorder.bottom);
    LineTo(dc, rBorder.left, rBorder.bottom);
    LineTo(dc, rBorder.left, rBorder.top);

    int bw = RW(rBorder);
    int bh = RH(rBorder);

    switch (state.paper.gridPattern) {
    case GridPattern::Cross1:
    {
        int mx = rBorder.left + bw / 2;
        int my = rBorder.top + bh / 2;
        MoveToEx(dc, mx, rBorder.top, nullptr); LineTo(dc, mx, rBorder.bottom);
        MoveToEx(dc, rBorder.left, my, nullptr); LineTo(dc, rBorder.right, my);
        DrawCross(dc, rBorder.left + bw / 4, rBorder.top + bh / 4, 10);
        DrawCross(dc, rBorder.left + 3 * bw / 4, rBorder.top + bh / 4, 10);
        DrawCross(dc, rBorder.left + 3 * bw / 4, rBorder.top + 3 * bh / 4, 10);
        DrawCross(dc, rBorder.left + bw / 4, rBorder.top + 3 * bh / 4, 10);
        break;
    }
    case GridPattern::Div2:
    {
        int my = rBorder.top + bh / 2;
        MoveToEx(dc, rBorder.left, my, nullptr); LineTo(dc, rBorder.right, my);
        int mx = rBorder.left + bw / 2;
        SelectObject(dc, gpDash);
        MoveToEx(dc, mx, rBorder.top, nullptr); LineTo(dc, mx, rBorder.bottom);
        SelectObject(dc, gp);
        break;
    }
    case GridPattern::Grid4:
    {
        int mx = rBorder.left + bw / 2;
        int my = rBorder.top + bh / 2;
        MoveToEx(dc, mx, rBorder.top, nullptr); LineTo(dc, mx, rBorder.bottom);
        MoveToEx(dc, rBorder.left, my, nullptr); LineTo(dc, rBorder.right, my);

        DrawCross(dc, rBorder.left + bw / 4, rBorder.top + bh / 4, 12);
        DrawCross(dc, rBorder.left + 3 * bw / 4, rBorder.top + bh / 4, 12);
        DrawCross(dc, rBorder.left + 3 * bw / 4, rBorder.top + 3 * bh / 4, 12);
        DrawCross(dc, rBorder.left + bw / 4, rBorder.top + 3 * bh / 4, 12);
        break;
    }
    case GridPattern::Grid6:
    {
        int mx = rBorder.left + bw / 2;
        MoveToEx(dc, mx, rBorder.top, nullptr); LineTo(dc, mx, rBorder.bottom);
        for (int r = 1; r < 3; ++r) {
            int y = rBorder.top + (bh * r) / 3;
            MoveToEx(dc, rBorder.left, y, nullptr); LineTo(dc, rBorder.right, y);
        }
        break;
    }
    case GridPattern::Grid8:
    {
        int mx = rBorder.left + bw / 2;
        MoveToEx(dc, mx, rBorder.top, nullptr); LineTo(dc, mx, rBorder.bottom);
        for (int r = 1; r < 4; ++r) {
            int y = rBorder.top + (bh * r) / 4;
            MoveToEx(dc, rBorder.left, y, nullptr); LineTo(dc, rBorder.right, y);
        }
        break;
    }
    case GridPattern::Lines3:
    {
        for (int c = 1; c < 3; ++c) {
            int x = rBorder.left + (bw * c) / 3;
            MoveToEx(dc, x, rBorder.top, nullptr); LineTo(dc, x, rBorder.bottom);
        }
        break;
    }
    case GridPattern::Lines4:
    {
        for (int c = 1; c < 4; ++c) {
            int x = rBorder.left + (bw * c) / 4;
            MoveToEx(dc, x, rBorder.top, nullptr); LineTo(dc, x, rBorder.bottom);
        }
        break;
    }
    case GridPattern::StarGrid:
    {
        int mx = rBorder.left + bw / 2;
        int my = rBorder.top + bh / 2;
        MoveToEx(dc, mx, rBorder.top, nullptr); LineTo(dc, mx, rBorder.bottom);
        MoveToEx(dc, rBorder.left, my, nullptr); LineTo(dc, rBorder.right, my);

        SelectObject(dc, gpDash);
        MoveToEx(dc, rBorder.left, rBorder.top, nullptr); LineTo(dc, rBorder.right, rBorder.bottom);
        MoveToEx(dc, rBorder.right, rBorder.top, nullptr); LineTo(dc, rBorder.left, rBorder.bottom);
        SelectObject(dc, gp);
        break;
    }
    default:
        break;
    }

    SelectObject(dc, op);
    DeleteObject(gpDash);
    DeleteObject(gp);
}

void CanvasView::RenderInk(HDC dc, GpuInk& gpuInk, const AppState& state) {
    gpuInk.Render(dc, state.ui.rPaper.left, state.ui.rPaper.top);
}
