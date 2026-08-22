#include "stdafx.h"
#include "InkStoneView.h"
#include "RenderUtils.h"

void InkStoneView::Draw(HDC dc, const AppState& state) {
    using namespace RenderUtils;
    const UIState& ui = state.ui;

    Fill(dc, ui.rRight, RGB(22, 24, 28));
    DrawPanelTitle(dc, ui.rRight, L"硯 (すずり)");

    // 1. 墨量表示ヘッダー
    HFONT fMeta = CreateCustomFont(13, FW_NORMAL);
    RECT rMeta1 = { ui.rRight.left + 16, ui.rInkStoneLarge.top - 24, ui.rRight.right - 16, ui.rInkStoneLarge.top - 4 };
    wchar_t buf[64];
    wsprintfW(buf, L"墨残量: %d%%", (int)(state.ink.stoneAmount * 100.0));
    DrawTextCustom(dc, rMeta1, buf, fMeta, RGB(190, 195, 205));
    DeleteObject(fMeta);

    // 2. 硯の外枠
    bool hoverStone = (ui.hoverInkStone == 1);
    COLORREF stoneBorder = hoverStone ? RGB(70, 120, 190) : RGB(46, 50, 60);
    Box(dc, ui.rInkStoneLarge, RGB(25, 27, 33), stoneBorder, 2, 10);

    RECT innerRim = { ui.rInkStoneLarge.left + 8, ui.rInkStoneLarge.top + 8, ui.rInkStoneLarge.right - 8, ui.rInkStoneLarge.bottom - 8 };
    Box(dc, innerRim, RGB(16, 17, 21), RGB(36, 39, 48), 1, 8);

    // 3. 墨池（海）
    RECT rPool = { innerRim.left + 10, innerRim.top + 10, innerRim.right - 10, innerRim.top + (int)(RH(innerRim) * 0.35) };
    Box(dc, rPool, RGB(10, 11, 14), RGB(30, 33, 40), 1, 6);

    double inkFrac = Clamp(state.ink.stoneAmount / INK_MAX_VALUE, 0.0, 1.0);
    if (inkFrac > 0.01) {
        int poolH = RH(rPool) - 4;
        int fillH = (int)(poolH * inkFrac);
        RECT rLiquid = { rPool.left + 3, rPool.bottom - 3 - fillH, rPool.right - 3, rPool.bottom - 3 };
        Fill(dc, rLiquid, RGB(5, 6, 8));

        HPEN hp = CreatePen(PS_SOLID, 2, RGB(75, 95, 125));
        HPEN ohp = (HPEN)SelectObject(dc, hp);
        MoveToEx(dc, rLiquid.left + 8, rLiquid.top + 1, nullptr);
        LineTo(dc, rLiquid.right - 8, rLiquid.top + 1);
        SelectObject(dc, ohp);
        DeleteObject(hp);
    }

    HFONT fPool = CreateCustomFont(14, FW_BOLD);
    RECT rPoolText = { rPool.left, rPool.top + 6, rPool.right, rPool.top + 26 };
    Center(dc, rPoolText, L"墨 池 (海)", fPool, inkFrac > 0.4 ? RGB(80, 95, 120) : RGB(130, 135, 145));
    DeleteObject(fPool);

    // 4. 墨堂（陸）
    RECT rLand = { innerRim.left + 10, innerRim.top + (int)(RH(innerRim) * 0.37), innerRim.right - 10, innerRim.bottom - 10 };
    Box(dc, rLand, RGB(22, 24, 30), RGB(32, 35, 44), 1, 6);

    HPEN tp = CreatePen(PS_SOLID, 1, RGB(28, 30, 38));
    HPEN otp = (HPEN)SelectObject(dc, tp);
    for (int y = rLand.top + 10; y < rLand.bottom - 10; y += 14) {
        MoveToEx(dc, rLand.left + 12, y, nullptr);
        LineTo(dc, rLand.right - 12, y);
    }
    SelectObject(dc, otp);
    DeleteObject(tp);

    HFONT fKanji = CreateCustomFont(16, FW_BOLD);
    RECT rLandText = { rLand.left, rLand.top + 10, rLand.right, rLand.top + 30 };
    Center(dc, rLandText, L"墨 堂 (陸)", fKanji, RGB(65, 70, 82));
    DeleteObject(fKanji);

    // 5. 墨を補充ボタン
    bool hovRefill = (ui.hoverInkStone == 2);
    Box(dc, ui.rInkRefillBtn, hovRefill ? RGB(40, 65, 96) : RGB(30, 34, 42), hovRefill ? RGB(70, 120, 190) : RGB(48, 54, 66), 1, 6);
    HFONT fBtn = CreateCustomFont(14, FW_BOLD);
    Center(dc, ui.rInkRefillBtn, L"💧 墨を補充", fBtn, hovRefill ? RGB(255, 255, 255) : RGB(210, 215, 225));
    DeleteObject(fBtn);

    // 6. 全消しボタン
    bool hovClear = (ui.hoverInkStone == 3);
    Box(dc, ui.rClearAllBtn, hovClear ? RGB(75, 36, 36) : RGB(38, 28, 30), hovClear ? RGB(160, 60, 60) : RGB(68, 44, 48), 1, 6);
    HFONT fClear = CreateCustomFont(14, FW_BOLD);
    Center(dc, ui.rClearAllBtn, L"🗑️ すべての筆跡を消す", fClear, hovClear ? RGB(255, 220, 220) : RGB(220, 175, 175));
    DeleteObject(fClear);
}
