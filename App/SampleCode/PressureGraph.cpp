#include "stdafx.h"
#include "PressureGraph.h"
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

// グローバル変数 =====================
static HWND g_hWnd = NULL;          // グラフウィンドウのハンドル
static HINSTANCE g_hInst = NULL;    // インスタンスハンドル
static std::vector<int> g_pressure; // 筆圧データ
static int MAX_DATA = 300;          // 最大データ数
static int g_maxPressure = 32767;    // 最大筆圧 (デフォルト)
static const int GRAPH_WIDTH = 600; // グラフ描画幅
static const int GRAPH_HEIGHT = 250;// グラフ描画高さ
static const int LEFT_MARGIN = 50;  // 左余白
static const int RIGHT_MARGIN = 20; // 右余白
static const int TOP_MARGIN = 20;   // 上余白
static const int BOTTOM_MARGIN = 30;// 下余白 (数値表示用)
static const int GRID_COLOR = RGB(220, 220, 220); // グリッド色
static const int AXIS_COLOR = RGB(0, 0, 0);       // 軸の色
static const int LINE_COLOR = RGB(0, 0, 255);     // 筆圧線の色
static const int TEXT_COLOR = RGB(0, 0, 0);       // 文字色

LRESULT CALLBACK PressureGraphProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void DrawGraph(HDC hdc, const RECT& rcClient);
void DrawGrid(HDC hdc, const RECT& rcClient);
void DrawPressureLine(HDC hdc, const RECT& rcClient);
std::wstring IntToWString(int value);

LRESULT CALLBACK PressureGraphProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        return 0;
    case WM_PAINT:
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        DrawGraph(hdc, rcClient);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);

}
std::wstring IntToWString(int value)
{
    std::wstringstream ss;
    ss << value;
    return ss.str();
}
void DrawGrid(HDC hdc, const RECT& rcClient)
{
    HPEN hGridPen = CreatePen(PS_SOLID, 1, GRID_COLOR);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hGridPen);

    int graphLeft = LEFT_MARGIN;
    int graphRight = rcClient.right - RIGHT_MARGIN;
    int graphTop = TOP_MARGIN;
    int graphBottom = rcClient.bottom - BOTTOM_MARGIN;

    // 横線
    for (int i = 0; i <= 5; i++)
    {
        int y = graphTop + (graphBottom - graphTop) * i / 5;

        MoveToEx(hdc, graphLeft, y, NULL);
        LineTo(hdc, graphRight, y);
    }

    // 縦線
    for (int i = 0; i <= 10; i++)
    {
        int x = graphLeft + (graphRight - graphLeft) * i / 10;

        MoveToEx(hdc, x, graphTop, NULL);
        LineTo(hdc, x, graphBottom);
    }

    SelectObject(hdc, hOldPen);
    DeleteObject(hGridPen);
}


void DrawGraph(HDC hdc, const RECT& rcClient)
{
    Rectangle(
        hdc,
        LEFT_MARGIN,
        TOP_MARGIN,
        rcClient.right - RIGHT_MARGIN,
        rcClient.bottom - BOTTOM_MARGIN);

    DrawGrid(hdc, rcClient);

    DrawPressureLine(hdc, rcClient);

    SetTextColor(hdc, TEXT_COLOR);
    SetBkMode(hdc, TRANSPARENT);

    TextOut(
        hdc,
        10,
        5,
        L"Pressure Graph",
        14);

    TextOut(
        hdc,
        5,
        TOP_MARGIN,
        L"32767",
        5);

    TextOut(
        hdc,
        15,
        rcClient.bottom - BOTTOM_MARGIN,
        L"0",
        1);
}

void DrawPressureLine(HDC hdc, const RECT& rcClient)
{
    if (g_pressure.size() < 2)
        return;

    int graphLeft = LEFT_MARGIN;
    int graphRight = rcClient.right - RIGHT_MARGIN;
    int graphTop = TOP_MARGIN;
    int graphBottom = rcClient.bottom - BOTTOM_MARGIN;

    int graphWidth = graphRight - graphLeft;
    int graphHeight = graphBottom - graphTop;

    // 線を青色にする
    HPEN hPen = CreatePen(PS_SOLID, 2, LINE_COLOR);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    // X方向の間隔
    double dx = (double)graphWidth / (MAX_DATA - 1);

    // 最初の点
    int x = graphLeft;
    int y = graphBottom -
        (int)((double)g_pressure[0] / g_maxPressure * graphHeight);

    MoveToEx(hdc, x, y, NULL);

    // 折れ線描画
    for (size_t i = 1; i < g_pressure.size(); i++)
    {
        x = graphLeft + (int)(dx * i);

        y = graphBottom -
            (int)((double)g_pressure[i] / g_maxPressure * graphHeight);

        LineTo(hdc, x, y);
    }

    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);

    // 現在の筆圧表示
    if (!g_pressure.empty())
    {
        std::wstring str =
            L"Current Pressure : " +
            IntToWString(g_pressure.back());

        TextOutW(
            hdc,
            LEFT_MARGIN,
            rcClient.bottom - 20,
            str.c_str(),
            (int)str.length());
    }
}

//======================================================
// グラフウィンドウ生成
//======================================================
bool CreatePressureGraph(HINSTANCE hInstance)
{
    g_hInst = hInstance;

    WNDCLASS wc = {};

    wc.lpfnWndProc = PressureGraphProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"PressureGraphClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClass(&wc);

    g_hWnd = CreateWindow(
        L"PressureGraphClass",
        L"Pressure Graph",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        900,
        100,
        700,
        400,
        NULL,
        NULL,
        hInstance,
        NULL);

    return (g_hWnd != NULL);
}

//======================================================
// グラフウィンドウ破棄
//======================================================
void DestroyPressureGraph()
{
    if (g_hWnd != NULL)
    {
        DestroyWindow(g_hWnd);
        g_hWnd = NULL;
    }
}

//======================================================
// 筆圧追加
//======================================================
void AddPressureData(int pressure)
{
    if (pressure < 0)
        pressure = 0;

    if (pressure > g_maxPressure)
        pressure = g_maxPressure;

    g_pressure.push_back(pressure);

    if ((int)g_pressure.size() > MAX_DATA)
    {
        g_pressure.erase(g_pressure.begin());
    }

    RefreshPressureGraph();
}

//======================================================
// グラフ更新
//======================================================
void RefreshPressureGraph()
{
    if (g_hWnd != NULL)
    {
        InvalidateRect(g_hWnd, NULL, TRUE);
        UpdateWindow(g_hWnd);
    }
}
//======================================================
// 最大筆圧設定
//======================================================
void SetMaxPressure(int maxPressure)
{
    if (maxPressure > 0)
    {
        g_maxPressure = maxPressure;
    }
}
