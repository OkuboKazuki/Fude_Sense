#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include "wintab.h"
#define PACKETDATA (PK_X | PK_Y | PK_BUTTONS | PK_NORMAL_PRESSURE | PK_TIME | PK_ORIENTATION)
#define PACKETMODE PK_BUTTONS
#include "pktdef.h"
#include "WintabUtils.h"
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

static HCTX g_wt = nullptr;
static LONG g_pressureMin = 0;
static LONG g_pressureMax = 1024;

namespace {
    constexpr wchar_t CLASS_NAME[] = L"SHUJI_STUDIO_WINDOW";
    constexpr wchar_t WINDOW_TITLE[] = L"SHUJI STUDIO - 習字制作ワークスペース";
    constexpr double INK_MAX = 1.0;

    enum class Screen { Select, Studio };
    enum class Brush { Small, Medium, Large };
    enum class Mode { Small, Medium, Large, All };
    enum class Tool { BrushTool, EraserTool, HandTool };

    HWND g_hwnd = nullptr;
    HDC g_dc = nullptr;
    HBITMAP g_bitmap = nullptr;
    HBITMAP g_oldBitmap = nullptr;
    int g_width = 0, g_height = 0;
    Screen g_screen = Screen::Select;
    Brush g_brush = Brush::Medium;
    Mode g_mode = Mode::Medium;
    Tool g_tool = Tool::BrushTool;
    int g_grid = 1;
    double g_ink = INK_MAX;
    bool g_down = false, g_first = true, g_hasPoint = false;
    double g_fx = 0.0, g_fy = 0.0, g_widthSmooth = 14.0, g_widthPrev = 14.0, g_speed = 0.0;
    ULONGLONG g_timePrev = 0;

    RECT rTop{}, rTools{}, rSub{}, rCanvasArea{}, rRight{}, rStatus{};
    RECT rPaper{}, rToolBrush{}, rToolEraser{}, rToolHand{};
    RECT rSubSmall{}, rSubMedium{}, rSubLarge{}, rNewPaper{}, rGrid{}, rBack{};
    RECT rNavigator{}, rProperty{}, rLayers{}, rInkStone{};
    RECT rSelPanel{}, rSelSmall{}, rSelMedium{}, rSelLarge{}, rSelAll{};

    template<class T> T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
    int RW(const RECT& r) { return r.right - r.left; }
    int RH(const RECT& r) { return r.bottom - r.top; }

    const wchar_t* BrushName(Brush b) {
        switch (b) {
        case Brush::Small: return L"小筆";
        case Brush::Medium: return L"中筆";
        case Brush::Large: return L"大筆";
        }
        return L"中筆";
    }
    const wchar_t* ModeName() {
        switch (g_mode) {
        case Mode::Small: return L"小筆練習";
        case Mode::Medium: return L"中筆練習";
        case Mode::Large: return L"大筆練習";
        case Mode::All: return L"全筆モード";
        }
        return L"中筆練習";
    }

    HFONT Font(int size, int weight = FW_NORMAL, const wchar_t* face = L"Yu Gothic UI") {
        return CreateFontW(size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, face);
    }
    void Fill(HDC dc, const RECT& r, COLORREF color) {
        HBRUSH b = CreateSolidBrush(color); if (!b) return; FillRect(dc, &r, b); DeleteObject(b);
    }
    void Box(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int bw = 1, int round = 5) {
        HBRUSH b = CreateSolidBrush(fill); HPEN p = CreatePen(PS_SOLID, bw, border);
        if (!b || !p) { if (b) DeleteObject(b); if (p) DeleteObject(p); return; }
        HBRUSH ob = (HBRUSH)SelectObject(dc, b); HPEN op = (HPEN)SelectObject(dc, p);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, round, round);
        SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(p); DeleteObject(b);
    }
    void Text(HDC dc, RECT r, const wchar_t* s, HFONT f, COLORREF c, UINT align = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
        HFONT old = f ? (HFONT)SelectObject(dc, f) : nullptr;
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, c);
        DrawTextW(dc, s, -1, &r, align | DT_NOPREFIX);
        if (f && old) SelectObject(dc, old);
    }
    void Center(HDC dc, RECT r, const wchar_t* s, HFONT f, COLORREF c) {
        Text(dc, r, s, f, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void EndStroke() {
        g_down = false; g_first = true; g_hasPoint = false; g_speed = 0.0;
        g_widthSmooth = 14.0; g_widthPrev = 14.0;
    }
    void DeleteCanvas() {
        if (g_dc) {
            if (g_oldBitmap) { SelectObject(g_dc, g_oldBitmap); g_oldBitmap = nullptr; }
            if (g_bitmap) { DeleteObject(g_bitmap); g_bitmap = nullptr; }
            DeleteDC(g_dc); g_dc = nullptr;
        }
        g_width = g_height = 0;
    }

    void Layout(int w, int h) {
        const int topH = 66, statusH = 26, toolW = 50, subW = 210, rightW = 265;
        rTop = { 0, 0, w, topH }; rStatus = { 0, h - statusH, w, h };
        rTools = { 0, topH, toolW, h - statusH };
        rSub = { toolW, topH, toolW + subW, h - statusH };
        rRight = { w - rightW, topH, w, h - statusH };
        rCanvasArea = { rSub.right, topH, rRight.left, h - statusH };
        rToolBrush = { 8, topH + 12, 42, topH + 48 };
        rToolEraser = { 8, topH + 58, 42, topH + 94 };
        rToolHand = { 8, topH + 104, 42, topH + 140 };
        rSubSmall = { rSub.left + 10, topH + 48, rSub.right - 10, topH + 90 };
        rSubMedium = { rSub.left + 10, topH + 96, rSub.right - 10, topH + 138 };
        rSubLarge = { rSub.left + 10, topH + 144, rSub.right - 10, topH + 186 };
        rNewPaper = { rSub.left + 10, h - statusH - 154, rSub.right - 10, h - statusH - 112 };
        rGrid = { rSub.left + 10, h - statusH - 104, rSub.right - 10, h - statusH - 62 };
        rBack = { rSub.left + 10, h - statusH - 54, rSub.right - 10, h - statusH - 12 };
        rNavigator = { rRight.left + 8, topH + 8, rRight.right - 8, topH + 190 };
        rProperty = { rRight.left + 8, topH + 198, rRight.right - 8, topH + 430 };
        rLayers = { rRight.left + 8, topH + 438, rRight.right - 8, h - statusH - 8 };
        rInkStone = { rProperty.left + 18, rProperty.top + 125, rProperty.right - 18, rProperty.bottom - 18 };
        int aw = std::max(1, RW(rCanvasArea) - 56), ah = std::max(1, RH(rCanvasArea) - 56);
        int paper = std::max(1, std::min(aw, ah));
        int px = rCanvasArea.left + (RW(rCanvasArea) - paper) / 2;
        int py = rCanvasArea.top + (RH(rCanvasArea) - paper) / 2;
        rPaper = { px, py, px + paper, py + paper };

        int pw = Clamp(w - 140, 620, 920), ph = Clamp(h - 100, 560, 720);
        pw = std::min(pw, w - 30); ph = std::min(ph, h - 30);
        int pl = (w - pw) / 2, pt = (h - ph) / 2;
        rSelPanel = { pl, pt, pl + pw, pt + ph };
        int left = pl + 55, right = pl + pw - 55, gap = 16;
        int bw = (right - left - gap) / 2, bh = 104, y = pt + 238;
        rSelSmall = { left, y, left + bw, y + bh };
        rSelMedium = { left + bw + gap, y, right, y + bh };
        y += bh + gap;
        rSelLarge = { left, y, left + bw, y + bh };
        rSelAll = { left + bw + gap, y, right, y + bh };
    }

    void DrawPanelTitle(HDC dc, RECT r, const wchar_t* s) {
        RECT head = { r.left, r.top, r.right, r.top + 27 }; Fill(dc, head, RGB(45, 47, 51));
        HPEN p = CreatePen(PS_SOLID, 1, RGB(24, 25, 28)); HPEN op = (HPEN)SelectObject(dc, p);
        MoveToEx(dc, head.left, head.bottom - 1, nullptr); LineTo(dc, head.right, head.bottom - 1);
        SelectObject(dc, op); DeleteObject(p);
        HFONT f = Font(14, FW_BOLD); RECT tr = { head.left + 9, head.top, head.right - 6, head.bottom };
        Text(dc, tr, s, f, RGB(210, 212, 215)); DeleteObject(f);
    }
    void DrawTop() {
        Fill(g_dc, rTop, RGB(42, 44, 48));
        RECT menu = { 0, 0, g_width, 28 }; Fill(g_dc, menu, RGB(34, 36, 39));
        HFONT f = Font(14); RECT t = { 12, 0, 420, 28 };
        Text(g_dc, t, L"ファイル　編集　表示　練習　ウィンドウ　ヘルプ", f, RGB(215, 217, 220));
        RECT badge = { 12, 34, 112, 59 }; Box(g_dc, badge, RGB(72, 76, 84), RGB(95, 99, 108), 1, 4);
        Center(g_dc, badge, ModeName(), f, RGB(235, 237, 240));
        RECT undo = { 126, 34, 186, 59 }, redo = { 192, 34, 252, 59 }, clear = { 258, 34, 344, 59 };
        Box(g_dc, undo, RGB(55, 58, 63), RGB(76, 79, 85)); Center(g_dc, undo, L"戻す", f, RGB(190, 193, 197));
        Box(g_dc, redo, RGB(55, 58, 63), RGB(76, 79, 85)); Center(g_dc, redo, L"進む", f, RGB(190, 193, 197));
        Box(g_dc, clear, RGB(55, 58, 63), RGB(76, 79, 85)); Center(g_dc, clear, L"用紙消去", f, RGB(225, 225, 227));
        DeleteObject(f);
    }
    void DrawToolIcon(RECT r, const wchar_t* label, bool active) {
        Box(g_dc, r, active ? RGB(73, 108, 145) : RGB(48, 50, 54), active ? RGB(116, 160, 204) : RGB(68, 71, 76), 1, 4);
        HFONT f = Font(18, FW_BOLD); Center(g_dc, r, label, f, active ? RGB(255, 255, 255) : RGB(190, 193, 198)); DeleteObject(f);
    }
    void DrawSubItem(RECT r, const wchar_t* name, const wchar_t* detail, bool active) {
        Box(g_dc, r, active ? RGB(68, 91, 117) : RGB(52, 54, 59), active ? RGB(102, 143, 184) : RGB(70, 73, 79), 1, 4);
        HFONT f1 = Font(15, FW_BOLD), f2 = Font(12);
        RECT a = { r.left + 10, r.top + 3, r.right - 8, r.top + 23 };
        RECT b = { r.left + 10, r.top + 21, r.right - 8, r.bottom - 2 };
        Text(g_dc, a, name, f1, RGB(238, 240, 243)); Text(g_dc, b, detail, f2, RGB(170, 174, 180));
        DeleteObject(f1); DeleteObject(f2);
    }

    void DrawCross(HDC dc, int x, int y, int n) {
        MoveToEx(dc, x - n, y, nullptr); LineTo(dc, x + n, y);
        MoveToEx(dc, x, y - n, nullptr); LineTo(dc, x, y + n);
    }
    void DrawPaper() {
        HBRUSH b = CreateSolidBrush(RGB(249, 248, 243)); HPEN p = CreatePen(PS_SOLID, 1, RGB(184, 184, 181));
        HBRUSH ob = (HBRUSH)SelectObject(g_dc, b); HPEN op = (HPEN)SelectObject(g_dc, p);
        Rectangle(g_dc, rPaper.left, rPaper.top, rPaper.right, rPaper.bottom);
        SelectObject(g_dc, op); SelectObject(g_dc, ob); DeleteObject(p); DeleteObject(b);
        if (g_grid) {
            HPEN gp = CreatePen(PS_DOT, 1, RGB(216, 123, 123)); HPEN og = (HPEN)SelectObject(g_dc, gp);
            int w = RW(rPaper), h = RH(rPaper), mx = rPaper.left + w / 2;
            MoveToEx(g_dc, mx, rPaper.top, nullptr); LineTo(g_dc, mx, rPaper.bottom);
            if (g_grid == 1) {
                int my = rPaper.top + h / 2; MoveToEx(g_dc, rPaper.left, my, nullptr); LineTo(g_dc, rPaper.right, my);
                DrawCross(g_dc, rPaper.left + w / 4, rPaper.top + h / 4, 12); DrawCross(g_dc, rPaper.left + 3 * w / 4, rPaper.top + h / 4, 12);
                DrawCross(g_dc, rPaper.left + w / 4, rPaper.top + 3 * h / 4, 12); DrawCross(g_dc, rPaper.left + 3 * w / 4, rPaper.top + 3 * h / 4, 12);
            }
            else {
                int y1 = rPaper.top + h / 3, y2 = rPaper.top + 2 * h / 3;
                MoveToEx(g_dc, rPaper.left, y1, nullptr); LineTo(g_dc, rPaper.right, y1);
                MoveToEx(g_dc, rPaper.left, y2, nullptr); LineTo(g_dc, rPaper.right, y2);
            }
            SelectObject(g_dc, og); DeleteObject(gp);
        }
    }
    void ResetPaper() { DrawPaper(); g_ink = INK_MAX; EndStroke(); }

    void DrawNavigator() {
        Fill(g_dc, rNavigator, RGB(50, 52, 57)); DrawPanelTitle(g_dc, rNavigator, L"ナビゲーター");
        RECT view = { rNavigator.left + 15, rNavigator.top + 39, rNavigator.right - 15, rNavigator.bottom - 15 };
        Fill(g_dc, view, RGB(37, 39, 43));
        int s = std::min(RW(view) - 24, RH(view) - 24); RECT mini = { (view.left + view.right - s) / 2, (view.top + view.bottom - s) / 2, (view.left + view.right + s) / 2, (view.top + view.bottom + s) / 2 };
        Fill(g_dc, mini, RGB(244, 244, 240));
        HPEN p = CreatePen(PS_SOLID, 1, RGB(112, 151, 191)); HPEN op = (HPEN)SelectObject(g_dc, p); HBRUSH ob = (HBRUSH)SelectObject(g_dc, GetStockObject(NULL_BRUSH));
        Rectangle(g_dc, mini.left, mini.top, mini.right, mini.bottom); SelectObject(g_dc, ob); SelectObject(g_dc, op); DeleteObject(p);
    }
    void DrawProperties() {
        Fill(g_dc, rProperty, RGB(50, 52, 57)); DrawPanelTitle(g_dc, rProperty, L"ツールプロパティ");
        HFONT f = Font(14), fb = Font(14, FW_BOLD); RECT a = { rProperty.left + 12,rProperty.top + 34,rProperty.right - 10,rProperty.top + 58 };
        wchar_t buf[64]; wsprintfW(buf, L"筆: %s", BrushName(g_brush)); Text(g_dc, a, buf, fb, RGB(226, 228, 231));
        RECT lab = { rProperty.left + 12,rProperty.top + 62,rProperty.left + 72,rProperty.top + 84 }; Text(g_dc, lab, L"筆圧", f, RGB(190, 193, 198));
        RECT track = { rProperty.left + 72,rProperty.top + 69,rProperty.right - 15,rProperty.top + 77 }; Fill(g_dc, track, RGB(31, 33, 36));
        RECT level = track; level.right = level.left + (int)(RW(track) * 0.72); Fill(g_dc, level, RGB(76, 123, 169));
        RECT il = { rProperty.left + 12,rProperty.top + 91,rProperty.right - 12,rProperty.top + 114 }; wsprintfW(buf, L"墨量: %d%%", (int)(g_ink * 100.0)); Text(g_dc, il, buf, f, RGB(190, 193, 198));
        Box(g_dc, rInkStone, RGB(42, 43, 46), RGB(72, 74, 79), 1, 6);
        HBRUSH ib = CreateSolidBrush(RGB(12, 12, 13)); HPEN ip = CreatePen(PS_SOLID, 1, RGB(5, 5, 5)); HBRUSH oib = (HBRUSH)SelectObject(g_dc, ib); HPEN oip = (HPEN)SelectObject(g_dc, ip);
        Ellipse(g_dc, rInkStone.left + 18, rInkStone.top + 12, rInkStone.right - 18, rInkStone.bottom - 12);
        SelectObject(g_dc, oip); SelectObject(g_dc, oib); DeleteObject(ip); DeleteObject(ib);
        DeleteObject(f); DeleteObject(fb);
    }
    void DrawLayers() {
        Fill(g_dc, rLayers, RGB(50, 52, 57)); DrawPanelTitle(g_dc, rLayers, L"レイヤー");
        RECT row = { rLayers.left + 8,rLayers.top + 38,rLayers.right - 8,rLayers.top + 76 }; Box(g_dc, row, RGB(66, 89, 113), RGB(95, 127, 159), 1, 3);
        HFONT f = Font(14); RECT eye = { row.left + 8,row.top,row.left + 30,row.bottom }; Center(g_dc, eye, L"●", f, RGB(215, 218, 222));
        RECT name = { row.left + 36,row.top,row.right - 8,row.bottom }; Text(g_dc, name, L"墨線レイヤー", f, RGB(238, 240, 242));
        RECT paper = { rLayers.left + 8,rLayers.top + 82,rLayers.right - 8,rLayers.top + 120 }; Box(g_dc, paper, RGB(55, 57, 62), RGB(70, 73, 78), 1, 3);
        RECT pn = { paper.left + 36,paper.top,paper.right - 8,paper.bottom }; Text(g_dc, pn, L"半紙", f, RGB(200, 203, 207)); DeleteObject(f);
    }
    void DrawStudioChrome() {
        RECT all = { 0,0,g_width,g_height }; Fill(g_dc, all, RGB(36, 38, 42)); DrawTop();
        Fill(g_dc, rTools, RGB(43, 45, 49)); Fill(g_dc, rSub, RGB(50, 52, 57)); Fill(g_dc, rRight, RGB(43, 45, 49)); Fill(g_dc, rCanvasArea, RGB(72, 74, 78));
        DrawToolIcon(rToolBrush, L"筆", g_tool == Tool::BrushTool); DrawToolIcon(rToolEraser, L"消", g_tool == Tool::EraserTool); DrawToolIcon(rToolHand, L"手", g_tool == Tool::HandTool);
        DrawPanelTitle(g_dc, rSub, L"サブツール［習字筆］");
        DrawSubItem(rSubSmall, L"小筆", L"かな・名前・細字", g_brush == Brush::Small);
        DrawSubItem(rSubMedium, L"中筆", L"半紙・基本漢字", g_brush == Brush::Medium);
        DrawSubItem(rSubLarge, L"大筆", L"大字・力強い線", g_brush == Brush::Large);
        HFONT f = Font(14, FW_BOLD); Box(g_dc, rNewPaper, RGB(55, 58, 63), RGB(77, 80, 86)); Center(g_dc, rNewPaper, L"新しい用紙", f, RGB(225, 227, 230));
        Box(g_dc, rGrid, RGB(55, 58, 63), RGB(77, 80, 86)); wchar_t gb[32]; wsprintfW(gb, L"下敷き: %s", g_grid == 0 ? L"無地" : (g_grid == 1 ? L"4マス" : L"6マス")); Center(g_dc, rGrid, gb, f, RGB(225, 227, 230));
        Box(g_dc, rBack, RGB(55, 58, 63), RGB(77, 80, 86)); Center(g_dc, rBack, L"筆選択へ戻る", f, RGB(225, 227, 230)); DeleteObject(f);
        DrawNavigator(); DrawProperties(); DrawLayers();
        RECT shadow = { rPaper.left + 7,rPaper.top + 7,rPaper.right + 7,rPaper.bottom + 7 }; Fill(g_dc, shadow, RGB(45, 46, 49)); ResetPaper();
        Fill(g_dc, rStatus, RGB(31, 33, 36)); HFONT sf = Font(13); wchar_t st[256]; wsprintfW(st, L"  %s  |  現在の筆: %s  |  墨量: %d%%  |  Wintab: %s  |  Esc: 筆選択", ModeName(), BrushName(g_brush), (int)(g_ink * 100.0), g_wt ? L"接続済み" : L"マウスモード");
        Text(g_dc, rStatus, st, sf, RGB(185, 188, 193)); DeleteObject(sf);
    }

    void DrawSelectButton(RECT r, const wchar_t* title, const wchar_t* desc, COLORREF color) {
        Box(g_dc, r, color, RGB(93, 98, 106), 1, 6); HFONT f1 = Font(22, FW_BOLD), f2 = Font(13);
        RECT a = { r.left + 10,r.top + 8,r.right - 10,r.top + 44 }, b = { r.left + 10,r.top + 44,r.right - 10,r.bottom - 8 }; Center(g_dc, a, title, f1, RGB(245, 247, 249)); Center(g_dc, b, desc, f2, RGB(203, 207, 212)); DeleteObject(f1); DeleteObject(f2);
    }
    void DrawSelection() {
        RECT all = { 0,0,g_width,g_height }; Fill(g_dc, all, RGB(29, 31, 34));
        Box(g_dc, rSelPanel, RGB(47, 49, 54), RGB(70, 73, 79), 1, 8);
        HFONT title = Font(35, FW_BOLD), sub = Font(16); RECT a = { rSelPanel.left + 20,rSelPanel.top + 26,rSelPanel.right - 20,rSelPanel.top + 78 }; Center(g_dc, a, L"SHUJI STUDIO", title, RGB(237, 239, 242));
        RECT b = { rSelPanel.left + 20,rSelPanel.top + 78,rSelPanel.right - 20,rSelPanel.top + 114 }; Center(g_dc, b, L"使用する習字筆を選択してください", sub, RGB(183, 187, 193));
        RECT preview = { rSelPanel.left + 55,rSelPanel.top + 132,rSelPanel.right - 55,rSelPanel.top + 210 }; Fill(g_dc, preview, RGB(37, 39, 43));
        HPEN hp = CreatePen(PS_SOLID, 10, RGB(126, 83, 48)); HPEN oh = (HPEN)SelectObject(g_dc, hp); int cy = (preview.top + preview.bottom) / 2; MoveToEx(g_dc, preview.left + 85, cy, nullptr); LineTo(g_dc, preview.right - 140, cy); SelectObject(g_dc, oh); DeleteObject(hp);
        POINT tip[3] = { {preview.right - 165,cy - 22},{preview.right - 65,cy},{preview.right - 165,cy + 22} }; HBRUSH tb = CreateSolidBrush(RGB(12, 12, 13)); HBRUSH ot = (HBRUSH)SelectObject(g_dc, tb); HPEN on = (HPEN)SelectObject(g_dc, GetStockObject(NULL_PEN)); Polygon(g_dc, tip, 3); SelectObject(g_dc, on); SelectObject(g_dc, ot); DeleteObject(tb);
        DrawSelectButton(rSelSmall, L"小筆", L"かな・名前・細字", RGB(55, 66, 78)); DrawSelectButton(rSelMedium, L"中筆", L"半紙・基本漢字", RGB(58, 70, 84));
        DrawSelectButton(rSelLarge, L"大筆", L"大字・力強い線", RGB(60, 72, 87)); DrawSelectButton(rSelAll, L"全部使う", L"練習中に3種類を切替", RGB(66, 69, 84));
        DeleteObject(title); DeleteObject(sub);
    }

    void DrawCurrent() { if (!g_dc) return; if (g_screen == Screen::Select) DrawSelection(); else DrawStudioChrome(); InvalidateRect(g_hwnd, nullptr, FALSE); }
    bool ResizeCanvas(HWND hwnd, int w, int h) {
        if (w <= 0 || h <= 0)return false; Layout(w, h); HDC wd = GetDC(hwnd); if (!wd)return false; HDC nd = CreateCompatibleDC(wd); HBITMAP nb = CreateCompatibleBitmap(wd, w, h); ReleaseDC(hwnd, wd);
        if (!nd || !nb) { if (nb)DeleteObject(nb); if (nd)DeleteDC(nd); return false; }HBITMAP no = (HBITMAP)SelectObject(nd, nb); DeleteCanvas(); g_dc = nd; g_bitmap = nb; g_oldBitmap = no; g_width = w; g_height = h; DrawCurrent(); return true;
    }
    void Start(Mode m) { g_mode = m; g_brush = m == Mode::Small ? Brush::Small : (m == Mode::Large ? Brush::Large : Brush::Medium); g_tool = Tool::BrushTool; g_screen = Screen::Studio; EndStroke(); DrawCurrent(); }
    void Back() { EndStroke(); g_screen = Screen::Select; DrawCurrent(); }

    void BrushParams(double& base, double& pressure, double& minw, double& maxw) {
        if (g_brush == Brush::Small) { base = 1.5; pressure = 8.5; minw = 1.2; maxw = 12.0; }
        else if (g_brush == Brush::Medium) { base = 3.0; pressure = 19.0; minw = 2.3; maxw = 27.0; }
        else { base = 5.0; pressure = 34.0; minw = 4.0; maxw = 46.0; }
    }
    COLORREF InkColor() { int v = Clamp((int)((1.0 - g_ink) * 65.0), 0, 65); return RGB(v, v, v); }
    void VariableSegment(double x0, double y0, double w0, double x1, double y1, double w1, COLORREF color) {
        double dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy); if (len < 0.01)return; double nx = -dy / len, ny = dx / len, r0 = w0 / 2.0, r1 = w1 / 2.0;
        POINT q[4] = { {(LONG)std::lround(x0 + nx * r0),(LONG)std::lround(y0 + ny * r0)},{(LONG)std::lround(x1 + nx * r1),(LONG)std::lround(y1 + ny * r1)},{(LONG)std::lround(x1 - nx * r1),(LONG)std::lround(y1 - ny * r1)},{(LONG)std::lround(x0 - nx * r0),(LONG)std::lround(y0 - ny * r0)} };
        HBRUSH b = CreateSolidBrush(color); if (!b)return; HBRUSH ob = (HBRUSH)SelectObject(g_dc, b); HPEN op = (HPEN)SelectObject(g_dc, GetStockObject(NULL_PEN)); Polygon(g_dc, q, 4); int x = (int)std::lround(x1), y = (int)std::lround(y1), r = std::max(1, (int)std::lround(r1)); Ellipse(g_dc, x - r, y - r, x + r + 1, y + r + 1); SelectObject(g_dc, op); SelectObject(g_dc, ob); DeleteObject(b);
    }
    void Stroke(POINT pt, double pressure, bool start, double altitude) {
        if (!g_dc || g_screen != Screen::Studio || g_tool == Tool::HandTool)return; if (!PtInRect(&rPaper, pt)) { EndStroke(); return; }
        double base, pw, minw, maxw; BrushParams(base, pw, minw, maxw); ULONGLONG now = GetTickCount64(); pressure = Clamp(pressure, 0.0, 1.0);
        if (g_tool == Tool::EraserTool) { base = 18; pw = 0; minw = 18; maxw = 18; }
        COLORREF color = g_tool == Tool::EraserTool ? RGB(249, 248, 243) : InkColor();
        if (g_first || start || !g_hasPoint) { g_fx = pt.x; g_fy = pt.y; g_timePrev = now; g_speed = 0; g_first = false; g_hasPoint = true; g_widthSmooth = Clamp(base + pw * std::pow(pressure, 0.8), minw, maxw); g_widthPrev = g_widthSmooth; VariableSegment(g_fx - 0.01, g_fy, g_widthSmooth, g_fx + 0.01, g_fy, g_widthSmooth, color); InvalidateRect(g_hwnd, &rPaper, FALSE); return; }
        double rdx = pt.x - g_fx, rdy = pt.y - g_fy, rd = std::sqrt(rdx * rdx + rdy * rdy); if (rd < 0.05)return; double follow = Clamp(0.30 + rd * 0.035, 0.30, 0.82), nx = g_fx + rdx * follow, ny = g_fy + rdy * follow, dx = nx - g_fx, dy = ny - g_fy, d = std::sqrt(dx * dx + dy * dy); if (d < 0.1)return;
        ULONGLONG elapsed = std::max<ULONGLONG>(1ULL, now - g_timePrev); double sp = rd / (double)elapsed; g_speed = 0.82 * g_speed + 0.18 * sp; if (altitude <= 0 || altitude > 90)altitude = 45;
        double target = (base + pw * std::pow(pressure, 0.78)) * (1.0 + Clamp((90.0 - altitude) / 150.0, 0.0, 0.40)); target *= 1.05 - Clamp(g_speed * 0.45, 0.0, 0.48); target = Clamp(target, minw, maxw); g_widthSmooth += (target - g_widthSmooth) * (target < g_widthSmooth ? 0.40 : 0.23);
        int n = std::max(1, (int)std::ceil(d / 1.8)); double sx = g_fx, sy = g_fy, sw = g_widthPrev; for (int i = 1; i <= n; ++i) { double t = (double)i / n, u = t * t * (3.0 - 2.0 * t), ex = g_fx + dx * u, ey = g_fy + dy * u, ew = g_widthPrev + (g_widthSmooth - g_widthPrev) * u; VariableSegment(sx, sy, sw, ex, ey, ew, color); sx = ex; sy = ey; sw = ew; }
        if (g_tool == Tool::BrushTool)g_ink = std::max(0.0, g_ink - 0.00018 * d); g_fx = nx; g_fy = ny; g_widthPrev = g_widthSmooth; g_timePrev = now; InvalidateRect(g_hwnd, &rPaper, FALSE);
    }

    void InitWintab(HWND hwnd) {
        if (!LoadWintab()) { g_wt = nullptr; return; }LOGCONTEXT lc = {}; if (!gpWTInfoA(WTI_DEFSYSCTX, 0, &lc)) { UnloadWintab(); return; }
        lc.lcPktData = PACKETDATA; lc.lcPktMode = PACKETMODE; lc.lcMoveMask = PACKETDATA; lc.lcOptions |= CXO_MESSAGES; lc.lcBtnUpMask = lc.lcBtnDnMask;
        int l = GetSystemMetrics(SM_XVIRTUALSCREEN), t = GetSystemMetrics(SM_YVIRTUALSCREEN), w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        lc.lcOutOrgX = l; lc.lcOutExtX = w; lc.lcOutOrgY = t + h; lc.lcOutExtY = -h; AXIS ax = {}; if (gpWTInfoA(WTI_DEVICES, DVC_NPRESSURE, &ax)) { g_pressureMin = (LONG)ax.axMin; g_pressureMax = (LONG)ax.axMax; }if (g_pressureMax <= g_pressureMin) { g_pressureMin = 0; g_pressureMax = 1024; }g_wt = gpWTOpenA(hwnd, &lc, TRUE); if (!g_wt)UnloadWintab();
    }
    void SetActiveBrush(Brush b) { if (g_mode != Mode::All && ((g_mode == Mode::Small && b != Brush::Small) || (g_mode == Mode::Medium && b != Brush::Medium) || (g_mode == Mode::Large && b != Brush::Large)))return; g_brush = b; g_tool = Tool::BrushTool; EndStroke(); DrawStudioChrome(); InvalidateRect(g_hwnd, nullptr, FALSE); }
    void ClickSelect(POINT p) { if (PtInRect(&rSelSmall, p))Start(Mode::Small); else if (PtInRect(&rSelMedium, p))Start(Mode::Medium); else if (PtInRect(&rSelLarge, p))Start(Mode::Large); else if (PtInRect(&rSelAll, p))Start(Mode::All); }
    void ClickStudio(HWND hwnd, POINT p) {
        if (PtInRect(&rToolBrush, p)) { g_tool = Tool::BrushTool; DrawStudioChrome(); InvalidateRect(hwnd, nullptr, FALSE); return; }
        if (PtInRect(&rToolEraser, p)) { g_tool = Tool::EraserTool; DrawStudioChrome(); InvalidateRect(hwnd, nullptr, FALSE); return; }
        if (PtInRect(&rToolHand, p)) { g_tool = Tool::HandTool; DrawStudioChrome(); InvalidateRect(hwnd, nullptr, FALSE); return; }
        if (PtInRect(&rSubSmall, p)) { SetActiveBrush(Brush::Small); return; }if (PtInRect(&rSubMedium, p)) { SetActiveBrush(Brush::Medium); return; }if (PtInRect(&rSubLarge, p)) { SetActiveBrush(Brush::Large); return; }
        if (PtInRect(&rNewPaper, p)) { ResetPaper(); DrawStudioChrome(); InvalidateRect(hwnd, nullptr, FALSE); return; }
        if (PtInRect(&rGrid, p)) { g_grid = (g_grid + 1) % 3; DrawStudioChrome(); InvalidateRect(hwnd, nullptr, FALSE); return; }
        if (PtInRect(&rBack, p)) { Back(); return; }if (PtInRect(&rInkStone, p)) { g_ink = INK_MAX; DrawProperties(); InvalidateRect(hwnd, &rProperty, FALSE); return; }
        if (!g_wt && PtInRect(&rPaper, p) && g_tool != Tool::HandTool) { SetCapture(hwnd); g_down = true; Stroke(p, 0.72, true, 45.0); }
    }

    LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_CREATE: { RECT r{}; GetClientRect(hwnd, &r); ResizeCanvas(hwnd, r.right, r.bottom); InitWintab(hwnd); DrawCurrent(); return 0; }
        case WT_PACKET: { if (!g_wt || g_screen != Screen::Studio)return 0; PACKET pk = {}; if (!gpWTPacket((HCTX)lp, (UINT)wp, &pk))return 0; POINT p = { (LONG)pk.pkX,(LONG)pk.pkY }; ScreenToClient(hwnd, &p); LONG pp = (LONG)pk.pkNormalPressure; if (pp <= g_pressureMin) { if (g_down)EndStroke(); return 0; }double pr = (double)(pp - g_pressureMin) / (double)(g_pressureMax - g_pressureMin); pr = Clamp(pr, 0.0, 1.0); double alt = (double)pk.pkOrientation.orAltitude; if (std::abs(alt) > 90)alt /= 10.0; alt = std::abs(alt); if (!PtInRect(&rPaper, p)) { if (g_down)EndStroke(); return 0; }if (!g_down) { g_down = true; Stroke(p, pr, true, alt); } else Stroke(p, pr, false, alt); return 0; }
        case WM_SIZE: { int w = LOWORD(lp), h = HIWORD(lp); if (w > 0 && h > 0)ResizeCanvas(hwnd, w, h); return 0; }
        case WM_ERASEBKGND:return 1;
        case WM_PAINT: { PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); if (g_dc)BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top, g_dc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY); EndPaint(hwnd, &ps); return 0; }
        case WM_LBUTTONDOWN: { SetFocus(hwnd); POINT p = { GET_X_LPARAM(lp),GET_Y_LPARAM(lp) }; if (g_screen == Screen::Select)ClickSelect(p); else ClickStudio(hwnd, p); return 0; }
        case WM_MOUSEMOVE: { if (!g_wt && g_screen == Screen::Studio && g_down) { POINT p = { GET_X_LPARAM(lp),GET_Y_LPARAM(lp) }; Stroke(p, 0.72, false, 45.0); }return 0; }
        case WM_LBUTTONUP: { if (!g_wt && g_down) { POINT p = { GET_X_LPARAM(lp),GET_Y_LPARAM(lp) }; if (PtInRect(&rPaper, p))Stroke(p, 0.72, false, 45.0); EndStroke(); if (GetCapture() == hwnd)ReleaseCapture(); }return 0; }
        case WM_KEYDOWN: { if (g_screen == Screen::Select) { if (wp == '1')Start(Mode::Small); else if (wp == '2')Start(Mode::Medium); else if (wp == '3')Start(Mode::Large); else if (wp == '4')Start(Mode::All); } else { if (wp == VK_ESCAPE)Back(); else if (wp == VK_DELETE) { DrawStudioChrome(); InvalidateRect(hwnd, nullptr, FALSE); } else if (wp == 'B') { g_tool = Tool::BrushTool; DrawStudioChrome(); InvalidateRect(hwnd, nullptr, FALSE); } else if (wp == 'E') { g_tool = Tool::EraserTool; DrawStudioChrome(); InvalidateRect(hwnd, nullptr, FALSE); } else if (g_mode == Mode::All && wp == '1')SetActiveBrush(Brush::Small); else if (g_mode == Mode::All && wp == '2')SetActiveBrush(Brush::Medium); else if (g_mode == Mode::All && wp == '3')SetActiveBrush(Brush::Large); }return 0; }
        case WM_CAPTURECHANGED:case WM_CANCELMODE:EndStroke(); return 0;
        case WM_DESTROY:if (g_wt) { gpWTClose(g_wt); g_wt = nullptr; }UnloadWintab(); DeleteCanvas(); PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc); wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wc.hIconSm = wc.hIcon; wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassExW(&wc)) { MessageBoxW(nullptr, L"ウィンドウクラスの登録に失敗しました。", L"エラー", MB_OK | MB_ICONERROR); return 1; }
    g_hwnd = CreateWindowExW(0, CLASS_NAME, WINDOW_TITLE, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900, nullptr, nullptr, instance, nullptr);
    if (!g_hwnd) { MessageBoxW(nullptr, L"ウィンドウの作成に失敗しました。", L"エラー", MB_OK | MB_ICONERROR); return 1; }
    ShowWindow(g_hwnd, show ? show : SW_SHOW); UpdateWindow(g_hwnd); MSG m = {}; while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }return (int)m.wParam;
}
