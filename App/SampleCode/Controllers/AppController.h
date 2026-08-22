#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"
#include "GpuInk.h"

class AppController {
public:
    // UIイベントハンドリング
    static bool OnLButtonDown(HWND hWnd, POINT pt, AppState& state, GpuInk& gpuInk);
    static bool OnLButtonUp(HWND hWnd, POINT pt, AppState& state);
    static bool OnMouseMove(HWND hWnd, POINT pt, WPARAM wParam, AppState& state);
    static void OnSize(HWND hWnd, int width, int height, AppState& state, GpuInk& gpuInk);
    static void ClearAllInk(HWND hWnd, AppState& state, GpuInk& gpuInk);
};
