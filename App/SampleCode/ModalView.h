#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"

class ModalView {
public:
    // 全消し確認モーダルの描画
    static void DrawClearConfirm(HDC dc, int width, int height, const AppState& state);
};
