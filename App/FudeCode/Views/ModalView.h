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

private:
    // モーダル本体。width×height はモーダルの座標系（紙だけ表示では倒した向き）の広さ
    static void DrawClearConfirmBody(HDC dc, int width, int height, const AppState& state);
};
