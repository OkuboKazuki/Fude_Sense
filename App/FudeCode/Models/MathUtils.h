#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// 描画に依存しない計算の共通関数。Model からも View からも使える。
namespace MathUtils {

    // 範囲制限
    template<class T>
    inline T Clamp(T v, T lo, T hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // 矩形の幅・高さ
    inline int RW(const RECT& r) { return r.right - r.left; }
    inline int RH(const RECT& r) { return r.bottom - r.top; }

    // 点の内外判定
    inline bool PtIn(const RECT& r, POINT pt) {
        return (pt.x >= r.left && pt.x < r.right && pt.y >= r.top && pt.y < r.bottom);
    }

} // namespace MathUtils
