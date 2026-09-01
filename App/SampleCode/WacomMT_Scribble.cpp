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
#include "MainView.h"
#include "PenInputEvent.h"
#include "WintabAdapter.h"
#include "MouseAdapter.h"
#include "StrokeController.h"
#include "AppController.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

bool OpenTabletContexts(HWND hWnd);
void CloseTabletContexts(void);
void Cleanup(void);

// Global Variables
HINSTANCE hInst = NULL;
std::wstring szTitle = L"SHUJI STUDIO - 習字制作ワークスペース";
std::wstring szWindowClass = L"FUDESENCE";
HWND g_mainWnd = NULL;
HDC g_hdc = NULL;
HWND g_hWndAbout = NULL;

RECT g_clientRect = { 0, 0, 0, 0 };

static AppState g_appState;
static WintabManager g_wintab;
static GpuInk g_gpuInk;
static StrokeController g_strokeCtrl;
static AppController g_appCtrl;

HWND g_hInkWnd = NULL;
HWND g_hMonitorWnd = NULL;
static ATOM g_inkWndClassAtom = 0;
static ATOM g_monitorWndClassAtom = 0;

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
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_FUDESENCE));
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

	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_FUDESENCE));

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
	static bool s_isMouseDrawing = false;

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
		g_appState.Layout(w, h);

		int pw = RenderUtils::RW(g_appState.ui.rPaper);
		int ph = RenderUtils::RH(g_appState.ui.rPaper);
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
			AppController::ClearAllInk(hWnd, g_appState, g_gpuInk);
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
			AppController::ClearAllInk(hWnd, g_appState, g_gpuInk);
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
		return 1;

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
		if (AppController::OnLButtonDown(hWnd, pt, g_appState, g_gpuInk))
		{
			// UI を操作したクリックは運筆ではない。進行中のストロークがあれば
			// ここで打ち切る（運筆ロック解除はペンが紙から離れたときに行う）。
			g_strokeCtrl.ResetStroke(g_gpuInk);
		}
		else
		{
			// 半紙上での新しい押下なので運筆ロックを解除（マウス操作時の解除経路）
			g_appState.ui.suppressPenUntilLift = false;

			// ペン (Wintab) での描画中以外のみマウスによる運筆描画を許可
			if (!g_gpuInk.IsInStroke() && RenderUtils::PtIn(g_appState.ui.rPaper, pt))
			{
				SetCapture(hWnd);
				s_isMouseDrawing = true;
				PenInputEvent penEvent = MouseAdapter::CreatePenEvent(pt, true);
				g_strokeCtrl.ProcessPenEvent(hWnd, penEvent, g_appState, g_gpuInk);
			}
		}
		break;
	}

	case WM_MOUSEMOVE:
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		AppController::OnMouseMove(hWnd, pt, wParam, g_appState);

		// ペン描画中はマウスイベントによる描画を完全に無視（等間隔のイボ・定期割り込みを防止）
		if (!g_gpuInk.IsInStroke() && s_isMouseDrawing && (wParam & MK_LBUTTON))
		{
			if (RenderUtils::PtIn(g_appState.ui.rPaper, pt))
			{
				PenInputEvent penEvent = MouseAdapter::CreatePenEvent(pt, true);
				g_strokeCtrl.ProcessPenEvent(hWnd, penEvent, g_appState, g_gpuInk);
			}
		}
		break;
	}

	case WM_LBUTTONUP:
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		AppController::OnLButtonUp(hWnd, pt, g_appState);

		if (s_isMouseDrawing)
		{
			s_isMouseDrawing = false;
			if (!g_gpuInk.IsInStroke())
			{
				PenInputEvent penEvent = MouseAdapter::CreatePenEvent(pt, false);
				g_strokeCtrl.ProcessPenEvent(hWnd, penEvent, g_appState, g_gpuInk);
				g_strokeCtrl.ResetStroke(g_gpuInk);
			}
			ReleaseCapture();
		}
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
			AppController::OnSize(hWnd, w, h, g_appState, g_gpuInk);
		}
		break;
	}

	case WT_PACKET:
	{
		PenInputEvent penEvent;
		if (WintabAdapter::ConvertPacket(hWnd, wParam, lParam, g_wintab, penEvent))
		{
			g_strokeCtrl.ProcessPenEvent(hWnd, penEvent, g_appState, g_gpuInk);
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
			g_strokeCtrl.ResetStroke(g_gpuInk);
			g_gpuInk.UpdatePenZ(0, 0, 0, true);
			// ペンが圏外へ出た＝紙から離れたので運筆ロックを解除
			g_appState.ui.suppressPenUntilLift = false;
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
