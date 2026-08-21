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
#include <shlobj.h>
#include <iostream>
#include <vector>
#include <map>
#include <utility>
#include <algorithm>
#include <string>
#include <sstream>
#include <memory>
#include <crtdbg.h>

#include "WintabUtils.h"
#include "GpuInk.h"

#define PACKETDATA	(PK_X | PK_Y | PK_Z | PK_BUTTONS | PK_NORMAL_PRESSURE | PK_TANGENT_PRESSURE | PK_TIME | PK_ORIENTATION)
#define PACKETMODE	PK_BUTTONS
#include "pktdef.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

void Cleanup(void);

enum class Brush { Small, Medium, Large };

// Global Variables
HINSTANCE hInst = NULL;
std::wstring szTitle = L"SHUJI STUDIO - 習字制作ワークスペース";
std::wstring szWindowClass = L"WACOMMT_SCRIBBLE";
HWND g_mainWnd = NULL;
HDC g_hdc = NULL;
HWND g_hWndAbout = NULL;
int g_maxPressure = 1024;

RECT g_clientRect = { 0, 0, 0, 0 };

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

static GpuInk g_gpuInk;

HWND g_hInkWnd = NULL;
HWND g_hMonitorWnd = NULL;
static ATOM g_inkWndClassAtom = 0;
static ATOM g_monitorWndClassAtom = 0;

// UI System State
namespace {
    constexpr double INK_MAX = 1.0;

    enum class TbButton { None, NavToggle, Brush, Paper, Save, Otehon, InkRefill, ClearAll };
    enum class LeftTab { Brush, Paper, Save, Otehon };

    // 半紙の種類（実寸の縦横比に対応）
    enum class PaperType {
        Hanshi,    // 半紙   242x333  W:H=1:1.376
        Jofuku,    // 条幅   350x680  W:H=1:1.943
        Shikishi,  // 色紙   242x272  W:H=1:1.124
        Tanzaku    // 短冊   60x180   W:H=1:3.0
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

    Brush g_brush = Brush::Medium;
    double g_ink = INK_MAX;
    double g_brushHardness = 0.5; // 筆の硬さ (0.1: 超極軟 ~ 2.0: 非常に硬い)
    bool g_isDraggingHardness = false; // スライドバードラッグ中

    LeftTab g_leftTab = LeftTab::Brush;
    bool g_subPanelOpen = true; // 詳細パネルの開閉状態
    bool g_showClearConfirm = false; // 全消し確認画面の表示フラグ
    int  g_hoverClearModal = 0;      // 1: すべて消す, 2: キャンセル

    // お手本設定
    bool g_showOtehon = false;
    int g_selectedOtehon = 0; // 0: 永, 1: 夢, 2: 和, 3: 心, 4: 道, 5: 光, 6: 美, 7: 桜
    const wchar_t* g_otehonChars[] = { L"永", L"夢", L"和", L"心", L"道", L"光", L"美", L"桜" };
    double g_otehonOpacity = 0.35; // 0.1 ～ 1.0
    bool g_isDraggingOtehon = false;

    // 保存状態フィードバック
    std::wstring g_saveFeedback = L"";
    DWORD g_saveFeedbackTime = 0;

    GridPattern g_gridPattern = GridPattern::Grid4;
    GridColorTheme g_gridColor = GridColorTheme::RedLine;
    PaperType g_paperType = PaperType::Hanshi;

    TbButton g_hoverTb = TbButton::None;
    int g_hoverSub = 0;

    RECT rSub{}, rCanvasArea{}, rRight{}, rStatus{};
    RECT rTbNavToggle{}, rTbBrush{}, rTbPaper{}, rTbSave{}, rTbOtehon{};
    RECT rPaper{};        // 実際の表示座標（アスペクト比を維持して中央配置）
    RECT rSubSmall{}, rSubMedium{}, rSubLarge{};
    RECT rGridTile[9]{};
    RECT rColorBtn[3]{};
    RECT rPaperTile[4]{}; // 半紙種類タイル (半紙, 条幅, 色紙, 短冊)
    RECT rOtehonTile[8]{};
    RECT rOtehonToggleBtn{}, rOtehonOpacityTrack{};
    RECT rSaveBtnPng{}, rSaveBtnClip{};
    RECT rInkStoneLarge{}, rInkRefillBtn{}, rClearAllBtn{};
    RECT rHardnessTrack{};
    RECT rClearModalBox{}, rModalClearBtn{}, rModalCancelBtn{};
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
        const int statusH = 26;
        const int rightW = 280;

        rRight = { w - rightW, 0, w, h - statusH };
        rCanvasArea = { 0, 0, rRight.left, h - statusH };
        rStatus = { 0, h - statusH, w, h };

        // 1. フローティングメニュー & メニュー開閉ボタン
        if (!g_subPanelOpen) {
            // 閉じている状態: 画面左上にコンパクトな「<<<」ボタンのみ表示
            rTbNavToggle = { 16, 16, 76, 56 };
            rSub = { 0, 0, 0, 0 };
            rTbBrush = rTbPaper = rTbSave = rTbOtehon = { 0, 0, 0, 0 };
        } else {
            // 開いている状態: フローティングパネル
            int menuW = 390;
            int menuH = std::min(h - statusH - 32, 590);
            if (menuH < 420) menuH = 420;
            rSub = { 16, 16, 16 + menuW, 16 + menuH };

            // ヘッダーバー (閉じるボタン「>>>」 + 横並びタブ「筆」「紙」「保存」「お手本」)
            rTbNavToggle = { rSub.left + 10, rSub.top + 10, rSub.left + 62, rSub.top + 48 };

            int tabStartX = rTbNavToggle.right + 8;
            int tabAvailW = rSub.right - 10 - tabStartX;
            int tabW = (tabAvailW - 3 * 6) / 4;
            int tabH = 38;
            int tabY = rSub.top + 10;

            rTbBrush  = { tabStartX + 0 * (tabW + 6), tabY, tabStartX + 0 * (tabW + 6) + tabW, tabY + tabH };
            rTbPaper  = { tabStartX + 1 * (tabW + 6), tabY, tabStartX + 1 * (tabW + 6) + tabW, tabY + tabH };
            rTbSave   = { tabStartX + 2 * (tabW + 6), tabY, tabStartX + 2 * (tabW + 6) + tabW, tabY + tabH };
            rTbOtehon = { tabStartX + 3 * (tabW + 6), tabY, tabStartX + 3 * (tabW + 6) + tabW, tabY + tabH };

            int topOff = rSub.top + 64;

            // 筆タブ
            int cardH = 68;
            rSubSmall  = { rSub.left + 14, topOff, rSub.right - 14, topOff + cardH };
            rSubMedium = { rSub.left + 14, topOff + (cardH + 8), rSub.right - 14, topOff + (cardH + 8) + cardH };
            rSubLarge  = { rSub.left + 14, topOff + (cardH + 8) * 2, rSub.right - 14, topOff + (cardH + 8) * 2 + cardH };
            int cardBottom = topOff + (cardH + 8) * 2 + cardH;
            rHardnessTrack = { rSub.left + 30, cardBottom + 58, rSub.right - 30, cardBottom + 66 };

            // 紙タブ
            int ptW = (menuW - 36) / 2;
            int ptH = 46;
            for (int i = 0; i < 4; ++i) {
                int col = i % 2;
                int row = i / 2;
                rPaperTile[i] = { rSub.left + 14 + col * (ptW + 8), topOff + row * (ptH + 6), rSub.left + 14 + col * (ptW + 8) + ptW, topOff + row * (ptH + 6) + ptH };
            }

            int gridTop = topOff + 2 * (ptH + 6) + 30;
            int tileW = (menuW - 36) / 2;
            int tileH = 46;
            for (int i = 0; i < 9; ++i) {
                int col = i % 2;
                int row = i / 2;
                if (i == 8) {
                    rGridTile[i] = { rSub.left + 14, gridTop + row * (tileH + 6), rSub.left + 14 + tileW * 2 + 8, gridTop + row * (tileH + 6) + tileH };
                } else {
                    rGridTile[i] = { rSub.left + 14 + col * (tileW + 8), gridTop + row * (tileH + 6), rSub.left + 14 + col * (tileW + 8) + tileW, gridTop + row * (tileH + 6) + tileH };
                }
            }

            int colorY = gridTop + 5 * (tileH + 6) + 24;
            int colBtnW = (menuW - 40) / 3;
            for (int i = 0; i < 3; ++i) {
                rColorBtn[i] = { rSub.left + 14 + i * (colBtnW + 6), colorY, rSub.left + 14 + i * (colBtnW + 6) + colBtnW, colorY + 36 };
            }

            // 保存タブ
            rSaveBtnPng  = { rSub.left + 16, topOff + 16, rSub.right - 16, topOff + 76 };
            rSaveBtnClip = { rSub.left + 16, topOff + 90, rSub.right - 16, topOff + 150 };

            // お手本タブ
            rOtehonToggleBtn = { rSub.left + 16, topOff, rSub.right - 16, topOff + 48 };
            int oTop = topOff + 78;
            int oW = (menuW - 48) / 4;
            int oH = 54;
            for (int i = 0; i < 8; ++i) {
                int col = i % 4;
                int row = i / 4;
                rOtehonTile[i] = { rSub.left + 16 + col * (oW + 8), oTop + row * (oH + 8), rSub.left + 16 + col * (oW + 8) + oW, oTop + row * (oH + 8) + oH };
            }
            rOtehonOpacityTrack = { rSub.left + 28, oTop + 2 * (oH + 8) + 40, rSub.right - 28, oTop + 2 * (oH + 8) + 48 };
        }

        // 2. キャンバス / 半紙（画面中央にアスペクト比を維持して配置）
        double ratioW = 242.0, ratioH = 333.0; // デフォルト半紙 (242x333)
        switch (g_paperType) {
        case PaperType::Hanshi:   ratioW = 242.0; ratioH = 333.0; break;
        case PaperType::Jofuku:   ratioW = 350.0; ratioH = 680.0; break;
        case PaperType::Shikishi: ratioW = 242.0; ratioH = 272.0; break;
        case PaperType::Tanzaku:  ratioW = 60.0;  ratioH = 180.0; break;
        }

        int maxPaperH = h - statusH - 48;
        if (maxPaperH < 100) maxPaperH = 100;

        // 画面中央 (w / 2) に半紙を配置する際の最大利用可能幅
        // 右側の硯パネル (幅 rightW) と重ならないように安全マージンを考慮
        int maxPaperW = (w / 2 - (rightW + 20)) * 2;
        if (maxPaperW < 100) maxPaperW = w - rightW - 48;
        if (maxPaperW < 100) maxPaperW = 100;

        int paperH = maxPaperH;
        int paperW = (int)(paperH * (ratioW / ratioH));
        if (paperW > maxPaperW) {
            paperW = maxPaperW;
            paperH = (int)(paperW * (ratioH / ratioW));
        }

        // 水平・垂直ともに画面中央に配置
        int canvasCenterX = w / 2;
        if (canvasCenterX + paperW / 2 > rRight.left - 12) {
            canvasCenterX = rRight.left / 2;
        }
        int canvasCenterY = (h - statusH) / 2;

        rPaper.left = canvasCenterX - paperW / 2;
        rPaper.right = rPaper.left + paperW;
        rPaper.top = canvasCenterY - paperH / 2;
        rPaper.bottom = rPaper.top + paperH;

        // 3. 右側 硯・墨量・全消し
        int rightW_actual = RW(rRight);
        int stoneW = std::min(240, rightW_actual - 32);
        int stoneH = (int)(stoneW * 1.48);
        int stoneX = rRight.left + (rightW_actual - stoneW) / 2;
        int stoneY = 60;

        rInkStoneLarge = { stoneX, stoneY, stoneX + stoneW, stoneY + stoneH };
        rInkRefillBtn  = { stoneX, stoneY + stoneH + 14, stoneX + stoneW, stoneY + stoneH + 54 };
        rClearAllBtn   = { stoneX, stoneY + stoneH + 68, stoneX + stoneW, stoneY + stoneH + 108 };

        // 5. 全消し確認モーダルダイアログ
        int modalW = 420;
        int modalH = 200;
        rClearModalBox = { w / 2 - modalW / 2, h / 2 - modalH / 2, w / 2 + modalW / 2, h / 2 + modalH / 2 };
        int btnW = 140;
        int btnH = 40;
        int btnY = rClearModalBox.bottom - 56;
        rModalCancelBtn = { rClearModalBox.left + 40, btnY, rClearModalBox.left + 40 + btnW, btnY + btnH };
        rModalClearBtn  = { rClearModalBox.right - 40 - btnW, btnY, rClearModalBox.right - 40, btnY + btnH };
    }

    void DrawPanelTitle(HDC dc, const RECT& rPanel, const wchar_t* title) {
        RECT rTitle = { rPanel.left + 16, 14, rPanel.right - 16, 38 };
        HFONT f = Font(15, FW_BOLD);
        Text(dc, rTitle, title, f, RGB(240, 244, 250));
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

        HFONT f1 = Font(16, FW_BOLD), f2 = Font(12);
        RECT a = { r.left + 50, r.top + 8, r.right - 10, r.top + 32 };
        RECT b = { r.left + 50, r.top + 34, r.right - 10, r.bottom - 8 };
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
        HFONT f1 = Font(15, FW_BOLD), f2 = Font(11);
        RECT r1 = { r.left + 4, r.top + 4, r.right - 4, r.top + 26 };
        RECT r2 = { r.left + 4, r.top + 26, r.right - 4, r.bottom - 3 };
        Center(dc, r1, title, f1, textMain);
        Center(dc, r2, sub, f2, textSub);
        DeleteObject(f1);
        DeleteObject(f2);
    }

    void DrawColorThemeBtn(HDC dc, RECT r, const wchar_t* label, COLORREF dotColor, bool active, bool hover) {
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

        HFONT f = Font(13, active ? FW_BOLD : FW_NORMAL);
        RECT tr = { r.left + 22, r.top, r.right - 4, r.bottom };
        Center(dc, tr, label, f, active ? RGB(255, 255, 255) : RGB(200, 205, 215));
        DeleteObject(f);
    }

    void DrawSub(HDC dc) {
        if (!g_subPanelOpen) return;

        if (g_leftTab == LeftTab::Brush) {
            DrawSubCard(dc, rSubSmall, L"小筆", L"かな・名入れ・細線", 2, g_brush == Brush::Small, g_hoverSub == 1);
            DrawSubCard(dc, rSubMedium, L"中筆", L"標準的な楷書・行書", 5, g_brush == Brush::Medium, g_hoverSub == 2);
            DrawSubCard(dc, rSubLarge, L"大筆", L"作品・力強い大字", 9, g_brush == Brush::Large, g_hoverSub == 3);

            // 筆の硬さカード
            RECT rCard = { rSub.left + 14, rSubLarge.bottom + 12, rSub.right - 14, rSubLarge.bottom + 112 };
            Box(dc, rCard, RGB(32, 35, 42), RGB(50, 55, 68), 1, 6);

            HFONT fTitle = Font(13, FW_BOLD);
            RECT rTitle = { rCard.left + 14, rCard.top + 12, rCard.left + 160, rCard.top + 32 };
            Text(dc, rTitle, L"筆の硬さ（感度補正）", fTitle, RGB(220, 225, 235));

            wchar_t valBuf[64];
            const wchar_t* hardState = (g_brushHardness < 0.3) ? L"超極軟" : ((g_brushHardness < 0.7) ? L"柔らかめ" : ((g_brushHardness > 1.2) ? L"硬め" : L"標準"));
            swprintf_s(valBuf, 64, L"%.2f (%s)", g_brushHardness, hardState);
            RECT rVal = { rCard.right - 130, rCard.top + 12, rCard.right - 14, rCard.top + 32 };
            Text(dc, rVal, valBuf, fTitle, RGB(100, 160, 230), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            DeleteObject(fTitle);

            // トラック描画
            Fill(dc, rHardnessTrack, RGB(18, 20, 24));
            double normHardness = (g_brushHardness - 0.1) / (2.0 - 0.1);
            normHardness = Clamp(normHardness, 0.0, 1.0);
            int thumbX = rHardnessTrack.left + (int)(RW(rHardnessTrack) * normHardness);

            RECT rLevel = rHardnessTrack;
            rLevel.right = thumbX;
            Fill(dc, rLevel, RGB(65, 120, 190));

            RECT thumb = { thumbX - 5, rHardnessTrack.top - 4, thumbX + 5, rHardnessTrack.bottom + 4 };
            Box(dc, thumb, g_isDraggingHardness ? RGB(180, 210, 255) : RGB(140, 180, 230), RGB(220, 235, 255), 1, 3);

            HFONT fSub = Font(11, FW_NORMAL);
            RECT rMinLab = { rHardnessTrack.left, rHardnessTrack.bottom + 6, rHardnessTrack.left + 80, rHardnessTrack.bottom + 22 };
            Text(dc, rMinLab, L"0.1 (極軟)", fSub, RGB(140, 145, 155));

            RECT rMaxLab = { rHardnessTrack.right - 80, rHardnessTrack.bottom + 6, rHardnessTrack.right, rHardnessTrack.bottom + 22 };
            Text(dc, rMaxLab, L"2.0 (極硬)", fSub, RGB(140, 145, 155), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            DeleteObject(fSub);
        }
        else if (g_leftTab == LeftTab::Paper) {
            // 用紙サイズ・種類
            const wchar_t* pTitles[4] = { L"半紙", L"条幅", L"色紙", L"短冊" };
            const wchar_t* pSubs[4]   = { L"242×333", L"350×680", L"242×272", L"60×180" };
            for (int i = 0; i < 4; ++i) {
                bool act = ((int)g_paperType == i);
                bool hov = (g_hoverSub == 50 + i);
                DrawGridTileCard(dc, rPaperTile[i], pTitles[i], pSubs[i], act, hov);
            }

            // 下敷き・升目
            RECT rGridHeader = { rSub.left + 14, rPaperTile[2].bottom + 8, rSub.right - 14, rPaperTile[2].bottom + 26 };
            HFONT fgh = Font(13, FW_BOLD);
            Text(dc, rGridHeader, L"下敷き・升目ガイド", fgh, RGB(180, 185, 195));
            DeleteObject(fgh);

            const wchar_t* titles[9] = {
                L"なし", L"1字 (十字)", L"2文字 (2段)",
                L"4文字 (田)", L"6文字 (2x3)", L"8文字 (2x4)",
                L"3行 罫線", L"4行 罫線", L"米字格 (対角)"
            };
            const wchar_t* subs[9] = {
                L"無地半紙", L"中心ガイド", L"二文字熟語",
                L"四字熟語", L"六文字配列", L"八文字配列",
                L"行書・かな", L"条幅・古典", L"臨書・骨格"
            };

            for (int i = 0; i < 9; ++i) {
                bool act = ((int)g_gridPattern == i);
                bool hov = (g_hoverSub == 10 + i);
                DrawGridTileCard(dc, rGridTile[i], titles[i], subs[i], act, hov);
            }

            // 配色
            RECT rColTitle = { rSub.left + 14, rColorBtn[0].top - 18, rSub.right - 14, rColorBtn[0].top };
            HFONT fct = Font(13, FW_BOLD);
            Text(dc, rColTitle, L"下敷き・罫線の配色", fct, RGB(170, 175, 185));
            DeleteObject(fct);

            DrawColorThemeBtn(dc, rColorBtn[0], L"朱赤", RGB(225, 80, 80), g_gridColor == GridColorTheme::RedLine, g_hoverSub == 30);
            DrawColorThemeBtn(dc, rColorBtn[1], L"白線", RGB(235, 238, 245), g_gridColor == GridColorTheme::WhiteLine, g_hoverSub == 31);
            DrawColorThemeBtn(dc, rColorBtn[2], L"薄墨", RGB(140, 145, 155), g_gridColor == GridColorTheme::InkGray, g_hoverSub == 32);
        }
        else if (g_leftTab == LeftTab::Save) {
            // 保存ボタン
            bool hovPng = (g_hoverSub == 60);
            Box(dc, rSaveBtnPng, hovPng ? RGB(45, 75, 120) : RGB(34, 48, 72), hovPng ? RGB(80, 140, 220) : RGB(55, 95, 160), 1, 8);
            HFONT fBtn1 = Font(16, FW_BOLD);
            Center(dc, rSaveBtnPng, L"🖼️ 画像（作品）を保存", fBtn1, RGB(255, 255, 255));
            DeleteObject(fBtn1);

            bool hovClip = (g_hoverSub == 61);
            Box(dc, rSaveBtnClip, hovClip ? RGB(42, 46, 56) : RGB(30, 33, 40), hovClip ? RGB(70, 75, 90) : RGB(48, 52, 64), 1, 8);
            HFONT fBtn2 = Font(15, FW_NORMAL);
            Center(dc, rSaveBtnClip, L"📋 クリップボードにコピー", fBtn2, RGB(220, 225, 235));
            DeleteObject(fBtn2);

            // フィードバック表示
            if (!g_saveFeedback.empty() && (GetTickCount() - g_saveFeedbackTime < 4000)) {
                RECT rMsg = { rSub.left + 16, rSaveBtnClip.bottom + 24, rSub.right - 16, rSaveBtnClip.bottom + 64 };
                Box(dc, rMsg, RGB(28, 56, 40), RGB(50, 130, 80), 1, 6);
                HFONT fMsg = Font(14, FW_BOLD);
                Center(dc, rMsg, g_saveFeedback.c_str(), fMsg, RGB(180, 255, 200));
                DeleteObject(fMsg);
            }
        }
        else if (g_leftTab == LeftTab::Otehon) {
            // お手本表示 ON/OFF トグル
            bool hovTog = (g_hoverSub == 70);
            Box(dc, rOtehonToggleBtn, g_showOtehon ? RGB(36, 68, 105) : (hovTog ? RGB(42, 46, 56) : RGB(30, 33, 40)),
                g_showOtehon ? RGB(70, 135, 220) : (hovTog ? RGB(66, 72, 86) : RGB(46, 50, 62)), 1, 8);
            HFONT fTog = Font(15, FW_BOLD);
            Center(dc, rOtehonToggleBtn, g_showOtehon ? L"✓ お手本表示: ON" : L"お手本表示: OFF", fTog, g_showOtehon ? RGB(255, 255, 255) : RGB(190, 195, 205));
            DeleteObject(fTog);

            // 漢字選択
            RECT rOteHeader = { rSub.left + 16, rOtehonToggleBtn.bottom + 12, rSub.right - 16, rOtehonToggleBtn.bottom + 28 };
            HFONT foh = Font(13, FW_BOLD);
            Text(dc, rOteHeader, L"お手本文字を選択", foh, RGB(180, 185, 195));
            DeleteObject(foh);

            for (int i = 0; i < 8; ++i) {
                bool act = (g_selectedOtehon == i);
                bool hov = (g_hoverSub == 80 + i);
                DrawGridTileCard(dc, rOtehonTile[i], g_otehonChars[i], L"", act, hov);
            }

            // 濃淡スライダー
            RECT rCard = { rSub.left + 14, rOtehonOpacityTrack.top - 24, rSub.right - 14, rOtehonOpacityTrack.bottom + 28 };
            Box(dc, rCard, RGB(32, 35, 42), RGB(50, 55, 68), 1, 6);

            HFONT fTitle = Font(13, FW_BOLD);
            RECT rTitle = { rCard.left + 14, rCard.top + 6, rCard.left + 150, rCard.top + 24 };
            Text(dc, rTitle, L"お手本の透過度（濃淡）", fTitle, RGB(220, 225, 235));

            wchar_t valBuf[32];
            swprintf_s(valBuf, 32, L"%d%%", (int)(g_otehonOpacity * 100.0));
            RECT rVal = { rCard.right - 80, rCard.top + 8, rCard.right - 14, rCard.top + 26 };
            Text(dc, rVal, valBuf, fTitle, RGB(100, 160, 230), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            DeleteObject(fTitle);

            Fill(dc, rOtehonOpacityTrack, RGB(18, 20, 24));
            int thumbX = rOtehonOpacityTrack.left + (int)(RW(rOtehonOpacityTrack) * g_otehonOpacity);
            RECT rLevel = rOtehonOpacityTrack;
            rLevel.right = thumbX;
            Fill(dc, rLevel, RGB(65, 120, 190));

            RECT thumb = { thumbX - 5, rOtehonOpacityTrack.top - 4, thumbX + 5, rOtehonOpacityTrack.bottom + 4 };
            Box(dc, thumb, g_isDraggingOtehon ? RGB(180, 210, 255) : RGB(140, 180, 230), RGB(220, 235, 255), 1, 3);
        }
    }

    void DrawFloatingMenu(HDC dc) {
        if (!g_subPanelOpen) {
            // 閉じているとき: 左上にコンパクトな「<<<」ボタン
            bool hov = (g_hoverTb == TbButton::NavToggle);
            Box(dc, rTbNavToggle, hov ? RGB(45, 52, 68) : RGB(30, 33, 42), hov ? RGB(85, 140, 230) : RGB(52, 58, 74), 1, 8);
            HFONT f = Font(16, FW_BOLD);
            Center(dc, rTbNavToggle, L"<<<", f, hov ? RGB(255, 255, 255) : RGB(195, 205, 225));
            DeleteObject(f);
            return;
        }

        // 開いているとき: フローティングパネル外枠
        Box(dc, rSub, RGB(22, 24, 30), RGB(56, 62, 78), 2, 12);

        // ヘッダーバー区切り線
        HPEN sp = CreatePen(PS_SOLID, 1, RGB(38, 42, 54));
        HPEN osp = (HPEN)SelectObject(dc, sp);
        MoveToEx(dc, rSub.left + 12, rSub.top + 54, nullptr);
        LineTo(dc, rSub.right - 12, rSub.top + 54);
        SelectObject(dc, osp);
        DeleteObject(sp);

        // 閉じるボタン「>>>」
        bool hovTog = (g_hoverTb == TbButton::NavToggle);
        Box(dc, rTbNavToggle, hovTog ? RGB(45, 52, 68) : RGB(32, 35, 46), hovTog ? RGB(85, 140, 230) : RGB(52, 58, 74), 1, 6);
        HFONT fTog = Font(16, FW_BOLD);
        Center(dc, rTbNavToggle, L">>>", fTog, hovTog ? RGB(255, 255, 255) : RGB(195, 205, 225));
        DeleteObject(fTog);

        // 横並びタブ: 筆、紙、保存、お手本
        auto DrawTab = [&](RECT r, const wchar_t* label, bool active, bool hover) {
            COLORREF bg = active ? RGB(42, 68, 110) : (hover ? RGB(38, 42, 54) : RGB(28, 30, 38));
            COLORREF border = active ? RGB(75, 135, 225) : (hover ? RGB(65, 72, 90) : RGB(44, 48, 60));
            COLORREF text = active ? RGB(255, 255, 255) : (hover ? RGB(240, 244, 252) : RGB(170, 175, 185));
            Box(dc, r, bg, border, 1, 6);
            HFONT f = Font(14, active ? FW_BOLD : FW_NORMAL);
            Center(dc, r, label, f, text);
            DeleteObject(f);
        };

        DrawTab(rTbBrush,  L"筆",   g_leftTab == LeftTab::Brush,  g_hoverTb == TbButton::Brush);
        DrawTab(rTbPaper,  L"紙",   g_leftTab == LeftTab::Paper,  g_hoverTb == TbButton::Paper);
        DrawTab(rTbSave,   L"保存", g_leftTab == LeftTab::Save,   g_hoverTb == TbButton::Save);
        DrawTab(rTbOtehon, L"お手本", g_leftTab == LeftTab::Otehon, g_hoverTb == TbButton::Otehon);

        DrawSub(dc);
    }

    void DrawLargeInkStone(HDC dc) {
        Fill(dc, rRight, RGB(22, 24, 28));
        DrawPanelTitle(dc, rRight, L"硯 (すずり)");

        // 1. 墨量表示ヘッダー（コンパクト）
        HFONT fMeta = Font(13, FW_NORMAL);
        RECT rMeta1 = { rRight.left + 16, rInkStoneLarge.top - 24, rRight.right - 16, rInkStoneLarge.top - 4 };
        wchar_t buf[64];
        wsprintfW(buf, L"墨残量: %d%%", (int)(g_ink * 100.0));
        Text(dc, rMeta1, buf, fMeta, RGB(190, 195, 205));
        DeleteObject(fMeta);

        // 2. 硯の外枠（硯身・石肌・角丸）
        bool hoverStone = (g_hoverInkStone == 1);
        COLORREF stoneBorder = hoverStone ? RGB(70, 120, 190) : RGB(46, 50, 60);
        Box(dc, rInkStoneLarge, RGB(25, 27, 33), stoneBorder, 2, 10);

        RECT innerRim = { rInkStoneLarge.left + 8, rInkStoneLarge.top + 8, rInkStoneLarge.right - 8, rInkStoneLarge.bottom - 8 };
        Box(dc, innerRim, RGB(16, 17, 21), RGB(36, 39, 48), 1, 8);

        // 3. 墨池（海）
        RECT rPool = { innerRim.left + 10, innerRim.top + 10, innerRim.right - 10, innerRim.top + (int)(RH(innerRim) * 0.35) };
        Box(dc, rPool, RGB(10, 11, 14), RGB(30, 33, 40), 1, 6);

        double inkFrac = Clamp(g_ink / INK_MAX, 0.0, 1.0);
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

        HFONT fPool = Font(14, FW_BOLD);
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

        HFONT fKanji = Font(16, FW_BOLD);
        RECT rLandText = { rLand.left, rLand.top + 10, rLand.right, rLand.top + 30 };
        Center(dc, rLandText, L"墨 堂 (陸)", fKanji, RGB(65, 70, 82));
        DeleteObject(fKanji);

        // 5. 墨を補充ボタン
        bool hovRefill = (g_hoverInkStone == 2);
        Box(dc, rInkRefillBtn, hovRefill ? RGB(40, 65, 96) : RGB(30, 34, 42), hovRefill ? RGB(70, 120, 190) : RGB(48, 54, 66), 1, 6);
        HFONT fBtn = Font(14, FW_BOLD);
        Center(dc, rInkRefillBtn, L"💧 墨を補充", fBtn, hovRefill ? RGB(255, 255, 255) : RGB(210, 215, 225));
        DeleteObject(fBtn);

        // 6. 全消しボタン（墨補充から少し離して配置）
        bool hovClear = (g_hoverInkStone == 3);
        Box(dc, rClearAllBtn, hovClear ? RGB(75, 36, 36) : RGB(38, 28, 30), hovClear ? RGB(160, 60, 60) : RGB(68, 44, 48), 1, 6);
        HFONT fClear = Font(14, FW_BOLD);
        Center(dc, rClearAllBtn, L"🗑️ すべての筆跡を消す", fClear, hovClear ? RGB(255, 220, 220) : RGB(220, 175, 175));
        DeleteObject(fClear);
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

    void DrawOtehonTemplate(HDC dc) {
        if (!g_showOtehon) return;
        if (g_selectedOtehon < 0 || g_selectedOtehon >= 8) return;

        const wchar_t* ch = g_otehonChars[g_selectedOtehon];
        int size = (int)(std::min(RW(rPaper), RH(rPaper)) * 0.72);
        if (size <= 0) return;

        HFONT fOtehon = CreateFontW(size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Yu Mincho");

        // 透過度に応じた薄墨色の計算
        int grayVal = (int)(248 - g_otehonOpacity * 105.0);
        grayVal = Clamp(grayVal, 80, 245);
        COLORREF oColor = RGB(grayVal, (int)(grayVal * 0.98), (int)(grayVal * 0.95));

        HFONT old = (HFONT)SelectObject(dc, fOtehon);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, oColor);
        DrawTextW(dc, ch, 1, &rPaper, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old);
        DeleteObject(fOtehon);
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
        wsprintfW(buf, L"状態: 準備完了 | 筆: %s | 下敷き: %s | お手本: %s | 墨量: %d%%", 
            BrushName(g_brush), 
            GridPatternName(g_gridPattern),
            g_showOtehon ? g_otehonChars[g_selectedOtehon] : L"なし",
            (int)(g_ink * 100.0));
        Text(dc, t, buf, f, RGB(150, 155, 165));
        DeleteObject(f);
    }

    void DrawClearConfirmDialog(HDC dc, int w, int h) {
        if (!g_showClearConfirm) return;

        // 全画面の半透明暗転オーバーレイ
        for (int y = 0; y < h; y += 2) {
            RECT line = { 0, y, w, y + 1 };
            Fill(dc, line, RGB(0, 0, 0));
        }

        // モーダルダイアログカード
        Box(dc, rClearModalBox, RGB(28, 30, 36), RGB(64, 70, 84), 2, 10);

        // タイトル
        HFONT fTitle = Font(18, FW_BOLD);
        RECT rT = { rClearModalBox.left + 24, rClearModalBox.top + 20, rClearModalBox.right - 24, rClearModalBox.top + 52 };
        Text(dc, rT, L"すべての筆跡を消しますか？", fTitle, RGB(245, 248, 252));
        DeleteObject(fTitle);

        // 説明文
        HFONT fDesc = Font(13, FW_NORMAL);
        RECT rD = { rClearModalBox.left + 24, rClearModalBox.top + 58, rClearModalBox.right - 24, rClearModalBox.top + 115 };
        DrawTextW(dc, L"現在の作品に書かれている筆跡をすべて消します。\nこの操作は元に戻すことができません。", -1, &rD, DT_LEFT | DT_TOP | DT_NOPREFIX);
        DeleteObject(fDesc);

        // キャンセルボタン
        bool hovCancel = (g_hoverClearModal == 2);
        Box(dc, rModalCancelBtn, hovCancel ? RGB(52, 57, 68) : RGB(38, 42, 50), hovCancel ? RGB(80, 88, 104) : RGB(56, 62, 74), 1, 6);
        HFONT fBtn = Font(14, FW_NORMAL);
        Center(dc, rModalCancelBtn, L"キャンセル", fBtn, hovCancel ? RGB(255, 255, 255) : RGB(210, 215, 225));

        // すべて消すボタン（落ち着いた警告色）
        bool hovClear = (g_hoverClearModal == 1);
        Box(dc, rModalClearBtn, hovClear ? RGB(185, 48, 48) : RGB(150, 36, 36), hovClear ? RGB(225, 75, 75) : RGB(190, 50, 50), 1, 6);
        Center(dc, rModalClearBtn, L"すべて消す", fBtn, RGB(255, 255, 255));
        DeleteObject(fBtn);
    }

    void DrawStudioChrome(HDC dc, int w, int h) {
        Fill(dc, rCanvasArea, RGB(20, 22, 26)); 
        DrawPaperBackground(dc);
        DrawOtehonTemplate(dc);
        DrawRight(dc); 
        DrawStatus(dc, w, h);
        DrawFloatingMenu(dc);
    }
    bool PtIn(const RECT& r, POINT p) { return PtInRect(&r, p) != FALSE; }

    void SaveCanvasImage(HWND hWnd, bool toClipboard) {
        int pw = RW(rPaper);
        int ph = RH(rPaper);
        if (pw <= 0 || ph <= 0) return;

        HDC screenDC = GetDC(hWnd);
        HDC memDC = CreateCompatibleDC(screenDC);
        HBITMAP memBmp = CreateCompatibleBitmap(screenDC, pw, ph);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        // 背景
        RECT rLocal = { 0, 0, pw, ph };
        HBRUSH pb = CreateSolidBrush(RGB(248, 247, 242));
        FillRect(memDC, &rLocal, pb);
        DeleteObject(pb);

        // お手本
        if (g_showOtehon && g_selectedOtehon >= 0 && g_selectedOtehon < 8) {
            const wchar_t* ch = g_otehonChars[g_selectedOtehon];
            int size = (int)(std::min(pw, ph) * 0.72);
            if (size > 0) {
                HFONT fOtehon = CreateFontW(size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, L"Yu Mincho");
                int grayVal = (int)(248 - g_otehonOpacity * 105.0);
                grayVal = Clamp(grayVal, 80, 245);
                HFONT old = (HFONT)SelectObject(memDC, fOtehon);
                SetBkMode(memDC, TRANSPARENT);
                SetTextColor(memDC, RGB(grayVal, (int)(grayVal * 0.98), (int)(grayVal * 0.95)));
                DrawTextW(memDC, ch, 1, &rLocal, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(memDC, old);
                DeleteObject(fOtehon);
            }
        }

        // 墨汁
        g_gpuInk.Render(memDC, 0, 0, pw, ph);

        if (toClipboard) {
            if (OpenClipboard(hWnd)) {
                EmptyClipboard();
                SetClipboardData(CF_BITMAP, memBmp);
                CloseClipboard();
                g_saveFeedback = L"✓ クリップボードにコピーしました";
                g_saveFeedbackTime = GetTickCount();
            }
        } else {
            // BMP ファイルとしてデスクトップに保存
            wchar_t deskPath[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, deskPath))) {
                SYSTEMTIME st;
                GetLocalTime(&st);
                wchar_t filePath[MAX_PATH];
                swprintf_s(filePath, MAX_PATH, L"%s\\習字作品_%04d%02d%02d_%02d%02d%02d.bmp",
                    deskPath, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

                BITMAP bmp;
                GetObject(memBmp, sizeof(BITMAP), &bmp);

                BITMAPFILEHEADER bfh = { 0 };
                BITMAPINFOHEADER bih = { 0 };

                bih.biSize = sizeof(BITMAPINFOHEADER);
                bih.biWidth = pw;
                bih.biHeight = ph;
                bih.biPlanes = 1;
                bih.biBitCount = 24;
                bih.biCompression = BI_RGB;

                DWORD dwSize = ((pw * bih.biBitCount + 31) / 32) * 4 * ph;
                bfh.bfType = 0x4D42; // "BM"
                bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dwSize;
                bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

                std::vector<BYTE> buf(dwSize);
                GetDIBits(memDC, memBmp, 0, ph, buf.data(), (BITMAPINFO*)&bih, DIB_RGB_COLORS);

                HANDLE hFile = CreateFileW(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    WriteFile(hFile, &bfh, sizeof(bfh), &written, NULL);
                    WriteFile(hFile, &bih, sizeof(bih), &written, NULL);
                    WriteFile(hFile, buf.data(), dwSize, &written, NULL);
                    CloseHandle(hFile);
                    g_saveFeedback = L"✓ デスクトップに保存しました";
                    g_saveFeedbackTime = GetTickCount();
                }
            }
        }

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        ReleaseDC(hWnd, screenDC);
    }
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
	wcex.lpszMenuName = NULL; // 古いメニューバーを非表示にしキャンバス領域を最大化
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

	SetMenu(g_mainWnd, NULL);
	g_hdc = GetDC(g_mainWnd);

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

		if (!OpenTabletContexts(hWnd))
		{
			ShowError("Could Not Open Wintab Tablet Contexts.");
		}
		break;
	}

	case WM_CLOSE:
	{
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	case WM_KEYDOWN:
	{
		switch (wParam)
		{
		case VK_ESCAPE:
		{
			g_gpuInk.Clear();
			InvalidateRect(hWnd, NULL, FALSE);
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
			g_gpuInk.Clear();
			InvalidateRect(hWnd, NULL, FALSE);
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

			// キャンバス背景・半紙背景・お手本・右側硯・ステータス
			Fill(memDC, rCanvasArea, RGB(20, 22, 26));
			DrawPaperBackground(memDC);
			DrawOtehonTemplate(memDC);

			// GPU 墨汁を paper 領域に合成描画
			g_gpuInk.Render(memDC, rPaper.left, rPaper.top);

			// 赤い補助線（格子）を墨汁の上に薄く描画して確実に表示させる
			DrawPaperGrid(memDC);

			// 右側 硯
			DrawRight(memDC);

			// ステータスバー
			DrawStatus(memDC, w, h);

			// フローティングメニューまたは「<<<」ボタンを最前面にオーバーレイ描画
			DrawFloatingMenu(memDC);

			// 全消し確認モーダル表示
			DrawClearConfirmDialog(memDC, w, h);

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
		int w = g_clientRect.right - g_clientRect.left;
		int h = g_clientRect.bottom - g_clientRect.top;

		// 全消し確認モーダル表示中のクリック
		if (g_showClearConfirm)
		{
			if (PtIn(rModalClearBtn, pt))
			{
				g_gpuInk.Clear();
				g_showClearConfirm = false;
				InvalidateRect(hWnd, NULL, FALSE);
				return 0;
			}
			else if (PtIn(rModalCancelBtn, pt) || !PtIn(rClearModalBox, pt))
			{
				g_showClearConfirm = false;
				InvalidateRect(hWnd, NULL, FALSE);
				return 0;
			}
			return 0;
		}

		// ナビゲーション開閉ボタン「<<<」/「>>>」
		if (PtIn(rTbNavToggle, pt))
		{
			g_subPanelOpen = !g_subPanelOpen;
			Layout(w, h);
			InvalidateRect(hWnd, NULL, FALSE);
			return 0;
		}

		// 開いているときの横並びタブ
		if (g_subPanelOpen)
		{
			if (PtIn(rTbBrush, pt))
			{
				g_leftTab = LeftTab::Brush;
				InvalidateRect(hWnd, &rSub, FALSE);
				return 0;
			}
			else if (PtIn(rTbPaper, pt))
			{
				g_leftTab = LeftTab::Paper;
				InvalidateRect(hWnd, &rSub, FALSE);
				return 0;
			}
			else if (PtIn(rTbSave, pt))
			{
				g_leftTab = LeftTab::Save;
				InvalidateRect(hWnd, &rSub, FALSE);
				return 0;
			}
			else if (PtIn(rTbOtehon, pt))
			{
				g_leftTab = LeftTab::Otehon;
				InvalidateRect(hWnd, &rSub, FALSE);
				return 0;
			}

			// 詳細パネル内の操作
			if (PtIn(rSub, pt))
			{
				if (g_leftTab == LeftTab::Brush)
				{
					if (RW(rHardnessTrack) > 0)
					{
						RECT hitBox = rHardnessTrack;
						hitBox.top -= 6; hitBox.bottom += 6;
						if (PtIn(hitBox, pt))
						{
							g_isDraggingHardness = true;
							SetCapture(hWnd);
							double trackW = (double)RW(rHardnessTrack);
							double norm = (trackW > 0.0) ? (double)(pt.x - rHardnessTrack.left) / trackW : 0.2;
							norm = Clamp(norm, 0.0, 1.0);
							g_brushHardness = 0.1 + norm * (2.0 - 0.1);
							InvalidateRect(hWnd, &rSub, FALSE);
							return 0;
						}
					}

					if (PtIn(rSubSmall, pt))
					{
						g_brush = Brush::Small;
						InvalidateRect(hWnd, &rSub, FALSE);
						InvalidateRect(hWnd, &rStatus, FALSE);
						return 0;
					}
					else if (PtIn(rSubMedium, pt))
					{
						g_brush = Brush::Medium;
						InvalidateRect(hWnd, &rSub, FALSE);
						InvalidateRect(hWnd, &rStatus, FALSE);
						return 0;
					}
					else if (PtIn(rSubLarge, pt))
					{
						g_brush = Brush::Large;
						InvalidateRect(hWnd, &rSub, FALSE);
						InvalidateRect(hWnd, &rStatus, FALSE);
						return 0;
					}
				}
				else if (g_leftTab == LeftTab::Paper)
				{
					// 用紙タイプ切り替え
					for (int i = 0; i < 4; ++i) {
						if (PtIn(rPaperTile[i], pt)) {
							g_paperType = (PaperType)i;
							Layout(w, h);
							int pw = RW(rPaper);
							int ph = RH(rPaper);
							if (pw > 0 && ph > 0) g_gpuInk.Resize(pw, ph);
							InvalidateRect(hWnd, NULL, FALSE);
							return 0;
						}
					}

					// 下敷き・升目切り替え
					for (int i = 0; i < 9; ++i) {
						if (PtIn(rGridTile[i], pt)) {
							g_gridPattern = (GridPattern)i;
							InvalidateRect(hWnd, NULL, FALSE);
							return 0;
						}
					}

					// 罫線配色切り替え
					for (int i = 0; i < 3; ++i) {
						if (PtIn(rColorBtn[i], pt)) {
							g_gridColor = (GridColorTheme)i;
							InvalidateRect(hWnd, NULL, FALSE);
							return 0;
						}
					}
				}
				else if (g_leftTab == LeftTab::Save)
				{
					if (PtIn(rSaveBtnPng, pt))
					{
						SaveCanvasImage(hWnd, false);
						InvalidateRect(hWnd, &rSub, FALSE);
						return 0;
					}
					else if (PtIn(rSaveBtnClip, pt))
					{
						SaveCanvasImage(hWnd, true);
						InvalidateRect(hWnd, &rSub, FALSE);
						return 0;
					}
				}
				else if (g_leftTab == LeftTab::Otehon)
				{
					if (PtIn(rOtehonToggleBtn, pt))
					{
						g_showOtehon = !g_showOtehon;
						InvalidateRect(hWnd, NULL, FALSE);
						return 0;
					}

					for (int i = 0; i < 8; ++i) {
						if (PtIn(rOtehonTile[i], pt)) {
							g_selectedOtehon = i;
							g_showOtehon = true;
							InvalidateRect(hWnd, NULL, FALSE);
							return 0;
						}
					}

					if (RW(rOtehonOpacityTrack) > 0)
					{
						RECT hitBox = rOtehonOpacityTrack;
						hitBox.top -= 6; hitBox.bottom += 6;
						if (PtIn(hitBox, pt))
						{
							g_isDraggingOtehon = true;
							SetCapture(hWnd);
							double trackW = (double)RW(rOtehonOpacityTrack);
							double norm = (trackW > 0.0) ? (double)(pt.x - rOtehonOpacityTrack.left) / trackW : 0.35;
							g_otehonOpacity = Clamp(norm, 0.05, 1.0);
							InvalidateRect(hWnd, NULL, FALSE);
							return 0;
						}
					}
				}
				return 0;
			}
		}

		// 右側 硯・墨補充・全消し
		if (PtIn(rInkStoneLarge, pt) || PtIn(rInkRefillBtn, pt))
		{
			g_ink = INK_MAX;
			InvalidateRect(hWnd, &rRight, FALSE);
			InvalidateRect(hWnd, &rStatus, FALSE);
			return 0;
		}
		else if (PtIn(rClearAllBtn, pt))
		{
			g_gpuInk.Clear();
			InvalidateRect(hWnd, NULL, FALSE);
			return 0;
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

		// モーダル表示中のホバー
		if (g_showClearConfirm)
		{
			int oldHov = g_hoverClearModal;
			if (PtIn(rModalClearBtn, pt)) g_hoverClearModal = 1;
			else if (PtIn(rModalCancelBtn, pt)) g_hoverClearModal = 2;
			else g_hoverClearModal = 0;

			if (oldHov != g_hoverClearModal)
			{
				InvalidateRect(hWnd, &rClearModalBox, FALSE);
			}
			return 0;
		}

		if (g_isDraggingHardness)
		{
			double trackW = (double)RW(rHardnessTrack);
			double norm = (trackW > 0.0) ? (double)(pt.x - rHardnessTrack.left) / trackW : 0.2;
			norm = Clamp(norm, 0.0, 1.0);
			g_brushHardness = 0.1 + norm * (2.0 - 0.1);
			InvalidateRect(hWnd, &rSub, FALSE);
			break;
		}

		if (g_isDraggingOtehon)
		{
			double trackW = (double)RW(rOtehonOpacityTrack);
			double norm = (trackW > 0.0) ? (double)(pt.x - rOtehonOpacityTrack.left) / trackW : 0.35;
			g_otehonOpacity = Clamp(norm, 0.05, 1.0);
			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}

		// ナビゲーションホバー
		TbButton oldHoverTb = g_hoverTb;
		if (PtIn(rTbNavToggle, pt)) g_hoverTb = TbButton::NavToggle;
		else if (g_subPanelOpen && PtIn(rTbBrush, pt)) g_hoverTb = TbButton::Brush;
		else if (g_subPanelOpen && PtIn(rTbPaper, pt)) g_hoverTb = TbButton::Paper;
		else if (g_subPanelOpen && PtIn(rTbSave, pt)) g_hoverTb = TbButton::Save;
		else if (g_subPanelOpen && PtIn(rTbOtehon, pt)) g_hoverTb = TbButton::Otehon;
		else g_hoverTb = TbButton::None;

		// 詳細パネルホバー
		int oldHoverSub = g_hoverSub;
		g_hoverSub = 0;
		if (g_subPanelOpen) {
			if (g_leftTab == LeftTab::Brush) {
				if (PtIn(rSubSmall, pt)) g_hoverSub = 1;
				else if (PtIn(rSubMedium, pt)) g_hoverSub = 2;
				else if (PtIn(rSubLarge, pt)) g_hoverSub = 3;
			} else if (g_leftTab == LeftTab::Paper) {
				for (int i = 0; i < 4; ++i) {
					if (PtIn(rPaperTile[i], pt)) { g_hoverSub = 50 + i; break; }
				}
				for (int i = 0; i < 9; ++i) {
					if (PtIn(rGridTile[i], pt)) { g_hoverSub = 10 + i; break; }
				}
				for (int i = 0; i < 3; ++i) {
					if (PtIn(rColorBtn[i], pt)) { g_hoverSub = 30 + i; break; }
				}
			} else if (g_leftTab == LeftTab::Save) {
				if (PtIn(rSaveBtnPng, pt)) g_hoverSub = 60;
				else if (PtIn(rSaveBtnClip, pt)) g_hoverSub = 61;
			} else if (g_leftTab == LeftTab::Otehon) {
				if (PtIn(rOtehonToggleBtn, pt)) g_hoverSub = 70;
				for (int i = 0; i < 8; ++i) {
					if (PtIn(rOtehonTile[i], pt)) { g_hoverSub = 80 + i; break; }
				}
			}
		}

		// 硯・墨補充・全消しホバー
		int oldHoverInkStone = g_hoverInkStone;
		if (PtIn(rInkStoneLarge, pt)) g_hoverInkStone = 1;
		else if (PtIn(rInkRefillBtn, pt)) g_hoverInkStone = 2;
		else if (PtIn(rClearAllBtn, pt)) g_hoverInkStone = 3;
		else g_hoverInkStone = 0;

		if (oldHoverTb != g_hoverTb || oldHoverSub != g_hoverSub || oldHoverInkStone != g_hoverInkStone)
		{
			if (g_subPanelOpen) InvalidateRect(hWnd, &rSub, FALSE);
			else InvalidateRect(hWnd, &rTbNavToggle, FALSE);
			InvalidateRect(hWnd, &rRight, FALSE);
		}

		if (wParam & MK_LBUTTON)
		{
			bool canDraw = !g_showClearConfirm && PtIn(rPaper, pt) && !(g_subPanelOpen && PtIn(rSub, pt));
			if (canDraw)
			{
				POINT paperPt = { pt.x - rPaper.left, pt.y - rPaper.top };
				if (!g_gpuInk.IsInStroke())
				{
					double dx = (double)(paperPt.x - s_ptMouseOld.x);
					double dy = (double)(paperPt.y - s_ptMouseOld.y);
					double dist = std::hypot(dx, dy);

					if (dist > 0.1 && dist < 150.0)
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
				}
				s_ptMouseOld = paperPt;
				InvalidateRect(hWnd, NULL, FALSE);
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

		if (g_isDraggingOtehon)
		{
			g_isDraggingOtehon = false;
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
		if (gpWTPacket && gpWTPacket(hCtx, static_cast<int>(wParam), &pkt))
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

			bool canDrawInk = !g_showClearConfirm && PtInRect(&rPaper, clientPt) && !(g_subPanelOpen && PtInRect(&rSub, clientPt));

			if (prsNew > 0 && canDrawInk)
			{
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

				if (dist > 500.0)
				{
					oldPaperPt = paperPt;
					dist = 0.0;
				}

				s_ptMouseOld = paperPt;

				double pressureFactor = (double)prsNew / (maxPrs > 0 ? maxPrs : 1.0);
				if (pressureFactor > 1.0) pressureFactor = 1.0;
				if (pressureFactor < 0.0) pressureFactor = 0.0;

				// 筆の硬さ（ガンマ補正）を筆圧に適用
				if (std::isnan(g_brushHardness) || g_brushHardness < 0.1) g_brushHardness = 1.0;
				pressureFactor = std::pow(pressureFactor, g_brushHardness);
				if (std::isnan(pressureFactor) || std::isinf(pressureFactor)) pressureFactor = 0.5;

				g_gpuInk.SetPressureFactor(pressureFactor);

				double altitudeDegrees = (double)ortNew.orAltitude / 10.0;
				double azimuthRad = ((double)ortNew.orAzimuth / 10.0) * (3.14159265358979323846 / 180.0);
				double tiltFactor = (90.0 - altitudeDegrees) / 90.0;
				if (tiltFactor < 0.0) tiltFactor = 0.0;

				double moveAngle = (dist > 1e-5) ? std::atan2(dy, dx) : 0.0;

				double tomeFactor = 1.0 + 0.1 * (1.0 - (std::min)(dist / 3.0, 1.0)) * std::pow(pressureFactor, 0.8);
				double haraiPower = 1.3 + 0.4 * (std::min)(dist, 10.0);
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
				g_gpuInk.DrawSegmentLinear(oldPaperPt, paperPt, startWidth, s_smoothedWidth, 255);
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
		if (pair.first != nullptr && gpWTClose)
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
