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
    static void ReleaseWaveformCache();

private:
    // 運筆アーカイブの読み込み / 自分の記録に戻る
    static void DrawImportBar(HDC dc, const AppState& state);
    static void DrawReplayControls(HDC dc, const RECT& rBox, const AppState& state);
    static void DrawMetricsCard(HDC dc, const RECT& rBox, const AppState& state);
    static void DrawTiltCompass(HDC dc, const RECT& rBox, const AppState& state);
    static void DrawWaveformGraph(HDC dc, const RECT& rBox, const AppState& state);
};

