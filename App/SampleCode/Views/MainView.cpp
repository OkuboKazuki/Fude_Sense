#include "stdafx.h"
#include "MainView.h"
#include "RenderUtils.h"
#include "CanvasView.h"
#include "FloatingMenuView.h"
#include "InkStoneView.h"
#include "ModalView.h"
#include "CalibrationView.h"
#include "TitleView.h"

#pragma comment(lib, "msimg32.lib")

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
BackBuffer g_titleBuffer;

} // namespace

void MainView::ReleaseBackBuffer() {
    g_backBuffer.Release();
    g_titleBuffer.Release();
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

    // ★ タイトル画面の単独描画
    if (state.currentScreen == AppScreen::Title && !state.isTransitioning) {
        TitleView::Draw(memDC, width, height, state);
        BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
        return;
    }

    // ★ タイトルからスタジオ画面へのアルファブレンド遷移演出
    if (state.isTransitioning) {
        // 1. 背景バッファ（memDC）にスタジオ画面を描画
        RenderUtils::DrawWoodDesk(memDC, width, height);
        CanvasView::DrawBackground(memDC, state);
        if (state.pendingStartTab == LeftTab::Analysis) {
            CanvasView::DrawReplayCanvas(memDC, gpuInk, state);
        } else {
            CanvasView::RenderInk(memDC, gpuInk, state);
        }
        CanvasView::DrawOtehon(memDC, state);
        CanvasView::DrawGrid(memDC, state);
        InkStoneView::Draw(memDC, state);
        FloatingMenuView::Draw(memDC, state);

        // 2. 一時バッファにタイトル画面を描画
        if (g_titleBuffer.Ensure(hdc, width, height)) {
            TitleView::Draw(g_titleBuffer.dc, width, height, state);

            // 3. アルファブレンドでタイトル画面を徐々にフェードアウト
            BLENDFUNCTION bf = {};
            bf.BlendOp = AC_SRC_OVER;
            bf.BlendFlags = 0;
            float alphaFactor = (std::max)(0.0f, (std::min)(1.0f, 1.0f - state.transitionProgress));
            bf.SourceConstantAlpha = static_cast<BYTE>(alphaFactor * 255.0f);
            bf.AlphaFormat = 0;

            AlphaBlend(memDC, 0, 0, width, height, g_titleBuffer.dc, 0, 0, width, height, bf);
        }

        // 4. 画面へ一括転送
        BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
        return;
    }

    RECT rcFull = { 0, 0, width, height };
    bool isFullRedraw = (rcPaint.left <= 0 && rcPaint.top <= 0 && rcPaint.right >= width && rcPaint.bottom >= height);

    RECT dummy;
    bool hitPaper = (IntersectRect(&dummy, &rcPaint, &state.ui.rPaper) != FALSE);
    bool hitRight = (IntersectRect(&dummy, &rcPaint, &state.ui.rRight) != FALSE);
    bool hitLeft = (IntersectRect(&dummy, &rcPaint, &state.ui.rTbNavToggle) != FALSE)
                || (IntersectRect(&dummy, &rcPaint, &state.ui.rTbHomeBtn) != FALSE)
                || (state.ui.isSubPanelOpen && IntersectRect(&dummy, &rcPaint, &state.ui.rSub) != FALSE);
    bool hitModal = state.ui.showClearConfirm || state.calibration.IsActive();

    // 運筆中など半紙専用の局所更新で、モーダルや左メニュー、硯パネル、木目余白と交差しない場合のみ背景を再描画しない
    // （硯パネルは透明テキスト「墨残量: ○○%」を描画するため、木目背景の再描画・クリアが必要）
    if (isFullRedraw || hitModal || hitLeft || hitRight || !hitPaper) {
        // 1. 和風木製机（文机）の背景描画
        RenderUtils::DrawWoodDesk(memDC, width, height);

        // 2. 半紙背景
        CanvasView::DrawBackground(memDC, state);

        // 3. 墨汁の描画（解析タブ表示中はリプレイ墨＆3D筆姿勢、通常時は GPU 墨汁テクスチャ）
        if (state.ui.leftTab == LeftTab::Analysis) {
            CanvasView::DrawReplayCanvas(memDC, gpuInk, state);
        } else {
            CanvasView::RenderInk(memDC, gpuInk, state);
        }

        // 3-2. お手本文字
        CanvasView::DrawOtehon(memDC, state);

        // 4. 下敷き・升目格子ガイド
        CanvasView::DrawGrid(memDC, state);

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
            if (state.ui.leftTab == LeftTab::Analysis) {
                CanvasView::DrawReplayCanvas(memDC, gpuInk, state);
            } else {
                CanvasView::RenderInk(memDC, gpuInk, state);
            }
            CanvasView::DrawOtehon(memDC, state);
            CanvasView::DrawGrid(memDC, state);
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

