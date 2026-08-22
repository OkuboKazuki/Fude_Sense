#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"

class InkStoneView {
public:
    static void Draw(HDC dc, const AppState& state);
};
