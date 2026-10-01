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

    // 筆圧 (0.0: 軽 〜 1.0: 強) に応じた視覚化カラー（青/水色 -> 緑/黄 -> 橙/赤）を取得
    inline COLORREF GetPressureColor(double p) {
        double t = Clamp(p, 0.0, 1.0);
        int r, g, b;
        if (t < 0.35) {
            double u = t / 0.35;
            r = static_cast<int>(60  + (80 - 60) * u);
            g = static_cast<int>(180 + (220 - 180) * u);
            b = static_cast<int>(255 + (100 - 255) * u);
        } else if (t < 0.70) {
            double u = (t - 0.35) / 0.35;
            r = static_cast<int>(80  + (255 - 80) * u);
            g = static_cast<int>(220 + (180 - 220) * u);
            b = static_cast<int>(100 + (40 - 100) * u);
        } else {
            double u = (t - 0.70) / 0.30;
            r = static_cast<int>(255 + (255 - 255) * u);
            g = static_cast<int>(180 + (50 - 180) * u);
            b = static_cast<int>(40  + (50 - 40) * u);
        }
        return RGB(Clamp(r, 0, 255), Clamp(g, 0, 255), Clamp(b, 0, 255));
    }

    // フォント生成
    HFONT CreateCustomFont(int size, int weight = FW_NORMAL, const wchar_t* face = L"Yu Gothic UI");

    // 矩形塗りつぶし
    void Fill(HDC dc, const RECT& r, COLORREF color);

    // 角丸枠線付きボックス描画
    void Box(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int bw = 1, int round = 6);

    // 和風木製机（文机）の背景描画
    void DrawWoodDesk(HDC dc, int width, int height);

    // テキスト描画
    void DrawTextCustom(HDC dc, RECT r, const wchar_t* s, HFONT f, COLORREF c, UINT align = DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 中央揃えテキスト描画
    void Center(HDC dc, RECT r, const wchar_t* s, HFONT f, COLORREF c);

    // タイルカード描画
    void DrawTileCard(HDC dc, RECT r, const wchar_t* title, const wchar_t* sub, bool active, bool hover);

    // カラーテーマ選択ボタン描画
    void DrawColorThemeButton(HDC dc, RECT r, const wchar_t* label, COLORREF dotColor, bool active, bool hover);

    // サブカード（筆の太さ等）描画
    void DrawSubCard(HDC dc, RECT r, const wchar_t* name, const wchar_t* detail, int strokeWidth, bool active, bool hover);

} // namespace RenderUtils
