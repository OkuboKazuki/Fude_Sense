#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// 軽量 UI ウィジェット構造体
// tagRECT (Win32 RECT) を継承し、left/top/right/bottom への直接アクセスを保ちながら
// 座標のゼロ初期化と、RECT や 4 つの座標からの代入を提供する
struct UIWidget : public tagRECT {
    UIWidget() {
        left = top = right = bottom = 0;
    }
    UIWidget(const RECT& r) : tagRECT(r) {}
    UIWidget(int l, int t, int r, int b) {
        left = l; top = t; right = r; bottom = b;
    }
};
