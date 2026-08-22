#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <algorithm>

namespace RenderUtils {

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

    // フォント生成
    HFONT CreateCustomFont(int size, int weight = FW_NORMAL, const wchar_t* face = L"Yu Gothic UI");

    // 矩形塗りつぶし
    void Fill(HDC dc, const RECT& r, COLORREF color);

    // 角丸枠線付きボックス描画
    void Box(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int bw = 1, int round = 6);

    // テキスト描画
    void DrawTextCustom(HDC dc, RECT r, const wchar_t* s, HFONT f, COLORREF c, UINT align = DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 中央揃えテキスト描画
    void Center(HDC dc, RECT r, const wchar_t* s, HFONT f, COLORREF c);

    // パネルタイトル描画
    void DrawPanelTitle(HDC dc, const RECT& rPanel, const wchar_t* title);

    // タイルカード描画
    void DrawTileCard(HDC dc, RECT r, const wchar_t* title, const wchar_t* sub, bool active, bool hover);

    // カラーテーマ選択ボタン描画
    void DrawColorThemeButton(HDC dc, RECT r, const wchar_t* label, COLORREF dotColor, bool active, bool hover);

    // サブカード（筆の太さ等）描画
    void DrawSubCard(HDC dc, RECT r, const wchar_t* name, const wchar_t* detail, int strokeWidth, bool active, bool hover);

} // namespace RenderUtils
