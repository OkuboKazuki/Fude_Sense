#include "stdafx.h"
#include "TitleView.h"
#include "RenderUtils.h"
#include <cmath>
#include <algorithm>

#pragma comment(lib, "msimg32.lib")

void TitleView::Draw(HDC dc, int width, int height, const AppState& state) {
    DrawBackground(dc, width, height);
    DrawCenterContent(dc, width, height, state);
}

void TitleView::DrawBackground(HDC dc, int width, int height) {
    using namespace RenderUtils;

    // 深みのある濃紺・墨染めの縦グラデーション
    TRIVERTEX v[2];
    v[0].x = 0;
    v[0].y = 0;
    v[0].Red   = 0x0E00; // RGB(14, 17, 24)
    v[0].Green = 0x1100;
    v[0].Blue  = 0x1800;
    v[0].Alpha = 0x0000;

    v[1].x = width;
    v[1].y = height;
    v[1].Red   = 0x1A00; // RGB(26, 32, 44)
    v[1].Green = 0x2000;
    v[1].Blue  = 0x2C00;
    v[1].Alpha = 0x0000;

    GRADIENT_RECT gr = { 0, 1 };
    GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);

    // 和風の繊細な背景装飾（中央の薄い円弧と格子ライン）
    HPEN pLine = CreatePen(PS_SOLID, 1, RGB(32, 40, 56));
    HPEN oldP = (HPEN)SelectObject(dc, pLine);

    int cx = width / 2;
    int cy = height / 2 - 20;

    // 薄い円環（禅円・円相の幾何学モチーフ）
    HBRUSH oldB = (HBRUSH)SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Ellipse(dc, cx - 340, cy - 340, cx + 340, cy + 340);
    Ellipse(dc, cx - 240, cy - 240, cx + 240, cy + 240);

    // 水平・垂直の繊細なガイドライン
    MoveToEx(dc, cx - 420, cy, nullptr); LineTo(dc, cx + 420, cy);
    MoveToEx(dc, cx, cy - 420, nullptr); LineTo(dc, cx, cy + 420);

    SelectObject(dc, oldB);
    SelectObject(dc, oldP);
    DeleteObject(pLine);
}

void TitleView::DrawCenterContent(HDC dc, int width, int height, const AppState& state) {
    using namespace RenderUtils;

    int cx = width / 2;
    int cy = height / 2 - 10;

    // 1. メインタイトルロゴ「Fude Sense」
    RECT rMainTitle = { cx - 400, cy - 60, cx + 400, cy + 10 };
    HFONT fMainTitle = CreateCustomFont(54, FW_BOLD);
    DrawTextCustom(dc, rMainTitle, L"Fude Sense", fMainTitle, RGB(245, 248, 255), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    DeleteObject(fMainTitle);

    // 2. 「タップして硯に向かう」ブレスアニメーション
    int promptY = cy + 45;
    RECT rPrompt = { cx - 220, promptY, cx + 220, promptY + 50 };

    // 呼吸するようにゆったり明滅するブレスアニメーション (sin波)
    DWORD tick = GetTickCount();
    double pulse = (std::sin(static_cast<double>(tick) * 0.0035) + 1.0) * 0.5; // 0.0 ~ 1.0
    int brightness = 140 + static_cast<int>(pulse * 95.0); // 140 ~ 235

    COLORREF textColor = RGB(brightness - 15, brightness, brightness + 20);

    // ほんのり光るカード状の囲み枠
    int frameAlpha = 35 + static_cast<int>(pulse * 45.0);
    COLORREF frameBg = RGB(22, 28, 40);
    COLORREF frameBorder = RGB(frameAlpha, frameAlpha + 15, frameAlpha + 45);
    Box(dc, rPrompt, frameBg, frameBorder, 1, 24);

    HFONT fPrompt = CreateCustomFont(18, FW_BOLD);
    Center(dc, rPrompt, L"— タップして硯に向かう —", fPrompt, textColor);
    DeleteObject(fPrompt);
}

