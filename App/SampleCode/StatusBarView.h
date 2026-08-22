#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"

class StatusBarView {
public:
    // 下部ステータスバーの描画
    static void Draw(HDC dc, int width, int height, const AppState& state);

private:
    static const wchar_t* GetGridPatternName(GridPattern pattern);
};
