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
    if (width <= 0 || height <= 0) return;

    // 裏画面へ一括描画してから転送する（フリッカー防止）
    if (!g_backBuffer.Ensure(hdc, width, height)) return;
    HDC memDC = g_backBuffer.dc;

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

    // 6. 左側 フローティングメニュー
    FloatingMenuView::Draw(memDC, state);

    // 8. 全消し確認モーダルオーバーレイ
    ModalView::DrawClearConfirm(memDC, width, height, state);

    // 9. 筆圧キャリブレーション（ガイダンスまたは結果モーダル）
    CalibrationView::Draw(memDC, width, height, state);

    // 画面へ一括転送 (フリッカーフリー)
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
}
