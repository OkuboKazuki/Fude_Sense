#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"

// リアルタイム運筆解析・グラフ可視化ビュー
class AnalysisView {
public:
    static void Draw(HDC dc, const AppState& state);

private:
    static void DrawMetricsCard(HDC dc, const RECT& rBox, const AppState& state);
    static void DrawTiltCompass(HDC dc, const RECT& rBox, const AppState& state);
    static void DrawWaveformGraph(HDC dc, const RECT& rBox, const AppState& state);
};
