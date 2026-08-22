#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include "AppState.h"
#include "GpuInk.h"

class ImageExporter {
public:
    // キャンバス作品をクリップボードまたはデスクトップBMPファイルへ保存
    static bool ExportCanvas(HWND hWnd, GpuInk& gpuInk, AppState& state, bool toClipboard);
};
