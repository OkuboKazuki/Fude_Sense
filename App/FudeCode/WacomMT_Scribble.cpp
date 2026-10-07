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

// 前回の再生タイマ刻。停止中は 0。
static DWORD g_lastReplayTick = 0;
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
		g_appState.Layout(w, h);

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
		if (wParam == TRANSITION_TIMER_ID)
		{
			if (!g_appState.isTransitioning)
			{
				KillTimer(hWnd, TRANSITION_TIMER_ID);
				return 0;
			}

			DWORD now = GetTickCount();
			DWORD elapsed = now - g_appState.transitionStartTime;
			if (elapsed >= g_appState.transitionDurationMs)
			{
				g_appState.isTransitioning = false;
				g_appState.transitionProgress = 1.0f;
				g_appState.currentScreen = AppScreen::Studio;
				g_appState.ui.leftTab = LeftTab::Brush;
				KillTimer(hWnd, TRANSITION_TIMER_ID);
			}
			else
			{
				g_appState.transitionProgress = static_cast<float>(elapsed) / static_cast<float>(g_appState.transitionDurationMs);
			}
			InvalidateRect(hWnd, NULL, FALSE);
			return 0;
		}

		if (wParam == TITLE_ANIM_TIMER_ID)
		{
			if (g_appState.currentScreen != AppScreen::Title || g_appState.isTransitioning)
			{
				KillTimer(hWnd, TITLE_ANIM_TIMER_ID);
				return 0;
			}
			InvalidateRect(hWnd, NULL, FALSE);
			return 0;
		}

		if (wParam == REPLAY_TIMER_ID)
		{
			// 再生中でなくなったらタイマを止める。再び張るのは再生ボタン側。
			if (g_appState.ui.leftTab != LeftTab::Analysis || g_appState.replay.state != ReplayState::Playing)
			{
				KillTimer(hWnd, REPLAY_TIMER_ID);
				g_lastReplayTick = 0;
				return 0;
			}

			DWORD now = GetTickCount();
			DWORD dt = (g_lastReplayTick != 0) ? (now - g_lastReplayTick) : 16;
			if (dt > 100) dt = 16;
			g_lastReplayTick = now;

			DWORD advanceMs = static_cast<DWORD>(dt * g_appState.replay.playbackSpeed);
			if (advanceMs < 1 && g_appState.replay.playbackSpeed > 0.0) advanceMs = 1;

			g_appState.replay.currentTimeMs += advanceMs;
			if (g_appState.replay.currentTimeMs >= g_appState.replay.totalDurationMs)
			{
				g_appState.replay.currentTimeMs = g_appState.replay.totalDurationMs;
				g_appState.replay.state = ReplayState::Paused;
				KillTimer(hWnd, REPLAY_TIMER_ID);
				g_lastReplayTick = 0;
			}

			g_appState.replay.hasValidSample = g_appState.AnalysisSession().GetReplaySample(
				g_appState.replay.currentTimeMs, g_appState.ui.rPaper, g_appState.replay.currentSample);

			// 動くのは半紙のリプレイと解析パネルだけ。Layout() 全体も全画面の
			// 無効化も要らない。
			g_appState.UpdateReplaySeekThumb();
			InvalidateRect(hWnd, &g_appState.ui.rPaper, FALSE);
			InvalidateRect(hWnd, &g_appState.ui.rSub, FALSE);
		}
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

		// F9 は「紙だけ表示（横向き）」の切り替え。硯パネルのボタンと同じ働き。
		if (wParam == VK_F9)
		{
			AppController::SetPaperOnly(hWnd, !g_appState.ui.paperOnly, g_appState, g_gpuInk);
			break;
		}

		// 文字入力中はショートカット（Esc=全消しの確認 / Ctrl+Z・Ctrl+Y=一画戻す・復元）を止める。
		// Esc と BackSpace は WM_CHAR 側で入力終了・1文字削除として処理する。
		if (g_appState.otehon.isTyping) break;

		switch (wParam)
		{
		case VK_ESCAPE:
		{
			// 確認モーダル表示中の Esc は「キャンセル」として閉じる
			if (g_appState.ui.showClearConfirm)
			{
				g_appState.ui.showClearConfirm = false;
				InvalidateRect(hWnd, NULL, FALSE);
				break;
			}

			// 硯パネルの「全消し」ボタンと同じ確認モーダルを出す。
			// 紙だけ表示では倒した向きで読めるよう回して描く（ModalView::DrawClearConfirm）。
			g_appState.ui.hoverClearModal = 0;
			g_appState.ui.showClearConfirm = true;
			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		case 'Z':
		case 'z':
		{
			// Ctrl+Z で一画戻す、Ctrl+Shift+Z で一画復元
			if (GetKeyState(VK_CONTROL) < 0)
			{
				if (GetKeyState(VK_SHIFT) < 0)
				{
					AppController::RedoStroke(hWnd, g_appState, g_gpuInk);
				}
				else
				{
					AppController::UndoStroke(hWnd, g_appState, g_gpuInk);
				}
			}
			break;
		}
		case 'Y':
		case 'y':
		{
			// Ctrl+Y でも一画復元
			if (GetKeyState(VK_CONTROL) < 0)
			{
				AppController::RedoStroke(hWnd, g_appState, g_gpuInk);
			}
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
			MainView::Render(hdc, w, h, g_gpuInk, g_appState, ps.rcPaint);
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
			g_appState.ui.suppressPenUntilLift = false;

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

		// ボタンが離れた＝紙から離れたので運筆ロックを解除（マウス操作時の解除経路）。
		// これがないとホームボタンでタイトルへ戻った後、マウスのクリックが
		// ロックに阻まれてスタジオ画面へ入れなくなる。
		g_appState.ui.suppressPenUntilLift = false;

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
				g_appState.ui.suppressPenUntilLift = false;
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
					g_appState.ui.suppressPenUntilLift = false;
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
			// ペンが圏外へ出た＝紙から離れたので運筆ロックを解除
			g_appState.ui.suppressPenUntilLift = false;
			g_appState.ui.isPenRefilling = false;
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
