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

} // namespace

void InkStoneView::Draw(HDC dc, const AppState& state) {
    using namespace RenderUtils;
    const UIState& ui = state.ui;

    // 1. 墨量表示ヘッダー（硯の真上にモダンなピルバッジとして表示）
    int inkPercent = (int)(state.ink.stoneAmount * 100.0);
    bool isLow = (inkPercent <= 20);

    COLORREF badgeBg     = isLow ? RGB(52, 22, 25)     : RGB(24, 28, 36);
    COLORREF badgeBorder = isLow ? RGB(240, 75, 75)    : RGB(46, 54, 70);
    COLORREF metaColor   = isLow ? RGB(255, 140, 140)  : RGB(225, 235, 250);

    int badgeW = 160;
    int badgeH = 26;
    int badgeX = (ui.rInkStoneLarge.left + ui.rInkStoneLarge.right - badgeW) / 2;
    int badgeY = ui.rInkStoneLarge.top - badgeH - 8;
    RECT rBadge = { badgeX, badgeY, badgeX + badgeW, badgeY + badgeH };

    Box(dc, rBadge, badgeBg, badgeBorder, 1, 13);

    HFONT fMeta = CreateCustomFont(15, FW_BOLD);
    wchar_t buf[64];
    if (isLow) {
        wsprintfW(buf, L"⚠️ 墨残量: %d%%", inkPercent);
    } else {
        wsprintfW(buf, L"💧 墨残量: %d%%", inkPercent);
    }
    Center(dc, rBadge, buf, fMeta, metaColor);
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

    // 8. 表示の切り替え（紙の向き）。
    //    紙タブを開かなくても通常画面から直接切り替えられるようにここへ置く。
    bool hovOrient = (ui.hoverInkStone == 6);
    Box(dc, ui.rPaperOrientBtn, hovOrient ? RGB(42, 72, 110) : RGB(30, 36, 46), hovOrient ? RGB(85, 145, 235) : RGB(54, 62, 78), 1, 8);
    HFONT fOrient = CreateCustomFont(17, FW_BOLD);
    Center(dc, ui.rPaperOrientBtn,
        state.paper.isLandscape ? L"↕ 紙を縦向きに" : L"↔ 紙を横向きに",
        fOrient, hovOrient ? RGB(255, 255, 255) : RGB(220, 230, 245));
    DeleteObject(fOrient);
}
