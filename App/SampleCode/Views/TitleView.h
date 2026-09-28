#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"

class TitleView {
public:
    // タイトル画面のレンダリング
    static void Draw(HDC dc, int width, int height, const AppState& state);
    static void ReleaseResources();

private:
    static void EnsureLogoLoaded();
    static void DrawBackground(HDC dc, int width, int height);
    static void DrawCenterContent(HDC dc, int width, int height, const AppState& state);
};


