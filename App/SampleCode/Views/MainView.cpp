#include "stdafx.h"
#include "MainView.h"
#include "RenderUtils.h"
#include "CanvasView.h"
#include "FloatingMenuView.h"
#include "InkStoneView.h"
#include "ModalView.h"
#include "CalibrationView.h"
#include "StatusBarView.h"

void MainView::Render(HDC hdc, int width, int height, GpuInk& gpuInk, const AppState& state) {
    if (width <= 0 || height <= 0) return;

    // メモリDCによるダブルバッファリング描画
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    // 1. 和風木製机（文机）の背景描画
    RenderUtils::DrawWoodDesk(memDC, width, height);

    // 2. 半紙背景
    CanvasView::DrawBackground(memDC, state);

    // 3. 墨汁の描画（解析タブ表示中はリプレイ墨＆3D筆姿勢、通常時は GPU 墨汁テクスチャ）
    if (state.ui.leftTab == LeftTab::Analysis) {
        CanvasView::DrawReplayCanvas(memDC, state);
    } else {
        CanvasView::RenderInk(memDC, gpuInk, state);
    }

    // 3-2. お手本文字

    // 墨のテクスチャは不透明で半紙全面を覆うため、必ず墨より後に重ねる。
    // 墨との合成は SRCAND なので、なぞった墨がお手本を隠す見え方になる。
    CanvasView::DrawOtehon(memDC, state);

    // 4. 下敷き・升目格子ガイド
    CanvasView::DrawGrid(memDC, state);

    // 5. 右側 硯パネル
    InkStoneView::Draw(memDC, state);

    // 6. 下部 ステータスバー
    StatusBarView::Draw(memDC, width, height, state);

    // 7. 左側 フローティングメニュー
    FloatingMenuView::Draw(memDC, state);

    // 8. 全消し確認モーダルオーバーレイ
    ModalView::DrawClearConfirm(memDC, width, height, state);

    // 9. 筆圧キャリブレーション（ガイダンスまたは結果モーダル）
    CalibrationView::Draw(memDC, width, height, state);

    // 画面へ一括転送 (フリッカーフリー)
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}
