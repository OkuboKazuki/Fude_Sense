#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"
#include "GpuInk.h"

class CanvasView {
public:
    // キャンバス・半紙関連の描画
    static void DrawBackground(HDC dc, const AppState& state);
    static void DrawOtehon(HDC dc, const AppState& state);
    static void DrawGrid(HDC dc, const AppState& state);
    static void RenderInk(HDC dc, GpuInk& gpuInk, const AppState& state);

private:
    static void DrawCross(HDC dc, int x, int y, int s);
};
