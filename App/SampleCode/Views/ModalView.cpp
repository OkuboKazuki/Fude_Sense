#include "stdafx.h"
#include "ModalView.h"
#include "RenderUtils.h"

void ModalView::DrawClearConfirm(HDC dc, int width, int height, const AppState& state) {
    if (!state.ui.showClearConfirm) return;

    using namespace RenderUtils;
    const UIState& ui = state.ui;

    // 全画面の半透明暗転オーバーレイ
    for (int y = 0; y < height; y += 2) {
        RECT line = { 0, y, width, y + 1 };
        Fill(dc, line, RGB(0, 0, 0));
    }

    // モーダルダイアログカード
    Box(dc, ui.rClearModalBox, RGB(28, 30, 36), RGB(64, 70, 84), 2, 10);

    // タイトル
    HFONT fTitle = CreateCustomFont(18, FW_BOLD);
    RECT rT = { ui.rClearModalBox.left + 24, ui.rClearModalBox.top + 20, ui.rClearModalBox.right - 24, ui.rClearModalBox.top + 52 };
    DrawTextCustom(dc, rT, L"すべての筆跡を消しますか？", fTitle, RGB(245, 248, 252));
    DeleteObject(fTitle);

    // 説明文
    HFONT fDesc = CreateCustomFont(13, FW_NORMAL);
    RECT rD = { ui.rClearModalBox.left + 24, ui.rClearModalBox.top + 58, ui.rClearModalBox.right - 24, ui.rClearModalBox.top + 115 };
    DrawTextW(dc, L"現在の作品に書かれている筆跡をすべて消します。\nこの操作は元に戻すことができません。", -1, &rD, DT_LEFT | DT_TOP | DT_NOPREFIX);
    DeleteObject(fDesc);

    // キャンセルボタン
    bool hovCancel = (ui.hoverClearModal == 2);
    Box(dc, ui.rModalCancelBtn, hovCancel ? RGB(52, 57, 68) : RGB(38, 42, 50), hovCancel ? RGB(80, 88, 104) : RGB(56, 62, 74), 1, 6);
    HFONT fBtn = CreateCustomFont(14, FW_NORMAL);
    Center(dc, ui.rModalCancelBtn, L"キャンセル", fBtn, hovCancel ? RGB(255, 255, 255) : RGB(210, 215, 225));

    // すべて消すボタン
    bool hovClear = (ui.hoverClearModal == 1);
    Box(dc, ui.rModalClearBtn, hovClear ? RGB(185, 48, 48) : RGB(150, 36, 36), hovClear ? RGB(225, 75, 75) : RGB(190, 50, 50), 1, 6);
    Center(dc, ui.rModalClearBtn, L"すべて消す", fBtn, RGB(255, 255, 255));
    DeleteObject(fBtn);
}
