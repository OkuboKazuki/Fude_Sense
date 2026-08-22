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
#include "AppEnums.h"
#include "RenderUtils.h"
#include "AppState.h"
#include "WintabManager.h"
#include "ImageExporter.h"

#include "MainView.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

bool OpenTabletContexts(HWND hWnd);
void CloseTabletContexts(void);
void Cleanup(void);

// Global Variables
HINSTANCE hInst = NULL;
std::wstring szTitle = L"SHUJI STUDIO - 習字制作ワークスペース";
std::wstring szWindowClass = L"WACOMMT_SCRIBBLE";
HWND g_mainWnd = NULL;
HDC g_hdc = NULL;
HWND g_hWndAbout = NULL;

RECT g_clientRect = { 0, 0, 0, 0 };

static AppState g_appState;
static WintabManager g_wintab;
static GpuInk g_gpuInk;

HWND g_hInkWnd = NULL;
HWND g_hMonitorWnd = NULL;
static ATOM g_inkWndClassAtom = 0;
static ATOM g_monitorWndClassAtom = 0;

// UI System State - AppStateへの参照バインディング
namespace {
    using namespace RenderUtils;
    constexpr double INK_MAX = INK_MAX_VALUE;

    // Model層の参照
    Brush& g_brush = g_appState.brush.type;
    double& g_ink = g_appState.ink.stoneAmount;
    double& g_brushHardness = g_appState.brush.hardness;
    bool& g_isDraggingHardness = g_appState.brush.isDraggingHardness;

    LeftTab& g_leftTab = g_appState.ui.leftTab;
    bool& g_subPanelOpen = g_appState.ui.isSubPanelOpen;
    bool& g_showClearConfirm = g_appState.ui.showClearConfirm;
    int&  g_hoverClearModal = g_appState.ui.hoverClearModal;

    // お手本設定
    bool& g_showOtehon = g_appState.otehon.isVisible;
    int& g_selectedOtehon = g_appState.otehon.selectedIndex;
    const wchar_t* g_otehonChars[] = { L"永", L"夢", L"和", L"心", L"道", L"光", L"美", L"桜" };
    double& g_otehonOpacity = g_appState.otehon.opacity;
    bool& g_isDraggingOtehon = g_appState.otehon.isDraggingOpacity;

    // 保存状態フィードバック
    std::wstring& g_saveFeedback = g_appState.ui.saveFeedback;
    DWORD& g_saveFeedbackTime = g_appState.ui.saveFeedbackTime;

    GridPattern& g_gridPattern = g_appState.paper.gridPattern;
    GridColorTheme& g_gridColor = g_appState.paper.gridColor;
    PaperType& g_paperType = g_appState.paper.type;

    TbButton& g_hoverTb = g_appState.ui.hoverTb;
    int& g_hoverSub = g_appState.ui.hoverSub;

    RECT& rSub = g_appState.ui.rSub;
    RECT& rCanvasArea = g_appState.ui.rCanvasArea;
    RECT& rRight = g_appState.ui.rRight;
    RECT& rStatus = g_appState.ui.rStatus;
    RECT& rTbNavToggle = g_appState.ui.rTbNavToggle;
    RECT& rTbBrush = g_appState.ui.rTbBrush;
    RECT& rTbPaper = g_appState.ui.rTbPaper;
    RECT& rTbSave = g_appState.ui.rTbSave;
    RECT& rTbOtehon = g_appState.ui.rTbOtehon;
    RECT& rPaper = g_appState.ui.rPaper;
    RECT& rSubSmall = g_appState.ui.rSubSmall;
    RECT& rSubMedium = g_appState.ui.rSubMedium;
    RECT& rSubLarge = g_appState.ui.rSubLarge;
    auto& rGridTile = g_appState.ui.rGridTile;
    auto& rColorBtn = g_appState.ui.rColorBtn;
    auto& rPaperTile = g_appState.ui.rPaperTile;
    auto& rOtehonTile = g_appState.ui.rOtehonTile;
    RECT& rOtehonToggleBtn = g_appState.ui.rOtehonToggleBtn;
    RECT& rOtehonOpacityTrack = g_appState.ui.rOtehonOpacityTrack;
    RECT& rSaveBtnPng = g_appState.ui.rSaveBtnPng;
    RECT& rSaveBtnClip = g_appState.ui.rSaveBtnClip;
    RECT& rInkStoneLarge = g_appState.ui.rInkStoneLarge;
    RECT& rInkRefillBtn = g_appState.ui.rInkRefillBtn;
    RECT& rClearAllBtn = g_appState.ui.rClearAllBtn;
    RECT& rHardnessTrack = g_appState.ui.rHardnessTrack;
    RECT& rClearModalBox = g_appState.ui.rClearModalBox;
    RECT& rModalClearBtn = g_appState.ui.rModalClearBtn;
    RECT& rModalCancelBtn = g_appState.ui.rModalCancelBtn;
    int& g_hoverInkStone = g_appState.ui.hoverInkStone;

    inline void Layout(int w, int h) {
        g_appState.Layout(w, h);
    }

    inline void SaveCanvasImage(HWND hWnd, bool toClipboard) {
        ImageExporter::ExportCanvas(hWnd, g_gpuInk, g_appState, toClipboard);
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
			MainView::Render(hdc, w, h, g_gpuInk, g_appState);
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
		if (!g_wintab.HasContext(hCtx))
		{
			hCtx = g_wintab.GetPrimaryContext();
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

			double maxPrs = g_wintab.GetMaxPressure(hCtx);

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
			if (g_wintab.IsSystemContext())
			{
				ScreenToClient(hWnd, &clientPt);
			}

			POINT oldClientPt = { ptOld.x, ptOld.y };
			if (g_wintab.IsSystemContext())
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
	return g_wintab.OpenContexts(hWnd);
}

void CloseTabletContexts(void)
{
	g_wintab.CloseContexts();
}

void Cleanup(void)
{
	g_wintab.Cleanup();
	g_gpuInk.Clear();
}
