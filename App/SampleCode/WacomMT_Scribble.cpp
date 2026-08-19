///////////////////////////////////////////////////////////////////////////////
//
//	PURPOSE
//		SHUJI STUDIO - Wacom Feel Multi-Touch & Wintab32 GPU Ink Application
//
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "WacomMT_Scribble.h"

#include <windows.h>
#include <windowsx.h>
#include <iostream>
#include <vector>
#include <map>
#include <utility>
#include <algorithm>
#include <string>
#include <sstream>
#include <memory>
#include <crtdbg.h>

#include "WacomMultiTouch.h"
#include "WintabUtils.h"
#include "GpuInk.h"

#define PACKETDATA	(PK_X | PK_Y | PK_Z | PK_BUTTONS | PK_NORMAL_PRESSURE | PK_TANGENT_PRESSURE | PK_TIME | PK_ORIENTATION)
#define PACKETMODE	PK_BUTTONS
#include "pktdef.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using WacomMTHitRectPtr = std::unique_ptr<WacomMTHitRect>;

enum class EDataType
{
	ENoData,
	EFingerData,
	EBlobData,
	ERawData
};

// Colors for touch points
#define NO_CONFIDENCE_COLOR	RGB(255,128,0)		// orange
#define CONFIDENCE_COLOR		RGB(0, 0, 255)		// blue
#define POSITION_ONLY_COLOR	RGB(0, 255, 0)		// green
#define NUM_HPENS		10

void Cleanup(void);

enum class Screen { Select, Studio };
enum class Brush { Small, Medium, Large };
enum class Mode { Small, Medium, Large, All };
enum class Tool { BrushTool, EraserTool, HandTool };

// Global Variables
HINSTANCE hInst = NULL;
std::wstring szTitle = L"SHUJI STUDIO - 習字制作ワークスペース";
std::wstring szWindowClass = L"WACOMMT_SCRIBBLE";
HWND g_mainWnd = NULL;
HDC g_hdc = NULL;
HWND g_hWndAbout = NULL;
int g_maxPressure = 1024;

RECT g_clientRect = { 0, 0, 0, 0 };
bool g_ShowTouchSize = true;
bool g_ShowTouchID = false;
std::map<int, WacomMTCapability> g_caps;
std::vector<int> g_devices;

typedef struct 
{
	int maxPressure;
	COLORREF penColor;
	char name[32];
	LONG tabletXExt;
	LONG tabletYExt;
	bool displayTablet;
	int maxZ;
} TabletInfo;

std::map<HCTX, TabletInfo> g_contextMap;
bool g_openSystemContext = true;

bool OpenTabletContexts(HWND hWnd);
void CloseTabletContexts(void);

std::map<int, HPEN> g_hPenMap;
std::map<int, HPEN> g_fingerHPenMap;

HBRUSH g_noConfidenceBrush = NULL;
HBRUSH g_confidenceBrush = NULL;
HBRUSH g_positionOnlyBrush = NULL;
HPEN g_noConfidencePen = NULL;
HPEN g_confidencePen = NULL;
std::map<int, WacomMTHitRectPtr> g_lastWTHitRect;

bool g_useConfidenceBits = true;
bool g_ObserverMode = false;

EDataType g_DataType = EDataType::EFingerData;
bool g_UseHWND = true;
bool g_UseWinHitRect = true;

CRITICAL_SECTION g_graphicsCriticalSection;

static GpuInk g_gpuInk;

HWND g_hInkWnd = NULL;
HWND g_hMonitorWnd = NULL;
static ATOM g_inkWndClassAtom = 0;
static ATOM g_monitorWndClassAtom = 0;

// UI System State
namespace {
    constexpr double INK_MAX = 1.0;

    Screen g_screen = Screen::Select;
    Brush g_brush = Brush::Medium;
    Mode g_mode = Mode::Medium;
    Tool g_tool = Tool::BrushTool;
    int g_grid = 1;
    double g_ink = INK_MAX;

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
    void DrawTop(HDC dc, int w) {
        Fill(dc, rTop, RGB(42, 44, 48));
        RECT menu = { 0, 0, w, 28 }; Fill(dc, menu, RGB(34, 36, 39));
        HFONT f = Font(14); RECT t = { 12, 0, 420, 28 };
        Text(dc, t, L"ファイル　編集　表示　練習　ウィンドウ　ヘルプ", f, RGB(215, 217, 220));
        RECT badge = { 12, 34, 112, 59 }; Box(dc, badge, RGB(72, 76, 84), RGB(95, 99, 108), 1, 4);
        Center(dc, badge, ModeName(), f, RGB(235, 237, 240));
        RECT undo = { 126, 34, 186, 59 }, redo = { 192, 34, 252, 59 }, clear = { 258, 34, 344, 59 };
        Box(dc, undo, RGB(55, 58, 63), RGB(76, 79, 85)); Center(dc, undo, L"戻す", f, RGB(190, 193, 197));
        Box(dc, redo, RGB(55, 58, 63), RGB(76, 79, 85)); Center(dc, redo, L"進む", f, RGB(190, 193, 197));
        Box(dc, clear, RGB(55, 58, 63), RGB(76, 79, 85)); Center(dc, clear, L"用紙消去", f, RGB(225, 225, 227));
        DeleteObject(f);
    }
    void DrawToolIcon(HDC dc, RECT r, const wchar_t* label, bool active) {
        Box(dc, r, active ? RGB(73, 108, 145) : RGB(48, 50, 54), active ? RGB(116, 160, 204) : RGB(68, 71, 76), 1, 4);
        HFONT f = Font(18, FW_BOLD); Center(dc, r, label, f, active ? RGB(255, 255, 255) : RGB(190, 193, 198)); DeleteObject(f);
    }
    void DrawSubItem(HDC dc, RECT r, const wchar_t* name, const wchar_t* detail, bool active) {
        Box(dc, r, active ? RGB(68, 91, 117) : RGB(52, 54, 59), active ? RGB(102, 143, 184) : RGB(70, 73, 79), 1, 4);
        HFONT f1 = Font(15, FW_BOLD), f2 = Font(12);
        RECT a = { r.left + 10, r.top + 3, r.right - 8, r.top + 23 };
        RECT b = { r.left + 10, r.top + 21, r.right - 8, r.bottom - 2 };
        Text(dc, a, name, f1, RGB(238, 240, 243)); Text(dc, b, detail, f2, RGB(170, 174, 180));
        DeleteObject(f1); DeleteObject(f2);
    }
    void DrawCross(HDC dc, int x, int y, int n) {
        MoveToEx(dc, x - n, y, nullptr); LineTo(dc, x + n, y);
        MoveToEx(dc, x, y - n, nullptr); LineTo(dc, x, y + n);
    }
    void DrawPaperBackground(HDC dc) {
        HBRUSH b = CreateSolidBrush(RGB(249, 248, 243)); HPEN p = CreatePen(PS_SOLID, 1, RGB(184, 184, 181));
        HBRUSH ob = (HBRUSH)SelectObject(dc, b); HPEN op = (HPEN)SelectObject(dc, p);
        Rectangle(dc, rPaper.left, rPaper.top, rPaper.right, rPaper.bottom);
        SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(p); DeleteObject(b);
    }
    void DrawPaperGrid(HDC dc) {
        if (!g_grid) return;
        HPEN gp = CreatePen(PS_DOT, 1, RGB(216, 123, 123)); HPEN og = (HPEN)SelectObject(dc, gp);
        int w = RW(rPaper), h = RH(rPaper), mx = rPaper.left + w / 2;
        MoveToEx(dc, mx, rPaper.top, nullptr); LineTo(dc, mx, rPaper.bottom);
        if (g_grid == 1) {
            int my = rPaper.top + h / 2; MoveToEx(dc, rPaper.left, my, nullptr); LineTo(dc, rPaper.right, my);
            DrawCross(dc, rPaper.left + w / 4, rPaper.top + h / 4, 12); DrawCross(dc, rPaper.left + 3 * w / 4, rPaper.top + h / 4, 12);
            DrawCross(dc, rPaper.left + w / 4, rPaper.top + 3 * h / 4, 12); DrawCross(dc, rPaper.left + 3 * w / 4, rPaper.top + 3 * h / 4, 12);
        }
        else {
            int y1 = rPaper.top + h / 3, y2 = rPaper.top + 2 * h / 3;
            MoveToEx(dc, rPaper.left, y1, nullptr); LineTo(dc, rPaper.right, y1);
            MoveToEx(dc, rPaper.left, y2, nullptr); LineTo(dc, rPaper.right, y2);
        }
        SelectObject(dc, og); DeleteObject(gp);
    }
    void DrawNavigator(HDC dc) {
        Fill(dc, rNavigator, RGB(50, 52, 57)); DrawPanelTitle(dc, rNavigator, L"ナビゲーター");
        RECT view = { rNavigator.left + 15, rNavigator.top + 39, rNavigator.right - 15, rNavigator.bottom - 15 };
        Fill(dc, view, RGB(37, 39, 43));
        int s = std::min(RW(view) - 24, RH(view) - 24); RECT mini = { (view.left + view.right - s) / 2, (view.top + view.bottom - s) / 2, (view.left + view.right + s) / 2, (view.top + view.bottom + s) / 2 };
        Fill(dc, mini, RGB(244, 244, 240));
        HPEN p = CreatePen(PS_SOLID, 1, RGB(112, 151, 191)); HPEN op = (HPEN)SelectObject(dc, p); HBRUSH ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, mini.left, mini.top, mini.right, mini.bottom); SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(p);
    }
    void DrawProperties(HDC dc) {
        Fill(dc, rProperty, RGB(50, 52, 57)); DrawPanelTitle(dc, rProperty, L"ツールプロパティ");
        HFONT f = Font(14), fb = Font(14, FW_BOLD); RECT a = { rProperty.left + 12,rProperty.top + 34,rProperty.right - 10,rProperty.top + 58 };
        wchar_t buf[64]; wsprintfW(buf, L"筆: %s", BrushName(g_brush)); Text(dc, a, buf, fb, RGB(226, 228, 231));
        RECT lab = { rProperty.left + 12,rProperty.top + 62,rProperty.left + 72,rProperty.top + 84 }; Text(dc, lab, L"筆圧", f, RGB(190, 193, 198));
        RECT track = { rProperty.left + 72,rProperty.top + 69,rProperty.right - 15,rProperty.top + 77 }; Fill(dc, track, RGB(31, 33, 36));
        RECT level = track; level.right = level.left + (int)(RW(track) * 0.72); Fill(dc, level, RGB(76, 123, 169));
        RECT il = { rProperty.left + 12,rProperty.top + 91,rProperty.right - 12,rProperty.top + 114 }; wsprintfW(buf, L"墨量: %d%%", (int)(g_ink * 100.0)); Text(dc, il, buf, f, RGB(190, 193, 198));
        Box(dc, rInkStone, RGB(42, 43, 46), RGB(72, 74, 79), 1, 6);
        HBRUSH ib = CreateSolidBrush(RGB(12, 12, 13)); HPEN ip = CreatePen(PS_SOLID, 1, RGB(5, 5, 5)); HBRUSH oib = (HBRUSH)SelectObject(dc, ib); HPEN oip = (HPEN)SelectObject(dc, ip);
        Ellipse(dc, rInkStone.left + 18, rInkStone.top + 12, rInkStone.right - 18, rInkStone.bottom - 12);
        SelectObject(dc, oip); SelectObject(dc, oib); DeleteObject(ip); DeleteObject(ib);
        DeleteObject(f); DeleteObject(fb);
    }
    void DrawLayers(HDC dc) {
        Fill(dc, rLayers, RGB(50, 52, 57)); DrawPanelTitle(dc, rLayers, L"レイヤー");
        RECT row = { rLayers.left + 8,rLayers.top + 38,rLayers.right - 8,rLayers.top + 76 }; Box(dc, row, RGB(66, 89, 113), RGB(95, 128, 162), 1, 4);
        RECT icon = { row.left + 8,row.top + 6,row.left + 34,row.bottom - 6 }; Fill(dc, icon, RGB(244, 244, 240));
        HFONT f = Font(13, FW_BOLD); RECT t = { row.left + 42,row.top,row.right - 8,row.bottom }; Text(dc, t, L"半紙レイヤー 1", f, RGB(240, 243, 246)); DeleteObject(f);
    }
    void DrawTools(HDC dc) {
        Fill(dc, rTools, RGB(38, 40, 44));
        DrawToolIcon(dc, rToolBrush, L"筆", g_tool == Tool::BrushTool);
        DrawToolIcon(dc, rToolEraser, L"消", g_tool == Tool::EraserTool);
        DrawToolIcon(dc, rToolHand, L"手", g_tool == Tool::HandTool);
    }
    void DrawSub(HDC dc) {
        Fill(dc, rSub, RGB(44, 46, 50)); DrawPanelTitle(dc, rSub, L"サブツール / 設定");
        DrawSubItem(dc, rSubSmall, L"小筆", L"細い線・かな名入れ", g_brush == Brush::Small);
        DrawSubItem(dc, rSubMedium, L"中筆", L"標準的な楷書・行書", g_brush == Brush::Medium);
        DrawSubItem(dc, rSubLarge, L"大筆", L"太い線・作品・大字", g_brush == Brush::Large);
        Box(dc, rNewPaper, RGB(55, 58, 63), RGB(78, 81, 87), 1, 4); HFONT f = Font(14, FW_BOLD); Center(dc, rNewPaper, L"新しい半紙", f, RGB(225, 227, 230));
        const wchar_t* gtext = g_grid == 0 ? L"格子: なし" : (g_grid == 1 ? L"格子: 4等分" : L"格子: 6等分");
        Box(dc, rGrid, RGB(55, 58, 63), RGB(78, 81, 87), 1, 4); Center(dc, rGrid, gtext, f, RGB(225, 227, 230));
        Box(dc, rBack, RGB(55, 58, 63), RGB(78, 81, 87), 1, 4); Center(dc, rBack, L"筆選択へ戻る", f, RGB(225, 227, 230)); DeleteObject(f);
    }
    void DrawRight(HDC dc) {
        Fill(dc, rRight, RGB(42, 44, 48)); DrawNavigator(dc); DrawProperties(dc); DrawLayers(dc);
    }
    void DrawStatus(HDC dc, int w, int h) {
        Fill(dc, rStatus, RGB(31, 33, 36)); HFONT f = Font(12); RECT t = { 10, h - 22, w - 10, h - 4 };
        wchar_t buf[128]; wsprintfW(buf, L"状態: 準備完了 | ツール: %s | 筆: %s | 格子: %d", g_tool == Tool::BrushTool ? L"筆" : (g_tool == Tool::EraserTool ? L"消しゴム" : L"手のひら"), BrushName(g_brush), g_grid);
        Text(dc, t, buf, f, RGB(160, 164, 170)); DeleteObject(f);
    }
    void DrawStudioChrome(HDC dc, int w, int h) {
        Fill(dc, rCanvasArea, RGB(26, 28, 31)); DrawTop(dc, w); DrawTools(dc); DrawSub(dc);
        DrawPaperBackground(dc); DrawRight(dc); DrawStatus(dc, w, h);
    }
    void DrawSelectButton(HDC dc, RECT r, const wchar_t* title, const wchar_t* desc, COLORREF color) {
        Box(dc, r, color, RGB(93, 98, 106), 1, 6); HFONT f1 = Font(22, FW_BOLD), f2 = Font(13);
        RECT a = { r.left + 10,r.top + 8,r.right - 10,r.top + 44 }, b = { r.left + 10,r.top + 44,r.right - 10,r.bottom - 8 }; Center(dc, a, title, f1, RGB(245, 247, 249)); Center(dc, b, desc, f2, RGB(203, 207, 212)); DeleteObject(f1); DeleteObject(f2);
    }
    void DrawSelection(HDC dc, int w, int h) {
        RECT all = { 0,0,w,h }; Fill(dc, all, RGB(29, 31, 34));
        Box(dc, rSelPanel, RGB(47, 49, 54), RGB(70, 73, 79), 1, 8);
        HFONT title = Font(35, FW_BOLD), sub = Font(16); RECT a = { rSelPanel.left + 20,rSelPanel.top + 26,rSelPanel.right - 20,rSelPanel.top + 78 }; Center(dc, a, L"SHUJI STUDIO", title, RGB(237, 239, 242));
        RECT b = { rSelPanel.left + 20,rSelPanel.top + 78,rSelPanel.right - 20,rSelPanel.top + 114 }; Center(dc, b, L"使用する習字筆を選択してください", sub, RGB(183, 187, 193));
        RECT preview = { rSelPanel.left + 55,rSelPanel.top + 132,rSelPanel.right - 55,rSelPanel.top + 210 }; Fill(dc, preview, RGB(37, 39, 43));
        HPEN hp = CreatePen(PS_SOLID, 10, RGB(126, 83, 48)); HPEN oh = (HPEN)SelectObject(dc, hp); int cy = (preview.top + preview.bottom) / 2; MoveToEx(dc, preview.left + 85, cy, nullptr); LineTo(dc, preview.right - 140, cy); SelectObject(dc, oh); DeleteObject(hp);
        POINT tip[3] = { {preview.right - 165,cy - 22},{preview.right - 65,cy},{preview.right - 165,cy + 22} }; HBRUSH tb = CreateSolidBrush(RGB(12, 12, 13)); HBRUSH ot = (HBRUSH)SelectObject(dc, tb); HPEN on = (HPEN)SelectObject(dc, GetStockObject(NULL_BRUSH)); Polygon(dc, tip, 3); SelectObject(dc, on); SelectObject(dc, ot); DeleteObject(tb);
        DrawSelectButton(dc, rSelSmall, L"小筆", L"かな・名前・細線", RGB(55, 66, 78)); DrawSelectButton(dc, rSelMedium, L"中筆", L"半紙・基本漢字", RGB(58, 70, 84));
        DrawSelectButton(dc, rSelLarge, L"大筆", L"大字・力強い線", RGB(60, 72, 87)); DrawSelectButton(dc, rSelAll, L"全部使う", L"練習中に3種類を切替", RGB(66, 69, 84));
        DeleteObject(title); DeleteObject(sub);
    }

    void Start(Mode m) {
        g_mode = m; g_brush = m == Mode::Small ? Brush::Small : (m == Mode::Large ? Brush::Large : Brush::Medium);
        g_tool = Tool::BrushTool; g_screen = Screen::Studio;
        InvalidateRect(g_mainWnd, NULL, FALSE);
    }
    void Back() {
        g_screen = Screen::Select;
        InvalidateRect(g_mainWnd, NULL, FALSE);
    }
    bool PtIn(const RECT& r, POINT p) { return PtInRect(&r, p) != FALSE; }
}

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
void ClearScreen();
LRESULT CALLBACK InkWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MonitorWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
static bool CreateInkWindow();
static void DestroyInkWindow();

LRESULT CALLBACK MonitorWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
		SetTimer(hWnd, 1, 30, NULL);
		return 0;
	case WM_TIMER:
		InvalidateRect(hWnd, NULL, FALSE);
		return 0;
	case WM_DESTROY:
		KillTimer(hWnd, 1);
		g_hMonitorWnd = NULL;
		return 0;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		RECT rc;
		GetClientRect(hWnd, &rc);
		int width = rc.right - rc.left;
		int height = rc.bottom - rc.top;

		HDC memDC = CreateCompatibleDC(hdc);
		HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
		HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

		HBRUSH bgBrush = CreateSolidBrush(RGB(25, 25, 30));
		FillRect(memDC, &rc, bgBrush);
		DeleteObject(bgBrush);

		SetBkMode(memDC, TRANSPARENT);
		HFONT hFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
			FIXED_PITCH | FF_DONTCARE, L"Consolas");
		HFONT oldFont = (HFONT)SelectObject(memDC, hFont);

		KinematicsInfo info = g_gpuInk.GetKinematicsInfo();

		wchar_t buf[256];
		int y = 15;
		int x = 15;

		SetTextColor(memDC, RGB(255, 255, 255));
		TextOutW(memDC, x, y, L"=== Kinematics Debug Monitor ===", 32);
		y += 28;

		SetTextColor(memDC, RGB(100, 220, 255));
		swprintf_s(buf, L"Current Speed : %7.1f px/sec", info.currentSpeed);
		TextOutW(memDC, x, y, buf, static_cast<int>(wcslen(buf)));
		y += 22;

		SetTextColor(memDC, RGB(180, 255, 100));
		swprintf_s(buf, L"Current Accel : %7.1f px/sec^2", info.currentAccel);
		TextOutW(memDC, x, y, buf, static_cast<int>(wcslen(buf)));
		y += 22;

		SetTextColor(memDC, RGB(255, 215, 0));
		swprintf_s(buf, L"Peak Max Speed: %7.1f px/sec", info.recentMaxSpeed);
		TextOutW(memDC, x, y, buf, static_cast<int>(wcslen(buf)));
		y += 22;

		swprintf_s(buf, L"Peak Max Accel: %7.1f px/sec^2", info.recentMaxAccel);
		TextOutW(memDC, x, y, buf, static_cast<int>(wcslen(buf)));
		y += 22;

		swprintf_s(buf, L"Peak Max Move : %7.1f px", info.recentMaxDist);
		TextOutW(memDC, x, y, buf, static_cast<int>(wcslen(buf)));
		y += 30;

		SetTextColor(memDC, RGB(220, 220, 220));
		TextOutW(memDC, x, y, L"-----------------------------------------", 41);
		y += 20;
		TextOutW(memDC, x, y, L"[ Last Stroke End Info ]", 24);
		y += 26;

		SetTextColor(memDC, RGB(200, 230, 255));
		swprintf_s(buf, L"End Last Speed     : %6.1f px/sec", info.lastEndSpeed);
		TextOutW(memDC, x, y, buf, static_cast<int>(wcslen(buf)));
		y += 22;

		swprintf_s(buf, L"End Effective Speed : %6.1f px/sec", info.lastEndEffectiveSpeed);
		TextOutW(memDC, x, y, buf, static_cast<int>(wcslen(buf)));
		y += 22;

		swprintf_s(buf, L"End Accel          : %6.1f px/sec^2", info.lastEndAccel);
		TextOutW(memDC, x, y, buf, static_cast<int>(wcslen(buf)));
		y += 28;

		if (info.lastIsFlick)
		{
			SetTextColor(memDC, RGB(50, 255, 120));
			swprintf_s(buf, L"Result: [ FLICK ]");
		}
		else
		{
			SetTextColor(memDC, RGB(255, 120, 100));
			swprintf_s(buf, L"Result: [ STOP ]");
		}
		TextOutW(memDC, x, y, buf, static_cast<int>(wcslen(buf)));
		y += 30;

		SetTextColor(memDC, RGB(220, 220, 220));
		TextOutW(memDC, x, y, L"-----------------------------------------", 41);
		y += 20;
		TextOutW(memDC, x, y, L"[ Pen Hover / Z-Axis Monitor ]", 30);
		y += 26;

		if (info.isHovering)
		{
			SetTextColor(memDC, RGB(255, 255, 100));
			swprintf_s(buf, L"Pen State : HOVERING (Floating)");
		}
		else
		{
			SetTextColor(memDC, RGB(100, 255, 180));
			swprintf_s(buf, L"Pen State : TOUCHING (On Screen)");
		}
		TextOutW(memDC, x, y, buf, static_cast<int>(wcslen(buf)));
		y += 22;

		SetTextColor(memDC, RGB(255, 190, 80));
		swprintf_s(buf, L"Pen Z (Distance) : %d", info.currentZ);
		TextOutW(memDC, x, y, buf, static_cast<int>(wcslen(buf)));
		y += 22;

		SetTextColor(memDC, RGB(180, 200, 255));
		swprintf_s(buf, L"Pen Tilt Altitude: %d deg", info.currentAltitude);
		TextOutW(memDC, x, y, buf, static_cast<int>(wcslen(buf)));
		y += 22;

		swprintf_s(buf, L"Pen Tilt Azimuth : %d deg", info.currentAzimuth);
		TextOutW(memDC, x, y, buf, static_cast<int>(wcslen(buf)));
		y += 24;

		RECT barRc = { x, y, x + 250, y + 16 };
		HBRUSH borderBrush = CreateSolidBrush(RGB(100, 100, 120));
		FrameRect(memDC, &barRc, borderBrush);
		DeleteObject(borderBrush);

		int barWidth = std::max(0, std::min(248, (info.currentZ * 248) / 1024));
		if (barWidth > 0)
		{
			RECT fillRc = { x + 1, y + 1, x + 1 + barWidth, y + 15 };
			HBRUSH fillBrush = info.isHovering ? CreateSolidBrush(RGB(255, 200, 50)) : CreateSolidBrush(RGB(50, 220, 100));
			FillRect(memDC, &fillRc, fillBrush);
			DeleteObject(fillBrush);
		}

		SelectObject(memDC, oldFont);
		DeleteObject(hFont);

		BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
		SelectObject(memDC, oldBmp);
		DeleteObject(memBmp);
		DeleteDC(memDC);

		EndPaint(hWnd, &ps);
		return 0;
	}
	default:
		break;
	}
	return DefWindowProcW(hWnd, message, wParam, lParam);
}

static bool CreateInkWindow()
{
	if (g_hInkWnd) return true;

	if (!g_inkWndClassAtom)
	{
		WNDCLASSEXW wc = { 0 };
		wc.cbSize = sizeof(wc);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = InkWndProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = hInst;
		wc.hIcon = NULL;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wc.lpszMenuName = NULL;
		wc.lpszClassName = L"INK_VIEWER_CLASS";
		wc.hIconSm = NULL;
		g_inkWndClassAtom = RegisterClassExW(&wc);
		if (!g_inkWndClassAtom) return false;
	}

	if (!g_monitorWndClassAtom)
	{
		WNDCLASSEXW wc = { 0 };
		wc.cbSize = sizeof(wc);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = MonitorWndProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = hInst;
		wc.hIcon = NULL;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wc.lpszMenuName = NULL;
		wc.lpszClassName = L"KINEMATICS_MONITOR_CLASS";
		wc.hIconSm = NULL;
		g_monitorWndClassAtom = RegisterClassExW(&wc);
	}

	g_hInkWnd = CreateWindowExW(WS_EX_TOOLWINDOW, (LPCWSTR)(ULONG_PTR)(WORD)(g_inkWndClassAtom),
		L"Ink Viewer",
		WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
		100, 100, 640, 480,
		NULL, NULL, hInst, NULL);
	if (!g_hInkWnd) return false;

	g_hMonitorWnd = CreateWindowExW(WS_EX_TOOLWINDOW, (LPCWSTR)(ULONG_PTR)(WORD)(g_monitorWndClassAtom),
		L"Kinematics Monitor",
		WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
		750, 100, 440, 480,
		NULL, NULL, hInst, NULL);

	ShowWindow(g_hInkWnd, SW_SHOW);
	UpdateWindow(g_hInkWnd);

	if (g_hMonitorWnd)
	{
		ShowWindow(g_hMonitorWnd, SW_SHOW);
		UpdateWindow(g_hMonitorWnd);
	}
	return true;
}

static void DestroyInkWindow()
{
	if (!g_hInkWnd) return;
	DestroyWindow(g_hInkWnd);
	g_hInkWnd = NULL;
}

LRESULT CALLBACK InkWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
	{
		HFONT hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
			FIXED_PITCH | FF_DONTCARE, L"Consolas");
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)hFont);
		return 0;
	}
	case WM_DESTROY:
	{
		HFONT hFont = (HFONT)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
		if (hFont) DeleteObject(hFont);
		return 0;
	}
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);

		FillRect(hdc, &ps.rcPaint, GetSysColorBrush(COLOR_WINDOW));

		HFONT hFont = (HFONT)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
		HFONT hOld = hFont ? (HFONT)SelectObject(hdc, hFont) : NULL;
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, RGB(0, 0, 0));

		std::vector<int> ink;
		int iw = 0, ih = 0;
		g_gpuInk.GetInkSnapshot(ink, iw, ih);

		if (ink.empty() || iw <= 0 || ih <= 0)
		{
			TextOutW(hdc, 4, 4, L"(no data)", 8);
		}
		else
		{
			const int maxRows = 40;
			const int maxCols = 80;
			int rows = std::min(ih, maxRows);
			int cols = std::min(iw, maxCols);

			int lineHeight = 16;
			for (int r = 0; r < rows; ++r)
			{
				std::wstring line;
				line.reserve(cols * 5);
				for (int c = 0; c < cols; ++c)
				{
					int v = ink[r * iw + c];
					wchar_t buf[16];
					swprintf_s(buf, L"%4d ", v);
					line += buf;
				}
				TextOutW(hdc, 4, 4 + r * lineHeight, line.c_str(), static_cast<int>(line.size()));
			}
		}

		if (hOld) SelectObject(hdc, hOld);
		EndPaint(hWnd, &ps);
		return 0;
	}
	default:
		return DefWindowProcW(hWnd, message, wParam, lParam);
	}
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WACOMMT_SCRIBBLE));
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = NULL;
	wcex.lpszMenuName = MAKEINTRESOURCE(IDC_WACOMMT_SCRIBBLE);
	wcex.lpszClassName = szWindowClass.c_str();
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance;

	g_mainWnd = CreateWindow(szWindowClass.c_str(),
		szTitle.c_str(),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		0,
		1280,
		800,
		NULL,
		NULL,
		hInstance,
		NULL);

	if (!g_mainWnd) return FALSE;

	g_hdc = GetDC(g_mainWnd);

	g_noConfidenceBrush = CreateSolidBrush(NO_CONFIDENCE_COLOR);
	g_confidenceBrush = CreateSolidBrush(CONFIDENCE_COLOR);
	g_positionOnlyBrush = CreateSolidBrush(POSITION_ONLY_COLOR);
	g_noConfidencePen = CreatePen(PS_SOLID, 3, NO_CONFIDENCE_COLOR);
	g_confidencePen = CreatePen(PS_SOLID, 3, CONFIDENCE_COLOR);

	ShowWindow(g_mainWnd, SW_SHOWMAXIMIZED);
	UpdateWindow(g_mainWnd);

	return TRUE;
}

int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPTSTR lpCmdLine,
	_In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	MSG msg;
	HACCEL hAccelTable;

	InitializeCriticalSection(&g_graphicsCriticalSection);
	MyRegisterClass(hInstance);

	if (!InitInstance(hInstance, nCmdShow))
	{
		return FALSE;
	}

	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WACOMMT_SCRIBBLE));

	while (GetMessage(&msg, NULL, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	if (g_noConfidenceBrush) DeleteObject(g_noConfidenceBrush);
	if (g_confidenceBrush) DeleteObject(g_confidenceBrush);
	if (g_positionOnlyBrush) DeleteObject(g_positionOnlyBrush);
	if (g_noConfidencePen) DeleteObject(g_noConfidencePen);
	if (g_confidencePen) DeleteObject(g_confidencePen);

	DeleteCriticalSection(&g_graphicsCriticalSection);

	return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static POINT s_ptMouseOld = { 0 };

	switch (message)
	{
	case WM_CREATE:
	{
		WINDOWINFO appWindowInfo = { 0 };
		appWindowInfo.cbSize = sizeof(appWindowInfo);
		GetWindowInfo(hWnd, &appWindowInfo);
		g_clientRect = appWindowInfo.rcClient;

		int w = g_clientRect.right - g_clientRect.left;
		int h = g_clientRect.bottom - g_clientRect.top;
		Layout(w, h);

		int pw = RW(rPaper);
		int ph = RH(rPaper);
		if (pw > 0 && ph > 0)
		{
			g_gpuInk.Initialize(hWnd, pw, ph);
		}

		for (int idx = 0; idx < NUM_HPENS; idx++)
		{
			g_hPenMap[idx] = CreatePen(PS_SOLID, 2, RGB(rand() % 255, rand() % 255, rand() % 255));
		}

		if (!OpenTabletContexts(hWnd))
		{
			ShowError("Could Not Open Wintab Tablet Contexts.");
		}
		break;
	}

	case WM_CLOSE:
	{
		for (int idx = 0; idx < NUM_HPENS; idx++)
		{
			DeleteObject(g_hPenMap[idx]);
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	case WM_KEYDOWN:
	{
		switch (wParam)
		{
		case VK_ESCAPE:
		{
			ClearScreen();
			break;
		}
		case 'I':
		case 'i':
		{
			if (!g_hInkWnd) CreateInkWindow();
			else DestroyInkWindow();
			break;
		}
		default:
			break;
		}
		break;
	}

	case WM_COMMAND:
	{
		WORD wmId = LOWORD(wParam);
		switch (wmId)
		{
		case IDM_ABOUT:
		{
			if (!IsWindow(g_hWndAbout))
			{
				CreateDialog(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
				ShowWindow(g_hWndAbout, SW_SHOW);
			}
			break;
		}
		case IDM_ERASE:
		{
			ClearScreen();
			break;
		}
		case IDM_EXIT:
		{
			DestroyWindow(hWnd);
			break;
		}
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	}

	case WM_ERASEBKGND:
		return 1; // 背景消去を無効化してチラツキ（ちらつき）を完全に防止

	case WM_PAINT:
	{
		PAINTSTRUCT ps = { 0 };
		HDC hdc = BeginPaint(hWnd, &ps);

		int w = g_clientRect.right - g_clientRect.left;
		int h = g_clientRect.bottom - g_clientRect.top;

		if (w > 0 && h > 0)
		{
			// メモリDCによるダブルバッファリング描画
			HDC memDC = CreateCompatibleDC(hdc);
			HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
			HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

			if (g_screen == Screen::Select)
			{
				DrawSelection(memDC, w, h);
			}
			else
			{
				DrawStudioChrome(memDC, w, h);

				// GPU 墨汁を paper 領域に合成描画
				g_gpuInk.Render(memDC, rPaper.left, rPaper.top);

				// 赤い補助線（格子）を墨汁の上に薄く描画して確実に表示させる
				DrawPaperGrid(memDC);
			}

			// メモリDCから画面へ一括転送 (フリッカーフリー)
			BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

			SelectObject(memDC, oldBmp);
			DeleteObject(memBmp);
			DeleteDC(memDC);
		}

		EndPaint(hWnd, &ps);
		break;
	}

	case WM_LBUTTONDOWN:
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

		if (g_screen == Screen::Select)
		{
			if (PtIn(rSelSmall, pt)) Start(Mode::Small);
			else if (PtIn(rSelMedium, pt)) Start(Mode::Medium);
			else if (PtIn(rSelLarge, pt)) Start(Mode::Large);
			else if (PtIn(rSelAll, pt)) Start(Mode::All);
		}
		else
		{
			if (PtIn(rBack, pt)) Back();
			else if (PtIn(rNewPaper, pt)) ClearScreen();
			else if (PtIn(rGrid, pt))
			{
				g_grid = (g_grid + 1) % 3;
				InvalidateRect(hWnd, NULL, FALSE);
			}
			else if (PtIn(rToolBrush, pt)) g_tool = Tool::BrushTool;
			else if (PtIn(rToolEraser, pt)) g_tool = Tool::EraserTool;
			else if (PtIn(rToolHand, pt)) g_tool = Tool::HandTool;
			else if (PtIn(rSubSmall, pt)) g_brush = Brush::Small;
			else if (PtIn(rSubMedium, pt)) g_brush = Brush::Medium;
			else if (PtIn(rSubLarge, pt)) g_brush = Brush::Large;
			else if (PtIn(rPaper, pt))
			{
				SetCapture(hWnd);
				POINT paperPt = { pt.x - rPaper.left, pt.y - rPaper.top };
				s_ptMouseOld = paperPt;
				InvalidateRect(hWnd, NULL, FALSE);
			}
		}
		break;
	}

	case WM_MOUSEMOVE:
	{
		if ((wParam & MK_LBUTTON) && g_screen == Screen::Studio)
		{
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			if (PtIn(rPaper, pt))
			{
				POINT paperPt = { pt.x - rPaper.left, pt.y - rPaper.top };
				double dx = (double)(paperPt.x - s_ptMouseOld.x);
				double dy = (double)(paperPt.y - s_ptMouseOld.y);
				double dist = std::hypot(dx, dy);

				if (dist > 0.1)
				{
					double baseWidth = 36.0;
					if (g_brush == Brush::Small) baseWidth = 16.0;
					else if (g_brush == Brush::Large) baseWidth = 56.0;

					double pressureFactor = 0.5;
					double haraiPower = 2.7 + 0.5 * (std::min)(dist, 10.0);
					double haraiFactor = std::pow(pressureFactor, haraiPower);
					double rawWidth = baseWidth * haraiFactor;
					int penWidth = (int)std::round(rawWidth);
					if (penWidth < 1) penWidth = 1;

					g_gpuInk.DrawSegment(s_ptMouseOld, paperPt, (double)penWidth, 255);
				}
				s_ptMouseOld = paperPt;
				InvalidateRect(hWnd, NULL, FALSE);
			}
		}
		break;
	}

	case WM_LBUTTONUP:
	{
		if (g_screen == Screen::Studio)
		{
			g_gpuInk.EndStroke();
			ReleaseCapture();
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;
	}

	case WM_SIZE:
	case WM_MOVE:
	{
		WINDOWINFO appWindowInfo = { 0 };
		appWindowInfo.cbSize = sizeof(appWindowInfo);
		GetWindowInfo(hWnd, &appWindowInfo);
		g_clientRect = appWindowInfo.rcClient;

		int w = g_clientRect.right - g_clientRect.left;
		int h = g_clientRect.bottom - g_clientRect.top;
		if (w > 0 && h > 0)
		{
			Layout(w, h);
			int pw = RW(rPaper);
			int ph = RH(rPaper);
			if (pw > 0 && ph > 0)
			{
				g_gpuInk.Resize(pw, ph);
			}
		}
		break;
	}

	case WT_PACKET:
	{
		HCTX hCtx = (HCTX)lParam;
		if (g_contextMap.count(hCtx) == 0 && !g_contextMap.empty())
		{
			hCtx = g_contextMap.begin()->first;
		}

		PACKET pkt = { 0 };
		if (gpWTPacket(hCtx, static_cast<int>(wParam), &pkt))
		{
			static POINT ptOld = { 0 };
			static POINT ptNew = { 0 };
			static UINT prsOld = 0;
			static UINT prsNew = 0;
			static ORIENTATION ortOld = { 0 };
			static ORIENTATION ortNew = { 0 };
			static double s_smoothedWidth = 0.0;
			static bool s_strokeActive = false;

			ptNew.x = pkt.pkX;
			ptNew.y = pkt.pkY;
			prsNew = pkt.pkNormalPressure;
			ortNew = pkt.pkOrientation;

			g_gpuInk.UpdatePenZ(static_cast<int>(pkt.pkZ),
				static_cast<int>(ortNew.orAltitude),
				static_cast<int>(ortNew.orAzimuth),
				prsNew == 0);

			double maxPrs = (g_contextMap.count(hCtx) > 0 && g_contextMap[hCtx].maxPressure > 0)
				? (double)g_contextMap[hCtx].maxPressure
				: (g_maxPressure > 0 ? (double)g_maxPressure : 1024.0);

			UINT minPrsThreshold = static_cast<UINT>(maxPrs * 0.01);
			if (prsNew <= minPrsThreshold)
			{
				prsNew = 0;
			}

			if (prsNew > 0)
			{
				POINT clientPt = { ptNew.x, ptNew.y };
				if (g_openSystemContext)
				{
					ScreenToClient(hWnd, &clientPt);
				}

				POINT oldClientPt = { ptOld.x, ptOld.y };
				if (g_openSystemContext)
				{
					ScreenToClient(hWnd, &oldClientPt);
				}

				// Paper 座標系へ変換
				POINT paperPt = { clientPt.x - rPaper.left, clientPt.y - rPaper.top };
				POINT oldPaperPt = { oldClientPt.x - rPaper.left, oldClientPt.y - rPaper.top };

				if (!s_strokeActive || prsOld == 0 || !g_gpuInk.IsInStroke())
				{
					s_strokeActive = true;
					ptOld = ptNew;
					oldPaperPt = paperPt;
				}

				double dx = (double)(paperPt.x - oldPaperPt.x);
				double dy = (double)(paperPt.y - oldPaperPt.y);
				double dist = std::hypot(dx, dy);

				if (dist > 50.0)
				{
					oldPaperPt = paperPt;
					ptOld = ptNew;
					dist = 0.0;
				}

				double pressureFactor = (double)prsNew / (maxPrs > 0 ? maxPrs : 1.0);
				if (pressureFactor > 1.0) pressureFactor = 1.0;
				if (pressureFactor < 0.0) pressureFactor = 0.0;

				double altitudeDegrees = (double)ortNew.orAltitude / 10.0;
				double azimuthRad = ((double)ortNew.orAzimuth / 10.0) * (3.14159265358979323846 / 180.0);
				double tiltFactor = (90.0 - altitudeDegrees) / 90.0;
				if (tiltFactor < 0.0) tiltFactor = 0.0;

				double moveAngle = (dist > 1e-5) ? std::atan2(dy, dx) : 0.0;

				double tomeFactor = 1.0 + 0.1 * (1.0 - (std::min)(dist / 3.0, 1.0)) * std::pow(pressureFactor, 0.8);
				double haraiPower = 2.7 + 0.5 * (std::min)(dist, 10.0);
				double haraiFactor = std::pow(pressureFactor, haraiPower);
				double angleDiff = std::sin(azimuthRad - (moveAngle + 1.57079632679));
				double angleFactor = 1.0 + 0.3 * std::abs(angleDiff);

				double baseMaxWidth = 36.0;
				if (g_brush == Brush::Small) baseMaxWidth = 16.0;
				else if (g_brush == Brush::Large) baseMaxWidth = 56.0;

				double rawWidth = baseMaxWidth * haraiFactor * tomeFactor * angleFactor * (1.0 + tiltFactor * 0.6);

				if (dist == 0.0 || !g_gpuInk.IsInStroke())
				{
					s_smoothedWidth = rawWidth;
				}
				else
				{
					const double alpha = 0.3;
					s_smoothedWidth = s_smoothedWidth * (1.0 - alpha) + rawWidth * alpha;
				}

				g_gpuInk.DrawSegment(oldPaperPt, paperPt, s_smoothedWidth, 255);
			}
			else
			{
				s_strokeActive = false;
				if (g_gpuInk.IsInStroke())
				{
					g_gpuInk.EndStroke();
				}
			}

			ptOld = ptNew;
			prsOld = prsNew;
			ortOld = ortNew;

			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;
	}

	case WT_INFOCHANGE:
	case WM_DISPLAYCHANGE:
	{
		CloseTabletContexts();
		OpenTabletContexts(hWnd);
		break;
	}

	case WT_PROXIMITY:
	{
		bool inRange = (lParam != 0);
		if (!inRange)
		{
			g_gpuInk.UpdatePenZ(0, 0, 0, true);
			g_gpuInk.EndStroke();
		}
		if (g_hMonitorWnd && IsWindow(g_hMonitorWnd)) InvalidateRect(g_hMonitorWnd, NULL, FALSE);
		break;
	}

	case WM_DESTROY:
	{
		ReleaseDC(hWnd, g_hdc);
		CloseTabletContexts();
		Cleanup();
		PostQuitMessage(0);
		break;
	}

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
	{
		g_hWndAbout = hDlg;
		return 1;
	}
	case WM_COMMAND:
	{
		if ((LOWORD(wParam) == IDOK) || (LOWORD(wParam) == IDCANCEL))
		{
			DestroyWindow(hDlg);
			g_hWndAbout = NULL;
			return 1;
		}
		break;
	}
	}
	return 0;
}

void ClearScreen()
{
	g_gpuInk.Clear();
	InvalidateRect(g_mainWnd, NULL, FALSE);
}

bool OpenTabletContexts(HWND hWnd)
{
	if (!LoadWintab())
	{
		ShowError("Wintab not available");
		return false;
	}

	if (!gpWTInfoA(0, 0, NULL))
	{
		ShowError("WinTab Services Not Available.");
		return false;
	}

	int ctxIndex = 0;
	int gnOpenContexts = 0;
	int gnAttachedDevices = 0;

	CloseTabletContexts();

	gpWTInfoA(WTI_INTERFACE, IFC_NDEVICES, &gnAttachedDevices);

	do
	{
		int foundCtx = 0;
		LOGCONTEXTA lcMine = { 0 };
		AXIS tabletX = { 0 };
		AXIS tabletY = { 0 };
		AXIS Pressure = { 0 };
		AXIS axisZ = { 0 };

		if (g_openSystemContext)
		{
			foundCtx = gpWTInfoA(WTI_DEFSYSCTX, 0, &lcMine);
		}
		else
		{
			foundCtx = gpWTInfoA(WTI_DDCTXS + ctxIndex, 0, &lcMine);
		}

		if (foundCtx > 0)
		{
			UINT result = 0;
			gpWTInfoA(WTI_DEVICES + ctxIndex, DVC_HARDWARE, &result);
			bool displayTablet = (result & HWC_INTEGRATED) != 0;

			lcMine.lcPktData = PACKETDATA;
			lcMine.lcOptions |= CXO_MESSAGES | CXO_SYSTEM;
			lcMine.lcPktMode = PACKETMODE;
			lcMine.lcMoveMask = PACKETDATA;
			lcMine.lcBtnUpMask = lcMine.lcBtnDnMask;

			if (gpWTInfoA(WTI_DEVICES + ctxIndex, DVC_X, &tabletX) != sizeof(AXIS))
			{
				ctxIndex++;
				if (g_openSystemContext) break;
				continue;
			}
			gpWTInfoA(WTI_DEVICES + ctxIndex, DVC_Y, &tabletY);
			gpWTInfoA(WTI_DEVICES + ctxIndex, DVC_NPRESSURE, &Pressure);
			gpWTInfoA(WTI_DEVICES + ctxIndex, DVC_Z, &axisZ);

			g_maxPressure = Pressure.axMax;

			lcMine.lcOutExtY = -lcMine.lcOutExtY;

			HCTX hCtx = gpWTOpenA(hWnd, (LPLOGCONTEXT)&lcMine, TRUE);
			if (hCtx)
			{
				TabletInfo info = { Pressure.axMax, RGB(0,0,0) };
				sprintf_s(info.name, sizeof(info.name), "Tablet %i", ctxIndex);
				info.tabletXExt = tabletX.axMax;
				info.tabletYExt = tabletY.axMax;
				info.displayTablet = displayTablet;
				info.maxZ = axisZ.axMax;
				g_contextMap[hCtx] = info;
				gnOpenContexts++;
			}
		}
		else
		{
			break;
		}

		if (g_openSystemContext) break;
		ctxIndex++;
	} while (true);

	return gnOpenContexts > 0;
}

void CloseTabletContexts(void)
{
	for (auto& pair : g_contextMap)
	{
		if (pair.first != nullptr)
		{
			gpWTClose(pair.first);
		}
	}
	g_contextMap.clear();
}

void Cleanup(void)
{
	UnloadWintab();
	g_gpuInk.Clear();
}
