#include "stdafx.h"
#include "RenderUtils.h"

namespace RenderUtils {

    HFONT CreateCustomFont(int size, int weight, const wchar_t* face) {
        return CreateFontW(size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, face);
    }

    void Fill(HDC dc, const RECT& r, COLORREF color) {
        HBRUSH b = CreateSolidBrush(color);
        if (!b) return;
        FillRect(dc, &r, b);
        DeleteObject(b);
    }

    void Box(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int bw, int round) {
        HBRUSH b = CreateSolidBrush(fill);
        HPEN p = CreatePen(PS_SOLID, bw, border);
        if (!b || !p) {
            if (b) DeleteObject(b);
            if (p) DeleteObject(p);
            return;
        }
        HBRUSH ob = (HBRUSH)SelectObject(dc, b);
        HPEN op = (HPEN)SelectObject(dc, p);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, round, round);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(p);
        DeleteObject(b);
    }

    void DrawTextCustom(HDC dc, RECT r, const wchar_t* s, HFONT f, COLORREF c, UINT align) {
        HFONT old = f ? (HFONT)SelectObject(dc, f) : nullptr;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, c);
        DrawTextW(dc, s, -1, &r, align | DT_NOPREFIX);
        if (f && old) SelectObject(dc, old);
    }

    void Center(HDC dc, RECT r, const wchar_t* s, HFONT f, COLORREF c) {
        DrawTextCustom(dc, r, s, f, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void DrawPanelTitle(HDC dc, const RECT& rPanel, const wchar_t* title) {
        RECT rTitle = { rPanel.left + 16, 14, rPanel.right - 16, 38 };
        HFONT f = CreateCustomFont(15, FW_BOLD);
        DrawTextCustom(dc, rTitle, title, f, RGB(240, 244, 250));
        DeleteObject(f);
    }

    void DrawTileCard(HDC dc, RECT r, const wchar_t* title, const wchar_t* sub, bool active, bool hover) {
        COLORREF bg, border, textMain, textSub;
        if (active) {
            bg = RGB(36, 56, 88);
            border = RGB(70, 120, 195);
            textMain = RGB(255, 255, 255);
            textSub = RGB(160, 205, 255);
        } else if (hover) {
            bg = RGB(42, 45, 54);
            border = RGB(68, 74, 88);
            textMain = RGB(245, 248, 252);
            textSub = RGB(175, 180, 192);
        } else {
            bg = RGB(28, 30, 36);
            border = RGB(42, 45, 54);
            textMain = RGB(190, 195, 205);
            textSub = RGB(120, 125, 138);
        }

        Box(dc, r, bg, border, 1, 6);
        HFONT f1 = CreateCustomFont(15, FW_BOLD);
        HFONT f2 = CreateCustomFont(11);
        RECT r1 = { r.left + 4, r.top + 4, r.right - 4, r.top + 26 };
        RECT r2 = { r.left + 4, r.top + 26, r.right - 4, r.bottom - 3 };
        Center(dc, r1, title, f1, textMain);
        Center(dc, r2, sub, f2, textSub);
        DeleteObject(f1);
        DeleteObject(f2);
    }

    void DrawColorThemeButton(HDC dc, RECT r, const wchar_t* label, COLORREF dotColor, bool active, bool hover) {
        COLORREF bg = active ? RGB(38, 54, 82) : (hover ? RGB(40, 44, 52) : RGB(28, 30, 36));
        COLORREF border = active ? RGB(70, 120, 195) : (hover ? RGB(66, 72, 86) : RGB(42, 45, 54));
        Box(dc, r, bg, border, 1, 6);

        HBRUSH db = CreateSolidBrush(dotColor);
        HBRUSH odb = (HBRUSH)SelectObject(dc, db);
        HPEN dp = CreatePen(PS_SOLID, 1, active ? RGB(255, 255, 255) : RGB(100, 105, 115));
        HPEN odp = (HPEN)SelectObject(dc, dp);
        Ellipse(dc, r.left + 8, r.top + (RH(r) - 12) / 2, r.left + 20, r.top + (RH(r) + 12) / 2);
        SelectObject(dc, odp);
        SelectObject(dc, odb);
        DeleteObject(dp);
        DeleteObject(db);

        HFONT f = CreateCustomFont(13, active ? FW_BOLD : FW_NORMAL);
        RECT tr = { r.left + 22, r.top, r.right - 4, r.bottom };
        Center(dc, tr, label, f, active ? RGB(255, 255, 255) : RGB(200, 205, 215));
        DeleteObject(f);
    }

    void DrawSubCard(HDC dc, RECT r, const wchar_t* name, const wchar_t* detail, int strokeWidth, bool active, bool hover) {
        COLORREF bg, border;
        if (active) {
            bg = RGB(36, 52, 78);
            border = RGB(70, 115, 175);
        } else if (hover) {
            bg = RGB(40, 43, 52);
            border = RGB(64, 70, 84);
        } else {
            bg = RGB(30, 32, 38);
            border = RGB(44, 47, 56);
        }

        Box(dc, r, bg, border, 1, 8);

        RECT tipBox = { r.left + 12, r.top + 10, r.left + 44, r.bottom - 10 };
        int cy = (tipBox.top + tipBox.bottom) / 2;
        HPEN sp = CreatePen(PS_SOLID, (int)(strokeWidth * 1.4 + 1), active ? RGB(100, 180, 255) : RGB(160, 165, 175));
        HPEN osp = (HPEN)SelectObject(dc, sp);
        MoveToEx(dc, tipBox.left + 4, cy, nullptr);
        LineTo(dc, tipBox.right - 4, cy);
        SelectObject(dc, osp);
        DeleteObject(sp);

        HFONT f1 = CreateCustomFont(16, FW_BOLD);
        HFONT f2 = CreateCustomFont(12);
        RECT a = { r.left + 50, r.top + 8, r.right - 10, r.top + 32 };
        RECT b = { r.left + 50, r.top + 34, r.right - 10, r.bottom - 8 };
        DrawTextCustom(dc, a, name, f1, active ? RGB(255, 255, 255) : RGB(230, 233, 238));
        DrawTextCustom(dc, b, detail, f2, active ? RGB(170, 205, 245) : RGB(150, 155, 165));
        DeleteObject(f1);
        DeleteObject(f2);
    }

} // namespace RenderUtils
