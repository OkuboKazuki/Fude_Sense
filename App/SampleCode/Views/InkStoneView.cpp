#include "stdafx.h"
#include "InkStoneView.h"
#include "RenderUtils.h"

namespace {

// 「一画戻す」「一画復元」は見た目が同じなので1か所で描く。
// 控えが無いときは押せないことが分かるよう沈める。
void DrawHistoryButton(HDC dc, const RECT& r, const wchar_t* label, int count,
                       bool enabled, bool hovered) {
    using namespace RenderUtils;
    const bool hov = enabled && hovered;
    Box(dc, r,
        enabled ? (hov ? RGB(44, 62, 92) : RGB(30, 36, 46)) : RGB(24, 26, 32),
        enabled ? (hov ? RGB(120, 165, 240) : RGB(54, 62, 78)) : RGB(40, 44, 54), 1, 8);

    wchar_t buf[64];
    if (enabled) {
        wsprintfW(buf, L"%s (%d)", label, count);
    } else {
        wsprintfW(buf, L"%s", label);
    }
    HFONT f = CreateCustomFont(17, FW_BOLD);
    Center(dc, r, buf, f,
        enabled ? (hov ? RGB(255, 255, 255) : RGB(215, 226, 245)) : RGB(96, 102, 116));
    DeleteObject(f);
}

// 文字を左回りに90度倒して矩形の中央へ描く。紙だけ表示は画面を右回りに
// 90度倒して使うので、倒した向きで見ると文字が正しい向きに読める。
void DrawRotatedCenter(HDC dc, const RECT& r, const wchar_t* s, int size, int weight, COLORREF color) {
    HFONT f = CreateFontW(size, 0, 900, 900, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    if (!f) return;
    HFONT old = (HFONT)SelectObject(dc, f);

    // 大きさは倒す前の向きで測る（GetTextExtentPoint32 は回転を考えない）
    int len = lstrlenW(s);
    SIZE ext{ 0, 0 };
    GetTextExtentPoint32W(dc, s, len, &ext);

    // 左回り90度では、文字列は基準点から上へ伸び、字の頭は左を向く。
    // 基準点（文字セルの左上）から見て、幅は -y 方向、高さは +x 方向に並ぶ。
    int cx = (r.left + r.right) / 2;
    int cy = (r.top + r.bottom) / 2;
    int x = cx - ext.cy / 2;
    int y = cy + ext.cx / 2;

    int oldBk = SetBkMode(dc, TRANSPARENT);
    COLORREF oldColor = SetTextColor(dc, color);
    UINT oldAlign = SetTextAlign(dc, TA_LEFT | TA_TOP);
    TextOutW(dc, x, y, s, len);
    SetTextAlign(dc, oldAlign);
    SetTextColor(dc, oldColor);
    SetBkMode(dc, oldBk);

    SelectObject(dc, old);
    DeleteObject(f);
}

} // namespace

void InkStoneView::Draw(HDC dc, const AppState& state) {
    using namespace RenderUtils;
    const UIState& ui = state.ui;

    // 1. 墨量表示ヘッダー（硯の真上にシンプルに表示）
    HFONT fMeta = CreateCustomFont(18, FW_BOLD);
    RECT rMeta1 = { ui.rInkStoneLarge.left, ui.rInkStoneLarge.top - 32, ui.rInkStoneLarge.right, ui.rInkStoneLarge.top - 6 };
    wchar_t buf[64];
    int inkPercent = (int)(state.ink.stoneAmount * 100.0);
    wsprintfW(buf, L"墨残量: %d%%", inkPercent);
    COLORREF metaColor = (inkPercent > 20) ? RGB(235, 240, 250) : RGB(255, 110, 110);
    Center(dc, rMeta1, buf, fMeta, metaColor);
    DeleteObject(fMeta);

    // 2. 硯本体（外枠の黒い影を無くし、スッキリした石のフォルム）
    bool hoverStone = (ui.hoverInkStone == 1);
    COLORREF stoneBorder = hoverStone ? RGB(75, 130, 210) : RGB(48, 52, 62);
    Box(dc, ui.rInkStoneLarge, RGB(26, 28, 34), stoneBorder, 1, 8);

    // 3. 墨溜まり（上部の窪み・墨汁）
    RECT rPool = { ui.rInkStoneLarge.left + 12, ui.rInkStoneLarge.top + 12, ui.rInkStoneLarge.right - 12, ui.rInkStoneLarge.top + (int)(RH(ui.rInkStoneLarge) * 0.38) };
    Box(dc, rPool, RGB(10, 11, 14), RGB(36, 40, 50), 1, 6);

    double inkFrac = Clamp(state.ink.stoneAmount / INK_MAX_VALUE, 0.0, 1.0);
    if (inkFrac > 0.01) {
        int poolH = RH(rPool) - 6;
        int fillH = (int)(poolH * inkFrac);
        RECT rLiquid = { rPool.left + 3, rPool.bottom - 3 - fillH, rPool.right - 3, rPool.bottom - 3 };
        Fill(dc, rLiquid, RGB(4, 5, 7));

        // 墨の液面の光沢ハイライト
        HPEN hp = CreatePen(PS_SOLID, 2, RGB(80, 110, 150));
        HPEN ohp = (HPEN)SelectObject(dc, hp);
        MoveToEx(dc, rLiquid.left + 8, rLiquid.top + 1, nullptr);
        LineTo(dc, rLiquid.right - 8, rLiquid.top + 1);
        SelectObject(dc, ohp);
        DeleteObject(hp);
    }

    // 4. 磨り面（下部の平坦な丘）
    RECT rLand = { ui.rInkStoneLarge.left + 12, ui.rInkStoneLarge.top + (int)(RH(ui.rInkStoneLarge) * 0.40), ui.rInkStoneLarge.right - 12, ui.rInkStoneLarge.bottom - 12 };
    Box(dc, rLand, RGB(20, 22, 28), RGB(38, 42, 52), 1, 6);

    // 微細な石目テクスチャライン
    HPEN tp = CreatePen(PS_SOLID, 1, RGB(30, 33, 42));
    HPEN otp = (HPEN)SelectObject(dc, tp);
    for (int y = rLand.top + 12; y < rLand.bottom - 12; y += 12) {
        MoveToEx(dc, rLand.left + 10, y, nullptr);
        LineTo(dc, rLand.right - 10, y);
    }
    SelectObject(dc, otp);
    DeleteObject(tp);

    // 5. 「💧 墨を補充」ボタン（シャドウなし・スッキリ配置）
    bool hovRefill = (ui.hoverInkStone == 2);
    Box(dc, ui.rInkRefillBtn, hovRefill ? RGB(42, 72, 110) : RGB(32, 38, 48), hovRefill ? RGB(85, 145, 235) : RGB(54, 62, 78), 1, 8);
    HFONT fBtn = CreateCustomFont(17, FW_BOLD);
    Center(dc, ui.rInkRefillBtn, L"💧 墨を補充", fBtn, hovRefill ? RGB(255, 255, 255) : RGB(220, 230, 245));
    DeleteObject(fBtn);

    // 6. 「↩ 一画戻す」「↪ 一画復元」ボタン
    DrawHistoryButton(dc, ui.rUndoBtn, L"↩ 一画戻す", state.undo.Depth(),
        state.undo.CanUndo(), ui.hoverInkStone == 4);
    DrawHistoryButton(dc, ui.rRedoBtn, L"↪ 一画復元", state.undo.RedoDepth(),
        state.undo.CanRedo(), ui.hoverInkStone == 5);

    // 7. 「🗑️ 筆跡をすべて消す」ボタン（シャドウなし・スッキリ配置）
    bool hovClear = (ui.hoverInkStone == 3);
    Box(dc, ui.rClearAllBtn, hovClear ? RGB(85, 38, 38) : RGB(42, 30, 32), hovClear ? RGB(180, 70, 70) : RGB(74, 48, 52), 1, 8);
    HFONT fClear = CreateCustomFont(17, FW_BOLD);
    Center(dc, ui.rClearAllBtn, L"🗑️ 筆跡をすべて消す", fClear, hovClear ? RGB(255, 225, 225) : RGB(230, 185, 185));
    DeleteObject(fClear);

    // 8. 紙だけ表示（横向き）に入る
    bool hovPaperOnly = (ui.hoverInkStone == 6);
    Box(dc, ui.rPaperOnlyBtn, hovPaperOnly ? RGB(42, 72, 110) : RGB(30, 36, 46), hovPaperOnly ? RGB(85, 145, 235) : RGB(54, 62, 78), 1, 8);
    HFONT fView = CreateCustomFont(17, FW_BOLD);
    Center(dc, ui.rPaperOnlyBtn, L"🖼️ 紙だけ表示（横向き）", fView, hovPaperOnly ? RGB(255, 255, 255) : RGB(220, 230, 245));
    DeleteObject(fView);
}

void InkStoneView::DrawPaperOnlyBar(HDC dc, const AppState& state) {
    using namespace RenderUtils;
    const UIState& ui = state.ui;

    // ボタンの帯。机の背景は描かないので、無地で塗る
    Fill(dc, ui.rPaperOnlyBar, RGB(24, 26, 34));

    // 墨を補充。硯が見えないので残量もボタンに添え、少ないときは赤くする
    int inkPercent = (int)(state.ink.stoneAmount * 100.0);
    bool low = (inkPercent <= 20);
    bool hovRefill = (ui.hoverPaperOnly == 2);
    Box(dc, ui.rInkRefillBtn, hovRefill ? RGB(42, 72, 110) : RGB(26, 30, 38),
        hovRefill ? RGB(85, 145, 235) : (low ? RGB(180, 70, 70) : RGB(72, 80, 98)), low ? 2 : 1, 8);
    // 画面を倒して見る前提なので、文字も倒して描く
    COLORREF fg = hovRefill ? RGB(255, 255, 255) : (low ? RGB(255, 140, 140) : RGB(220, 230, 245));
    wchar_t buf[48];
    wsprintfW(buf, L"墨を補充（残り %d%%）", inkPercent);
    DrawRotatedCenter(dc, ui.rInkRefillBtn, buf, 22, FW_BOLD, fg);

    // 通常表示（メニュー・硯パネルあり）へ戻る
    bool hovExit = (ui.hoverPaperOnly == 1);
    Box(dc, ui.rPaperOnlyExitBtn, hovExit ? RGB(42, 72, 110) : RGB(26, 30, 38),
        hovExit ? RGB(85, 145, 235) : RGB(72, 80, 98), 1, 8);
    DrawRotatedCenter(dc, ui.rPaperOnlyExitBtn, L"通常表示に戻る", 18, FW_BOLD,
        hovExit ? RGB(255, 255, 255) : RGB(220, 230, 245));
}
