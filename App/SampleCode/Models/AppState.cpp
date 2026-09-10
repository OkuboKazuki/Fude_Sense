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

// シークバーのツマミだけを再生時刻から置き直す。
// 再生中は毎フレーム動くが、他のレイアウトは変わらないため、
// Layout() 全体を呼び直す必要は無い。
void AppState::UpdateReplaySeekThumb() {
    double prog = (replay.totalDurationMs > 0) ? (static_cast<double>(replay.currentTimeMs) / static_cast<double>(replay.totalDurationMs)) : 0.0;
    prog = RenderUtils::Clamp(prog, 0.0, 1.0);
    int trackW = ui.rReplaySeekTrack.right - ui.rReplaySeekTrack.left;
    int thumbX = ui.rReplaySeekTrack.left + static_cast<int>(trackW * prog);
    int thumbCy = (ui.rReplaySeekTrack.top + ui.rReplaySeekTrack.bottom) / 2;
    int thumbR = 8;
    ui.rReplaySeekThumb = { thumbX - thumbR, thumbCy - thumbR, thumbX + thumbR, thumbCy + thumbR };
}

void AppState::Layout(int w, int h) {
    using namespace RenderUtils;

    const int rightW = 270;

    ui.rRight = { w - rightW, 0, w, h };
    ui.rCanvasArea = { 0, 0, ui.rRight.left, h };

    // 1. フローティングメニュー & メニュー開閉ボタン（大きめサイズ・一回り拡大）
    if (!ui.isSubPanelOpen) {
        ui.rTbNavToggle = { 20, 20, 84, 72 };
        ui.rSub = { 0, 0, 0, 0 };
        ui.rTbBrush = ui.rTbPaper = ui.rTbAnalysis = ui.rTbSave = ui.rTbOtehon = { 0, 0, 0, 0 };
    } else {
        int menuW = 560;
        int menuH = (std::min)(h - 36, 840);
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

        // 解析（グラフ・リプレイ）タブ
        int availSubH = ui.rSub.bottom - topOff - 18;
        int replayH = 126;
        int metricsH = 88;
        int compassH = 168;
        int graphH = (std::max)(150, availSubH - replayH - metricsH - compassH - 42);

        ui.rAnalysisReplayBox  = { ui.rSub.left + 18, topOff, ui.rSub.right - 18, topOff + replayH };
        ui.rAnalysisMetricsBox = { ui.rSub.left + 18, ui.rAnalysisReplayBox.bottom + 12, ui.rSub.right - 18, ui.rAnalysisReplayBox.bottom + 12 + metricsH };
        ui.rAnalysisCompassBox = { ui.rSub.left + 18, ui.rAnalysisMetricsBox.bottom + 12, ui.rSub.right - 18, ui.rAnalysisMetricsBox.bottom + 12 + compassH };
        ui.rAnalysisGraphBox   = { ui.rSub.left + 18, ui.rAnalysisCompassBox.bottom + 12, ui.rSub.right - 18, ui.rAnalysisCompassBox.bottom + 12 + graphH };

        // リプレイ操作コントロール内矩形
        int repLeft = ui.rAnalysisReplayBox.left;
        int repRight = ui.rAnalysisReplayBox.right;
        int repTop = ui.rAnalysisReplayBox.top;

        int seekY = repTop + 34;
        int seekH = 12;
        ui.rReplaySeekTrack = { repLeft + 16, seekY, repRight - 16, seekY + seekH };

        UpdateReplaySeekThumb();

        int btnY = repTop + 62;
        int btnH = 46;
        ui.rReplayResetBtn = { repLeft + 16, btnY, repLeft + 16 + 42, btnY + btnH };
        ui.rReplayPrevBtn  = { ui.rReplayResetBtn.right + 8, btnY, ui.rReplayResetBtn.right + 8 + 44, btnY + btnH };
        ui.rReplayPlayBtn  = { ui.rReplayPrevBtn.right + 8, btnY, ui.rReplayPrevBtn.right + 8 + 68, btnY + btnH };
        ui.rReplayNextBtn  = { ui.rReplayPlayBtn.right + 8, btnY, ui.rReplayPlayBtn.right + 8 + 44, btnY + btnH };

        int spdW = 48;
        int spdH = 36;
        int spdY = btnY + (btnH - spdH) / 2;
        ui.rReplaySpeedBtn[2] = { repRight - 16 - spdW, spdY, repRight - 16, spdY + spdH };
        ui.rReplaySpeedBtn[1] = { ui.rReplaySpeedBtn[2].left - 6 - spdW, spdY, ui.rReplaySpeedBtn[2].left - 6, spdY + spdH };
        ui.rReplaySpeedBtn[0] = { ui.rReplaySpeedBtn[1].left - 6 - spdW, spdY, ui.rReplaySpeedBtn[1].left - 6, spdY + spdH };


        // 保存タブ
        int saveBtnH = 68;
        ui.rSaveBtnPng  = { ui.rSub.left + 20, topOff + 12, ui.rSub.right - 20, topOff + 12 + saveBtnH };
        ui.rSaveBtnClip = { ui.rSub.left + 20, ui.rSaveBtnPng.bottom + 14, ui.rSub.right - 20, ui.rSaveBtnPng.bottom + 14 + saveBtnH };
        ui.rSaveBtnJson = { ui.rSub.left + 20, ui.rSaveBtnClip.bottom + 20, ui.rSub.right - 20, ui.rSaveBtnClip.bottom + 20 + saveBtnH };
        ui.rSaveBtnCsv  = { ui.rSub.left + 20, ui.rSaveBtnJson.bottom + 14, ui.rSub.right - 20, ui.rSaveBtnJson.bottom + 14 + saveBtnH };


        // お手本タブ
        ui.rOtehonToggleBtn = { ui.rSub.left + 20, topOff, ui.rSub.right - 20, topOff + 60 };

        // 配置: 左に升目のミニマップ（配置先の選択）、右に書体の切り替えと操作の説明を出す。
        // ミニマップ内の各マスの矩形は、升目のセルが確定してから 2-2 で算出する。
        int mapTop = ui.rOtehonToggleBtn.bottom + 12;
        ui.rOtehonCellMapBox = { ui.rSub.left + 20, mapTop, ui.rSub.left + 20 + 240, mapTop + 110 };

        // 書体の切り替え。ミニマップ右の余白へ横並びで置き、下に操作の説明を続ける
        int fontBtnLeft = ui.rOtehonCellMapBox.right + 14;
        int fontBtnW = (ui.rSub.right - 20 - fontBtnLeft - 2 * 8) / OTEHON_FONT_COUNT;
        for (int i = 0; i < OTEHON_FONT_COUNT; ++i) {
            int x = fontBtnLeft + i * (fontBtnW + 8);
            ui.rOtehonFontBtn[i] = { x, mapTop, x + fontBtnW, mapTop + 34 };
        }

        // 書きたい文字の入力欄
        ui.rOtehonInputBox = { ui.rSub.left + 20, ui.rOtehonCellMapBox.bottom + 28,
                               ui.rSub.right - 20, ui.rOtehonCellMapBox.bottom + 28 + 44 };

        int oTop = ui.rOtehonInputBox.bottom + 12;
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

    int maxPaperH = h - 56;
    if (maxPaperH < 100) maxPaperH = 100;

    // 画面中央に配置したときの最大幅（硯や左パネルとの干渉を防ぎつつ最大化）
    int maxPaperW = (int)(w * 0.54);
    if (maxPaperW < 100) maxPaperW = 100;

    // 横向きの紙は横へ広がるので、画面中央のままだと左のメニューへ潜り込む。
    // 開いているメニューと右の硯パネルの間へ収め、その帯の中心へ置く。
    int paperCenterX = w / 2;
    if (paper.isLandscape) {
        int bandLeft = ui.isSubPanelOpen ? (ui.rSub.right + 16) : 96;
        int bandRight = w - (rightW - 30);
        if (bandRight - bandLeft > 240) {
            maxPaperW = bandRight - bandLeft;
            paperCenterX = (bandLeft + bandRight) / 2;
        }
    }

    int paperH = maxPaperH;
    int paperW = (int)(paperH * (ratioW / ratioH));
    if (paperW > maxPaperW) {
        paperW = maxPaperW;
        paperH = (int)(paperW * (ratioH / ratioW));
    }

    // ★ 縦向きは画面全体の水平中央に固定配置。横向きは上で求めた帯の中心 ★
    int canvasCenterX = paperCenterX;
    int canvasCenterY = h / 2;

    ui.rPaper.left = canvasCenterX - paperW / 2;
    ui.rPaper.right = ui.rPaper.left + paperW;
    ui.rPaper.top = canvasCenterY - paperH / 2;
    ui.rPaper.bottom = ui.rPaper.top + paperH;

    // 2-2. 下敷き升目のジオメトリ
    LayoutGridGeometry();

    // 3. 右側 硯・墨量・全消し（★ 半紙の右端と画面の右端との真ん中 ★）
    int stoneW = 210;
    int stoneH = (int)(stoneW * 1.34);
    int refillBtnH = 46;
    int undoBtnH = 46;
    int redoBtnH = 46;
    int clearBtnH = 46;
    int viewBtnH = 44;   // 表示切り替え（紙の向き）
    int spacing = 12;
    int headerSpace = 32; // 墨残量ヘッダー用スペース

    int viewGap = 20;    // 硯まわりの操作と表示切り替えの間の区切り
    int totalBlockH = headerSpace + stoneH + spacing + refillBtnH + spacing + undoBtnH
                    + spacing + redoBtnH + spacing + clearBtnH
                    + viewGap + viewBtnH;
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

    ui.rRight = { rightSpaceLeft, 0, w, h };
    ui.rInkStoneLarge = { stoneX, stoneY, stoneX + stoneW, stoneY + stoneH };
    ui.rInkRefillBtn  = { stoneX, stoneY + stoneH + spacing, stoneX + stoneW, stoneY + stoneH + spacing + refillBtnH };
    ui.rUndoBtn       = { stoneX, ui.rInkRefillBtn.bottom + spacing, stoneX + stoneW, ui.rInkRefillBtn.bottom + spacing + undoBtnH };
    ui.rRedoBtn       = { stoneX, ui.rUndoBtn.bottom + spacing, stoneX + stoneW, ui.rUndoBtn.bottom + spacing + redoBtnH };
    ui.rClearAllBtn   = { stoneX, ui.rRedoBtn.bottom + spacing, stoneX + stoneW, ui.rRedoBtn.bottom + spacing + clearBtnH };

    // 表示の切り替え。墨や履歴の操作とは用途が違うので、少し間を空けて下へ置く。
    int viewTop = ui.rClearAllBtn.bottom + viewGap;
    ui.rPaperOrientBtn = { stoneX, viewTop, stoneX + stoneW, viewTop + viewBtnH };

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

// 下敷き升目のジオメトリ。半紙（rPaper）から罫線の外枠・各セル・お手本配置用の
// ミニマップまでを一括で算出する。内側の罫線とセル境界を一致させるため、
// CanvasView::DrawGrid と共有する。
void AppState::LayoutGridGeometry() {
    using namespace RenderUtils;

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

        // お手本の配置先を選ぶミニマップ。実際のセルを枠内へ相似縮小したものなので、
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
}
