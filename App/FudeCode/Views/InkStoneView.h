#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"

class InkStoneView {
public:
    static void Draw(HDC dc, const AppState& state);
    // 紙だけ表示中の、半紙の右のボタン（墨を補充 / 通常表示に戻る）
    static void DrawPaperOnlyBar(HDC dc, const AppState& state);
};
