///////////////////////////////////////////////////////////////////////////////
//
//	PURPOSE
//		Sample code showing how to use the Wacom Feel(TM) Multi-Touch API and
//		the Wintab32 API.
//
//	COPYRIGHT
//		Copyright (c) 2012-2020 Wacom Co., Ltd.
//
//		The text and information contained in this file may be freely used,
//		copied, or distributed without compensation or licensing restrictions.
//
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "WacomMT_Scribble.h"

#include <windowsx.h> // 追加: GET_X_LPARAM / GET_Y_LPARAM を使うため

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

// 追加: CPU 墨汁システム
#include "CpuInk.h"
#include "GpuInk.h"

///////////////////////////////////////////////////////////////////////////////
// Defines

// Colors for touch points
#define NO_CONFIDENCE_COLOR	RGB(255,128,0)		// orange
#define CONFIDENCE_COLOR		RGB(0, 0, 255)		// blue
#define POSITION_ONLY_COLOR	RGB(0, 255, 0)		// green

// Graphics HPEN objects
#define NUM_HPENS		10

///////////////////////////////////////////////////////////////////////////////
// Wintab support headers
#define PACKETDATA	(PK_X | PK_Y | PK_Z | PK_BUTTONS | PK_NORMAL_PRESSURE | PK_TANGENT_PRESSURE | PK_TIME | PK_ORIENTATION)
#define PACKETMODE	PK_BUTTONS
#include "pktdef.h"

///////////////////////////////////////////////////////////////////////////////
// Types

using WacomMTHitRectPtr = std::unique_ptr<WacomMTHitRect>;

enum class EDataType
{
	ENoData,
	EFingerData,
	EBlobData,
	ERawData
};

///////////////////////////////////////////////////////////////////////////////
// Global Variables

HINSTANCE								hInst = NULL;
std::wstring							szTitle = L"WacomMT_Scribble Pen, Consumer, Finger, HWND";
std::wstring							szWindowClass = L"WACOMMT_SCRIBBLE";
HWND										g_mainWnd = NULL;
HDC										g_hdc = NULL;
HWND										g_hWndAbout = NULL;
int										g_maxPressure = 1024;

// Cached client rect (system coordinates).
// Used for evaluating whether or not to render pen data by verifying whether
// the returned pen data (sys coords) falls within the client rect. Returned 
// touch contact locations use this rect to interpolate where they should be drawn.
// Similar interpolation done for raw and blob data rendering as well.
// This rect needs to be updated when the app is moved or resized.

RECT										g_clientRect = { 0, 0, 0, 0 };

bool										g_ShowTouchSize = true;
bool										g_ShowTouchID = false;
std::map<int, WacomMTCapability>	g_caps;
std::vector<int>						g_devices;

typedef struct 
{
	int		maxPressure;
	COLORREF	penColor;
	char		name[32];
	LONG		tabletXExt;
	LONG		tabletYExt;
	bool		displayTablet;
	int 		maxZ;
} TabletInfo;

std::map<HCTX, TabletInfo> g_contextMap;
bool g_openSystemContext = true;

bool OpenTabletContexts(HWND hWnd);
void CloseTabletContexts(void);

std::map<int, HPEN>					g_hPenMap;
std::map<int, HPEN>					g_fingerHPenMap;

HBRUSH									g_noConfidenceBrush = NULL;
HBRUSH									g_confidenceBrush = NULL;
HBRUSH									g_positionOnlyBrush = NULL;
HPEN										g_noConfidencePen = NULL;
HPEN										g_confidencePen = NULL;
std::map<int, WacomMTHitRectPtr>	g_lastWTHitRect;

bool										g_useConfidenceBits = true;
bool										g_ObserverMode = false;

EDataType								g_DataType = EDataType::EFingerData;
bool										g_UseHWND = true;
bool										g_UseWinHitRect = true;

CRITICAL_SECTION						g_graphicsCriticalSection;

// 追加: GPU 墨汁オブジェクト（グローバル）
static GpuInk g_gpuInk;

HWND g_hInkWnd = NULL;
HWND g_hMonitorWnd = NULL;
static ATOM g_inkWndClassAtom = 0;
static ATOM g_monitorWndClassAtom = 0;

// forward
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

		// 視覚的 Z軸 (Distance) メーターバーの描画
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

// CreateInkWindow / DestroyInkWindow 実装
static bool CreateInkWindow()
{
	if (g_hInkWnd) return true;

	// クラス登録（既に登録済みならスキップ）
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
		if (!g_inkWndClassAtom)
		{
			return false;
		}
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

	// ウィンドウ生成（ツールウィンドウ風）
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

// Ink window proc: シンプルに m_ink を数値で描画する
LRESULT CALLBACK InkWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
	{
		// フォント作成（固定幅）
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

		// 追加: ペイント領域を背景色でクリアして前回の文字残り（重なり）を消す
		FillRect(hdc, &ps.rcPaint, GetSysColorBrush(COLOR_WINDOW));

		HFONT hFont = (HFONT)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
		HFONT hOld = hFont ? (HFONT)SelectObject(hdc, hFont) : NULL;
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, RGB(0, 0, 0));

		// スナップショット取得
		std::vector<int> ink;
		int iw = 0, ih = 0;
		g_gpuInk.GetInkSnapshot(ink, iw, ih);

		if (ink.empty() || iw <= 0 || ih <= 0)
		{
			TextOutW(hdc, 4, 4, L"(no data)", 8);
		}
		else
		{
			// 表示する行数・列数を制限（大きいバッファはスクロール機能が必要）
			const int maxRows = 40;
			const int maxCols = 80;
			int rows = std::min(ih, maxRows);
			int cols = std::min(iw, maxCols);

			// 行ごとに結合して表示（見やすさのため列をスペースで区切る）
			int lineHeight = 16; // フォントサイズに合わせ必要なら GetTextMetrics で取得
			for (int r = 0; r < rows; ++r)
			{
				std::wstring line;
				line.reserve(cols * 5);
				for (int c = 0; c < cols; ++c)
				{
					int v = ink[r * iw + c];
					// 幅揃え（最大 4 桁 想定）
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

///////////////////////////////////////////////////////////////////////////////
// Forward declarations of functions included in this code module

ATOM					MyRegisterClass(HINSTANCE hInstance);
BOOL					InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	About(HWND, UINT, WPARAM, LPARAM);
void					ClearScreen();

///////////////////////////////////////////////////////////////////////////////
// Wintab support functions.

void Cleanup(void);

///////////////////////////////////////////////////////////////////////////////

BOOL Square(HDC hDC, int x, int y, int width)
{
	int offset = width / 2;
	return Rectangle(hDC, x - offset, y - offset, x + offset, y + offset);
}

///////////////////////////////////////////////////////////////////////////////

BOOL Circle(HDC hDC, int x, int y, int r)
{
	return Ellipse(hDC, x - r, y - r, x + r, y + r);
}

///////////////////////////////////////////////////////////////////////////////

BOOL CenterEllipse(HDC hDC, int x, int y, int w, int h)
{
	return Ellipse(hDC, x - w, y - h, x + w, y + h);
}

///////////////////////////////////////////////////////////////////////////////

WacomMTProcessingMode CurrentMode(void)
{
	return g_ObserverMode
		? WMTProcessingModeObserver
		: WMTProcessingModeNone;
}

///////////////////////////////////////////////////////////////////////////////

std::wstring GetTitle(void)
{
	std::wstring title = L"WacomMT_Scribble Pen";
	title.append(L", ");
	title.append(g_ObserverMode ? L"Observer" : L"Consumer");
	title.append(L", ");

	switch (g_DataType)
	{
	case EDataType::ENoData:
	{
		title.append(L"No Touch");
		break;
	}
	case EDataType::EFingerData:
	{
		title.append(L"Finger");
		break;
	}
	case EDataType::EBlobData:
	{
		title.append(L"Blob");
		break;
	}
	case EDataType::ERawData:
	{
		title.append(L"Raw");
		break;
	}
	default:
	{
		title.append(L"Unknown");
		break;
	}
	}

	title.append(L", ");
	title.append(g_UseHWND ? L"HWND" : g_UseWinHitRect ? L"Windowed" : L"Full Screen");
	return title;
}

///////////////////////////////////////////////////////////////////////////////

std::string GetStateString(WacomMTFingerState state_I)
{
	switch (state_I)
	{
	case WMTFingerStateDown:
	{
		return "D";
	}
	case WMTFingerStateHold:
	{
		return "H";
	}
	case WMTFingerStateUp:
	{
		return "U";
	}
	default:
	{
		return "N";
	}
	}
}

///////////////////////////////////////////////////////////////////////////////
// Purpose
//		Entrypoint (main) function for this application.
//
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

	// Initialize global strings
	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance(hInstance, nCmdShow))
	{
		return FALSE;
	}

	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WACOMMT_SCRIBBLE));

	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	// Cleanup global variables
	if (g_noConfidenceBrush)
	{
		DeleteObject(g_noConfidenceBrush);
		g_noConfidenceBrush = NULL;
	}

	if (g_confidenceBrush)
	{
		DeleteObject(g_confidenceBrush);
		g_confidenceBrush = NULL;
	}

	if (g_positionOnlyBrush)
	{
		DeleteObject(g_positionOnlyBrush);
		g_positionOnlyBrush = NULL;
	}

	if (g_noConfidencePen)
	{
		DeleteObject(g_noConfidencePen);
		g_noConfidencePen = NULL;
	}

	if (g_confidencePen)
	{
		DeleteObject(g_confidencePen);
		g_confidencePen = NULL;
	}

	DeleteCriticalSection(&g_graphicsCriticalSection);

	return static_cast<int>(msg.wParam);
}

///////////////////////////////////////////////////////////////////////////////
//	Purpose
//		Registers the window class.
//
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
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCE(IDC_WACOMMT_SCRIBBLE);
	wcex.lpszClassName = szWindowClass.c_str();
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}

///////////////////////////////////////////////////////////////////////////////
// Purpose
//		Saves instance handle and creates main window
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance; // Store instance handle in our global variable

	// 初期ウィンドウを 100x100 に指定
	g_mainWnd = CreateWindow(szWindowClass.c_str(),
		szTitle.c_str(),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		0,
		400,   // width = 100px
		400,   // height = 100px
		NULL,
		NULL,
		hInstance,
		NULL);

	if (!g_mainWnd)
	{
		return FALSE;
	}

	g_hdc = GetDC(g_mainWnd);

	// Create a brush and pens
	g_noConfidenceBrush = CreateSolidBrush(NO_CONFIDENCE_COLOR);
	g_confidenceBrush = CreateSolidBrush(CONFIDENCE_COLOR);
	g_positionOnlyBrush = CreateSolidBrush(POSITION_ONLY_COLOR);
	g_noConfidencePen = CreatePen(PS_SOLID, 3, NO_CONFIDENCE_COLOR);
	g_confidencePen = CreatePen(PS_SOLID, 3, CONFIDENCE_COLOR);

	// 起動時は通常表示
	ShowWindow(g_mainWnd, SW_SHOWNORMAL);
	UpdateWindow(g_mainWnd);

	return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// Purpose
//		Processes messages for the main window.
//
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

		// Initialize GPU ink with client size
		{
			int w = g_clientRect.right - g_clientRect.left;
			int h = g_clientRect.bottom - g_clientRect.top;
			if (w > 0 && h > 0)
			{
				g_gpuInk.Initialize(hWnd, w, h);
			}
		}

		// Create pens with random colors, which will be assigned to fingerIDs.
		for (int idx = 0; idx < NUM_HPENS; idx++)
		{
			g_hPenMap[idx] = CreatePen(PS_SOLID, 2, RGB(rand() % 255, rand() % 255, rand() % 255));
		}

		// Initialize Wintab API using ScribbleDemo multi-context initialization
		if (!OpenTabletContexts(hWnd))
		{
			ShowError("Could Not Open Wintab Tablet Contexts.");
		}
		break;
	}

	case WM_TIMER:
	{
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	case WM_CLOSE:
	{
		// Cleanup pens on close
		for (int idx = 0; idx < NUM_HPENS; idx++)
		{
			DeleteObject(g_hPenMap[idx]);
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	// Handle keyboard input
	case WM_KEYDOWN:
	{
		switch (wParam)
		{
			// Escape key clears the screen
		case VK_ESCAPE:
		{
			ClearScreen();
			break;
		}

		// 追加: I キーで Ink Viewer ウィンドウの表示/非表示
		case 'I':
		case 'i':
		{
			if (!g_hInkWnd) CreateInkWindow();
			else DestroyInkWindow();
			break;
		}

		// TODO - Handle other keys here

		default:
		{
			break;
		}
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
		{
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		}
		break;
	}

	case WM_PAINT:
	{
		PAINTSTRUCT ps = { 0 };
		HDC hdc = BeginPaint(hWnd, &ps);

		// GPU 墨汁を合成して描画
		g_gpuInk.Render(hdc, 0, 0);

		EndPaint(hWnd, &ps);
		break;
	}

	// --- ここからマウス入力をハンドルするケースを追加 ---
	case WM_LBUTTONDOWN:
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		SetCapture(hWnd);
		s_ptMouseOld = pt;
		InvalidateRect(hWnd, NULL, FALSE);
		break;
	}

	case WM_MOUSEMOVE:
	{
		if (wParam & MK_LBUTTON)
		{
			POINT ptNew = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			double dx = (double)(ptNew.x - s_ptMouseOld.x);
			double dy = (double)(ptNew.y - s_ptMouseOld.y);
			double dist = std::hypot(dx, dy);

			if (dist > 0.1)
			{
				double pressureFactor = 0.5;
				double haraiPower = 2.7 + 0.5 * (std::min)(dist, 10.0);
				double haraiFactor = std::pow(pressureFactor, haraiPower);
				double rawWidth = 36.0 * haraiFactor;
				int penWidth = (int)std::round(rawWidth);
				if (penWidth < 1) penWidth = 1;

				g_gpuInk.DrawSegment(s_ptMouseOld, ptNew, (double)penWidth, 255);
			}
			s_ptMouseOld = ptNew;
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;
	}

	case WM_LBUTTONUP:
	{
		g_gpuInk.EndStroke();
		ReleaseCapture();
		InvalidateRect(hWnd, NULL, FALSE);
		break;
	}
	// --- マウス入力ハンドラここまで ---

	case WM_SETTINGCHANGE:
	{
		if (lParam)
		{
			//DebugTrace("WM_SETTINGCHANGE %i, %S\n", wParam, lParam);
		}
		else
		{
			//DebugTrace("WM_SETTINGCHANGE %i, NULL\n", wParam);
		}
		break;
	}

	case WM_DESTROY:
	{
		ReleaseDC(hWnd, g_hdc);
		CloseTabletContexts();

		// Return Wintab and MTAPI resources.
		Cleanup();

		PostQuitMessage(0);
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
			g_gpuInk.Resize(w, h);
		}
		break;
	}

	// Capture pen data (ScribbleDemo 方式).
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

			// 筆離れ判定: ノイズ以下の微少筆圧 (1.0%) は筆離れとみなす
			UINT minPrsThreshold = static_cast<UINT>(maxPrs * 0.01);
			if (prsNew <= minPrsThreshold)
			{
				prsNew = 0;
			}

			if (prsNew > 0)
			{
				POINT newPoint = { ptNew.x, ptNew.y };
				if (g_openSystemContext)
				{
					ScreenToClient(hWnd, &newPoint);
				}

				POINT oldPoint = { ptOld.x, ptOld.y };
				if (g_openSystemContext)
				{
					ScreenToClient(hWnd, &oldPoint);
				}

				// ストロークの書き始め (着地瞬間) または離筆状態からの開始
				if (!s_strokeActive || prsOld == 0 || !g_gpuInk.IsInStroke())
				{
					s_strokeActive = true;
					ptOld = ptNew;
					oldPoint = newPoint; // oldPoint == newPoint に即時同期！
				}

				double dx = (double)(newPoint.x - oldPoint.x);
				double dy = (double)(newPoint.y - oldPoint.y);
				double dist = std::hypot(dx, dy);

				// 異常距離ジャンプ (パケット飛び > 50px)
				if (dist > 50.0)
				{
					oldPoint = newPoint;
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

				// 太さ計算 (ScribbleDemo 方式)
				double tomeFactor = 1.0 + 0.1 * (1.0 - (std::min)(dist / 3.0, 1.0)) * std::pow(pressureFactor, 0.8);
				double haraiPower = 2.7 + 0.5 * (std::min)(dist, 10.0);
				double haraiFactor = std::pow(pressureFactor, haraiPower);
				double angleDiff = std::sin(azimuthRad - (moveAngle + 1.57079632679));
				double angleFactor = 1.0 + 0.3 * std::abs(angleDiff);

				double baseMaxWidth = 36.0;
				double rawWidth = baseMaxWidth * haraiFactor * tomeFactor * angleFactor * (1.0 + tiltFactor * 0.6);

				// スムージング処理
				if (dist == 0.0 || !g_gpuInk.IsInStroke())
				{
					s_smoothedWidth = rawWidth;
				}
				else
				{
					const double alpha = 0.3;
					s_smoothedWidth = s_smoothedWidth * (1.0 - alpha) + rawWidth * alpha;
				}

				// インクスタンプ描画
				g_gpuInk.DrawSegment(oldPoint, newPoint, s_smoothedWidth, 255);
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
	{
		CloseTabletContexts();
		OpenTabletContexts(hWnd);
		break;
	}

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

	default:
	{
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// Purpose
//		Message handler for about box.
//
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
	{
		WacomMTError res = WMTErrorInvalidParam;

		g_hWndAbout = hDlg;
		for (size_t idx = 0; idx < g_devices.size(); idx++)
		{
			int deviceID = g_devices[idx];
			if (g_caps.count(deviceID))
			{
				res = WacomMTRegisterFingerReadHWND(deviceID, WMTProcessingModePassThrough, g_hWndAbout, 5);
				if (res != WMTErrorSuccess)
				{
					break;
				}
			}
		}
		return 1;
	}

	case WM_COMMAND:
	{
		if ((LOWORD(wParam) == IDOK) || (LOWORD(wParam) == IDCANCEL))
		{
			for (size_t idx = 0; idx < g_devices.size(); idx++)
			{
				int deviceID = g_devices[idx];
				if (g_caps.count(deviceID))
				{
					if (WacomMTUnRegisterFingerReadHWND(g_hWndAbout) != WMTErrorSuccess)
					{
						break;
					}
				}
			}
			DestroyWindow(hDlg);
			g_hWndAbout = NULL;

			return 1;
		}
		break;
	}
	}

	return 0;
}


// Purpose
//		Clears the canvas of finger and pen data.
//
void ClearScreen()
{
	// Clear GPU ink buffer and force repaint.
	g_gpuInk.Clear();

	// Also clear background immediate for responsiveness
	RECT cRect = g_clientRect;
	cRect.right -= cRect.left;
	cRect.left = 0;
	cRect.bottom -= cRect.top;
	cRect.top = 0;

	FillRect(g_hdc, &cRect, static_cast<HBRUSH>(GetStockObject((g_ObserverMode ? COLOR_WINDOW : COLOR_APPWORKSPACE) + 1)));

	InvalidateRect(g_mainWnd, NULL, FALSE);
}

///////////////////////////////////////////////////////////////////////////////
// Wintab support functions

///////////////////////////////////////////////////////////////////////////////
//  Purpose
//		Loads the Wintab32 DLL and sets up the API function pointers.
//
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

///////////////////////////////////////////////////////////////////////////////