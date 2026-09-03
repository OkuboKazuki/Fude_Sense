#include "stdafx.h"
#include "AppState.h"

void AppState::Layout(int w, int h) {
    using namespace RenderUtils;

    const int statusH = 28;
    const int rightW = 270;

    ui.rRight = { w - rightW, 0, w, h - statusH };
    ui.rCanvasArea = { 0, 0, ui.rRight.left, h - statusH };
    ui.rStatus = { 0, h - statusH, w, h };

    // 1. フローティングメニュー & メニュー開閉ボタン（大きめサイズ・一回り拡大）
    if (!ui.isSubPanelOpen) {
        ui.rTbNavToggle = { 20, 20, 84, 72 };
        ui.rSub = { 0, 0, 0, 0 };
        ui.rTbBrush = ui.rTbPaper = ui.rTbAnalysis = ui.rTbSave = ui.rTbOtehon = { 0, 0, 0, 0 };
    } else {
        int menuW = 560;
        int menuH = (std::min)(h - statusH - 36, 840);
        if (menuH < 600) menuH = 600;
        ui.rSub = { 20, 20, 20 + menuW, 20 + menuH };

        ui.rTbNavToggle = { ui.rSub.left + 14, ui.rSub.top + 14, ui.rSub.left + 74, ui.rSub.top + 66 };

        int tabStartX = ui.rTbNavToggle.right + 10;
        int tabAvailW = ui.rSub.right - 14 - tabStartX;
        int tabW = (tabAvailW - 4 * 8) / 5;
        int tabH = 52;
        int tabY = ui.rSub.top + 14;

        ui.rTbBrush    = { tabStartX + 0 * (tabW + 8), tabY, tabStartX + 0 * (tabW + 8) + tabW, tabY + tabH };
        ui.rTbPaper    = { tabStartX + 1 * (tabW + 8), tabY, tabStartX + 1 * (tabW + 8) + tabW, tabY + tabH };
        ui.rTbAnalysis = { tabStartX + 2 * (tabW + 8), tabY, tabStartX + 2 * (tabW + 8) + tabW, tabY + tabH };
        ui.rTbSave     = { tabStartX + 3 * (tabW + 8), tabY, tabStartX + 3 * (tabW + 8) + tabW, tabY + tabH };
        ui.rTbOtehon   = { tabStartX + 4 * (tabW + 8), tabY, tabStartX + 4 * (tabW + 8) + tabW, tabY + tabH };

        int topOff = ui.rSub.top + 84;

        // 筆タブ
        int cardH = 86;
        ui.rSubSmall  = { ui.rSub.left + 18, topOff, ui.rSub.right - 18, topOff + cardH };
        ui.rSubMedium = { ui.rSub.left + 18, topOff + (cardH + 10), ui.rSub.right - 18, topOff + (cardH + 10) + cardH };
        ui.rSubLarge  = { ui.rSub.left + 18, topOff + (cardH + 10) * 2, ui.rSub.right - 18, topOff + (cardH + 10) * 2 + cardH };
        int cardBottom = topOff + (cardH + 10) * 2 + cardH;
        ui.rHardnessTrack = { ui.rSub.left + 40, cardBottom + 74, ui.rSub.right - 40, cardBottom + 88 };

        // 筆圧キャリブレーションボタン
        int calibBtnY = cardBottom + 138;
        ui.rSubCalibBtn = { ui.rSub.left + 18, calibBtnY, ui.rSub.right - 18, calibBtnY + 58 };

        // 紙タブ
        int ptW = (menuW - 56) / 2;
        int ptH = 66;
        for (int i = 0; i < 4; ++i) {
            int col = i % 2;
            int row = i / 2;
            ui.rPaperTile[i] = { ui.rSub.left + 18 + col * (ptW + 12), topOff + row * (ptH + 10), ui.rSub.left + 18 + col * (ptW + 12) + ptW, topOff + row * (ptH + 10) + ptH };
        }

        int gridTop = topOff + 2 * (ptH + 10) + 36;
        int tileW = (menuW - 56) / 2;
        int tileH = 58;
        for (int i = 0; i < 9; ++i) {
            int col = i % 2;
            int row = i / 2;
            if (i == 8) {
                ui.rGridTile[i] = { ui.rSub.left + 18, gridTop + row * (tileH + 8), ui.rSub.left + 18 + tileW * 2 + 12, gridTop + row * (tileH + 8) + tileH };
            } else {
                ui.rGridTile[i] = { ui.rSub.left + 18 + col * (tileW + 12), gridTop + row * (tileH + 8), ui.rSub.left + 18 + col * (tileW + 12) + tileW, gridTop + row * (tileH + 8) + tileH };
            }
        }

        int colorY = gridTop + 5 * (tileH + 8) + 30;
        int colBtnW = (menuW - 60) / 3;
        for (int i = 0; i < 3; ++i) {
            ui.rColorBtn[i] = { ui.rSub.left + 18 + i * (colBtnW + 10), colorY, ui.rSub.left + 18 + i * (colBtnW + 10) + colBtnW, colorY + 48 };
        }

        // 解析（グラフ）タブ
        int availSubH = ui.rSub.bottom - topOff - 18;
        int metricsH = 104;
        int compassH = 190;
        int graphH = (std::max)(170, availSubH - metricsH - compassH - 28);

        ui.rAnalysisMetricsBox = { ui.rSub.left + 18, topOff, ui.rSub.right - 18, topOff + metricsH };
        ui.rAnalysisCompassBox = { ui.rSub.left + 18, ui.rAnalysisMetricsBox.bottom + 14, ui.rSub.right - 18, ui.rAnalysisMetricsBox.bottom + 14 + compassH };
        ui.rAnalysisGraphBox   = { ui.rSub.left + 18, ui.rAnalysisCompassBox.bottom + 14, ui.rSub.right - 18, ui.rAnalysisCompassBox.bottom + 14 + graphH };

        // 保存タブ
        int btnH = 68;
        ui.rSaveBtnPng  = { ui.rSub.left + 20, topOff + 12, ui.rSub.right - 20, topOff + 12 + btnH };
        ui.rSaveBtnClip = { ui.rSub.left + 20, ui.rSaveBtnPng.bottom + 14, ui.rSub.right - 20, ui.rSaveBtnPng.bottom + 14 + btnH };
        ui.rSaveBtnJson = { ui.rSub.left + 20, ui.rSaveBtnClip.bottom + 20, ui.rSub.right - 20, ui.rSaveBtnClip.bottom + 20 + btnH };
        ui.rSaveBtnCsv  = { ui.rSub.left + 20, ui.rSaveBtnJson.bottom + 14, ui.rSub.right - 20, ui.rSaveBtnJson.bottom + 14 + btnH };

        // お手本タブ
        ui.rOtehonToggleBtn = { ui.rSub.left + 20, topOff, ui.rSub.right - 20, topOff + 60 };
        int oTop = topOff + 94;
        int oW = (menuW - 68) / 4;
        int oH = 70;
        for (int i = 0; i < 8; ++i) {
            int col = i % 4;
            int row = i / 4;
            ui.rOtehonTile[i] = { ui.rSub.left + 20 + col * (oW + 10), oTop + row * (oH + 10), ui.rSub.left + 20 + col * (oW + 10) + oW, oTop + row * (oH + 10) + oH };
        }
        ui.rOtehonOpacityTrack = { ui.rSub.left + 40, oTop + 2 * (oH + 10) + 54, ui.rSub.right - 40, oTop + 2 * (oH + 10) + 68 };
    }

    // 2. キャンバス / 半紙（画面中央にアスペクト比を維持して配置）
    double ratioW = 242.0, ratioH = 333.0;
    paper.GetAspectRatio(ratioW, ratioH);

    int maxPaperH = h - statusH - 56;
    if (maxPaperH < 100) maxPaperH = 100;

    // 画面中央に配置したときの最大幅（硯や左パネルとの干渉を防ぎつつ最大化）
    int maxPaperW = (int)(w * 0.54);
    if (maxPaperW < 100) maxPaperW = 100;

    int paperH = maxPaperH;
    int paperW = (int)(paperH * (ratioW / ratioH));
    if (paperW > maxPaperW) {
        paperW = maxPaperW;
        paperH = (int)(paperW * (ratioH / ratioW));
    }

    // ★ 画面全体の水平中央に固定配置 ★
    int canvasCenterX = w / 2;
    int canvasCenterY = (h - statusH) / 2;

    ui.rPaper.left = canvasCenterX - paperW / 2;
    ui.rPaper.right = ui.rPaper.left + paperW;
    ui.rPaper.top = canvasCenterY - paperH / 2;
    ui.rPaper.bottom = ui.rPaper.top + paperH;

    // 3. 右側 硯・墨量・全消し（★ 半紙の右端と画面の右端との真ん中 ★）
    int stoneW = 210;
    int stoneH = (int)(stoneW * 1.34);
    int refillBtnH = 46;
    int clearBtnH = 46;
    int spacing = 12;
    int headerSpace = 32; // 墨残量ヘッダー用スペース

    int totalBlockH = headerSpace + stoneH + spacing + refillBtnH + spacing + clearBtnH;
    int blockStartY = canvasCenterY - totalBlockH / 2;
    if (blockStartY < 24) blockStartY = 24;

    // 半紙の右端から画面右端までの中心X
    int rightSpaceLeft = ui.rPaper.right;
    int rightSpaceRight = w;
    int stoneCenterX = (rightSpaceLeft + rightSpaceRight) / 2;
    int stoneX = stoneCenterX - stoneW / 2;
    if (stoneX + stoneW > w - 10) stoneX = w - stoneW - 10;
    if (stoneX < ui.rPaper.right + 10) stoneX = ui.rPaper.right + 10;

    int stoneY = blockStartY + headerSpace;

    ui.rRight = { rightSpaceLeft, 0, w, h - statusH };
    ui.rInkStoneLarge = { stoneX, stoneY, stoneX + stoneW, stoneY + stoneH };
    ui.rInkRefillBtn  = { stoneX, stoneY + stoneH + spacing, stoneX + stoneW, stoneY + stoneH + spacing + refillBtnH };
    ui.rClearAllBtn   = { stoneX, stoneY + stoneH + spacing + refillBtnH + spacing, stoneX + stoneW, stoneY + stoneH + spacing + refillBtnH + spacing + clearBtnH };

    // 4. 全消し確認モーダルダイアログ
    int modalW = 660;
    int modalH = 260;
    ui.rClearModalBox = { w / 2 - modalW / 2, h / 2 - modalH / 2, w / 2 + modalW / 2, h / 2 + modalH / 2 };
    int btnW = 140;
    int btnH = 40;
    int btnY = ui.rClearModalBox.bottom - 56;
    ui.rModalCancelBtn = { ui.rClearModalBox.left + 40, btnY, ui.rClearModalBox.left + 40 + btnW, btnY + btnH };
    ui.rModalClearBtn  = { ui.rClearModalBox.right - 40 - btnW, btnY, ui.rClearModalBox.right - 40, btnY + btnH };

    // 5. 筆圧キャリブレーション結果モーダル
    int calibModalW = 540;
    int calibModalH = 340;
    ui.rCalibModalBox = { w / 2 - calibModalW / 2, h / 2 - calibModalH / 2, w / 2 + calibModalW / 2, h / 2 + calibModalH / 2 };
    int cBtnH = 42;
    int cBtnY = ui.rCalibModalBox.bottom - 58;
    int cApplyW = 200;
    int cRetryW = 120;
    int cCloseW = 110;
    int cTotalW = cApplyW + cRetryW + cCloseW + 24;
    int cStartX = ui.rCalibModalBox.left + (calibModalW - cTotalW) / 2;

    ui.rCalibApplyBtn = { cStartX, cBtnY, cStartX + cApplyW, cBtnY + cBtnH };
    ui.rCalibRetryBtn = { cStartX + cApplyW + 12, cBtnY, cStartX + cApplyW + 12 + cRetryW, cBtnY + cBtnH };
    ui.rCalibCloseBtn = { cStartX + cApplyW + 12 + cRetryW + 12, cBtnY, cStartX + cApplyW + 12 + cRetryW + 12 + cCloseW, cBtnY + cBtnH };
}
