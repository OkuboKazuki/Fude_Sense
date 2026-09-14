#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// 軽量 UI ウィジェット構造体
// tagRECT (Win32 RECT) を継承し、left/top/right/bottom への直接アクセスを保ちながら
// ヒットテスト・寸法取得・状態管理メソッドを提供する
struct UIWidget : public tagRECT {
    bool isHovered = false;
    bool isPressed = false;
    bool isEnabled = true;

    UIWidget() {
        left = top = right = bottom = 0;
    }
    UIWidget(const RECT& r) : tagRECT(r) {}
    UIWidget(int l, int t, int r, int b) {
        left = l; top = t; right = r; bottom = b;
    }

    // ポイントが領域内にあるかのヒットテスト
    bool Contains(POINT pt) const {
        return (pt.x >= left && pt.x <= right && pt.y >= top && pt.y <= bottom);
    }

    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
};
