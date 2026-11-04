///////////////////////////////////////////////////////////////////////////////
//
//	PURPOSE
//		Fude Sense - Wintab32 / Windows Pointer GPU Ink Application
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
#include "CanvasView.h"
#include "PenInputEvent.h"
#include "WintabAdapter.h"
#include "WindowsPointerAdapter.h"
#include "MouseAdapter.h"
#include "StrokeController.h"
#include "AppController.h"
#include "AnalysisView.h"
#include "ScreenLayout.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

bool OpenTabletContexts(HWND hWnd);
void CloseTabletContexts(void);
void Cleanup(void);

// Global Variables
HINSTANCE hInst = NULL;
std::wstring szTitle = L"Fude Sense";
std::wstring szWindowClass = L"FUDESENSE";
HWND g_mainWnd = NULL;
HWND g_hWndAbout = NULL;

RECT g_clientRect = { 0, 0, 0, 0 };

static AppState g_appState;
static WintabManager g_wintab;
static GpuInk g_gpuInk;
static StrokeController g_strokeCtrl;
static AppController g_appCtrl;

DWORD g_lastWintabTick = 0;

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

// ===== 全画面表示（F11 で切り替え） =====
// 枠なしウィンドウをモニタいっぱいに広げる方式。元へ戻せるよう、切り替え前の
// ウィンドウスタイルと配置（最大化状態を含む）を控えておく。
// ショートカットキーを受け付けてよいか。タイトル画面と遷移中は F11 以外を止める。
static bool IsStudioAcceptingKeys()
{
	return g_appState.currentScreen == AppScreen::Studio && !g_appState.isTransitioning;
}

static bool g_isFullscreen = false;
static WINDOWPLACEMENT g_prevPlacement = { sizeof(WINDOWPLACEMENT) };
static LONG_PTR g_prevStyle = 0;
static LONG_PTR g_prevExStyle = 0;

static void ToggleFullscreen(HWND hWnd)
{
	if (!g_isFullscreen)
	{
		// マルチモニタでは、いまウィンドウが載っているモニタいっぱいに広げる
		MONITORINFO mi = { sizeof(MONITORINFO) };
		if (!GetMonitorInfoW(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi)) return;

		g_prevStyle = GetWindowLongPtrW(hWnd, GWL_STYLE);
		g_prevExStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
		g_prevPlacement.length = sizeof(WINDOWPLACEMENT);
		GetWindowPlacement(hWnd, &g_prevPlacement);

		SetWindowLongPtrW(hWnd, GWL_STYLE,
			(g_prevStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU)) | WS_POPUP);
		SetWindowLongPtrW(hWnd, GWL_EXSTYLE,
			g_prevExStyle & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));

		// SWP_FRAMECHANGED でスタイル変更を反映させる。これが無いと枠が残る。
		SetWindowPos(hWnd, HWND_TOP,
			mi.rcMonitor.left, mi.rcMonitor.top,
			mi.rcMonitor.right - mi.rcMonitor.left,
			mi.rcMonitor.bottom - mi.rcMonitor.top,
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

		g_isFullscreen = true;
	}
	else
	{
		SetWindowLongPtrW(hWnd, GWL_STYLE, g_prevStyle);
		SetWindowLongPtrW(hWnd, GWL_EXSTYLE, g_prevExStyle);
		SetWindowPlacement(hWnd, &g_prevPlacement);
		SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

		g_isFullscreen = false;
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
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_FUDESENSE));
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

	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_FUDESENSE));

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
		ScreenLayout::Layout(g_appState.ui, g_appState.paper, g_appState.viewingImport, g_appState.replay, w, h);

		int canvasW = 0, canvasH = 0;
		g_appState.paper.GetCanvasSize(canvasW, canvasH);
		if (canvasW > 0 && canvasH > 0)
		{
			g_gpuInk.Initialize(hWnd, canvasW, canvasH);
		}

		if (!OpenTabletContexts(hWnd))
		{
			OutputDebugStringA("Could Not Open Wintab Tablet Contexts.\n");
		}

		// タイトル画面のブレスアニメーションタイマ開始
		SetTimer(hWnd, TITLE_ANIM_TIMER_ID, 33, NULL);

		break;
	}

	case WM_TIMER:
	{
		AppController::OnTimer(hWnd, wParam, g_appState);
		return 0;
	}

	case WM_CLOSE:
	{
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	case WM_CHAR:
	{
		// お手本の文字入力（IME 変換確定後の文字がここへ届く）。
		// タイトル画面では入力欄が見えないので受け付けない。
		if (!IsStudioAcceptingKeys()) break;
		AppController::OnChar(hWnd, (wchar_t)wParam, g_appState);
		break;
	}

	case WM_KEYDOWN:
	{
		// 全画面の切り替えだけは、お手本の文字入力中でも受け付ける
		if (wParam == VK_F11)
		{
			ToggleFullscreen(hWnd);
			break;
		}

		// タイトル画面と遷移中は、ほかのショートカットを受け付けない。
		// 見えないスタジオ画面の状態（紙だけ表示・一画戻す等）が裏で変わってしまうため。
		if (!IsStudioAcceptingKeys()) break;

		AppController::OnKeyDown(hWnd, wParam, g_appState, g_gpuInk);
		break;
	}

	case WM_COMMAND:
	{
		WORD wmId = LOWORD(wParam);
		switch (wmId)
		{
		case IDM_ABOUT:
		{
			// Alt+/ ・ Alt+? のショートカットで届くので、タイトル画面では開かない
			if (!IsStudioAcceptingKeys()) break;
			if (!IsWindow(g_hWndAbout))
			{
				CreateDialog(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
				ShowWindow(g_hWndAbout, SW_SHOW);
			}
			break;
		}
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	}

	case WM_SETCURSOR:
	{
		if (AppController::OnSetCursor(hWnd, lParam, g_appState))
		{
			return TRUE;
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
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
			MainView::Render(hdc, w, h, g_gpuInk, g_appState, g_appState.ui, ps.rcPaint);
		}

		EndPaint(hWnd, &ps);
		break;
	}

	case WM_LBUTTONDOWN:
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		// ペン (Wintab) 入力の直後なら、このクリックはペンによるもの
		extern DWORD g_lastWintabTick;
		bool isPenActive = (GetTickCount() - g_lastWintabTick <= 500);
		// ペンなら直近の筆圧を渡す（硯の補充量に使う）。マウスは筆圧なし（負値）。
		double penPressure = isPenActive ? g_strokeCtrl.GetLastRawPressure() : -1.0;
		if (AppController::OnLButtonDown(hWnd, pt, g_appState, g_gpuInk, penPressure))
		{
			// UI を操作したクリックは運筆ではない。進行中のストロークがあれば
			// ここで打ち切る（運筆ロック解除はペンが紙から離れたときに行う）。
			g_strokeCtrl.ResetStroke(g_gpuInk, &g_appState);
		}
		else
		{
			// 半紙上での新しい押下なので運筆ロックを解除（マウス操作時の解除経路）
			AppController::ReleasePenLock(g_appState);

			// ペン (Wintab) 入力の直後でない場合のみマウスによる運筆描画を開始
			if (!isPenActive && RenderUtils::PtIn(g_appState.ui.rPaper, pt))
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

		// マウスドラッグ描画中のストローク処理
		if (s_isMouseDrawing && (wParam & MK_LBUTTON))
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
			PenInputEvent penEvent = MouseAdapter::CreatePenEvent(pt, false);
			g_strokeCtrl.ProcessPenEvent(hWnd, penEvent, g_appState, g_gpuInk);
			g_strokeCtrl.ResetStroke(g_gpuInk, &g_appState);
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
		extern DWORD g_lastWintabTick;
		g_lastWintabTick = GetTickCount();
		PenInputEvent penEvent;
		if (WintabAdapter::ConvertPacket(hWnd, wParam, lParam, g_wintab, penEvent))
		{
			if (penEvent.pressure <= 0.0)
			{
				AppController::ReleasePenLock(g_appState);
			}

			if (penEvent.pressure > 0.0 && g_appState.currentScreen == AppScreen::Title)
			{
				if (!g_appState.ui.suppressPenUntilLift)
				{
					POINT pt = { penEvent.x, penEvent.y };
					AppController::OnLButtonDown(hWnd, pt, g_appState, g_gpuInk);
				}
				break;
			}
			g_strokeCtrl.ProcessPenEvent(hWnd, penEvent, g_appState, g_gpuInk);
		}
		break;
	}

	case WM_POINTERDOWN:
	case WM_POINTERUPDATE:
	case WM_POINTERUP:
	{
		// Wintab で正常に入力パケットを受信できている間は Wintab を優先（二重入力防止）
		// Surface 等の Windows Ink デバイスや Wintab 非対応環境で本ルートが自動的にアクティブ化
		extern DWORD g_lastWintabTick;
		if (GetTickCount() - g_lastWintabTick > 500)
		{
			PenInputEvent penEvent;
			if (WindowsPointerAdapter::ConvertPointer(hWnd, message, wParam, lParam, penEvent))
			{
				if (message == WM_POINTERDOWN)
				{
					POINT pt = { penEvent.x, penEvent.y };
					if (AppController::OnLButtonDown(hWnd, pt, g_appState, g_gpuInk, penEvent.pressure))
					{
						g_strokeCtrl.ResetStroke(g_gpuInk, &g_appState);
						break;
					}
				}
				g_strokeCtrl.ProcessPenEvent(hWnd, penEvent, g_appState, g_gpuInk);
				if (message == WM_POINTERUP)
				{
					AppController::ReleasePenLock(g_appState);
					g_strokeCtrl.ResetStroke(g_gpuInk, &g_appState);
				}
				return 0;
			}
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
			g_strokeCtrl.ResetStroke(g_gpuInk, &g_appState);
			g_gpuInk.UpdatePenZ(0, 0, 0, true);
			AppController::OnPenOutOfRange(g_appState);
		}
		break;
	}

	case WM_DESTROY:
	{
		KillTimer(hWnd, REPLAY_TIMER_ID);
		MainView::ReleaseBackBuffer();
		CanvasView::ReleaseReplayCache();
		AnalysisView::ReleaseWaveformCache();
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
