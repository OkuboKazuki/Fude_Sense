#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "PenInputEvent.h"
#include "AppState.h"
#include "GpuInk.h"

class StrokeController {
public:
    StrokeController();

    // デバイス非依存のペン入力イベントを処理して墨を描画
    void ProcessPenEvent(HWND hWnd, const PenInputEvent& event, AppState& state, GpuInk& gpuInk);

    // ストロークのリセット / ペンが離れたときの処理
    void ResetStroke(GpuInk& gpuInk);

private:
    POINT m_ptOld = { 0, 0 };
    double m_smoothedPressure = 0.0;
    double m_smoothedWidth = 0.0;
    bool m_strokeActive = false;
};
