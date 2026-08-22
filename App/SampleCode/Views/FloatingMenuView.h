#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"

class FloatingMenuView {
public:
    static void Draw(HDC dc, const AppState& state);

private:
    static void DrawSub(HDC dc, const AppState& state);
};
