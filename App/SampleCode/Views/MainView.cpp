#include "stdafx.h"
#include "MainView.h"
#include "RenderUtils.h"
#include "CanvasView.h"
#include "FloatingMenuView.h"
#include "InkStoneView.h"
#include "ModalView.h"
#include "CalibrationView.h"

namespace {

// 画面と同じ大きさの裏画面。WM_PAINT ごとに作り直すと、再生中は毎フレーム
// 数MBのビットマップを確保・破棄することになるため、大きさが変わった時だけ
// 作り直して使い回す。
struct BackBuffer {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HBITMAP oldBmp = nullptr;
    int w = 0;
    int h = 0;

    bool Ensure(HDC ref, int W, int H) {
        if (dc && w == W && h == H) return true;
        Release();
        dc = CreateCompatibleDC(ref);
        if (!dc) return false;
        bmp = CreateCompatibleBitmap(ref, W, H);
        if (!bmp) {
            DeleteDC(dc);
            dc = nullptr;
            return false;
        }
        oldBmp = (HBITMAP)SelectObject(dc, bmp);
        w = W;
        h = H;
        return true;
    }
    void Release() {
        if (dc) {
            SelectObject(dc, oldBmp);
            DeleteDC(dc);
            dc = nullptr;
        }
        if (bmp) {
            DeleteObject(bmp);
            bmp = nullptr;
        }
        oldBmp = nullptr;
        w = 0;
        h = 0;
    }
};

BackBuffer g_backBuffer;

} // namespace

void MainView::ReleaseBackBuffer() {
    g_backBuffer.Release();
}

void MainView::Render(HDC hdc, int width, int height, GpuInk& gpuInk, const AppState& state) {
    RECT rcFull = { 0, 0, width, height };
    Render(hdc, width, height, gpuInk, state, rcFull);
}

void MainView::Render(HDC hdc, int width, int height, GpuInk& gpuInk, const AppState& state, const RECT& rcPaint) {
    if (width <= 0 || height <= 0) return;

    // 裏画面へ一括描画してから転送する（フリッカー防止）
    if (!g_backBuffer.Ensure(hdc, width, height)) return;
    HDC memDC = g_backBuffer.dc;

    RECT rcFull = { 0, 0, width, height };
    bool isFullRedraw = (rcPaint.left <= 0 && rcPaint.top <= 0 && rcPaint.right >= width && rcPaint.bottom >= height);

    RECT dummy;
    bool hitPaper = (IntersectRect(&dummy, &rcPaint, &state.ui.rPaper) != FALSE);
    bool hitRight = (IntersectRect(&dummy, &rcPaint, &state.ui.rRight) != FALSE);
    bool hitLeft = (IntersectRect(&dummy, &rcPaint, &state.ui.rTbNavToggle) != FALSE)
                || (state.ui.isSubPanelOpen && IntersectRect(&dummy, &rcPaint, &state.ui.rSub) != FALSE);
    bool hitModal = state.ui.showClearConfirm || state.calibration.IsActive();

    // 運筆中など半紙専用の局所更新で、モーダルや左メニュー、硯パネル、木目余白と交差しない場合のみ背景を再描画しない
    // （硯パネルは透明テキスト「墨残量: ○○%」を描画するため、木目背景の再描画・クリアが必要）
    // 紙だけ表示では解析パネルが無いので、常に書いている墨を出す。
    bool showReplay = (state.ui.leftTab == LeftTab::Analysis && !state.ui.paperOnly);

    if (isFullRedraw || hitModal || hitLeft || hitRight || !hitPaper) {
        // 1. 和風木製机（文机）の背景描画。紙だけ表示では机を描かず、
        //    半紙の比率に合わせて空いた余白をボタンの帯と同じ無地で塗る。
        if (state.ui.paperOnly) {
            RenderUtils::Fill(memDC, rcFull, RGB(24, 26, 34));
        } else {
            RenderUtils::DrawWoodDesk(memDC, width, height);
        }

        // 2. 半紙背景
        CanvasView::DrawBackground(memDC, state);

        // 3. 墨汁の描画（解析タブ表示中はリプレイ墨＆3D筆姿勢、通常時は GPU 墨汁テクスチャ）
        if (showReplay) {
            CanvasView::DrawReplayCanvas(memDC, gpuInk, state);
        } else {
            CanvasView::RenderInk(memDC, gpuInk, state);
        }

        // 3-2. お手本文字
        CanvasView::DrawOtehon(memDC, state);

        // 4. 下敷き・升目格子ガイド
        CanvasView::DrawGrid(memDC, state);

        if (state.ui.paperOnly) {
            // 5. 紙だけ表示。操作は半紙の右の「墨を補充」「通常表示に戻る」「筆跡を消す」だけ。
            InkStoneView::DrawPaperOnlyBar(memDC, state);
            BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
            return;
        }

        // 5. 右側 硯パネル
        InkStoneView::Draw(memDC, state);

        // 6. 左側 フローティングメニュー
        FloatingMenuView::Draw(memDC, state);

        // 8. 全消し確認モーダルオーバーレイ
        ModalView::DrawClearConfirm(memDC, width, height, state);

        // 9. 筆圧キャリブレーション（ガイダンスまたは結果モーダル）
        CalibrationView::Draw(memDC, width, height, state);

        // 画面へ一括転送 (フリッカーフリー)
        BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    } else {
        // 局所更新パス（運筆中の半紙専用高速描画パス）
        if (hitPaper) {
            CanvasView::DrawBackground(memDC, state);
            if (showReplay) {
                CanvasView::DrawReplayCanvas(memDC, gpuInk, state);
            } else {
                CanvasView::RenderInk(memDC, gpuInk, state);
            }
            CanvasView::DrawOtehon(memDC, state);
            CanvasView::DrawGrid(memDC, state);
        }

        // 紙だけ表示では墨の残量を「墨を補充」ボタンに出しているので、
        // 運筆中もボタンの帯を描き直して残量を追従させる
        if (state.ui.paperOnly && IntersectRect(&dummy, &rcPaint, &state.ui.rPaperOnlyBar)) {
            InkStoneView::DrawPaperOnlyBar(memDC, state);
        }

        // 無効化領域のみを画面へ高速転送 (CPU/GPU バス帯域を劇的に節約)
        int bltX = (std::max)(0, static_cast<int>(rcPaint.left));
        int bltY = (std::max)(0, static_cast<int>(rcPaint.top));
        int bltW = (std::min)(width, static_cast<int>(rcPaint.right)) - bltX;
        int bltH = (std::min)(height, static_cast<int>(rcPaint.bottom)) - bltY;
        if (bltW > 0 && bltH > 0) {
            BitBlt(hdc, bltX, bltY, bltW, bltH, memDC, bltX, bltY, SRCCOPY);
        }
    }
}
