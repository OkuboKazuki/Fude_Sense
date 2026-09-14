#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"
#include "GpuInk.h"

class MainView {
public:
    // メインウィンドウのダブルバッファリング一括描画 (WM_PAINT ハンドラ)
    static void Render(HDC hdc, int width, int height, GpuInk& gpuInk, const AppState& state);

    // ダブルバッファ用の裏画面を破棄する（終了時）
    static void ReleaseBackBuffer();
};
