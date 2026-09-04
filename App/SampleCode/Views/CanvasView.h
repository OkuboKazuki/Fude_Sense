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

    // 指定した矩形にお手本を1字、em ボックス基準で内接描画する
    // （画像エクスポートからも使う）
    static void DrawOtehonGlyph(HDC dc, const RECT& cell, const std::wstring& text, double opacity);
    static void DrawGrid(HDC dc, const AppState& state);
    static void RenderInk(HDC dc, GpuInk& gpuInk, const AppState& state);

    // リプレイ用半紙描画
    static void DrawReplayCanvas(HDC dc, const AppState& state);
    static void DrawReplayGhostStrokes(HDC dc, const AppState& state);
    static void DrawReplayActiveStrokes(HDC dc, const AppState& state);
    static void Draw3DBrushPose(HDC dc, const AppState& state, const StrokePoint& pose, bool isPenDown);

private:
    static void DrawCross(HDC dc, int x, int y, int s);
};

