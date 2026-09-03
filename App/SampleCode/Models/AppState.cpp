#include "stdafx.h"
#include "AppState.h"

namespace {

// 升目の分割数（列×行）をパターンから求める。
// 列は縦書きの「行（ぎょう）」に相当し、右列から左列へ向かって書き進める。
// Lines3 / Lines4 は縦罫線のみで横の区切りが無いため、列幅を1辺とする
// 正方セルを縦に敷き詰め、お手本配置用の仮想的な行として扱う。
void GetGridDivision(GridPattern pattern, int borderW, int borderH, int& cols, int& rows) {
    cols = 1;
    rows = 1;

    switch (pattern) {
    case GridPattern::Div2:  cols = 1; rows = 2; break;
    case GridPattern::Grid4: cols = 2; rows = 2; break;
    case GridPattern::Grid6: cols = 2; rows = 3; break;
    case GridPattern::Grid8: cols = 2; rows = 4; break;
    case GridPattern::Lines3:
    case GridPattern::Lines4:
    {
        cols = (pattern == GridPattern::Lines3) ? 3 : 4;
        int colW = borderW / cols;
        rows = (colW > 0) ? static_cast<int>(static_cast<double>(borderH) / colW + 0.5) : 1;
        break;
    }
    case GridPattern::None:
    case GridPattern::Cross1:
    case GridPattern::StarGrid:
    default:
        break; // 1字用（1マス）
    }

    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (cols * rows > MAX_GRID_CELLS) {
        rows = MAX_GRID_CELLS / cols;
        if (rows < 1) rows = 1;
    }
}

} // namespace

void AppState::Layout(int w, int h) {
    using namespace RenderUtils;

    const int statusH = 26;
    const int rightW = 280;

    ui.rRight = { w - rightW, 0, w, h - statusH };
    ui.rCanvasArea = { 0, 0, ui.rRight.left, h - statusH };
    ui.rStatus = { 0, h - statusH, w, h };

    // 1. フローティングメニュー & メニュー開閉ボタン
    if (!ui.isSubPanelOpen) {
        ui.rTbNavToggle = { 16, 16, 76, 56 };
        ui.rSub = { 0, 0, 0, 0 };
        ui.rTbBrush = ui.rTbPaper = ui.rTbAnalysis = ui.rTbSave = ui.rTbOtehon = { 0, 0, 0, 0 };
    } else {
        int menuW = 410;
        int menuH = (std::min)(h - statusH - 32, 640);
        if (menuH < 460) menuH = 460;
        ui.rSub = { 16, 16, 16 + menuW, 16 + menuH };

        ui.rTbNavToggle = { ui.rSub.left + 10, ui.rSub.top + 10, ui.rSub.left + 54, ui.rSub.top + 48 };

        int tabStartX = ui.rTbNavToggle.right + 6;
        int tabAvailW = ui.rSub.right - 10 - tabStartX;
        int tabW = (tabAvailW - 4 * 5) / 5;
        int tabH = 38;
        int tabY = ui.rSub.top + 10;

        ui.rTbBrush    = { tabStartX + 0 * (tabW + 5), tabY, tabStartX + 0 * (tabW + 5) + tabW, tabY + tabH };
        ui.rTbPaper    = { tabStartX + 1 * (tabW + 5), tabY, tabStartX + 1 * (tabW + 5) + tabW, tabY + tabH };
        ui.rTbAnalysis = { tabStartX + 2 * (tabW + 5), tabY, tabStartX + 2 * (tabW + 5) + tabW, tabY + tabH };
        ui.rTbSave     = { tabStartX + 3 * (tabW + 5), tabY, tabStartX + 3 * (tabW + 5) + tabW, tabY + tabH };
        ui.rTbOtehon   = { tabStartX + 4 * (tabW + 5), tabY, tabStartX + 4 * (tabW + 5) + tabW, tabY + tabH };

        int topOff = ui.rSub.top + 64;

        // 筆タブ
        int cardH = 62;
        ui.rSubSmall  = { ui.rSub.left + 14, topOff, ui.rSub.right - 14, topOff + cardH };
        ui.rSubMedium = { ui.rSub.left + 14, topOff + (cardH + 6), ui.rSub.right - 14, topOff + (cardH + 6) + cardH };
        ui.rSubLarge  = { ui.rSub.left + 14, topOff + (cardH + 6) * 2, ui.rSub.right - 14, topOff + (cardH + 6) * 2 + cardH };
        int cardBottom = topOff + (cardH + 6) * 2 + cardH;
        ui.rHardnessTrack = { ui.rSub.left + 30, cardBottom + 54, ui.rSub.right - 30, cardBottom + 62 };

        // 筆圧キャリブレーションボタン
        int calibBtnY = cardBottom + 106;
        ui.rSubCalibBtn = { ui.rSub.left + 14, calibBtnY, ui.rSub.right - 14, calibBtnY + 44 };

        // 紙タブ
        int ptW = (menuW - 36) / 2;
        int ptH = 46;
        for (int i = 0; i < 4; ++i) {
            int col = i % 2;
            int row = i / 2;
            ui.rPaperTile[i] = { ui.rSub.left + 14 + col * (ptW + 8), topOff + row * (ptH + 6), ui.rSub.left + 14 + col * (ptW + 8) + ptW, topOff + row * (ptH + 6) + ptH };
        }

        int gridTop = topOff + 2 * (ptH + 6) + 30;
        int tileW = (menuW - 36) / 2;
        int tileH = 46;
        for (int i = 0; i < 9; ++i) {
            int col = i % 2;
            int row = i / 2;
            if (i == 8) {
                ui.rGridTile[i] = { ui.rSub.left + 14, gridTop + row * (tileH + 6), ui.rSub.left + 14 + tileW * 2 + 8, gridTop + row * (tileH + 6) + tileH };
            } else {
                ui.rGridTile[i] = { ui.rSub.left + 14 + col * (tileW + 8), gridTop + row * (tileH + 6), ui.rSub.left + 14 + col * (tileW + 8) + tileW, gridTop + row * (tileH + 6) + tileH };
            }
        }

        int colorY = gridTop + 5 * (tileH + 6) + 24;
        int colBtnW = (menuW - 40) / 3;
        for (int i = 0; i < 3; ++i) {
            ui.rColorBtn[i] = { ui.rSub.left + 14 + i * (colBtnW + 6), colorY, ui.rSub.left + 14 + i * (colBtnW + 6) + colBtnW, colorY + 36 };
        }

        // 解析（グラフ）タブ
        int availSubH = ui.rSub.bottom - topOff - 14;
        int metricsH = 86;
        int compassH = 160;
        int graphH = (std::max)(150, availSubH - metricsH - compassH - 24);

        ui.rAnalysisMetricsBox = { ui.rSub.left + 14, topOff, ui.rSub.right - 14, topOff + metricsH };
        ui.rAnalysisCompassBox = { ui.rSub.left + 14, ui.rAnalysisMetricsBox.bottom + 10, ui.rSub.right - 14, ui.rAnalysisMetricsBox.bottom + 10 + compassH };
        ui.rAnalysisGraphBox   = { ui.rSub.left + 14, ui.rAnalysisCompassBox.bottom + 10, ui.rSub.right - 14, ui.rAnalysisCompassBox.bottom + 10 + graphH };

        // 保存タブ
        int btnH = 58;
        ui.rSaveBtnPng  = { ui.rSub.left + 16, topOff + 10, ui.rSub.right - 16, topOff + 10 + btnH };
        ui.rSaveBtnClip = { ui.rSub.left + 16, ui.rSaveBtnPng.bottom + 10, ui.rSub.right - 16, ui.rSaveBtnPng.bottom + 10 + btnH };
        ui.rSaveBtnJson = { ui.rSub.left + 16, ui.rSaveBtnClip.bottom + 16, ui.rSub.right - 16, ui.rSaveBtnClip.bottom + 16 + btnH };
        ui.rSaveBtnCsv  = { ui.rSub.left + 16, ui.rSaveBtnJson.bottom + 10, ui.rSub.right - 16, ui.rSaveBtnJson.bottom + 10 + btnH };

        // お手本タブ
        ui.rOtehonToggleBtn = { ui.rSub.left + 16, topOff, ui.rSub.right - 16, topOff + 48 };

        // 配置: 左に升目のミニマップ（固定先の選択）、右に「ペンに追従」へ戻すボタン。
        // ミニマップ内の各マスの矩形は、升目のセルが確定してから 2-2 で算出する。
        int mapTop = ui.rOtehonToggleBtn.bottom + 12;
        ui.rOtehonCellMapBox = { ui.rSub.left + 16, mapTop, ui.rSub.left + 16 + 190, mapTop + 104 };
        ui.rOtehonFollowBtn = { ui.rOtehonCellMapBox.right + 12, mapTop, ui.rSub.right - 16, mapTop + 38 };

        int oTop = ui.rOtehonCellMapBox.bottom + 28;
        int oW = (menuW - 48) / 4;
        int oH = 54;
        for (int i = 0; i < 8; ++i) {
            int col = i % 4;
            int row = i / 4;
            ui.rOtehonTile[i] = { ui.rSub.left + 16 + col * (oW + 8), oTop + row * (oH + 8), ui.rSub.left + 16 + col * (oW + 8) + oW, oTop + row * (oH + 8) + oH };
        }
        ui.rOtehonOpacityTrack = { ui.rSub.left + 28, oTop + 2 * (oH + 8) + 40, ui.rSub.right - 28, oTop + 2 * (oH + 8) + 48 };
    }

    // 2. キャンバス / 半紙（画面中央にアスペクト比を維持して配置）
    double ratioW = 242.0, ratioH = 333.0;
    paper.GetAspectRatio(ratioW, ratioH);

    int maxPaperH = h - statusH - 48;
    if (maxPaperH < 100) maxPaperH = 100;

    int maxPaperW = (w / 2 - (rightW + 20)) * 2;
    if (maxPaperW < 100) maxPaperW = w - rightW - 48;
    if (maxPaperW < 100) maxPaperW = 100;

    int paperH = maxPaperH;
    int paperW = (int)(paperH * (ratioW / ratioH));
    if (paperW > maxPaperW) {
        paperW = maxPaperW;
        paperH = (int)(paperW * (ratioH / ratioW));
    }

    int canvasCenterX = w / 2;
    if (canvasCenterX + paperW / 2 > ui.rRight.left - 12) {
        canvasCenterX = ui.rRight.left / 2;
    }
    int canvasCenterY = (h - statusH) / 2;

    ui.rPaper.left = canvasCenterX - paperW / 2;
    ui.rPaper.right = ui.rPaper.left + paperW;
    ui.rPaper.top = canvasCenterY - paperH / 2;
    ui.rPaper.bottom = ui.rPaper.top + paperH;

    // 2-2. 下敷き升目のジオメトリ
    // 内側の罫線とセル境界を一致させるため、外枠のマージンからセル矩形まで
    // ここで一括して算出する（CanvasView::DrawGrid と共有）。
    {
        int margin = (std::max)(6, RW(ui.rPaper) / 48);
        if (paper.gridPattern == GridPattern::None) {
            // 罫線なしのときは半紙全体を1マスとして扱う
            ui.rGridBorder = ui.rPaper;
        } else {
            ui.rGridBorder = { ui.rPaper.left + margin, ui.rPaper.top + margin,
                               ui.rPaper.right - margin, ui.rPaper.bottom - margin };
        }

        int bw = RW(ui.rGridBorder);
        int bh = RH(ui.rGridBorder);
        GetGridDivision(paper.gridPattern, bw, bh, ui.gridCols, ui.gridRows);

        ui.gridCellCount = 0;
        for (int i = 0; i < ui.gridCols; ++i) {
            int col = ui.gridCols - 1 - i; // 縦書きは右列から左列へ
            for (int row = 0; row < ui.gridRows; ++row) {
                if (ui.gridCellCount >= MAX_GRID_CELLS) break;
                ui.rGridCell[ui.gridCellCount++] = {
                    ui.rGridBorder.left + (bw * col) / ui.gridCols,
                    ui.rGridBorder.top + (bh * row) / ui.gridRows,
                    ui.rGridBorder.left + (bw * (col + 1)) / ui.gridCols,
                    ui.rGridBorder.top + (bh * (row + 1)) / ui.gridRows
                };
            }
        }

        // 升目パターンや用紙の変更でセル数が減ることがある。
        // 追従中のマスが範囲外に取り残されないよう書字順の先頭へ戻す。
        if (otehon.activeCell < 0 || otehon.activeCell >= ui.gridCellCount) {
            otehon.activeCell = 0;
        }

        // お手本の固定先を選ぶミニマップ。実際のセルを枠内へ相似縮小したものなので、
        // 升目の割り付けと必ず一致する（並びも rGridCell と同じ書字順）。
        const RECT& mapBox = ui.rOtehonCellMapBox;
        int boxW = RW(mapBox);
        int boxH = RH(mapBox);
        if (bw > 0 && bh > 0 && boxW > 0 && boxH > 0) {
            int mapH = boxH;
            int mapW = (bh > 0) ? (mapH * bw / bh) : boxW;
            if (mapW > boxW) {
                mapW = boxW;
                mapH = (bw > 0) ? (mapW * bh / bw) : boxH;
            }
            int mapX = mapBox.left + (boxW - mapW) / 2;
            int mapY = mapBox.top + (boxH - mapH) / 2;

            for (int i = 0; i < ui.gridCellCount; ++i) {
                const RECT& c = ui.rGridCell[i];
                ui.rOtehonCellBtn[i] = {
                    mapX + (c.left   - ui.rGridBorder.left) * mapW / bw,
                    mapY + (c.top    - ui.rGridBorder.top)  * mapH / bh,
                    mapX + (c.right  - ui.rGridBorder.left) * mapW / bw,
                    mapY + (c.bottom - ui.rGridBorder.top)  * mapH / bh
                };
            }
        }
        for (int i = ui.gridCellCount; i < MAX_GRID_CELLS; ++i) {
            ui.rOtehonCellBtn[i] = { 0, 0, 0, 0 };
        }
    }

    // 3. 右側 硯・墨量・全消し
    int rightW_actual = RW(ui.rRight);
    int stoneW = (std::min)(240, rightW_actual - 32);
    int stoneH = (int)(stoneW * 1.48);
    int stoneX = ui.rRight.left + (rightW_actual - stoneW) / 2;
    int stoneY = 60;

    ui.rInkStoneLarge = { stoneX, stoneY, stoneX + stoneW, stoneY + stoneH };
    ui.rInkRefillBtn  = { stoneX, stoneY + stoneH + 14, stoneX + stoneW, stoneY + stoneH + 54 };
    ui.rClearAllBtn   = { stoneX, stoneY + stoneH + 68, stoneX + stoneW, stoneY + stoneH + 108 };

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
