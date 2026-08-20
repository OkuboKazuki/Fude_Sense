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

    // 筆の墨量（0.0〜1.0）：硯に浸けると満タン、書くほど消耗する
    double g_brushInk = 0.0;    // 初期は0（最初に硯に浸ける必要あり）
    bool g_strokeStarted = false; // 現在のストロークの開始フラグ

    enum class TbButton { None, Brush, Shitajiki, Paper, Eraser, Hand, NewPaper, InkRefill };

    enum class LeftTab { Brush, Shitajiki, Paper };

    // 半紙の種類（実寸の縦横比に対応）
    enum class PaperType {
        Hanshi,    // 半紙   242x333  W:H=1:1.38
        Jofuku,    // 条幅   350x680  W:H=1:1.94
        Shikishi,  // 色紙   272x242  W:H=1:0.89 (横長)
        Tanzaku,   // 短冊   60x180   W:H=1:3.0
        Zenshi,    // 全紙   350x680  W:H=1:1.94
        Gasenshi   // 画仙紙 300x600  W:H=1:2.0
    };
    enum class GridPattern {
        None,           // なし
        Cross1,         // 1字 (十字補助)
        Div2,           // 2文字 (上下2段)
        Grid4,          // 4文字 (2x2 田の字)
        Grid6,          // 6文字 (2x3)
        Grid8,          // 8文字 (2x4)
        Lines3,         // 3行 縦罫線
        Lines4,         // 4行 縦罫線
        StarGrid        // 米字格 (対角線入り)
    };
    enum class GridColorTheme {
        RedLine,        // 定番朱赤線
        WhiteLine,      // 高級毛氈白線
        InkGray         // 薄墨点線
    };

    Screen g_screen = Screen::Studio;
    Brush g_brush = Brush::Medium;
    Mode g_mode = Mode::All;
    Tool g_tool = Tool::BrushTool;
    int g_grid = 1;
    double g_ink = INK_MAX;
    double g_brushHardness = 1.0; // 筆の硬さ (0.5: 非常に柔らかい ~ 2.0: 非常に硬い)
    bool g_isDraggingHardness = false; // スライドバードラッグ中

    LeftTab g_leftTab = LeftTab::Brush;
    GridPattern g_gridPattern = GridPattern::Grid4;
    GridColorTheme g_gridColor = GridColorTheme::RedLine;

    PaperType g_paperType = PaperType::Hanshi;
    double g_paperZoom = 1.0;      // 1.0〜4.0
    int g_paperPanX   = 0;         // 表示オフセットX(px)
    int g_paperPanY   = 0;         // 表示オフセットY(px)
    bool g_isPanning  = false;     // 手のひらツールでパン中
    POINT g_panStart  = {0, 0};    // パン開始点

    TbButton g_hoverTb = TbButton::None;
    int g_hoverSub = 0;

    RECT rTop{}, rTaskbar{}, rSub{}, rCanvasArea{}, rRight{}, rStatus{};
    RECT rTbLogo{}, rTbBrush{}, rTbShitajiki{}, rTbPaper{}, rTbEraser{}, rTbHand{}, rTbNewPaper{}, rTbInk{};
    RECT rPaper{};        // 実際の表示座標（ズーム適用後）
    RECT rPaperBase{};    // ズーム1.0のときの基準座標
    RECT rSubSmall{}, rSubMedium{}, rSubLarge{};
    RECT rGridTile[9]{};
    RECT rColorBtn[3]{};
    RECT rPaperTile[6]{}; // 半紙種類タイル
    RECT rInkStoneLarge{}, rInkRefillBtn{};
    RECT rHardnessTrack{};
    int g_hoverInkStone = 0;

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
        return L"全筆モード";
    }

    HFONT Font(int size, int weight = FW_NORMAL, const wchar_t* face = L"Yu Gothic UI") {
        return CreateFontW(size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, face);
    }
    void Fill(HDC dc, const RECT& r, COLORREF color) {
        HBRUSH b = CreateSolidBrush(color); if (!b) return; FillRect(dc, &r, b); DeleteObject(b);
    }
    void Box(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int bw = 1, int round = 6) {
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
        const int tbW = 116, topH = 54, statusH = 26, subW = 360, rightW = 370;
        rTaskbar = { 0, 0, tbW, h - statusH };
        rTop = { tbW, 0, w, topH };
        rSub = { tbW, topH, tbW + subW, h - statusH };
        rRight = { w - rightW, topH, w, h - statusH };
        rCanvasArea = { rSub.right, topH, rRight.left, h - statusH };
        rStatus = { 0, h - statusH, w, h };

        // Left Taskbar items (116px width, items are 96x68px)
        rTbLogo = { 10, 8, tbW - 10, 54 };
        int tbY = 66;
        rTbBrush     = { 10, tbY, tbW - 10, tbY + 66 }; tbY += 74;
        rTbShitajiki = { 10, tbY, tbW - 10, tbY + 66 }; tbY += 74;
        rTbEraser    = { 10, tbY, tbW - 10, tbY + 66 }; tbY += 74;
        rTbHand      = { 10, tbY, tbW - 10, tbY + 66 }; tbY += 84; // 区切りマージン
        rTbNewPaper  = { 10, tbY, tbW - 10, tbY + 66 }; tbY += 74;
        rTbInk       = { 10, tbY, tbW - 10, tbY + 66 }; tbY += 74;

        // Subtool Panel Items (筆用: 360px幅、ゆったりとした大型カード)
        rSubSmall  = { rSub.left + 16, topH + 48, rSub.right - 16, topH + 132 };
        rSubMedium = { rSub.left + 16, topH + 144, rSub.right - 16, topH + 228 };
        rSubLarge  = { rSub.left + 16, topH + 240, rSub.right - 16, topH + 324 };
        rHardnessTrack = { rSub.left + 16, topH + 348, rSub.right - 16, topH + 356 };

        // Subtool Panel Items (下敷き用: 360px幅、大型タイル2列配置)
        int tileW = (subW - 44) / 2; // 約 158px
        int tileH = 56;
        int gx0 = rSub.left + 16;
        int gy0 = topH + 48;

        for (int i = 0; i < 9; ++i) {
            int col = i % 2;
            int row = i / 2;
            if (i == 8) {
                rGridTile[i] = { gx0, gy0 + row * (tileH + 8), gx0 + tileW * 2 + 12, gy0 + row * (tileH + 8) + tileH };
            } else {
                rGridTile[i] = { gx0 + col * (tileW + 12), gy0 + row * (tileH + 8), gx0 + col * (tileW + 12) + tileW, gy0 + row * (tileH + 8) + tileH };
            }
        }

        int colorY = gy0 + 5 * (tileH + 8) + 24;
        int colBtnW = (subW - 44) / 3;
        for (int i = 0; i < 3; ++i) {
            rColorBtn[i] = { gx0 + i * (colBtnW + 6), colorY, gx0 + i * (colBtnW + 6) + colBtnW, colorY + 42 };
        }

        // Canvas / Paper
        int aw = std::max(1, RW(rCanvasArea) - 48), ah = std::max(1, RH(rCanvasArea) - 48);
        int paper = std::max(1, std::min(aw, ah));
        int px = rCanvasArea.left + (RW(rCanvasArea) - paper) / 2;
        int py = rCanvasArea.top + (RH(rCanvasArea) - paper) / 2;
        rPaper = { px, py, px + paper, py + paper };

        // Right side: 超大型本格硯（半紙の垂直中央に合わせてどっしりと配置）
        int stoneW = std::min(324, RW(rRight) - 40);
        int stoneH = (int)(stoneW * 1.54); // 約 500px
        int stoneX = rRight.left + (RW(rRight) - stoneW) / 2;

        // 半紙の垂直中心 (py + paper / 2) に硯の中心を合わせる
        int paperCenterY = py + paper / 2;
        int stoneY = paperCenterY - (stoneH + 60) / 2;
        stoneY = std::max(topH + 36, stoneY);

        rInkStoneLarge = { stoneX, stoneY, stoneX + stoneW, stoneY + stoneH };
        rInkRefillBtn = { stoneX, stoneY + stoneH + 16, stoneX + stoneW, stoneY + stoneH + 58 };
    }

    void DrawPanelTitle(HDC dc, RECT r, const wchar_t* s) {
        RECT head = { r.left, r.top, r.right, r.top + 28 }; Fill(dc, head, RGB(38, 40, 46));
        HPEN p = CreatePen(PS_SOLID, 1, RGB(26, 28, 33)); HPEN op = (HPEN)SelectObject(dc, p);
        MoveToEx(dc, head.left, head.bottom - 1, nullptr); LineTo(dc, head.right, head.bottom - 1);
        SelectObject(dc, op); DeleteObject(p);
        HFONT f = Font(13, FW_BOLD); RECT tr = { head.left + 10, head.top, head.right - 6, head.bottom };
        Text(dc, tr, s, f, RGB(215, 218, 224)); DeleteObject(f);
    }

    void DrawTop(HDC dc, int w) {
        Fill(dc, rTop, RGB(30, 32, 38));
        RECT menu = { rTop.left, 0, w, 24 }; Fill(dc, menu, RGB(24, 25, 30));
        HFONT f = Font(13); RECT t = { rTop.left + 12, 0, rTop.left + 420, 24 };
        Text(dc, t, L"ファイル　編集　表示　練習　ウィンドウ　ヘルプ", f, RGB(195, 198, 204));
        
        RECT badge = { rTop.left + 12, 28, rTop.left + 116, 49 }; 
        Box(dc, badge, RGB(48, 52, 62), RGB(70, 75, 88), 1, 4);
        Center(dc, badge, ModeName(), f, RGB(235, 238, 242));

        RECT clear = { rTop.left + 126, 28, rTop.left + 212, 49 };
        Box(dc, clear, RGB(44, 47, 56), RGB(64, 68, 78), 1, 4); 
        Center(dc, clear, L"半紙を新調", f, RGB(210, 214, 220));

        DeleteObject(f);
    }

    void DrawTbButton(HDC dc, RECT r, const wchar_t* icon, const wchar_t* label, bool active, bool hover) {
        COLORREF bg, border, textMain, textSub;
        if (active) {
            bg = RGB(36, 56, 88);
            border = RGB(70, 115, 180);
            textMain = RGB(255, 255, 255);
            textSub = RGB(160, 205, 255);
        } else if (hover) {
            bg = RGB(42, 46, 56);
            border = RGB(68, 74, 88);
            textMain = RGB(245, 247, 250);
            textSub = RGB(175, 182, 195);
        } else {
            bg = RGB(26, 28, 34);
            border = RGB(38, 41, 50);
            textMain = RGB(175, 180, 190);
            textSub = RGB(115, 120, 130);
        }

        Box(dc, r, bg, border, 1, 6);

        // Active vertical indicator bar on the left
        if (active) {
            RECT ind = { r.left - 5, r.top + 10, r.left - 1, r.bottom - 10 };
            Fill(dc, ind, RGB(59, 130, 246));
        }

        HFONT fIcon = Font(26, FW_BOLD);
        HFONT fLabel = Font(13, FW_NORMAL);

        RECT rIcon = { r.left, r.top + 6, r.right, r.top + 40 };
        RECT rLabel = { r.left, r.top + 40, r.right, r.bottom - 6 };

        Center(dc, rIcon, icon, fIcon, textMain);
        Center(dc, rLabel, label, fLabel, textSub);

        DeleteObject(fIcon);
        DeleteObject(fLabel);
    }

    void DrawTaskbar(HDC dc, int h) {
        Fill(dc, rTaskbar, RGB(20, 21, 26));

        // Right border line
        HPEN pBorder = CreatePen(PS_SOLID, 1, RGB(36, 38, 46));
        HPEN op = (HPEN)SelectObject(dc, pBorder);
        MoveToEx(dc, rTaskbar.right - 1, rTaskbar.top, nullptr);
        LineTo(dc, rTaskbar.right - 1, rTaskbar.bottom);
        SelectObject(dc, op);
        DeleteObject(pBorder);

        // Top App Logo Badge ("書")
        Box(dc, rTbLogo, RGB(35, 38, 48), RGB(180, 150, 85), 1, 8);
        HFONT fLogo = Font(26, FW_BOLD);
        Center(dc, rTbLogo, L"書", fLogo, RGB(245, 215, 130));
        DeleteObject(fLogo);

        // Divider
        HPEN pDiv = CreatePen(PS_SOLID, 1, RGB(38, 40, 48));
        op = (HPEN)SelectObject(dc, pDiv);
        MoveToEx(dc, 16, 62, nullptr);
        LineTo(dc, rTaskbar.right - 16, 62);
        SelectObject(dc, op);
        DeleteObject(pDiv);

        // Buttons
        DrawTbButton(dc, rTbBrush, L"筆", L"Brush", g_leftTab == LeftTab::Brush, g_hoverTb == TbButton::Brush);
        DrawTbButton(dc, rTbShitajiki, L"敷", L"Grid", g_leftTab == LeftTab::Shitajiki, g_hoverTb == TbButton::Shitajiki);
        DrawTbButton(dc, rTbEraser, L"消", L"Eraser", g_tool == Tool::EraserTool, g_hoverTb == TbButton::Eraser);
        DrawTbButton(dc, rTbHand, L"手", L"Pan", g_tool == Tool::HandTool, g_hoverTb == TbButton::Hand);

        DrawTbButton(dc, rTbNewPaper, L"紙", L"New", false, g_hoverTb == TbButton::NewPaper);
        DrawTbButton(dc, rTbInk, L"墨", L"Refill", false, g_hoverTb == TbButton::InkRefill);
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

        // Brush Tip Preview Stroke on the left
        RECT tipBox = { r.left + 12, r.top + 16, r.left + 46, r.bottom - 16 };
        int cy = (tipBox.top + tipBox.bottom) / 2;
        HPEN sp = CreatePen(PS_SOLID, (int)(strokeWidth * 1.5 + 1), active ? RGB(100, 180, 255) : RGB(160, 165, 175));
        HPEN osp = (HPEN)SelectObject(dc, sp);
        MoveToEx(dc, tipBox.left + 4, cy, nullptr);
        LineTo(dc, tipBox.right - 4, cy);
        SelectObject(dc, osp);
        DeleteObject(sp);

        HFONT f1 = Font(18, FW_BOLD), f2 = Font(14);
        RECT a = { r.left + 54, r.top + 14, r.right - 12, r.top + 44 };
        RECT b = { r.left + 54, r.top + 44, r.right - 12, r.bottom - 12 };
        Text(dc, a, name, f1, active ? RGB(255, 255, 255) : RGB(230, 233, 238));
        Text(dc, b, detail, f2, active ? RGB(170, 205, 245) : RGB(150, 155, 165));
        DeleteObject(f1);
        DeleteObject(f2);
    }

    void DrawGridTileCard(HDC dc, RECT r, const wchar_t* title, const wchar_t* sub, bool active, bool hover) {
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
        HFONT f1 = Font(16, FW_BOLD), f2 = Font(12);
        RECT r1 = { r.left + 4, r.top + 6, r.right - 4, r.top + 30 };
        RECT r2 = { r.left + 4, r.top + 30, r.right - 4, r.bottom - 4 };
        Center(dc, r1, title, f1, textMain);
        Center(dc, r2, sub, f2, textSub);
        DeleteObject(f1);
        DeleteObject(f2);
    }

    void DrawColorThemeBtn(HDC dc, RECT r, const wchar_t* label, COLORREF dotColor, bool active, bool hover) {
        COLORREF bg = active ? RGB(38, 54, 82) : (hover ? RGB(40, 44, 52) : RGB(28, 30, 36));
        COLORREF border = active ? RGB(70, 120, 195) : (hover ? RGB(66, 72, 86) : RGB(42, 45, 54));
        Box(dc, r, bg, border, 1, 6);

        // Color dot
        HBRUSH db = CreateSolidBrush(dotColor);
        HBRUSH odb = (HBRUSH)SelectObject(dc, db);
        HPEN dp = CreatePen(PS_SOLID, 1, active ? RGB(255, 255, 255) : RGB(100, 105, 115));
        HPEN odp = (HPEN)SelectObject(dc, dp);
        Ellipse(dc, r.left + 10, r.top + (RH(r) - 14) / 2, r.left + 24, r.top + (RH(r) + 14) / 2);
        SelectObject(dc, odp);
        SelectObject(dc, odb);
        DeleteObject(dp);
        DeleteObject(db);

        HFONT f = Font(13, active ? FW_BOLD : FW_NORMAL);
        RECT tr = { r.left + 26, r.top, r.right - 4, r.bottom };
        Center(dc, tr, label, f, active ? RGB(255, 255, 255) : RGB(200, 205, 215));
        DeleteObject(f);
    }

    void DrawSub(HDC dc) {
        Fill(dc, rSub, RGB(26, 28, 33)); 

        if (g_leftTab == LeftTab::Brush) {
            DrawPanelTitle(dc, rSub, L"筆設定 / サブツール");
            DrawSubCard(dc, rSubSmall, L"小筆", L"かな・名入れ・細線", 2, g_brush == Brush::Small, g_hoverSub == 1);
            DrawSubCard(dc, rSubMedium, L"中筆", L"標準的な楷書・行書", 5, g_brush == Brush::Medium, g_hoverSub == 2);
            DrawSubCard(dc, rSubLarge, L"大筆", L"作品・力強い大字", 9, g_brush == Brush::Large, g_hoverSub == 3);
        } else {
            DrawPanelTitle(dc, rSub, L"下敷き・升目設定");

            const wchar_t* titles[9] = {
                L"なし", L"1字 (十字)", L"2文字 (2段)",
                L"4文字 (田)", L"6文字 (2x3)", L"8文字 (2x4)",
                L"3行 罫線", L"4行 罫線", L"米字格 (放射対角線)"
            };
            const wchar_t* subs[9] = {
                L"無地半紙", L"中心ガイド", L"二文字熟語",
                L"四字熟語", L"六文字配列", L"八文字配列",
                L"行書・かな", L"条幅・古典", L"美しい骨格・臨書"
            };

            for (int i = 0; i < 9; ++i) {
                bool act = ((int)g_gridPattern == i);
                bool hov = (g_hoverSub == 10 + i);
                DrawGridTileCard(dc, rGridTile[i], titles[i], subs[i], act, hov);
            }

            // カラー設定タイトル
            RECT rColTitle = { rSub.left + 16, rColorBtn[0].top - 20, rSub.right - 16, rColorBtn[0].top };
            HFONT fct = Font(13, FW_BOLD);
            Text(dc, rColTitle, L"下敷き・罫線の配色", fct, RGB(170, 175, 185));
            DeleteObject(fct);

            DrawColorThemeBtn(dc, rColorBtn[0], L"朱赤", RGB(225, 80, 80), g_gridColor == GridColorTheme::RedLine, g_hoverSub == 30);
            DrawColorThemeBtn(dc, rColorBtn[1], L"白線", RGB(235, 238, 245), g_gridColor == GridColorTheme::WhiteLine, g_hoverSub == 31);
            DrawColorThemeBtn(dc, rColorBtn[2], L"薄墨", RGB(140, 145, 155), g_gridColor == GridColorTheme::InkGray, g_hoverSub == 32);
        }
    }

    void DrawLargeInkStone(HDC dc) {
        Fill(dc, rRight, RGB(24, 26, 31));
        DrawPanelTitle(dc, rRight, L"硯 (すずり) / 墨池");

        // 硯情報ヘッダー（銘・墨残量）
        HFONT fMeta = Font(14, FW_NORMAL);
        RECT rMeta1 = { rRight.left + 20, rInkStoneLarge.top - 26, rRight.right - 20, rInkStoneLarge.top - 4 };
        wchar_t buf[64];
        wsprintfW(buf, L"天然石・四五平長方硯　墨残量: %d%%", (int)(g_ink * 100.0));
        Text(dc, rMeta1, buf, fMeta, RGB(185, 190, 200));
        DeleteObject(fMeta);

        // 1. 硯の外枠（硯身・石肌・角丸）
        bool hover = (g_hoverInkStone != 0);
        COLORREF stoneBorder = hover ? RGB(70, 120, 190) : RGB(48, 52, 64);
        Box(dc, rInkStoneLarge, RGB(26, 28, 34), stoneBorder, 2, 12);

        // 外縁の内側立体彫り込み線
        RECT innerRim = { rInkStoneLarge.left + 10, rInkStoneLarge.top + 10, rInkStoneLarge.right - 10, rInkStoneLarge.bottom - 10 };
        Box(dc, innerRim, RGB(16, 17, 21), RGB(38, 41, 50), 1, 10);

        // 2. 墨池（ぼくち / 海）: 硯の上部窪み
        RECT rPool = { innerRim.left + 12, innerRim.top + 12, innerRim.right - 12, innerRim.top + (int)(RH(innerRim) * 0.36) };
        Box(dc, rPool, RGB(10, 11, 14), RGB(30, 33, 40), 1, 8);

        // 墨池内の墨汁（現在の墨量 g_ink に連動した満水・液面表現）
        double inkFrac = Clamp(g_ink / INK_MAX, 0.0, 1.0);
        if (inkFrac > 0.01) {
            int poolH = RH(rPool) - 6;
            int fillH = (int)(poolH * inkFrac);
            RECT rLiquid = { rPool.left + 4, rPool.bottom - 4 - fillH, rPool.right - 4, rPool.bottom - 4 };
            
            // 墨汁の黒漆色
            Fill(dc, rLiquid, RGB(5, 6, 8));

            // 液面の水面ハイライト反射光
            HPEN hp = CreatePen(PS_SOLID, 2, RGB(75, 95, 125));
            HPEN ohp = (HPEN)SelectObject(dc, hp);
            MoveToEx(dc, rLiquid.left + 12, rLiquid.top + 1, nullptr);
            LineTo(dc, rLiquid.right - 12, rLiquid.top + 1);
            SelectObject(dc, ohp);
            DeleteObject(hp);

            // 水面の光沢反射（小さな三日月 / スポット光）
            HBRUSH glb = CreateSolidBrush(RGB(130, 155, 190));
            HBRUSH ogb = (HBRUSH)SelectObject(dc, glb);
            HPEN gpen = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
            RoundRect(dc, rLiquid.left + 16, rLiquid.top + 4, rLiquid.left + 48, rLiquid.top + 9, 4, 4);
            SelectObject(dc, gpen);
            SelectObject(dc, ogb);
            DeleteObject(glb);
        }

        // 墨池の刻印
        HFONT fPool = Font(16, FW_BOLD);
        RECT rPoolText = { rPool.left, rPool.top + 8, rPool.right, rPool.top + 30 };
        Center(dc, rPoolText, L"墨 池 (海)", fPool, inkFrac > 0.4 ? RGB(90, 105, 130) : RGB(140, 145, 155));
        DeleteObject(fPool);

        // 3. 墨堂（ぼくどう / 陸）: 硯の中央〜下部
        RECT rLand = { innerRim.left + 12, innerRim.top + (int)(RH(innerRim) * 0.38), innerRim.right - 12, innerRim.bottom - 12 };
        Box(dc, rLand, RGB(24, 26, 32), RGB(34, 37, 46), 1, 8);

        // 墨堂の微細な研磨テクスチャ線
        HPEN tp = CreatePen(PS_SOLID, 1, RGB(30, 33, 40));
        HPEN otp = (HPEN)SelectObject(dc, tp);
        for (int y = rLand.top + 12; y < rLand.bottom - 12; y += 16) {
            MoveToEx(dc, rLand.left + 16, y, nullptr);
            LineTo(dc, rLand.right - 16, y);
        }
        SelectObject(dc, otp);
        DeleteObject(tp);

        // 墨堂の刻印
        HFONT fKanji = Font(18, FW_BOLD);
        RECT rLandText = { rLand.left, rLand.top + 14, rLand.right, rLand.top + 36 };
        Center(dc, rLandText, L"墨 堂 (陸)", fKanji, RGB(70, 75, 88));
        DeleteObject(fKanji);

        // 4. 硯下部の墨補充クイックボタン
        Box(dc, rInkRefillBtn, hover ? RGB(45, 68, 100) : RGB(34, 37, 46), hover ? RGB(70, 120, 190) : RGB(52, 57, 70), 1, 8);
        HFONT fBtn = Font(15, FW_BOLD);
        Center(dc, rInkRefillBtn, L"💧 硯をクリックして墨を補充", fBtn, hover ? RGB(255, 255, 255) : RGB(210, 215, 225));
        DeleteObject(fBtn);
    }

    void DrawPaperBackground(HDC dc) {
        HBRUSH pb = CreateSolidBrush(RGB(248, 247, 242));
        HPEN pp = CreatePen(PS_SOLID, 1, RGB(175, 172, 162));
        HBRUSH ob = (HBRUSH)SelectObject(dc, pb);
        HPEN op = (HPEN)SelectObject(dc, pp);
        Rectangle(dc, rPaper.left, rPaper.top, rPaper.right, rPaper.bottom);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(pp);
        DeleteObject(pb);
    }

    void DrawCross(HDC dc, int x, int y, int s) {
        MoveToEx(dc, x - s, y, nullptr); LineTo(dc, x + s, y);
        MoveToEx(dc, x, y - s, nullptr); LineTo(dc, x, y + s);
    }

    void DrawPaperGrid(HDC dc) {
        if (g_gridPattern == GridPattern::None) return;

        COLORREF lineColor = RGB(228, 90, 90);
        if (g_gridColor == GridColorTheme::WhiteLine) lineColor = RGB(220, 222, 230);
        else if (g_gridColor == GridColorTheme::InkGray) lineColor = RGB(160, 165, 175);

        HPEN gp = CreatePen(PS_SOLID, 1, lineColor);
        HPEN gpDash = CreatePen(PS_DOT, 1, lineColor);
        HPEN op = (HPEN)SelectObject(dc, gp);

        int left = rPaper.left;
        int top = rPaper.top;
        int right = rPaper.right;
        int bottom = rPaper.bottom;
        int w = RW(rPaper);
        int h = RH(rPaper);

        // 外枠マージン（半紙の端から内側に外枠を描画して下敷きの風格を出す）
        int m = std::max(6, w / 48);
        RECT rBorder = { left + m, top + m, right - m, bottom - m };
        MoveToEx(dc, rBorder.left, rBorder.top, nullptr);
        LineTo(dc, rBorder.right, rBorder.top);
        LineTo(dc, rBorder.right, rBorder.bottom);
        LineTo(dc, rBorder.left, rBorder.bottom);
        LineTo(dc, rBorder.left, rBorder.top);

        int bw = RW(rBorder);
        int bh = RH(rBorder);

        switch (g_gridPattern) {
        case GridPattern::Cross1:
        {
            int mx = rBorder.left + bw / 2;
            int my = rBorder.top + bh / 2;
            MoveToEx(dc, mx, rBorder.top, nullptr); LineTo(dc, mx, rBorder.bottom);
            MoveToEx(dc, rBorder.left, my, nullptr); LineTo(dc, rBorder.right, my);
            DrawCross(dc, rBorder.left + bw / 4, rBorder.top + bh / 4, 10);
            DrawCross(dc, rBorder.left + 3 * bw / 4, rBorder.top + bh / 4, 10);
            DrawCross(dc, rBorder.left + bw / 4, rBorder.top + 3 * bh / 4, 10);
            DrawCross(dc, rBorder.left + 3 * bw / 4, rBorder.top + 3 * bh / 4, 10);
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
            DrawCross(dc, rBorder.left + 3 * bw / 4, rBorder.top + 3 * bw / 4, 12);
            DrawCross(dc, rBorder.left + bw / 4, rBorder.top + 3 * bw / 4, 12);
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

    const wchar_t* GridPatternName(GridPattern p) {
        switch (p) {
        case GridPattern::None: return L"なし";
        case GridPattern::Cross1: return L"1字(十字)";
        case GridPattern::Div2: return L"2文字(2段)";
        case GridPattern::Grid4: return L"4文字(田)";
        case GridPattern::Grid6: return L"6文字";
        case GridPattern::Grid8: return L"8文字";
        case GridPattern::Lines3: return L"3行罫線";
        case GridPattern::Lines4: return L"4行罫線";
        case GridPattern::StarGrid: return L"米字格";
        }
        return L"なし";
    }

    void DrawRight(HDC dc) {
        DrawLargeInkStone(dc);
    }

    void DrawStatus(HDC dc, int w, int h) {
        Fill(dc, rStatus, RGB(18, 19, 23)); 
        HFONT f = Font(12); RECT t = { 12, h - 22, w - 12, h - 4 };
        wchar_t buf[192]; 
        int brushInkPct = (int)(g_brushInk * 100.0);
        if (g_brushInk <= 0.0) {
            wsprintfW(buf, L"状態: ★硯に筆を浸けてください | ツール: %s | 筆: %s | 下敷き: %s | 筆の墨量: 0%% | 硯残量: %d%%",
                g_tool == Tool::BrushTool ? L"筆" : (g_tool == Tool::EraserTool ? L"消しゴム" : L"手のひら"),
                BrushName(g_brush),
                GridPatternName(g_gridPattern),
                (int)(g_ink * 100.0));
            Text(dc, t, buf, f, RGB(255, 160, 60));  // 橙色で警告表示
        } else {
            wsprintfW(buf, L"状態: 準備完了 | ツール: %s | 筆: %s | 下敷き: %s | 筆の墨量: %d%% | 硯残量: %d%%",
                g_tool == Tool::BrushTool ? L"筆" : (g_tool == Tool::EraserTool ? L"消しゴム" : L"手のひら"),
                BrushName(g_brush),
                GridPatternName(g_gridPattern),
                brushInkPct,
                (int)(g_ink * 100.0));
            Text(dc, t, buf, f, RGB(150, 155, 165));
        }
        DeleteObject(f);
    }

    void DrawStudioChrome(HDC dc, int w, int h) {
        Fill(dc, rCanvasArea, RGB(20, 22, 26)); 
        DrawTaskbar(dc, h);
        DrawTop(dc, w); 
        DrawSub(dc);
        DrawPaperBackground(dc); 
        DrawRight(dc); 
        DrawStatus(dc, w, h);
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

			DrawStudioChrome(memDC, w, h);

			// GPU 墨汁を paper 領域に合成描画
			g_gpuInk.Render(memDC, rPaper.left, rPaper.top);

			// 赤い補助線（格子）を墨汁の上に薄く描画して確実に表示させる
			DrawPaperGrid(memDC);

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

		// 筆の硬さスライダー操作
		if (g_screen == Screen::Studio && g_leftTab == LeftTab::Brush)
		{
			RECT hitBox = rHardnessTrack;
			hitBox.top -= 6; hitBox.bottom += 6;
			if (PtIn(hitBox, pt))
			{
				g_isDraggingHardness = true;
				SetCapture(hWnd);

				double norm = (double)(pt.x - rHardnessTrack.left) / (double)RW(rHardnessTrack);
				norm = Clamp(norm, 0.0, 1.0);
				g_brushHardness = 0.5 + norm * (2.0 - 0.5);
				InvalidateRect(hWnd, NULL, FALSE);
				break;
			}
		}

		// Taskbar Button Clicks
		if (PtIn(rTbBrush, pt))
		{
			g_leftTab = LeftTab::Brush;
			g_tool = Tool::BrushTool;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		else if (PtIn(rTbShitajiki, pt))
		{
			g_leftTab = LeftTab::Shitajiki;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		else if (PtIn(rTbEraser, pt))
		{
			g_tool = Tool::EraserTool;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		else if (PtIn(rTbHand, pt))
		{
			g_tool = Tool::HandTool;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		else if (PtIn(rTbNewPaper, pt))
		{
			ClearScreen();
		}
		else if (PtIn(rTbInk, pt))
		{
			g_ink = INK_MAX;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		// Subtool Panel Items (Brush Tab)
		else if (g_leftTab == LeftTab::Brush && PtIn(rSubSmall, pt))
		{
			g_brush = Brush::Small;
			g_tool = Tool::BrushTool;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		else if (g_leftTab == LeftTab::Brush && PtIn(rSubMedium, pt))
		{
			g_brush = Brush::Medium;
			g_tool = Tool::BrushTool;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		else if (g_leftTab == LeftTab::Brush && PtIn(rSubLarge, pt))
		{
			g_brush = Brush::Large;
			g_tool = Tool::BrushTool;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		// Subtool Panel Items (Shitajiki Tab)
		else if (g_leftTab == LeftTab::Shitajiki)
		{
			bool changed = false;
			for (int i = 0; i < 9; ++i) {
				if (PtIn(rGridTile[i], pt)) {
					g_gridPattern = (GridPattern)i;
					changed = true;
					break;
				}
			}
			for (int i = 0; i < 3; ++i) {
				if (PtIn(rColorBtn[i], pt)) {
					g_gridColor = (GridColorTheme)i;
					changed = true;
					break;
				}
			}
			if (changed) {
				InvalidateRect(hWnd, NULL, FALSE);
			}
		}
		// Large InkStone Clicks (Ink Dip + Refill)
		else if (PtIn(rInkStoneLarge, pt) || PtIn(rInkRefillBtn, pt))
		{
			// 硯に筆を浸ける → 筆の墨量を満タンに補充
			g_brushInk = 1.0;
			g_ink = INK_MAX;
			g_strokeStarted = false;
			InvalidateRect(hWnd, &rRight, FALSE);
			InvalidateRect(hWnd, &rStatus, FALSE);
		}
		else if (PtIn(rPaper, pt))
		{
			SetCapture(hWnd);
			POINT paperPt = { pt.x - rPaper.left, pt.y - rPaper.top };
			s_ptMouseOld = paperPt;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;
	}

	case WM_MOUSEMOVE:
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

		if (g_isDraggingHardness)
		{
			double norm = (double)(pt.x - rHardnessTrack.left) / (double)RW(rHardnessTrack);
			norm = Clamp(norm, 0.0, 1.0);
			g_brushHardness = 0.5 + norm * (2.0 - 0.5);
			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}

		// Check Taskbar Hover
		TbButton oldHoverTb = g_hoverTb;
		if (PtIn(rTbBrush, pt)) g_hoverTb = TbButton::Brush;
		else if (PtIn(rTbShitajiki, pt)) g_hoverTb = TbButton::Shitajiki;
		else if (PtIn(rTbEraser, pt)) g_hoverTb = TbButton::Eraser;
		else if (PtIn(rTbHand, pt)) g_hoverTb = TbButton::Hand;
		else if (PtIn(rTbNewPaper, pt)) g_hoverTb = TbButton::NewPaper;
		else if (PtIn(rTbInk, pt)) g_hoverTb = TbButton::InkRefill;
		else g_hoverTb = TbButton::None;

		// Check Subtool Hover
		int oldHoverSub = g_hoverSub;
		g_hoverSub = 0;
		if (g_leftTab == LeftTab::Brush) {
			if (PtIn(rSubSmall, pt)) g_hoverSub = 1;
			else if (PtIn(rSubMedium, pt)) g_hoverSub = 2;
			else if (PtIn(rSubLarge, pt)) g_hoverSub = 3;
		} else {
			for (int i = 0; i < 9; ++i) {
				if (PtIn(rGridTile[i], pt)) {
					g_hoverSub = 10 + i;
					break;
				}
			}
			for (int i = 0; i < 3; ++i) {
				if (PtIn(rColorBtn[i], pt)) {
					g_hoverSub = 30 + i;
					break;
				}
			}
		}

		// Check InkStone Hover
		int oldHoverInkStone = g_hoverInkStone;
		if (PtIn(rInkStoneLarge, pt) || PtIn(rInkRefillBtn, pt)) g_hoverInkStone = 1;
		else g_hoverInkStone = 0;

		if (oldHoverTb != g_hoverTb || oldHoverSub != g_hoverSub || oldHoverInkStone != g_hoverInkStone)
		{
			InvalidateRect(hWnd, &rTaskbar, FALSE);
			InvalidateRect(hWnd, &rSub, FALSE);
			InvalidateRect(hWnd, &rRight, FALSE);
		}

		if (wParam & MK_LBUTTON)
		{
			if (PtIn(rPaper, pt))
			{
				// 墨が0なら描けない（硯に浸けてから書く必要あり）
				if (g_brushInk <= 0.0)
				{
					s_ptMouseOld = { pt.x - rPaper.left, pt.y - rPaper.top };
				}
				else
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

						// 墨量に応じたアルファ値（かすれ表現）
						double inkAlpha = Clamp(g_brushInk, 0.0, 1.0);
						int alpha = (int)(inkAlpha * inkAlpha * 255); // 二乗でリアルなかすれ感
						if (alpha < 20) alpha = 20; // 完全消去の手前で止める

						g_gpuInk.DrawSegment(s_ptMouseOld, paperPt, (double)penWidth, alpha);

						// 筆の墨量を消耗（大筆は早く減る）
						double consumeRate = (g_brush == Brush::Small) ? 0.00018 :
										 (g_brush == Brush::Large) ? 0.00055 : 0.00032;
						g_brushInk -= dist * consumeRate;
						if (g_brushInk < 0.0) g_brushInk = 0.0;
					}
					s_ptMouseOld = paperPt;
					InvalidateRect(hWnd, NULL, FALSE);
				}
			}
		}
		break;
	}

	case WM_LBUTTONUP:
	{
		if (g_isDraggingHardness)
		{
			g_isDraggingHardness = false;
			ReleaseCapture();
			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}

		g_gpuInk.EndStroke();
		ReleaseCapture();
		InvalidateRect(hWnd, NULL, FALSE);
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
			static double s_smoothedPressure = 0.0;
			static double s_smoothedWidth = 0.0;
			static bool s_strokeActive = false;

			ptNew.x = pkt.pkX;
			ptNew.y = pkt.pkY;
			UINT prsRaw = pkt.pkNormalPressure;
			ortNew = pkt.pkOrientation;

			g_gpuInk.UpdatePenZ(static_cast<int>(pkt.pkZ),
				static_cast<int>(ortNew.orAltitude),
				static_cast<int>(ortNew.orAzimuth),
				prsRaw == 0);

			double maxPrs = (g_contextMap.count(hCtx) > 0 && g_contextMap[hCtx].maxPressure > 0)
				? (double)g_contextMap[hCtx].maxPressure
				: (g_maxPressure > 0 ? (double)g_maxPressure : 1024.0);

			UINT minPrsThreshold = static_cast<UINT>(maxPrs * 0.01);
			if (prsRaw <= minPrsThreshold)
			{
				prsRaw = 0;
			}

			if (prsRaw > 0)
			{
				if (!s_strokeActive || prsOld == 0 || !g_gpuInk.IsInStroke() || s_smoothedPressure <= 0.0)
				{
					s_smoothedPressure = static_cast<double>(prsRaw);
				}
				else
				{
					double doublePrs = static_cast<double>(prsRaw);
					double alphaPrs = (doublePrs < s_smoothedPressure) ? 0.85 : 0.35;
					s_smoothedPressure = s_smoothedPressure * (1.0 - alphaPrs) + doublePrs * alphaPrs;
				}

				prsNew = static_cast<UINT>(std::round(s_smoothedPressure));
				if (prsNew == 0 && prsRaw > 0) prsNew = 1;
			}
			else
			{
				s_smoothedPressure = 0.0;
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
					oldPaperPt = paperPt;
				}

				double dx = (double)(paperPt.x - oldPaperPt.x);
				double dy = (double)(paperPt.y - oldPaperPt.y);
				double dist = std::hypot(dx, dy);

				if (dist > 50.0)
				{
					oldPaperPt = paperPt;
					dist = 0.0;
				}

				double pressureFactor = (double)prsNew / (maxPrs > 0 ? maxPrs : 1.0);
				if (pressureFactor > 1.0) pressureFactor = 1.0;
				if (pressureFactor < 0.0) pressureFactor = 0.0;

				// 筆の硬さ（ガンマ補正）を筆圧に適用
				pressureFactor = std::pow(pressureFactor, g_brushHardness);

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

				double startWidth = s_smoothedWidth;
				if (dist == 0.0 || !g_gpuInk.IsInStroke() || !s_strokeActive)
				{
					s_smoothedWidth = rawWidth;
					startWidth = rawWidth;
				}
				else
				{
					double alphaWidth = 0.3;
					if (rawWidth < s_smoothedWidth || dist > 3.0)
					{
						alphaWidth = 0.8;
					}
					s_smoothedWidth = s_smoothedWidth * (1.0 - alphaWidth) + rawWidth * alphaWidth;
				}

					// 墨が0なら描かない（硯に浸ける必要あり）
					if (g_brushInk <= 0.0) goto skip_draw;

					{
						// 墨量に応じたアルファ値（かすれ表現）
						double inkAlpha = Clamp(g_brushInk, 0.0, 1.0);
						int alpha = (int)(inkAlpha * inkAlpha * 255);
						if (alpha < 20) alpha = 20;

						g_gpuInk.DrawSegmentLinear(oldPaperPt, paperPt, startWidth, s_smoothedWidth, alpha);

						// 筆の墨量を消耗（筆サイズ・ストローク幅に応じて）
						double consumeRate = (g_brush == Brush::Small) ? 0.00015 :
										 (g_brush == Brush::Large) ? 0.00050 : 0.00028;
						g_brushInk -= dist * consumeRate;
						if (g_brushInk < 0.0) g_brushInk = 0.0;
						// 墨残量（硯）も少しずつ減らす
						g_ink -= dist * consumeRate * 0.05;
						if (g_ink < 0.0) g_ink = 0.0;
					}
					skip_draw:;
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
